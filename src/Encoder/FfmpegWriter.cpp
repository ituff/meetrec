#include "FfmpegWriter.h"

#include <chrono>
#include <new>

#include <Infrastructure/Guid.h>
#include <Infrastructure/Log.h>
#include <Infrastructure/Strings.h>

namespace vsr::encoder {

namespace {

// A freshly installed ffmpeg is ~185 MB of shared DLLs. Real-time antivirus
// scans them on first use; measured at 15.5 s on this machine, so the pipe
// deadline must tolerate a scan that starts when the user clicks Record
// before the startup prewarm has finished.
constexpr DWORD kConnectTimeoutMs = 30'000;  // child must open its pipe
constexpr DWORD kWriteTimeoutMs = 10'000;    // single pipe write deadline
// EOF -> child exit. Writing the MP4 index for a multi-hour recording can
// take several seconds; 20 s leaves margin for slower disks and busy systems.
constexpr DWORD kFinalizeTimeoutMs = 20'000;
// Remuxing a long meeting (40+ minutes) previously hit the old 20 s deadline
// while the disk/AV was busy, silently dropping the audio track. A generous
// deadline keeps the audio; a genuinely wedged mux is still bounded.
constexpr DWORD kMuxTimeoutMs = 120'000;     // -c copy remux deadline
constexpr DWORD kPipeOutBufferBytes = 32u * 1024 * 1024; // ~10 NV12 1080p frames of burst absorption

Error ChildError(const char* what, const std::wstring& detail) {
    return Error(ErrorCode::EncoderWriteFailed,
                 L"The recording encoder stopped and the recording was saved up to this "
                 L"point. Try again; if this repeats, check the ffmpeg installation.",
                 std::wstring(what, what + std::char_traits<char>::length(what)) + L": " +
                     detail,
                 S_OK, true);
}

Error EncodeHresultError(const char* what, HRESULT hr) {
    return ErrorFromHresult(hr, ErrorCode::EncoderWriteFailed,
                            L"Recording could not write video and was stopped safely. Free "
                            L"disk space and check the GPU driver, then try again.",
                            std::wstring(what, what + std::char_traits<char>::length(what)) +
                                L" failed: " + FormatHresult(hr),
                            true);
}

std::wstring PipeName(const char* tag) {
    return L"\\\\.\\pipe\\vsr-" + Utf8ToWide(tag) + L"-" + NewGuidString();
}

std::wstring Quoted(const std::filesystem::path& p) {
    return L"\"" + p.wstring() + L"\"";
}

// "Recording_x.partial.mp4" -> "Recording_x.video.mp4" / ".audio.m4a".
std::filesystem::path IntermediatePath(const std::filesystem::path& partialPath,
                                       const wchar_t* suffix) {
    std::wstring name = partialPath.wstring();
    const std::wstring from = L".partial.mp4";
    const std::size_t at = name.rfind(from);
    if (at != std::wstring::npos) name.replace(at, from.size(), suffix);
    else name += suffix;
    return name;
}

} // namespace

Result<std::wstring> FfmpegWriter::LocateExecutable() {
    // 1. Explicit override.
    wchar_t envPath[MAX_PATH] = {};
    if (GetEnvironmentVariableW(L"VSR_FFMPEG_PATH", envPath, MAX_PATH) > 0 &&
        GetFileAttributesW(envPath) != INVALID_FILE_ATTRIBUTES) {
        return std::wstring(envPath);
    }
    // 2. Beside the application (product distribution).
    wchar_t exePath[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    std::filesystem::path candidate =
        std::filesystem::path(exePath).parent_path() / L"ffmpeg.exe";
    if (GetFileAttributesW(candidate.c_str()) != INVALID_FILE_ATTRIBUTES) {
        return candidate.wstring();
    }
    // 3. PATH.
    wchar_t found[MAX_PATH] = {};
    if (SearchPathW(nullptr, L"ffmpeg.exe", L".exe", MAX_PATH, found, nullptr) > 0) {
        return std::wstring(found);
    }
    // 4. Common stand-alone install.
    candidate = L"C:\\ffmpeg\\bin\\ffmpeg.exe";
    if (GetFileAttributesW(candidate.c_str()) != INVALID_FILE_ATTRIBUTES) {
        return candidate.wstring();
    }
    return Error(ErrorCode::EncoderUnavailable,
                 L"The ffmpeg encoder backend requires ffmpeg.exe. Install it (for example "
                 L"C:\\ffmpeg) or set VSR_FFMPEG_PATH, then try again.",
                 L"ffmpeg.exe not found (env, app dir, PATH, C:\\ffmpeg\\bin)");
}

namespace {

struct PrewarmContext {
    std::wstring ffmpegPath;
};

void CALLBACK PrewarmCallback(PTP_CALLBACK_INSTANCE, PVOID rawContext) {
    std::unique_ptr<PrewarmContext> context(static_cast<PrewarmContext*>(rawContext));
    if (context == nullptr || context->ffmpegPath.empty()) return;

    SECURITY_ATTRIBUTES inherit = {};
    inherit.nLength = sizeof(inherit);
    inherit.bInheritHandle = TRUE;
    HANDLE nul = CreateFileW(L"NUL", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                             &inherit, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    STARTUPINFOW si = {};
    si.cb = sizeof(si);
    if (nul != INVALID_HANDLE_VALUE) {
        si.dwFlags = STARTF_USESTDHANDLES;
        si.hStdInput = nul;
        si.hStdOutput = nul;
        si.hStdError = nul;
    }
    std::wstring commandLine = L"\"" + context->ffmpegPath + L"\" -hide_banner -version";
    PROCESS_INFORMATION pi = {};
    if (CreateProcessW(nullptr, commandLine.data(), nullptr, nullptr,
                       nul != INVALID_HANDLE_VALUE, CREATE_NO_WINDOW, nullptr, nullptr, &si,
                       &pi)) {
        CloseHandle(pi.hThread);
        WaitForSingleObject(pi.hProcess, 60'000);
        CloseHandle(pi.hProcess);
    }
    if (nul != INVALID_HANDLE_VALUE) CloseHandle(nul);
}

} // namespace

void FfmpegWriter::PrewarmAsync() {
    auto exe = LocateExecutable();
    if (!exe) return;
    auto* context = new (std::nothrow) PrewarmContext{exe.Value()};
    if (context == nullptr) return;
    log::Info("Encoder", "FFMPEG_PREWARM_STARTED", L"",
              L"Warming the ffmpeg binaries in the background.",
              {{"ffmpegPath", WideToUtf8(exe.Value())}});
    if (!TrySubmitThreadpoolCallback(PrewarmCallback, context, nullptr)) {
        delete context;
    }
}

namespace {

// One-shot per process: pick a hardware encoder by actually encoding three
// frames. The encoders are listed even without working hardware, so a probe
// that produces frames is the only reliable availability test.
const std::wstring& DetectHardwareVideoEncoder(const std::wstring& ffmpegPath) {
    static std::wstring cached;
    static bool checked = false;
    if (checked) return cached;
    checked = true;
    for (const wchar_t* hw : {L"h264_qsv", L"h264_nvenc", L"h264_amf"}) {
        std::wstring commandLine = L"\"" + ffmpegPath + L"\" -hide_banner -loglevel error " +
                                   L"-nostats -nostdin -y -f lavfi " +
                                   L"-i color=black:s=320x180:d=0.1:r=10 " +
                                   std::wstring(L"-frames:v 3 -c:v ") + hw + L" -f null -";
        STARTUPINFOW si = {};
        si.cb = sizeof(si);
        PROCESS_INFORMATION pi = {};
        if (!CreateProcessW(nullptr, commandLine.data(), nullptr, nullptr, FALSE,
                            CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
            continue;
        }
        CloseHandle(pi.hThread);
        if (WaitForSingleObject(pi.hProcess, 8'000) == WAIT_OBJECT_0) {
            DWORD exitCode = 1;
            GetExitCodeProcess(pi.hProcess, &exitCode);
            if (exitCode == 0) {
                CloseHandle(pi.hProcess);
                cached = hw;
                return cached;
            }
        } else {
            TerminateProcess(pi.hProcess, 1);
        }
        CloseHandle(pi.hProcess);
    }
    return cached;
}

// Resolves the recording video encoder from the user's 编码方式 setting.
// Auto keeps libx264 (the predictable, smallest-file recipe); Hardware uses
// the detected GPU encoder; Software forces libx264. The environment
// variables remain as development overrides.
Result<std::wstring> SelectVideoEncoder(const std::wstring& ffmpegPath,
                                        EncoderPreference preference) {
    wchar_t overrideVenc[64] = {};
    if (GetEnvironmentVariableW(L"VSR_FFMPEG_VENCODER", overrideVenc, 64) > 0 &&
        overrideVenc[0] != 0) {
        return std::wstring(overrideVenc);
    }
    if (preference == EncoderPreference::Software) {
        return std::wstring(L"libx264");
    }
    if (preference == EncoderPreference::Hardware) {
        const std::wstring hw = DetectHardwareVideoEncoder(ffmpegPath);
        if (!hw.empty()) return hw;
        return Error(ErrorCode::EncoderUnavailable,
                     L"No hardware H.264 encoder is available, but the encoder preference "
                     L"is set to Hardware. Switch the encoder preference to Auto or "
                     L"Software, then try again.",
                     L"No h264_qsv/h264_nvenc/h264_amf encoder passed the ffmpeg test "
                     L"encode");
    }
    wchar_t allowQsv[4] = {};
    if (GetEnvironmentVariableW(L"VSR_FFMPEG_ALLOW_QSV", allowQsv, 4) > 0 &&
        allowQsv[0] == L'1') {
        const std::wstring hw = DetectHardwareVideoEncoder(ffmpegPath);
        if (!hw.empty()) return hw;
    }
    return std::wstring(L"libx264");
}

} // namespace

std::wstring FfmpegWriter::BuildVideoArguments(const PresetParams& preset,
                                               const std::wstring& videoPipeName,
                                               const std::filesystem::path& output,
                                               const wchar_t* videoEncoder) {
    std::wstring cmd;
    cmd += L"-hide_banner -loglevel error -nostats -nostdin -y ";
    cmd += L"-f rawvideo -pix_fmt nv12 ";
    cmd += L"-video_size " + std::to_wstring(preset.width) + L"x" +
           std::to_wstring(preset.height) + L" ";
    cmd += L"-framerate " + std::to_wstring(preset.fps) + L" -i \"" + videoPipeName + L"\" ";
    const bool hw = wcscmp(videoEncoder, L"libx264") != 0;
    cmd += std::wstring(L"-c:v ") + videoEncoder + L" ";
    if (hw) {
        // Same capped constant-quality rate control as the offline compressor
        // (Transcode/Arguments.cpp): a pure ABR target spends the full ceiling
        // on static slides, which made GPU recordings several times larger
        // than the same clip compressed with the same GPU encoder. The preset
        // CRF maps to each vendor's CQ control, exactly as in the compressor.
        if (wcscmp(videoEncoder, L"h264_nvenc") == 0) {
            // VBR + cq is NVENC's constant-quality mode; -b:v 0 disables the
            // target, and maxrate still caps scene-change bursts.
            cmd += L"-preset p5 -tune hq -rc vbr -cq " +
                   std::to_wstring(preset.crf) + L" -b:v 0 -maxrate " +
                   std::to_wstring(preset.bitrateBps) + L" -bufsize " +
                   std::to_wstring(preset.bitrateBps * 2) + L" ";
        } else if (wcscmp(videoEncoder, L"h264_amf") == 0) {
            // AMF has no capped constant-quality mode; CQP is what the
            // compressor uses, so both paths produce the same size.
            cmd += L"-quality balanced -rc cqp -qp_i " +
                   std::to_wstring(preset.crf) + L" -qp_p " +
                   std::to_wstring(preset.crf) + L" -qp_b " +
                   std::to_wstring(preset.crf) + L" ";
        } else {
            // QSV has no CRF; ICQ (global_quality) is its constant-quality
            // mode. The +5 offset matches the compressor.
            cmd += L"-preset veryfast -global_quality " +
                   std::to_wstring(preset.crf + 5) + L" ";
        }
    } else {
        // Meeting-oriented rate control (capped CRF): constant quality keeps
        // shared text and slides sharp while static stretches spend few bits;
        // scene changes burst up to the preset bitrate ceiling. Pure ABR
        // burns the full rate on every frame regardless of content.
        cmd += L"-preset veryfast ";
        if (preset.screenContentTune) cmd += L"-tune stillimage ";
        cmd += L"-crf " + std::to_wstring(preset.crf) + L" -maxrate " +
               std::to_wstring(preset.bitrateBps) + L" -bufsize " +
               std::to_wstring(preset.bitrateBps * 2) + L" ";
    }
    // Long keyframe interval: each IDR re-codes the whole frame at full
    // quality, which on mostly-static meeting content was the single largest
    // cost in the file (see VideoPreset.h). 5 s keeps seeking responsive.
    cmd += L"-pix_fmt yuv420p -g " +
           std::to_wstring(preset.fps * preset.gopSeconds) + L" ";
    cmd += L"-f mp4 " + Quoted(output);
    return cmd;
}

std::wstring FfmpegWriter::BuildAudioArguments(const AudioFormat& audioFormat,
                                               const PresetParams& preset,
                                               const std::wstring& audioPipeName,
                                               const std::filesystem::path& output) {
    std::wstring cmd;
    cmd += L"-hide_banner -loglevel error -nostats -nostdin -y ";
    cmd += L"-f s16le -ar " + std::to_wstring(audioFormat.sampleRate) + L" -ac " +
           std::to_wstring(audioFormat.channels) + L" -i \"" + audioPipeName + L"\" ";
    cmd += L"-c:a aac -b:a " + std::to_wstring(preset.audioBitrateBps / 1000) + L"k ";
    // Meeting speech carries nothing in a second channel; the preset downmixes
    // the capture mix when its target layout is narrower than the endpoint's.
    if (preset.audioChannels < audioFormat.channels) {
        cmd += L"-ac " + std::to_wstring(preset.audioChannels) + L" ";
    }
    cmd += L"-f ipod " + Quoted(output); // ipod = m4a container
    return cmd;
}

std::wstring FfmpegWriter::BuildMuxArguments(const std::filesystem::path& video,
                                             const std::filesystem::path& audio,
                                             const std::filesystem::path& output) {
    std::wstring cmd;
    cmd += L"-hide_banner -loglevel error -nostats -nostdin -y ";
    cmd += L"-i " + Quoted(video) + L" -i " + Quoted(audio) + L" ";
    cmd += L"-c copy -movflags +faststart ";
    cmd += L"-f mp4 " + Quoted(output);
    return cmd;
}

Result<HANDLE> FfmpegWriter::SpawnChild(const std::wstring& ffmpegPath,
                                        const std::wstring& args) {
    std::wstring commandLine = L"\"" + ffmpegPath + L"\" " + args;
    STARTUPINFOW si = {};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = nullptr;
    si.hStdOutput = stderrPipe_; // keep stdout silent; diagnostics on stderr
    si.hStdError = stderrPipe_;
    PROCESS_INFORMATION pi = {};
    if (!CreateProcessW(nullptr, commandLine.data(), nullptr, nullptr, TRUE,
                        CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        return EncodeHresultError("CreateProcess(ffmpeg)", HRESULT_FROM_WIN32(GetLastError()));
    }
    CloseHandle(pi.hThread);
    return Result<HANDLE>(pi.hProcess);
}

Result<void> FfmpegWriter::WaitForChildConnection(HANDLE pipe, HANDLE child,
                                                  const char* what,
                                                  const wchar_t* label,
                                                  const std::atomic<bool>* cancel) {
    OVERLAPPED ov = {};
    ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (ov.hEvent == nullptr) {
        return EncodeHresultError("CreateEvent(pipe connect)",
                                  HRESULT_FROM_WIN32(GetLastError()));
    }

    // With FILE_FLAG_OVERLAPPED, ConnectNamedPipe returns zero with
    // ERROR_PIPE_CONNECTED when the client won the race and ERROR_IO_PENDING
    // while the connect is outstanding. A nonzero return means the pipe is
    // already connected, so GetLastError() must not be consulted then (it
    // would read a stale error and wait on an event that never signals).
    const BOOL connectedNow = ConnectNamedPipe(pipe, &ov);
    const DWORD initialError = connectedNow ? ERROR_SUCCESS : GetLastError();
    bool connected = connectedNow != FALSE || initialError == ERROR_PIPE_CONNECTED;
    Result<void> result{};
    if (!connected && initialError != ERROR_IO_PENDING) {
        result = ChildError(what, L"ConnectNamedPipe failed: " +
                                    std::to_wstring(initialError) + L"; stderr: " +
                                    StderrTail());
    }

    const ULONGLONG startedMs = GetTickCount64();
    while (!connected && result.IsOk()) {
        if (cancel != nullptr && cancel->load(std::memory_order_relaxed)) {
            result = ChildError("recording start cancelled",
                                L"stop requested while ffmpeg was starting.");
            break;
        }
        // Short slices: a child that exits (bad arguments, missing DLL, AV
        // block) is reported immediately instead of masquerading as a slow
        // startup after the full deadline.
        const DWORD wait = WaitForSingleObject(ov.hEvent, 100);
        if (wait == WAIT_OBJECT_0) {
            DWORD bytes = 0;
            if (GetOverlappedResult(pipe, &ov, &bytes, FALSE) ||
                GetLastError() == ERROR_PIPE_CONNECTED) {
                connected = true;
            } else {
                result = ChildError(what, L"pipe connect failed: " +
                                            std::to_wstring(GetLastError()) + L"; stderr: " +
                                            StderrTail());
            }
            break;
        }
        if (wait == WAIT_FAILED) {
            result = EncodeHresultError("WaitForSingleObject(pipe connect)",
                                        HRESULT_FROM_WIN32(GetLastError()));
            break;
        }
        DWORD exitCode = 0;
        if (child != nullptr && GetExitCodeProcess(child, &exitCode) &&
            exitCode != STILL_ACTIVE) {
            result = ChildError("ffmpeg exited during startup",
                                L"exit code " + std::to_wstring(exitCode) + L"; stderr: " +
                                    StderrTail());
            break;
        }
        if (GetTickCount64() - startedMs >= kConnectTimeoutMs) {
            result = ChildError(what,
                                std::wstring(L"ffmpeg did not open its ") + label +
                                    L" input within " +
                                    std::to_wstring(kConnectTimeoutMs / 1000) +
                                    L" s (child still running); stderr: " + StderrTail());
            break;
        }
    }

    if (!result.IsOk()) CancelIoEx(pipe, &ov);
    CloseHandle(ov.hEvent);
    return result;
}

Result<std::unique_ptr<FfmpegWriter>> FfmpegWriter::Create(const std::wstring& ffmpegPath,
                                                           const PresetParams& preset,
                                                           const std::filesystem::path&
                                                               partialPath,
                                                           const AudioFormat* audioFormat,
                                                           const std::atomic<bool>* cancel,
                                                           EncoderPreference
                                                               encoderPreference) {
    auto encoderResult = SelectVideoEncoder(ffmpegPath, encoderPreference);
    if (!encoderResult) return encoderResult.TakeError();
    const std::wstring videoEncoder = encoderResult.Value();

    const ULONGLONG startupBegin = GetTickCount64();
    SECURITY_ATTRIBUTES inherit = {};
    inherit.nLength = sizeof(inherit);
    inherit.bInheritHandle = TRUE;

    // stderr is drained on a dedicated thread: ffmpeg writes diagnostics
    // there, and an undrained pipe would block the children.
    HANDLE stderrRead = nullptr;
    HANDLE stderrWrite = nullptr;
    if (!CreatePipe(&stderrRead, &stderrWrite, &inherit, 64 * 1024)) {
        return EncodeHresultError("CreatePipe(stderr)", HRESULT_FROM_WIN32(GetLastError()));
    }
    SetHandleInformation(stderrRead, HANDLE_FLAG_INHERIT, 0);

    auto writer = std::unique_ptr<FfmpegWriter>(new FfmpegWriter());
    writer->preset_ = preset;
    writer->audioEnabled_ = audioFormat != nullptr;
    writer->audioChannels_ = audioFormat != nullptr ? audioFormat->channels : 2;
    writer->audioRate_ = audioFormat != nullptr ? audioFormat->sampleRate : 48000;
    writer->ffmpegPath_ = ffmpegPath;
    writer->partialPath_ = partialPath;
    writer->stderrPipe_ = stderrWrite; // released after the drain join
    writer->videoIntermediate_ =
        writer->audioEnabled_ ? IntermediatePath(partialPath, L".video.mp4") : partialPath;
    writer->audioIntermediate_ = IntermediatePath(partialPath, L".audio.m4a");

    // Drain stderr for the lifetime of the children (bounded ring buffer).
    writer->stderrThread_ = std::thread([reader = stderrRead, writerPtr = writer.get()] {
        char chunk[4096] = {};
        DWORD got = 0;
        std::string tail;
        while (ReadFile(reader, chunk, sizeof(chunk), &got, nullptr) && got > 0) {
            tail.append(chunk, got);
            if (tail.size() > 8 * 1024) tail.erase(0, tail.size() - 8 * 1024);
            std::lock_guard<std::mutex> lock(writerPtr->stderrMutex_);
            writerPtr->stderrBuffer_ = tail;
        }
        CloseHandle(reader);
    });

    // Video child: single rawvideo pipe input (the proven configuration).
    const std::wstring videoName = PipeName("video");
    writer->videoPipe_ = CreateNamedPipeW(
        videoName.c_str(),
        PIPE_ACCESS_OUTBOUND | FILE_FLAG_FIRST_PIPE_INSTANCE | FILE_FLAG_OVERLAPPED,
        PIPE_TYPE_BYTE | PIPE_WAIT, 1, kPipeOutBufferBytes, 64 * 1024, 0, nullptr);
    if (writer->videoPipe_ == INVALID_HANDLE_VALUE) {
        CloseHandle(stderrRead);
        CloseHandle(stderrWrite);
        return EncodeHresultError("CreateNamedPipe(video)",
                                  HRESULT_FROM_WIN32(GetLastError()));
    }
    auto spawned = writer->SpawnChild(
        ffmpegPath,
        BuildVideoArguments(preset, videoName, writer->videoIntermediate_,
                            videoEncoder.c_str()));
    if (!spawned) {
        CloseHandle(stderrRead);
        CloseHandle(stderrWrite);
        return spawned.TakeError();
    }
    writer->child_ = spawned.Value();

    // Audio child (own process): a second raw input into the SAME ffmpeg
    // stalls its interleaved demuxer, so audio always gets a dedicated child.
    if (writer->audioEnabled_) {
        const std::wstring audioName = PipeName("audio");
        writer->audioPipe_ = CreateNamedPipeW(
            audioName.c_str(),
            PIPE_ACCESS_OUTBOUND | FILE_FLAG_FIRST_PIPE_INSTANCE | FILE_FLAG_OVERLAPPED,
            PIPE_TYPE_BYTE | PIPE_WAIT, 1, 256 * 1024, 64 * 1024, 0, nullptr);
        if (writer->audioPipe_ == INVALID_HANDLE_VALUE) {
            CloseHandle(stderrRead);
            CloseHandle(stderrWrite);
            return EncodeHresultError("CreateNamedPipe(audio)",
                                      HRESULT_FROM_WIN32(GetLastError()));
        }
        auto audioSpawned = writer->SpawnChild(
            ffmpegPath, BuildAudioArguments(*audioFormat, preset, audioName,
                                            writer->audioIntermediate_));
        if (!audioSpawned) {
            CloseHandle(stderrRead);
            CloseHandle(stderrWrite);
            return audioSpawned.TakeError();
        }
        writer->audioChild_ = audioSpawned.Value();
    }

    // Each child must open its (single) input pipe.
    auto videoConnected =
        writer->WaitForChildConnection(writer->videoPipe_, writer->child_,
                                       "video pipe connect", L"video", cancel);
    if (!videoConnected) {
        writer->KillChild();
        return videoConnected.TakeError();
    }
    if (writer->audioPipe_ != nullptr) {
        auto audioConnected =
            writer->WaitForChildConnection(writer->audioPipe_, writer->audioChild_,
                                           "audio pipe connect", L"audio", cancel);
        if (!audioConnected) {
            writer->KillChild();
            return audioConnected.TakeError();
        }
    }

    // Early-exit probe: a bad encoder argument or missing binary exits here,
    // before any content is recorded (FR-063).
    Sleep(500);
    for (HANDLE h : {writer->child_, writer->audioChild_}) {
        if (h == nullptr) continue;
        DWORD exitCode = 0;
        if (GetExitCodeProcess(h, &exitCode) && exitCode != STILL_ACTIVE) {
            return ChildError("ffmpeg exited during startup",
                              L"exit code " + std::to_wstring(exitCode) + L"; stderr: " +
                                  writer->StderrTail());
        }
    }

    log::Info("Encoder", "FFMPEG_WRITER_CREATED", L"", L"ffmpeg encode pipeline ready.",
              {{"path", WideToUtf8(partialPath.wstring())},
               {"videoEncoder", WideToUtf8(std::wstring(videoEncoder))},
               {"ffmpegPath", WideToUtf8(ffmpegPath)},
               {"startupMs",
                std::to_string(static_cast<unsigned long long>(GetTickCount64() - startupBegin))},
               {"audio", writer->audioEnabled_ ? "1" : "0"},
               {"width", std::to_string(preset.width)},
               {"height", std::to_string(preset.height)},
               {"fps", std::to_string(preset.fps)},
               {"bitrateBps", std::to_string(preset.bitrateBps)}});

    // The real-time feeder owns all pipe writes from here on.
    writer->pacerThread_ = std::thread([w = writer.get()] { w->PacerLoop(); });
    return writer;
}

FfmpegWriter::~FfmpegWriter() {
    // Safety net: a destroyed writer without Finalize must not leave orphaned
    // encoder processes. (The normal paths call Finalize first.)
    pacerExit_.store(true, std::memory_order_relaxed);
    if (videoPipe_ != nullptr) CancelIoEx(videoPipe_, nullptr);
    if (audioPipe_ != nullptr) CancelIoEx(audioPipe_, nullptr);
    if (pacerThread_.joinable()) pacerThread_.join();
    KillChild(child_);
    KillChild(audioChild_);
    if (stderrPipe_ != nullptr) {
        CloseHandle(stderrPipe_); // release the drain thread (EOF)
        stderrPipe_ = nullptr;
    }
    if (stderrThread_.joinable()) stderrThread_.join();
    if (videoPipe_ != nullptr) CloseHandle(videoPipe_);
    if (audioPipe_ != nullptr) CloseHandle(audioPipe_);

    // A writer destroyed without Finalize never produced a usable output
    // (startup failure or a session rolled back before recording began).
    // Remove its intermediates so failed starts leave no orphan files.
    if (!finalized_.load(std::memory_order_relaxed)) {
        std::error_code ec;
        const std::filesystem::path paths[] = {videoIntermediate_, audioIntermediate_,
                                               partialPath_};
        for (const auto& path : paths) {
            if (!path.empty()) std::filesystem::remove(path, ec);
        }
    }
}

void FfmpegWriter::KillChild(HANDLE& child) noexcept {
    if (child != nullptr) {
        TerminateProcess(child, 1);
        WaitForSingleObject(child, 3000);
        CloseHandle(child);
        child = nullptr;
    }
}

void FfmpegWriter::KillChild() noexcept { KillChild(child_); }

std::wstring FfmpegWriter::StderrTail() const {
    std::lock_guard<std::mutex> lock(stderrMutex_);
    return Utf8ToWide(stderrBuffer_.substr(0, 2048));
}

Result<void> FfmpegWriter::WritePipe(HANDLE pipe, const void* data, std::size_t bytes,
                                     const char* what) {
    if (pipe == nullptr) return Result<void>{};
    OVERLAPPED ov = {};
    ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (ov.hEvent == nullptr) {
        return EncodeHresultError("CreateEvent(write)", HRESULT_FROM_WIN32(GetLastError()));
    }
    const BYTE* cursor = static_cast<const BYTE*>(data);
    std::size_t remaining = bytes;
    Result<void> result{};
    while (remaining > 0 && result.IsOk()) {
        const DWORD chunk = static_cast<DWORD>(
            remaining > 8u * 1024 * 1024 ? 8u * 1024 * 1024 : remaining);
        DWORD written = 0;
        // Sync completion (returns TRUE, e.g. the chunk fits the pipe buffer)
        // leaves the event unsignaled: only wait when the write is pending.
        const BOOL immediate = WriteFile(pipe, cursor, chunk, nullptr, &ov);
        if (!immediate) {
            const DWORD err = GetLastError();
            if (err != ERROR_IO_PENDING) {
                childBroken_.store(true, std::memory_order_relaxed);
                result = ChildError(what, L"pipe broken (child exited).");
                break;
            }
            const DWORD wait = WaitForSingleObject(ov.hEvent, kWriteTimeoutMs);
            if (wait != WAIT_OBJECT_0) {
                CancelIoEx(pipe, &ov);
                writeTimedOut_.store(true, std::memory_order_relaxed);
                result = ChildError(what,
                                    L"pipe write timed out (encoder stopped consuming; "
                                    L"child terminated on finalize).");
                break;
            }
        }
        if (!GetOverlappedResult(pipe, &ov, &written, FALSE)) {
            const DWORD err = GetLastError();
            childBroken_.store(true, std::memory_order_relaxed);
            result =
                ChildError(what, L"write failed: " + FormatHresult(HRESULT_FROM_WIN32(err)));
            break;
        }
        cursor += written;
        remaining -= written;
        ResetEvent(ov.hEvent);
    }
    CloseHandle(ov.hEvent);
    return result;
}

Result<void> FfmpegWriter::StageToCpu(ID3D11Texture2D* nv12Frame,
                                      std::vector<std::uint8_t>& outNv12) {
    if (staging_ == nullptr) {
        D3D11_TEXTURE2D_DESC desc = {};
        nv12Frame->GetDesc(&desc);
        desc.Usage = D3D11_USAGE_STAGING;
        desc.BindFlags = 0;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        desc.MiscFlags = 0;
        Microsoft::WRL::ComPtr<ID3D11Device> frameDevice;
        nv12Frame->GetDevice(&frameDevice);
        const HRESULT hr = frameDevice->CreateTexture2D(&desc, nullptr, staging_.GetAddressOf());
        if (FAILED(hr)) {
            return EncodeHresultError("CreateTexture2D(staging NV12)", hr);
        }
    }
    Microsoft::WRL::ComPtr<ID3D11Device> frameDevice;
    nv12Frame->GetDevice(&frameDevice);
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;
    frameDevice->GetImmediateContext(&context);
    context->CopyResource(staging_.Get(), nv12Frame);

    D3D11_TEXTURE2D_DESC desc = {};
    staging_->GetDesc(&desc);
    const UINT luma = desc.Width * desc.Height;
    outNv12.resize(static_cast<std::size_t>(luma) + luma / 2);
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    const HRESULT hr = context->Map(staging_.Get(), 0, D3D11_MAP_READ, 0, &mapped);
    if (FAILED(hr)) {
        return EncodeHresultError("ID3D11DeviceContext::Map(staging)", hr);
    }
    const BYTE* src = static_cast<const BYTE*>(mapped.pData);
    for (UINT row = 0; row < desc.Height; ++row) {
        memcpy(outNv12.data() + static_cast<std::size_t>(row) * desc.Width,
               src + static_cast<std::size_t>(row) * mapped.RowPitch, desc.Width);
    }
    const BYTE* srcUv = src + static_cast<std::size_t>(mapped.RowPitch) * desc.Height;
    BYTE* dstUv = outNv12.data() + luma;
    for (UINT row = 0; row < desc.Height / 2; ++row) {
        memcpy(dstUv + static_cast<std::size_t>(row) * desc.Width,
               srcUv + static_cast<std::size_t>(row) * mapped.RowPitch, desc.Width);
    }
    context->Unmap(staging_.Get(), 0);
    return Result<void>{};
}

Result<void> FfmpegWriter::SubmitFrame(ID3D11Texture2D* nv12Frame, std::int64_t, std::int64_t) {
    if (finalized_.load(std::memory_order_acquire)) {
        return Error(ErrorCode::InternalState, L"Internal recording error.",
                     L"SubmitFrame after finalize");
    }
    if (nv12Frame == nullptr) {
        return Error(ErrorCode::InternalState, L"Internal recording error.",
                     L"SubmitFrame null frame");
    }
    // Stage to CPU and swap the latest-frame buffer. The pipe write happens
    // on the pacer thread at the nominal rate; capture never blocks on
    // encoder backpressure.
    std::vector<std::uint8_t> nv12;
    auto staged = StageToCpu(nv12Frame, nv12);
    if (!staged) return staged;
    std::lock_guard<std::mutex> lock(latestMutex_);
    latestNv12_ = std::move(nv12);
    haveLatest_ = true;
    return Result<void>{};
}

Result<void> FfmpegWriter::WriteAudioSample(const std::int16_t* pcm, std::size_t frames,
                                            std::int64_t, std::int64_t) {
    if (!audioEnabled_ || finalized_.load(std::memory_order_acquire)) return Result<void>{};
    if (frames == 0 || pcm == nullptr) return Result<void>{};
    // s16le bytes are self-timed by sample count; the pacer drains the FIFO
    // at real-time rate and pads silence for shortfalls.
    const std::size_t bytes = frames * audioChannels_ * sizeof(std::int16_t);
    std::lock_guard<std::mutex> lock(audioMutex_);
    const std::size_t cap = static_cast<std::size_t>(audioRate_) * audioChannels_ * 2 * 4;
    if (audioFifo_.size() + bytes > cap) {
        const std::size_t drop = audioFifo_.size() + bytes - cap;
        audioFifo_.erase(audioFifo_.begin(), audioFifo_.begin() + static_cast<long>(drop));
    }
    audioFifo_.insert(audioFifo_.end(), reinterpret_cast<const std::uint8_t*>(pcm),
                      reinterpret_cast<const std::uint8_t*>(pcm) + bytes);
    return Result<void>{};
}

void FfmpegWriter::PacerLoop() {
    // Video ticks at the preset rate repeating the latest frame (CFR
    // container; capture under-delivery becomes duplicates); audio drains
    // the FIFO at real-time rate, padding silence. The two children are
    // independent, so neither input can stall the other. Any pipe failure
    // (ffmpeg wedged or dead) is latched and surfaced through QueueHealth.
    //
    // Tick schedule: anchored to the absolute start plus tick count in
    // nanoseconds. An integer-millisecond cadence (1000/30 = 33 ms vs the
    // true 33.333 ms) runs 1% fast and stretched a 30-minute recording by
    // 18 s against wall clock - both tracks equally, but the meeting length
    // must match the wall clock.
    const std::int64_t tickPeriodNs =
        preset_.fps > 0 ? 1'000'000'000LL / preset_.fps : 33'333'333LL;
    const std::size_t audioTickFrames =
        static_cast<std::size_t>(audioRate_ > 0 ? audioRate_ / preset_.fps : 1600);
    const std::size_t audioTickBytes = audioTickFrames * audioChannels_ * 2;

    const auto startedAt = std::chrono::steady_clock::now();
    std::uint64_t ticks = 0;
    std::uint64_t videoTicks = 0;
    std::uint64_t videoBytes = 0;
    while (!pacerExit_.load(std::memory_order_relaxed)) {
        ++ticks;
        const auto nextTick =
            startedAt + std::chrono::nanoseconds(tickPeriodNs * static_cast<std::int64_t>(ticks));

        bool haveFrame = false;
        {
            std::lock_guard<std::mutex> lock(latestMutex_);
            if (haveLatest_) {
                lastSentNv12_ = latestNv12_;
                haveSent_ = true;
                haveLatest_ = false;
            }
            haveFrame = haveSent_;
        }
        if (haveFrame) {
            auto written = WritePipe(videoPipe_, lastSentNv12_.data(), lastSentNv12_.size(),
                                     "video frame write");
            if (!written) break;
            ++videoTicks;
            videoBytes += lastSentNv12_.size();
            writtenFrames_.fetch_add(1, std::memory_order_relaxed);
        }
        // Audio waits for the FIRST VIDEO FRAME: capture bring-up (WGC idle
        // windows before the GDI repair) can delay the first frame by seconds,
        // and audio written before it has no video to align with - the audio
        // track would start early and run longer than the video (observed as
        // a 2.7 s A/V offset on a user recording). The FIFO (4 s cap) holds
        // the audio captured during the wait; leftovers beyond the cap are
        // dropped with the video.
        if (audioEnabled_ && haveFrame) {
            std::vector<std::uint8_t> chunk(audioTickBytes, 0);
            {
                std::lock_guard<std::mutex> lock(audioMutex_);
                const std::size_t take =
                    audioTickBytes < audioFifo_.size() ? audioTickBytes : audioFifo_.size();
                if (take > 0) {
                    memcpy(chunk.data(), audioFifo_.data(), take);
                    audioFifo_.erase(audioFifo_.begin(),
                                     audioFifo_.begin() + static_cast<long>(take));
                }
            }
            auto written = WritePipe(audioPipe_, chunk.data(), chunk.size(), "audio write");
            if (!written) break;
        }
        if (pacerExit_.load(std::memory_order_relaxed)) break;
        // Deterministic pacing: sleep to ~2 ms before the tick, then spin the
        // remainder. Waitable timers misbehaved on this OS build (stuck
        // signaled / immediate return), oversampling the pipe; the spin costs
        // ~6-12% of one core and behaves identically everywhere.
        const auto now = std::chrono::steady_clock::now();
        if (now < nextTick) {
            const auto sleepUntil = nextTick - std::chrono::milliseconds(2);
            if (sleepUntil > now) std::this_thread::sleep_until(sleepUntil);
            while (!pacerExit_.load(std::memory_order_relaxed) &&
                   std::chrono::steady_clock::now() < nextTick) {
                YieldProcessor();
            }
        }
    }
    const auto aliveMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                             std::chrono::steady_clock::now() - startedAt)
                             .count();
    log::Info("Encoder", "PACER_STATS", L"", L"Pacer exited.",
              {{"ticks", std::to_string(ticks)},
               {"videoTicks", std::to_string(videoTicks)},
               {"videoBytes", std::to_string(videoBytes)},
               {"aliveMs", std::to_string(aliveMs)},
               {"tickPeriodNs", std::to_string(tickPeriodNs)}});
}

Result<void> FfmpegWriter::QueueHealth() {
    if (writeTimedOut_.load(std::memory_order_relaxed)) {
        return ChildError("encoder stopped consuming", L"a pipe write timed out earlier.");
    }
    if (childBroken_.load(std::memory_order_relaxed)) {
        return ChildError("ffmpeg exited", StderrTail());
    }
    return Result<void>{};
}

Result<void> FfmpegWriter::WaitChild(HANDLE child, const char* what) {
    if (child == nullptr) return Result<void>{};
    if (WaitForSingleObject(child, kFinalizeTimeoutMs) != WAIT_OBJECT_0) {
        return ChildError(what, L"ffmpeg did not exit after EOF within the deadline.");
    }
    DWORD exitCode = 1;
    GetExitCodeProcess(child, &exitCode);
    if (exitCode != 0) {
        return ChildError(what, L"ffmpeg exit code " + std::to_wstring(exitCode) +
                                    L"; stderr: " + StderrTail());
    }
    return Result<void>{};
}

Result<void> FfmpegWriter::Finalize() {
    bool expected = false;
    if (!finalized_.compare_exchange_strong(expected, true)) {
        return Result<void>{}; // idempotent (FR-073)
    }
    // Stop the feeder first: cancel any pending pipe write so the pacer
    // observes the exit flag promptly, then EOF tells both children the
    // streams are complete; each finalizes its container and exits.
    pacerExit_.store(true, std::memory_order_relaxed);
    if (videoPipe_ != nullptr) CancelIoEx(videoPipe_, nullptr);
    if (audioPipe_ != nullptr) CancelIoEx(audioPipe_, nullptr);
    if (pacerThread_.joinable()) pacerThread_.join();
    if (videoPipe_ != nullptr) CloseHandle(videoPipe_);
    if (audioPipe_ != nullptr) CloseHandle(audioPipe_);
    videoPipe_ = nullptr;
    audioPipe_ = nullptr;

    const auto finalizeStart = std::chrono::steady_clock::now();
    const auto elapsedMs = [](std::chrono::steady_clock::time_point start) {
        return static_cast<long long>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                          std::chrono::steady_clock::now() - start)
                                          .count());
    };

    const auto videoStart = std::chrono::steady_clock::now();
    Result<void> result = WaitChild(child_, "video finalize");
    const long long videoMs = elapsedMs(videoStart);
    if (!result) {
        log::Error("Encoder", "ENCODER_FINALIZE_FAILED", L"", L"Video child failed to finalize.",
                   {{"writtenFrames", std::to_string(writtenFrames_.load())},
                    {"stderr", WideToUtf8(StderrTail())}});
    }

    long long audioMs = 0;
    long long muxMs = 0;

    // Remux the two finalized intermediates into the recording MP4. A remux
    // failure degrades to the video-only intermediate (valid MP4, the
    // documented audio degrade path) instead of losing the recording.
    if (audioEnabled_) {
        const auto audioStart = std::chrono::steady_clock::now();
        auto audioFinalized = WaitChild(audioChild_, "audio finalize");
        audioMs = elapsedMs(audioStart);
        if (!audioFinalized) {
            log::Warning("Encoder", "AUDIO_FINALIZE_FAILED", L"",
                         L"The audio encoder did not finalize cleanly; keeping the "
                         L"video-only recording.",
                         {{"detail",
                           WideToUtf8(audioFinalized.GetError().technicalMessage)}});
        }
        if (result.IsOk()) {
            bool muxFailed = false;
            if (audioFinalized) {
                // Only mux a finalized audio file: an unterminated M4A has no
                // moov atom, and the mux child would wait for it until the
                // deadline (the old silent audio-drop path).
                const auto muxStart = std::chrono::steady_clock::now();
                auto muxSpawned =
                    SpawnChild(ffmpegPath_, BuildMuxArguments(videoIntermediate_,
                                                              audioIntermediate_,
                                                              partialPath_));
                if (muxSpawned) {
                    HANDLE mux = muxSpawned.Value();
                    if (WaitForSingleObject(mux, kMuxTimeoutMs) != WAIT_OBJECT_0) {
                        TerminateProcess(mux, 1);
                        WaitForSingleObject(mux, 3000);
                        result = ChildError("remux", L"remux did not finish in time.");
                        muxFailed = true;
                    } else {
                        DWORD muxExit = 1;
                        GetExitCodeProcess(mux, &muxExit);
                        if (muxExit != 0) {
                            result = ChildError(
                                "remux", L"ffmpeg exit code " + std::to_wstring(muxExit) +
                                             L"; stderr: " + StderrTail());
                            muxFailed = true;
                        }
                    }
                    CloseHandle(mux);
                } else {
                    result = muxSpawned.TakeError();
                    muxFailed = true;
                }
                muxMs = elapsedMs(muxStart);
            } else {
                result = ChildError("audio finalize",
                                    L"audio child failed; skipping the remux.");
            }
            if (!result.IsOk()) {
                // Degrade to video-only: move the valid video intermediate
                // over the partial so the coordinator's rename completes.
                if (muxFailed) {
                    log::Warning("Encoder", "AUDIO_MUX_FAILED", L"",
                                 L"Muxing the audio track failed; keeping the video-only "
                                 L"recording.",
                                 {{"stderr", WideToUtf8(StderrTail())}});
                }
                DeleteFileW(partialPath_.c_str());
                MoveFileExW(videoIntermediate_.c_str(), partialPath_.c_str(),
                            MOVEFILE_REPLACE_EXISTING);
                result = Result<void>{};
            } else {
                DeleteFileW(videoIntermediate_.c_str());
            }
            DeleteFileW(audioIntermediate_.c_str());
        }
    }

    // Release the drain thread: close our stderr write end AFTER the last
    // child was spawned (it inherited the handle) so ReadFile sees EOF.
    if (stderrPipe_ != nullptr) {
        CloseHandle(stderrPipe_);
        stderrPipe_ = nullptr;
    }
    if (stderrThread_.joinable()) stderrThread_.join();

    const long long totalMs = elapsedMs(finalizeStart);
    const LogProperties timing = {{"videoMs", std::to_string(videoMs)},
                                  {"audioMs", std::to_string(audioMs)},
                                  {"muxMs", std::to_string(muxMs)},
                                  {"totalMs", std::to_string(totalMs)},
                                  {"writtenFrames", std::to_string(writtenFrames_.load())}};
    log::Info("Encoder", "ENCODER_FINALIZE_TIMING", L"", L"Finalize timing.", timing);
    if (totalMs > 10'000) {
        // The coordinator's user-stop wait is 10 s; a longer finalize is not
        // a wedge, but record it so slow disks/long recordings are visible.
        log::Warning("Encoder", "FINALIZE_SLOW", L"",
                     L"Recording finalize exceeded the 10 s stop-wait budget.", timing);
    }

    if (result.IsOk()) {
        log::Info("Encoder", "ENCODER_FINALIZED", L"", L"Output finalized.",
                  {{"writtenFrames", std::to_string(writtenFrames_.load())},
                   {"path", WideToUtf8(partialPath_.wstring())}});
    }
    return result;
}

} // namespace vsr::encoder

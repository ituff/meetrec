// Virtual Screen Recorder - ffmpeg child-process recording backend.
//
// Pipeline: NV12 frames are staged to CPU and written into a named pipe read
// by an ffmpeg.exe child as rawvideo (libx264 -> MP4). System audio streams
// through a SECOND pipe to a SEPARATE ffmpeg child (s16le -> AAC -> M4A) and
// Finalize remuxes both with a third `-c copy` invocation. One child per
// input is deliberate: a single ffmpeg fed by two raw pipes stalls inside its
// interleaved demuxer (verified against builds N-121437 and N-128121; the
// producer's write to whichever pipe ffmpeg stops reading blocks forever).
// The OS Media Foundation stack is not involved at all, which removes the
// encoder-MFT failure family (stalls, wedged writes, hung finalize)
// documented on Insider builds.
//
// Bounded by construction:
//   - pipe writes are overlapped with a hard timeout (a stopped child fails
//     the write instead of blocking the pipeline);
//   - Finalize closes the pipes (EOF), waits for both children with a
//     deadline, then remuxes with its own deadline - past any deadline the
//     child is terminated and an error (or a video-only degrade) is
//     reported; a finalize can never hang the session;
//   - a child that dies mid-recording surfaces as a write error with the
//     captured stderr tail for diagnostics.
//
// Frame cadence: rawvideo pipes carry no timestamps, so the video container
// is CFR at the preset rate. A dedicated pacer thread writes the latest frame
// at the nominal fps (duplicating it when capture under-delivers) and drains
// the audio FIFO at real-time rate (padding silence for shortfalls), so
// neither capture backpressure nor WGC's static-scene silence can stall the
// encoder inputs.
#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <Windows.h>
#include <wrl/client.h>

#include "AudioCapture.h"
#include "IVideoWriter.h"
#include "VideoPreset.h"

namespace vsr::encoder {

class FfmpegWriter : public IVideoWriter {
public:
    // Locates ffmpeg.exe: VSR_FFMPEG_PATH environment override, the
    // application directory, PATH, then the common C:\ffmpeg\bin install.
    static Result<std::wstring> LocateExecutable();

    // Spawns the ffmpeg children wired to fresh named pipes (one child per
    // input; see the class comment). Fails when a child exits immediately
    // (bad encoder argument, missing binary), with the child's stderr tail in
    // the error - before recording starts.
    static Result<std::unique_ptr<FfmpegWriter>> Create(const std::wstring& ffmpegPath,
                                                        const PresetParams& preset,
                                                        const std::filesystem::path& partialPath,
                                                        const AudioFormat* audioFormat);

    ~FfmpegWriter() override;

    // Pure argument builders (unit-tested): video child, audio child, and
    // the finalize remux command line.
    static std::wstring BuildVideoArguments(const PresetParams& preset,
                                            const std::wstring& videoPipeName,
                                            const std::filesystem::path& output,
                                            const wchar_t* videoEncoder = L"libx264");
    static std::wstring BuildAudioArguments(const AudioFormat& audioFormat,
                                            const std::wstring& audioPipeName,
                                            const std::filesystem::path& output);
    static std::wstring BuildMuxArguments(const std::filesystem::path& video,
                                          const std::filesystem::path& audio,
                                          const std::filesystem::path& output);

    Result<void> SubmitFrame(ID3D11Texture2D* nv12Frame, std::int64_t time100Ns,
                             std::int64_t duration100Ns) override;
    Result<void> WriteAudioSample(const std::int16_t* pcm, std::size_t frames,
                                  std::int64_t time100Ns,
                                  std::int64_t duration100Ns) override;
    Result<void> QueueHealth() override;
    Result<void> Finalize() override;

    bool HasAudio() const noexcept override { return audioEnabled_; }
    bool IsFinalized() const noexcept override { return finalized_.load(); }
    std::uint64_t WrittenFrames() const noexcept override { return writtenFrames_.load(); }

private:
    FfmpegWriter() = default;

    // Spawns one ffmpeg child with `args` sharing the stderr pipe. Returns
    // the process handle.
    Result<HANDLE> SpawnChild(const std::wstring& ffmpegPath, const std::wstring& args);

    Result<void> StageToCpu(ID3D11Texture2D* nv12Frame, std::vector<std::uint8_t>& outNv12);
    Result<void> WritePipe(HANDLE pipe, const void* data, std::size_t bytes,
                           const char* what);
    // Real-time feeder (see the class comment): video ticks at the preset
    // rate repeating the latest frame; audio drains the FIFO with silence
    // padding. Runs until Finalize sets pacerExit_.
    void PacerLoop();
    void KillChild(HANDLE& child) noexcept;
    void KillChild() noexcept;
    std::wstring StderrTail() const;
    // Bounded EOF->exit wait for one child; reports its exit code.
    Result<void> WaitChild(HANDLE child, const char* what);

    HANDLE videoPipe_ = nullptr;   // named pipe servers (outbound to ffmpeg)
    HANDLE audioPipe_ = nullptr;
    HANDLE child_ = nullptr;       // video ffmpeg
    HANDLE audioChild_ = nullptr;  // audio ffmpeg (audio mode only)
    HANDLE stderrPipe_ = nullptr;  // anonymous pipe, drained by stderrThread_
    std::thread stderrThread_;
    mutable std::mutex stderrMutex_;
    std::string stderrBuffer_;     // last bytes written by the drain thread

    // NV12 staging (texture -> CPU) and the latest-frame buffer the pacer
    // repeats. SubmitFrame only swaps this buffer; all pipe I/O lives on the
    // pacer thread so capture never blocks on encoder backpressure.
    Microsoft::WRL::ComPtr<ID3D11Texture2D> staging_;
    std::mutex latestMutex_;
    std::vector<std::uint8_t> latestNv12_;
    bool haveLatest_ = false;
    std::vector<std::uint8_t> lastSentNv12_;
    bool haveSent_ = false;

    // Audio FIFO (bytes) drained by the pacer at real-time rate; silence is
    // padded for shortfalls, oldest-dropped beyond the cap.
    std::mutex audioMutex_;
    std::vector<std::uint8_t> audioFifo_;

    std::thread pacerThread_;

    PresetParams preset_{};
    bool audioEnabled_ = false;
    std::uint32_t audioChannels_ = 2;
    std::uint32_t audioRate_ = 48000;
    std::filesystem::path ffmpegPath_;
    std::filesystem::path partialPath_;      // coordinator-visible .partial.mp4
    std::filesystem::path videoIntermediate_; // audio mode: child #1 output
    std::filesystem::path audioIntermediate_; // audio mode: child #2 output
    std::atomic<bool> finalized_{false};
    std::atomic<std::uint64_t> writtenFrames_{0};
    std::atomic<bool> writeTimedOut_{false};
    std::atomic<bool> childBroken_{false};
    std::atomic<bool> pacerExit_{false};
};

} // namespace vsr::encoder

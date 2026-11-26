#include "ModelDownloader.h"

#include <windows.h>

#include <algorithm>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

#include <Infrastructure/Log.h>
#include <Infrastructure/Paths.h>
#include <Infrastructure/Strings.h>
#include <Speech/SpeechModelPack.h>
#include <Transcode/ChildProcess.h>
#include <Update/Http.h>
#include <Update/UpdateChecker.h>

namespace vsr::app {

namespace {

constexpr std::uint64_t kTempSpaceBytes = 480ull * 1024 * 1024;
constexpr std::uint64_t kDestinationSpaceBytes = 270ull * 1024 * 1024;

bool CheckSpace(const std::filesystem::path& directory, std::uint64_t required,
                std::wstring& error) {
    std::error_code ec;
    std::filesystem::create_directories(directory, ec);
    if (ec) {
        error = L"Could not create " + directory.wstring();
        return false;
    }
    const std::filesystem::space_info info = std::filesystem::space(directory, ec);
    if (ec) return true; // best effort; the write itself will report failure
    if (info.available < required) {
        error = L"Not enough free disk space in " + directory.wstring() + L" (" +
                std::to_wstring(info.available / (1024 * 1024)) + L" MB available, " +
                std::to_wstring(required / (1024 * 1024)) + L" MB needed).";
        return false;
    }
    return true;
}

std::wstring Quote(const std::filesystem::path& path) {
    return L"\"" + path.wstring() + L"\"";
}

bool MoveOrCopy(const std::filesystem::path& source,
                const std::filesystem::path& destination) {
    std::error_code ec;
    std::filesystem::remove(destination, ec);
    ec.clear();
    std::filesystem::rename(source, destination, ec);
    if (!ec) return true;
    ec.clear();
    std::filesystem::copy_file(source, destination,
                               std::filesystem::copy_options::overwrite_existing, ec);
    return !ec;
}

std::wstring ArchiveStem(const std::wstring& archiveName) {
    const std::wstring suffix = L".tar.bz2";
    if (archiveName.size() > suffix.size() &&
        archiveName.compare(archiveName.size() - suffix.size(), suffix.size(), suffix) ==
            0) {
        return archiveName.substr(0, archiveName.size() - suffix.size());
    }
    return archiveName;
}

std::filesystem::path FindExtractedRoot(const std::filesystem::path& extractDir,
                                        const std::wstring& archiveName) {
    const std::filesystem::path expected = extractDir / ArchiveStem(archiveName);
    std::error_code ec;
    if (std::filesystem::is_regular_file(expected / L"model.int8.onnx", ec) && !ec) {
        return expected;
    }
    for (std::filesystem::recursive_directory_iterator it(extractDir, ec), end;
         !ec && it != end; it.increment(ec)) {
        if (it->is_regular_file() && it->path().filename() == L"model.int8.onnx") {
            return it->path().parent_path();
        }
    }
    return {};
}

bool DownloadVerified(const std::wstring& directUrl, const std::filesystem::path& target,
                      const std::string& expectedSha, std::uint64_t expectedBytes,
                      const std::atomic<bool>& cancel, ModelInstallPhase phase,
                      int basePercent, int spanPercent, const ModelInstallProgressFn& progress,
                      std::wstring& error) {
    auto attempt = [&](const std::wstring& url) -> bool {
        auto result = update::HttpDownloadFile(
            url, target, cancel, [&](std::uint64_t downloaded, std::uint64_t total) {
                if (progress == nullptr) return;
                const std::uint64_t denominator =
                    total > 0 ? total : (expectedBytes > 0 ? expectedBytes : 1);
                int percent = basePercent;
                if (denominator > 0) {
                    percent += static_cast<int>(
                        (static_cast<double>(spanPercent) *
                         static_cast<double>(downloaded)) /
                        static_cast<double>(denominator));
                }
                percent = std::clamp(percent, basePercent, basePercent + spanPercent);
                progress(ModelInstallProgress{phase, percent, downloaded, denominator});
            });
        if (!result) {
            error = result.GetError().technicalMessage;
            std::error_code ec;
            std::filesystem::remove(target, ec);
            return false;
        }
        if (cancel.load()) {
            error = L"Download cancelled.";
            std::error_code ec;
            std::filesystem::remove(target, ec);
            return false;
        }
        auto actual = update::Sha256File(target);
        if (!actual) {
            error = actual.GetError().technicalMessage;
            std::error_code ec;
            std::filesystem::remove(target, ec);
            return false;
        }
        if (actual.Value() != expectedSha) {
            error = L"SHA-256 mismatch for " + target.filename().wstring();
            std::error_code ec;
            std::filesystem::remove(target, ec);
            return false;
        }
        if (progress != nullptr) {
            progress(ModelInstallProgress{phase, basePercent + spanPercent,
                                          expectedBytes, expectedBytes});
        }
        return true;
    };

    const std::wstring proxied = update::ProxiedDownloadUrl(directUrl);
    if (attempt(proxied)) return true;
    if (cancel.load()) return false;
    if (proxied != directUrl) {
        if (progress != nullptr) {
            progress(ModelInstallProgress{phase, basePercent, 0, expectedBytes});
        }
        return attempt(directUrl);
    }
    return false;
}

} // namespace

ModelInstallResult DownloadAndInstallSpeechModel(const std::atomic<bool>& cancel,
                                                 const ModelInstallProgressFn& progress) {
    ModelInstallResult result;
    const speech::SpeechModelDownloadInfo info = speech::SpeechModelDownload();
    const std::filesystem::path destination = speech::PreferredSpeechModelDirectory();
    if (destination.empty()) {
        result.error = L"No writable speech model directory is available.";
        return result;
    }

    const std::filesystem::path temp = AppDataDir() / L"ModelDownload";
    const std::filesystem::path archive = temp / info.archiveName;
    const std::filesystem::path vad = temp / info.vadName;
    const std::filesystem::path extractDir = temp / L"extract";

    log::Info("Speech", "MODEL_DOWNLOAD_START", L"",
              L"Downloading the local meeting transcription model.",
              {{"destination", WideToUtf8(destination.wstring())},
               {"archiveBytes", std::to_string(info.archiveBytes)}});

    if (progress != nullptr) progress(ModelInstallProgress{ModelInstallPhase::Preparing, 1, 0, 0});
    if (!CheckSpace(temp, kTempSpaceBytes, result.error)) return result;
    if (!CheckSpace(destination, kDestinationSpaceBytes, result.error)) return result;
    if (cancel.load()) {
        result.cancelled = true;
        return result;
    }

    struct TempCleanup {
        const std::filesystem::path& archive;
        const std::filesystem::path& vad;
        const std::filesystem::path& extract;
        ~TempCleanup() {
            std::error_code ec;
            std::filesystem::remove(archive, ec);
            std::filesystem::remove(vad, ec);
            std::filesystem::remove_all(extract, ec);
        }
    } cleanup{archive, vad, extractDir};

    std::error_code ec;
    std::filesystem::remove(archive, ec);
    std::filesystem::remove(vad, ec);
    std::filesystem::remove_all(extractDir, ec);
    std::filesystem::create_directories(extractDir, ec);
    if (ec) {
        result.error = L"Could not create " + extractDir.wstring();
        return result;
    }

    if (!DownloadVerified(info.archiveUrl, archive, info.archiveSha256, info.archiveBytes,
                          cancel, ModelInstallPhase::DownloadingModel, 2, 68, progress,
                          result.error)) {
        result.cancelled = cancel.load();
        return result;
    }
    if (progress != nullptr) {
        progress(ModelInstallProgress{ModelInstallPhase::VerifyingModel, 71, 0, 0});
    }
    if (cancel.load()) {
        result.cancelled = true;
        return result;
    }

    if (!DownloadVerified(info.vadUrl, vad, info.vadSha256, info.vadBytes, cancel,
                          ModelInstallPhase::DownloadingVad, 73, 5, progress,
                          result.error)) {
        result.cancelled = cancel.load();
        return result;
    }
    if (cancel.load()) {
        result.cancelled = true;
        return result;
    }

    if (progress != nullptr) {
        progress(ModelInstallProgress{ModelInstallPhase::Extracting, 80, 0, 0});
    }
    wchar_t systemDir[MAX_PATH] = {};
    const UINT systemLen = GetSystemDirectoryW(systemDir, MAX_PATH);
    if (systemLen == 0 || systemLen >= MAX_PATH) {
        result.error = L"Could not locate the Windows system directory.";
        return result;
    }
    const std::filesystem::path tar =
        std::filesystem::path(systemDir) / L"tar.exe";
    if (!std::filesystem::is_regular_file(tar, ec) || ec) {
        result.error = L"Windows tar.exe is not available (Windows 10 1803+ required).";
        return result;
    }

    const std::wstring tarArgs =
        L"-xjf " + Quote(archive) + L" -C " + Quote(extractDir);
    transcode::StreamOptions options;
    options.cancel = &cancel;
    options.timeoutMs = 300000;
    std::string stderrTail;
    bool timedOut = false;
    auto extracted = transcode::RunChildStream(tar.wstring(), tarArgs, options, stderrTail,
                                               timedOut);
    if (!extracted) {
        result.error = extracted.GetError().technicalMessage;
        return result;
    }
    if (cancel.load()) {
        result.cancelled = true;
        return result;
    }
    if (timedOut || extracted.Value() != 0) {
        result.error = L"tar.exe failed with exit code " +
                       std::to_wstring(extracted.Value()) +
                       (timedOut ? L" (timed out)." : L".");
        if (!stderrTail.empty()) result.error += L" " + Utf8ToWide(stderrTail);
        return result;
    }

    const std::filesystem::path root = FindExtractedRoot(extractDir, info.archiveName);
    if (root.empty()) {
        result.error = L"The downloaded archive does not contain model.int8.onnx.";
        return result;
    }

    if (progress != nullptr) {
        progress(ModelInstallProgress{ModelInstallPhase::Installing, 94, 0, 0});
    }
    std::filesystem::create_directories(destination, ec);
    if (ec) {
        result.error = L"Could not create " + destination.wstring();
        return result;
    }
    if (!MoveOrCopy(root / L"model.int8.onnx", destination / L"model.int8.onnx")) {
        result.error = L"Could not install model.int8.onnx.";
        return result;
    }
    if (!MoveOrCopy(root / L"tokens.txt", destination / L"tokens.txt")) {
        result.error = L"Could not install tokens.txt.";
        return result;
    }
    if (!MoveOrCopy(vad, destination / L"silero_vad.onnx")) {
        result.error = L"Could not install silero_vad.onnx.";
        return result;
    }
    if (std::filesystem::is_regular_file(root / L"LICENSE", ec) && !ec) {
        (void)MoveOrCopy(root / L"LICENSE", destination / L"MODEL-LICENSE.txt");
    } else {
        std::ofstream notice(destination / L"MODEL-LICENSE.txt",
                             std::ios::binary | std::ios::trunc);
        notice << "Ref to https://github.com/modelscope/FunASR#license\n";
    }
    {
        std::ofstream marker(destination / L"version.txt",
                             std::ios::binary | std::ios::trunc);
        marker << "sherpa-onnx-sense-voice-int8-2024-07-17 " << info.archiveSha256 << " "
               << info.vadSha256 << "\n";
    }

    result.ok = true;
    result.directory = destination;
    if (progress != nullptr) {
        progress(ModelInstallProgress{ModelInstallPhase::Done, 100, 0, 0});
    }
    log::Info("Speech", "MODEL_DOWNLOAD_INSTALLED", L"",
              L"The local meeting transcription model is installed.",
              {{"directory", WideToUtf8(destination.wstring())}});
    return result;
}

} // namespace vsr::app

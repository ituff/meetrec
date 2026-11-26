// MeetRec - in-app speech model download and installation.
//
// Downloads the pinned sherpa-onnx SenseVoice int8 archive (+ Silero VAD),
// verifies both SHA-256 digests, extracts with the Windows bsdtar and places
// the model in a writable models/sensevoice directory. Runs on a worker
// thread; the UI polls progress and can set the cancel flag.
#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>

namespace vsr::app {

enum class ModelInstallPhase {
    Preparing,
    DownloadingModel,
    VerifyingModel,
    DownloadingVad,
    Extracting,
    Installing,
    Done,
};

struct ModelInstallProgress {
    ModelInstallPhase phase = ModelInstallPhase::Preparing;
    int percent = 0; // 0..100 for the whole install
    std::uint64_t downloaded = 0;
    std::uint64_t total = 0;
};

struct ModelInstallResult {
    bool ok = false;
    bool cancelled = false;
    std::wstring error; // technical detail shown under the localized title
    std::filesystem::path directory;
};

using ModelInstallProgressFn = std::function<void(const ModelInstallProgress&)>;

ModelInstallResult DownloadAndInstallSpeechModel(const std::atomic<bool>& cancel,
                                                 const ModelInstallProgressFn& progress);

} // namespace vsr::app

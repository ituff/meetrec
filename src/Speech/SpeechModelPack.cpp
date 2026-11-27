#include "SpeechModelPack.h"

#include <windows.h>

#include <Infrastructure/Paths.h>

#include <algorithm>
#include <fstream>
#include <vector>

namespace vsr::speech {

namespace {

constexpr std::uintmax_t kMinimumModelBytes = 10ull * 1024 * 1024;
constexpr std::uintmax_t kMinimumTokensBytes = 1024;
constexpr std::uintmax_t kMinimumVadBytes = 10ull * 1024;

std::filesystem::path ExecutableDirectory() {
    std::wstring buffer(MAX_PATH, L'\0');
    for (;;) {
        const DWORD length =
            GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (length == 0) return std::filesystem::current_path();
        if (length < buffer.size()) {
            buffer.resize(length);
            break;
        }
        buffer.resize(buffer.size() * 2);
    }
    return std::filesystem::path(buffer).parent_path();
}

bool FileLargeEnough(const std::filesystem::path& path, std::uintmax_t minimum) {
    std::error_code ec;
    if (!std::filesystem::is_regular_file(path, ec) || ec) return false;
    const std::uintmax_t size = std::filesystem::file_size(path, ec);
    return !ec && size >= minimum;
}

std::wstring MissingList(const SpeechModelPaths& paths) {
    std::wstring missing;
    auto add = [&](const wchar_t* name) {
        if (!missing.empty()) missing += L", ";
        missing += name;
    };
    if (!FileLargeEnough(paths.model, kMinimumModelBytes)) add(L"model.int8.onnx");
    if (!FileLargeEnough(paths.tokens, kMinimumTokensBytes)) add(L"tokens.txt");
    if (!FileLargeEnough(paths.vad, kMinimumVadBytes)) add(L"silero_vad.onnx");
    return missing;
}

SpeechModelPaths PathsIn(const std::filesystem::path& directory) {
    SpeechModelPaths paths;
    paths.directory = directory;
    const std::filesystem::path int8 = directory / L"model.int8.onnx";
    const std::filesystem::path fp32 = directory / L"model.onnx";
    paths.model = FileLargeEnough(int8, kMinimumModelBytes) ? int8 : fp32;
    paths.tokens = directory / L"tokens.txt";
    paths.vad = directory / L"silero_vad.onnx";
    return paths;
}

bool IsWritableDirectory(const std::filesystem::path& directory) {
    std::error_code ec;
    std::filesystem::create_directories(directory, ec);
    if (ec) return false;
    const std::filesystem::path probe =
        directory / (L".write-probe-" + std::to_wstring(GetCurrentProcessId()));
    {
        std::ofstream out(probe, std::ios::binary | std::ios::trunc);
        if (!out.good()) return false;
        out.put('x');
        out.flush();
        if (!out.good()) {
            out.close();
            std::filesystem::remove(probe, ec);
            return false;
        }
    }
    std::filesystem::remove(probe, ec);
    return !ec;
}

bool DirectoryHasAnyPackFile(const std::filesystem::path& directory) {
    std::error_code ec;
    return std::filesystem::is_regular_file(directory / L"model.int8.onnx", ec) ||
           std::filesystem::is_regular_file(directory / L"model.onnx", ec) ||
           std::filesystem::is_regular_file(directory / L"tokens.txt", ec);
}

} // namespace

std::filesystem::path DefaultSpeechModelDirectory() {
    return ExecutableDirectory() / L"models" / L"sensevoice";
}

std::filesystem::path PreferredSpeechModelDirectory() {
    const std::filesystem::path portable = DefaultSpeechModelDirectory();
    if (IsWritableDirectory(portable)) return portable;
    const std::filesystem::path perUser =
        AppDataDir() / L"models" / L"sensevoice";
    if (IsWritableDirectory(perUser)) return perUser;
    return portable;
}

SpeechModelDownloadInfo SpeechModelDownload() {
    SpeechModelDownloadInfo info;
    info.archiveName =
        L"sherpa-onnx-sense-voice-zh-en-ja-ko-yue-int8-2024-07-17.tar.bz2";
    info.archiveUrl =
        L"https://github.com/k2-fsa/sherpa-onnx/releases/download/asr-models/" +
        info.archiveName;
    info.archiveSha256 =
        "7d1efa2138a65b0b488df37f8b89e3d91a60676e416f515b952358d83dfd347e";
    info.archiveBytes = 163002883;
    info.vadName = L"silero_vad.onnx";
    info.vadUrl =
        L"https://github.com/k2-fsa/sherpa-onnx/releases/download/asr-models/" +
        info.vadName;
    info.vadSha256 =
        "9e2449e1087496d8d4caba907f23e0bd3f78d91fa552479bb9c23ac09cbb1fd6";
    info.vadBytes = 643854;

    // QA/offline mirrors: same pinned files, alternate transport. The SHA-256
    // checks still apply.
    wchar_t overrideUrl[2048] = {};
    if (GetEnvironmentVariableW(L"VSR_SPEECH_MODEL_URL", overrideUrl,
                                static_cast<DWORD>(std::size(overrideUrl))) > 0 &&
        overrideUrl[0] != 0) {
        info.archiveUrl = overrideUrl;
    }
    if (GetEnvironmentVariableW(L"VSR_SPEECH_MODEL_VAD_URL", overrideUrl,
                                static_cast<DWORD>(std::size(overrideUrl))) > 0 &&
        overrideUrl[0] != 0) {
        info.vadUrl = overrideUrl;
    }
    return info;
}

SpeechModelStatus LocateSpeechModel(const std::filesystem::path& overrideDirectory) {
    std::vector<std::filesystem::path> candidates;
    if (!overrideDirectory.empty()) candidates.push_back(overrideDirectory);

    wchar_t envBuffer[MAX_PATH] = {};
    if (GetEnvironmentVariableW(L"VSR_SPEECH_MODEL_DIR", envBuffer, MAX_PATH) > 0 &&
        envBuffer[0] != 0) {
        candidates.emplace_back(envBuffer);
    }

    const std::filesystem::path exeDir = ExecutableDirectory();
    candidates.push_back(exeDir / L"models" / L"sensevoice");
    candidates.push_back(exeDir / L"models" / L"sensevoice-int8");
    candidates.push_back(AppDataDir() / L"models" / L"sensevoice");

    SpeechModelStatus status;
    // When nothing is installed, point the UI at the most specific directory
    // the caller asked for (explicit override, env, then the default).
    status.paths.directory = candidates.empty() ? DefaultSpeechModelDirectory()
                                                : candidates.front();

    // Prefer a directory that is actually complete; otherwise report the
    // first directory that looks like a (broken) pack so the UI can point at
    // it.
    for (const std::filesystem::path& directory : candidates) {
        SpeechModelPaths paths = PathsIn(directory);
        if (MissingList(paths).empty()) {
            status.ready = true;
            status.paths = std::move(paths);
            status.detail = L"ready";
            return status;
        }
    }
    for (const std::filesystem::path& directory : candidates) {
        if (DirectoryHasAnyPackFile(directory)) {
            status.paths = PathsIn(directory);
            status.detail = L"missing " + MissingList(status.paths);
            return status;
        }
    }

    status.detail = L"no speech model pack under " + DefaultSpeechModelDirectory().wstring();
    return status;
}

bool SpeechRuntimeAvailable(std::wstring& detail) {
    // The app delay-loads sherpa-onnx-c-api.dll, so a deployment without the
    // runtime must fail here instead of raising during the first API call.
    HMODULE module = LoadLibraryW(L"sherpa-onnx-c-api.dll");
    if (module == nullptr) {
        detail = L"sherpa-onnx-c-api.dll could not be loaded (error " +
                 std::to_wstring(GetLastError()) +
                 L"). Keep sherpa-onnx-c-api.dll and onnxruntime.dll next to MeetRec.exe.";
        return false;
    }
    const bool recognizer = GetProcAddress(module, "SherpaOnnxCreateOfflineRecognizer") != nullptr;
    const bool vad =
        GetProcAddress(module, "SherpaOnnxCreateVoiceActivityDetector") != nullptr;
    if (!recognizer || !vad) {
        detail = L"sherpa-onnx-c-api.dll does not expose the expected C API.";
        return false;
    }
    // Deliberately keep the module loaded: the delay-load import table binds
    // to this handle for the rest of the process.
    return true;
}

} // namespace vsr::speech

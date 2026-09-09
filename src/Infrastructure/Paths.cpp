#include "Paths.h"

#include <shlobj.h>

namespace vsr {

namespace {

std::filesystem::path LocalAppData() {
    PWSTR raw = nullptr;
    std::filesystem::path result;
    // NFR-024: Unicode shell APIs for user paths.
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &raw))) {
        result = std::filesystem::path(raw);
        CoTaskMemFree(raw);
    } else {
        wchar_t buffer[MAX_PATH] = {};
        if (GetEnvironmentVariableW(L"LOCALAPPDATA", buffer, MAX_PATH) > 0) {
            result = std::filesystem::path(buffer);
        } else {
            result = std::filesystem::path(L"C:\\");
        }
    }
    return result;
}

std::filesystem::path EnsureDir(const std::filesystem::path& p) {
    std::error_code ec;
    std::filesystem::create_directories(p, ec);
    return p;
}

} // namespace

std::filesystem::path AppDataDir() {
    return EnsureDir(LocalAppData() / L"VirtualScreenRecorder");
}

std::filesystem::path LogDir() {
    return EnsureDir(AppDataDir() / L"Logs");
}

std::filesystem::path SettingsPath() {
    return AppDataDir() / L"settings.json";
}

std::filesystem::path DiagnosticsDir() {
    return EnsureDir(AppDataDir() / L"Diagnostics");
}

} // namespace vsr

// Virtual Screen Recorder - diagnostics package (FR-096; T061).
//
// Packages logs plus sanitized runtime metadata into a .zip under
// %LOCALAPPDATA%\VirtualScreenRecorder\Diagnostics. Contents are privacy-
// filtered (FR-094): no video frames, window titles, credentials, tokens, or
// environment values are included.
#pragma once

#include <filesystem>
#include <string>

#include <Infrastructure/Result.h>

namespace vsr::app {

struct DiagnosticsContext {
    std::wstring appVersion;
    std::wstring correlationId;
    std::wstring encoderName;
    bool encoderHardware = false;
    std::wstring gpuDescription;
    std::wstring driverDevicePresent = L"unknown"; // "yes" / "no" / "unknown"
    std::wstring osBuild;
};

// Builds the diagnostics package; returns the created .zip path (or a folder
// path when archiving is unavailable on this machine).
Result<std::filesystem::path> CreateDiagnosticsPackage(const DiagnosticsContext& context);

} // namespace vsr::app

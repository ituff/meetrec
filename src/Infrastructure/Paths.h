// Virtual Screen Recorder - per-user data locations (spec.md §7.5).
#pragma once

#include <filesystem>

namespace vsr {

// %LOCALAPPDATA%\VirtualScreenRecorder — created on demand.
std::filesystem::path AppDataDir();
// %LOCALAPPDATA%\VirtualScreenRecorder\Logs
std::filesystem::path LogDir();
// %LOCALAPPDATA%\VirtualScreenRecorder\settings.json
std::filesystem::path SettingsPath();
// %LOCALAPPDATA%\VirtualScreenRecorder\Diagnostics
std::filesystem::path DiagnosticsDir();

} // namespace vsr

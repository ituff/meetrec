// Virtual Screen Recorder - local crash dump capture (T103).
//
// Strictly opt-in via the `enableCrashDumps` setting (default off). When
// enabled, an unhandled exception writes a minidump beside the other user
// data (%LOCALAPPDATA%\VirtualScreenRecorder\CrashDumps) for manual
// diagnostics. Nothing is ever uploaded (P1/T103 privacy default).
#pragma once

#include <filesystem>

namespace vsr {

class CrashReporter {
public:
    // Installs the unhandled-exception filter. Safe to call once per
    // process; later calls are no-ops. Dump writing is enabled only when
    // `enabled` is true; without it the filter still converts crashes into a
    // logged, graceful exit code.
    static void Install(bool enabled);

    // Directory where dumps are written (created on demand).
    // %LOCALAPPDATA%\VirtualScreenRecorder\CrashDumps
    static std::filesystem::path DumpDirectory();
};

} // namespace vsr

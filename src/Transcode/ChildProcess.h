// MeetRec - child-process helpers for offline transcoding (ADR-007).
//
// ffmpeg/ffprobe are launched as windowless children at below-normal
// priority so a background job never competes with the recorder's real-time
// encoder. Both helpers are bounded: pipes are drained on reader threads and
// cancellation terminates the child within roughly 250 ms.
#pragma once

#include <windows.h>

#include <atomic>
#include <cstddef>
#include <functional>
#include <string>

#include <Infrastructure/Result.h>

namespace vsr::transcode {

struct ChildOutput {
    DWORD exitCode = 0;
    std::string stdoutText;
    std::string stderrText;
    bool timedOut = false;
};

// Runs a child and captures stdout/stderr (bounded). A start failure is an
// Error; a non-zero exit code is reported through ChildOutput.
Result<ChildOutput> RunChildCapture(const std::wstring& executable,
                                    const std::wstring& arguments,
                                    DWORD timeoutMs = 30000);

struct StreamOptions {
    // Polled while the child runs; set to true to terminate it.
    const std::atomic<bool>* cancel = nullptr;
    // Called from the stdout reader thread for every complete line.
    std::function<void(const std::string&)> onStdoutLine;
    // 0 = wait indefinitely.
    DWORD timeoutMs = 0;
    // Only this many trailing stderr bytes are retained for diagnostics.
    std::size_t stderrTailBytes = 8192;
    // Hard cap on stdout bytes consumed by onStdoutLine (protects against a
    // child that floods the pipe).
    std::size_t stdoutLimitBytes = 4 * 1024 * 1024;
};

// Runs a child while streaming stdout lines. Returns the exit code; `timedOut`
// and `stderrTail` describe abnormal exits.
Result<DWORD> RunChildStream(const std::wstring& executable,
                             const std::wstring& arguments, const StreamOptions& options,
                             std::string& stderrTail, bool& timedOut);

} // namespace vsr::transcode

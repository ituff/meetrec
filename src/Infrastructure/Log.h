// Virtual Screen Recorder - structured JSON-lines logger (FR-093..FR-095).
//
// Records are written asynchronously by a single worker thread so UI and
// capture threads never block on disk I/O (design.md §4.11, §5). Files rotate
// at 10 MB per file; retention keeps at most 5 files, 50 MB total, and 7 days
// of age, whichever is reached first.
//
// Privacy rule (FR-094): callers must never log captured image content,
// document content, credentials, tokens, or environment values. Window titles
// are sanitized via SanitizeWindowTitle before they reach the log.
#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include <windows.h>

namespace vsr {

enum class LogLevel { Debug = 0, Info = 1, Warning = 2, Error = 3 };

using LogProperties = std::vector<std::pair<std::string, std::string>>;

struct LogConfig {
    std::filesystem::path directory;
    std::wstring filePrefix = L"vsr";
    LogLevel minLevel = LogLevel::Info;
    std::uintmax_t maxFileBytes = 10ull * 1024 * 1024;
    std::size_t maxFiles = 5;
    std::uintmax_t maxTotalBytes = 50ull * 1024 * 1024;
    std::chrono::system_clock::duration maxAge = std::chrono::hours(24) * 7;
};

const char* ToString(LogLevel level) noexcept;

class Logger {
public:
    static Logger& Instance() noexcept;

    // Installs the configuration and starts the worker. Safe to call once at
    // startup; a second call reconfigures the output directory.
    void Configure(const LogConfig& config);
    // Flushes pending records and stops the worker (bounded wait).
    void Shutdown();

    void Write(LogLevel level, const char* component, const char* eventId,
               const std::wstring& correlationId, std::wstring message,
               LogProperties properties = {});

    // Synchronously enforces retention (delete over-age and over-quota files).
    // Called by the worker after rotation; exposed for tests.
    void EnforceRetentionLocked();

    // Test seam: number of records currently queued.
    std::size_t PendingCountForTesting() const;

private:
    Logger() = default;
    ~Logger();
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    void WorkerLoop();
    void RotateIfFullLocked();
    std::filesystem::path NextFilePathLocked();
    void OpenFileLocked();

    struct ConfigState {
        LogConfig config;
        bool valid = false;
    };

    mutable std::mutex queueMutex_;
    std::vector<std::wstring> queue_;
    ConfigState config_;
    std::filesystem::path currentFile_;
    std::uintmax_t currentFileSize_ = 0;
    bool stop_ = false;
    void* worker_ = nullptr; // std::thread; void* avoids <thread> in the header
    void* wakeEvent_ = nullptr; // HANDLE
};

// Convenience wrappers. `component` and `eventId` are stable identifiers.
namespace log {
inline void Debug(const char* component, const char* eventId, const std::wstring& corr,
                  const std::wstring& msg, LogProperties props = {}) {
    Logger::Instance().Write(LogLevel::Debug, component, eventId, corr, msg, std::move(props));
}
inline void Info(const char* component, const char* eventId, const std::wstring& corr,
                 const std::wstring& msg, LogProperties props = {}) {
    Logger::Instance().Write(LogLevel::Info, component, eventId, corr, msg, std::move(props));
}
inline void Warning(const char* component, const char* eventId, const std::wstring& corr,
                    const std::wstring& msg, LogProperties props = {}) {
    Logger::Instance().Write(LogLevel::Warning, component, eventId, corr, msg, std::move(props));
}
inline void Error(const char* component, const char* eventId, const std::wstring& corr,
                  const std::wstring& msg, LogProperties props = {}) {
    Logger::Instance().Write(LogLevel::Error, component, eventId, corr, msg, std::move(props));
}
} // namespace log

// Diagnostics-facing sanitizer: window titles may contain document names, so
// they are never written verbatim (design.md §4.11). Returns "<omitted-title>"
// plus the title length in characters.
std::wstring SanitizeWindowTitle(const std::wstring& title);

} // namespace vsr

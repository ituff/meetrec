#include "Log.h"

#include "Clock.h"
#include "Json.h"
#include "Strings.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <thread>

namespace vsr {

namespace {

std::wstring TimestampIso8601Utc(std::int64_t systemMs) {
    const std::time_t seconds = static_cast<std::time_t>(systemMs / 1000);
    const int millis = static_cast<int>(systemMs % 1000);
    std::tm tmUtc{};
    gmtime_s(&tmUtc, &seconds);
    wchar_t buf[40];
    swprintf_s(buf, L"%04d-%02d-%02dT%02d:%02d:%02d.%03dZ", tmUtc.tm_year + 1900,
               tmUtc.tm_mon + 1, tmUtc.tm_mday, tmUtc.tm_hour, tmUtc.tm_min, tmUtc.tm_sec,
               millis);
    return buf;
}

std::string FileNameStamp(std::int64_t systemMs) {
    const std::time_t seconds = static_cast<std::time_t>(systemMs / 1000);
    std::tm tmLocal{};
    localtime_s(&tmLocal, &seconds);
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%04d%02d%02d-%02d%02d%02d", tmLocal.tm_year + 1900,
                  tmLocal.tm_mon + 1, tmLocal.tm_mday, tmLocal.tm_hour, tmLocal.tm_min,
                  tmLocal.tm_sec);
    return buf;
}

// Returns the file age in milliseconds given the "vsr-YYYYMMDD-hhmmss" stamp
// embedded in log file names.
bool ParseStampMs(const std::wstring& fileName, std::int64_t& outMs) {
    // vsr-YYYYMMDD-hhmmss.log
    if (fileName.size() < 20 || fileName.compare(0, 4, L"vsr-") != 0) return false;
    const int y = _wtoi(fileName.substr(4, 4).c_str());
    const int mo = _wtoi(fileName.substr(8, 2).c_str());
    const int d = _wtoi(fileName.substr(10, 2).c_str());
    const int h = _wtoi(fileName.substr(13, 2).c_str());
    const int mi = _wtoi(fileName.substr(15, 2).c_str());
    const int s = _wtoi(fileName.substr(17, 2).c_str());
    if (y <= 1990 || mo < 1 || mo > 12 || d < 1 || d > 31) return false;
    std::tm tmLocal{};
    tmLocal.tm_year = y - 1900;
    tmLocal.tm_mon = mo - 1;
    tmLocal.tm_mday = d;
    tmLocal.tm_hour = h;
    tmLocal.tm_min = mi;
    tmLocal.tm_sec = s;
    tmLocal.tm_isdst = -1;
    const std::time_t t = std::mktime(&tmLocal);
    if (t == -1) return false;
    outMs = static_cast<std::int64_t>(t) * 1000;
    return true;
}

} // namespace

const char* ToString(LogLevel level) noexcept {
    switch (level) {
        case LogLevel::Debug: return "Debug";
        case LogLevel::Info: return "Info";
        case LogLevel::Warning: return "Warning";
        case LogLevel::Error: return "Error";
    }
    return "Info";
}

Logger& Logger::Instance() noexcept {
    static Logger logger;
    return logger;
}

Logger::~Logger() {
    Shutdown();
}

void Logger::Configure(const LogConfig& config) {
    const bool first = worker_ == nullptr;
    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        config_.config = config;
        config_.valid = true;
        currentFile_.clear();
        currentFileSize_ = 0;
    }
    if (first) {
        wakeEvent_ = new HANDLE(CreateEventW(nullptr, FALSE, FALSE, nullptr));
        auto* thread = new std::thread([this] { WorkerLoop(); });
        worker_ = thread;
    }
}

void Logger::Shutdown() {
    std::thread* thread = nullptr;
    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        stop_ = true;
        thread = static_cast<std::thread*>(worker_);
    }
    if (thread != nullptr) {
        SetEvent(*static_cast<HANDLE*>(wakeEvent_));
        if (thread->joinable()) thread->join();
        delete thread;
        {
            std::lock_guard<std::mutex> lock(queueMutex_);
            worker_ = nullptr;
            stop_ = false;
        }
    }
}

void Logger::Write(LogLevel level, const char* component, const char* eventId,
                   const std::wstring& correlationId, std::wstring message,
                   LogProperties properties) {
    LogConfig snapshot;
    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        if (!config_.valid || static_cast<int>(level) < static_cast<int>(config_.config.minLevel)) {
            return;
        }
        snapshot = config_.config;
    }

    const std::int64_t nowMs = SystemClock{}.SystemTimeMs();
    json::Value record{json::Object{}};
    record.Set("timestampUtc", json::Value(WideToUtf8(TimestampIso8601Utc(nowMs))));
    record.Set("level", json::Value(ToString(level)));
    record.Set("component", json::Value(component));
    record.Set("eventId", json::Value(eventId));
    record.Set("correlationId", json::Value(WideToUtf8(correlationId)));
    record.Set("message", json::Value(WideToUtf8(message)));
    if (!properties.empty()) {
        json::Value props{json::Object{}};
        for (const auto& kv : properties) {
            props.Set(kv.first, json::Value(kv.second));
        }
        record.Set("properties", std::move(props));
    }

    std::lock_guard<std::mutex> lock(queueMutex_);
    // Bounded queue: never let logging memory grow without limit.
    if (queue_.size() >= 4096) {
        queue_.erase(queue_.begin());
    }
    queue_.push_back(Utf8ToWide(record.Serialize()));
    if (wakeEvent_ != nullptr) {
        SetEvent(*static_cast<HANDLE*>(wakeEvent_));
    }
}

std::size_t Logger::PendingCountForTesting() const {
    std::lock_guard<std::mutex> lock(queueMutex_);
    return queue_.size();
}

std::filesystem::path Logger::NextFilePathLocked() {
    const std::int64_t nowMs = SystemClock{}.SystemTimeMs();
    const std::string stamp = FileNameStamp(nowMs);
    wchar_t buf[64];
    swprintf_s(buf, L"%s-%hs.log", config_.config.filePrefix.c_str(), stamp.c_str());
    return config_.config.directory / buf;
}

void Logger::RotateIfFullLocked() {
    if (currentFileSize_ < config_.config.maxFileBytes) return;
    if (config_.config.directory.empty()) return;
    // Windows cannot replace an open file; rotate by renaming the current file
    // to a stamped archive name, then start a fresh current file.
    const std::int64_t nowMs = SystemClock{}.SystemTimeMs();
    const std::string stamp = FileNameStamp(nowMs);
    wchar_t archive[96];
    swprintf_s(archive, L"%s-%hs.1.log", config_.config.filePrefix.c_str(), stamp.c_str());
    std::error_code ec;
    std::filesystem::rename(currentFile_, config_.config.directory / archive, ec);
    currentFile_.clear();
    currentFileSize_ = 0;
}

void Logger::EnforceRetentionLocked() {
    if (config_.config.directory.empty()) return;
    const std::int64_t nowMs = SystemClock{}.SystemTimeMs();
    const std::int64_t maxAgeMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                      config_.config.maxAge)
                                      .count();

    struct LogFile {
        std::filesystem::path path;
        std::int64_t stampMs;
        std::uintmax_t size;
    };
    std::vector<LogFile> files;
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(config_.config.directory, ec)) {
        if (!entry.is_regular_file()) continue;
        const auto name = entry.path().filename().wstring();
        // The active file is never deleted; archives carry a trailing ".N".
        if (name.size() > 4 && name.compare(name.size() - 4, 4, L".log") == 0) {
            std::int64_t stamp = 0;
            if (!ParseStampMs(name, stamp)) continue;
            files.push_back({entry.path(), stamp, entry.file_size(ec)});
        }
    }
    std::sort(files.begin(), files.end(),
              [](const LogFile& a, const LogFile& b) { return a.stampMs < b.stampMs; });

    std::uintmax_t total = 0;
    for (const auto& f : files) total += f.size;

    // Delete oldest first when over age, count, or size quotas.
    for (size_t i = 0; i < files.size() && files.size() - i > config_.config.maxFiles; ++i) {
        std::filesystem::remove(files[i].path, ec);
        total -= files[i].size;
        files[i].size = 0;
    }
    for (size_t i = 0; i < files.size(); ++i) {
        if (files[i].size == 0) continue;
        if (nowMs - files[i].stampMs > maxAgeMs) {
            std::filesystem::remove(files[i].path, ec);
            total -= files[i].size;
            files[i].size = 0;
        }
    }
    for (size_t i = 0; i < files.size() && total > config_.config.maxTotalBytes; ++i) {
        if (files[i].size == 0) continue;
        std::filesystem::remove(files[i].path, ec);
        total -= files[i].size;
        files[i].size = 0;
    }
}

void Logger::OpenFileLocked() {
    if (config_.config.directory.empty()) return;
    std::error_code ec;
    std::filesystem::create_directories(config_.config.directory, ec);
    currentFile_ = NextFilePathLocked();
    currentFileSize_ = 0;
    // Truncate if this exact file already exists (same-second restart).
    DeleteFileW(currentFile_.c_str());
}

void Logger::WorkerLoop() {
    HANDLE wake = *static_cast<HANDLE*>(wakeEvent_);
    for (;;) {
        bool stopAndEmpty = false;
        std::vector<std::wstring> batch;
        {
            std::unique_lock<std::mutex> lock(queueMutex_);
            if (stop_ && queue_.empty()) {
                stopAndEmpty = true;
            } else if (queue_.empty()) {
                lock.unlock();
                WaitForSingleObject(wake, 1000);
                lock.lock();
            }
            batch.swap(queue_);
        }

        std::lock_guard<std::mutex> fileLock(queueMutex_);
        if (stopAndEmpty) {
            // Close (and thereby flush) the active file on shutdown.
            currentFile_.clear();
            currentFileSize_ = 0;
            return;
        }
        if (!config_.valid) continue;
        if (currentFile_.empty()) OpenFileLocked();
        if (currentFile_.empty()) continue;

        HANDLE file = CreateFileW(currentFile_.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ, nullptr,
                                  OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE) {
            // Keep records queued so a transient lock does not lose logs; the
            // next iteration retries. Cap to avoid unbounded growth.
            for (const auto& line : batch) {
                if (queue_.size() >= 4096) break;
                queue_.push_back(line);
            }
            continue;
        }
        for (const auto& line : batch) {
            const std::string utf8 = WideToUtf8(line) + "\n";
            DWORD written = 0;
            WriteFile(file, utf8.c_str(), static_cast<DWORD>(utf8.size()), &written, nullptr);
            currentFileSize_ += written;
        }
        FlushFileBuffers(file);
        CloseHandle(file);
        RotateIfFullLocked();
        EnforceRetentionLocked();
    }
}

std::wstring SanitizeWindowTitle(const std::wstring& title) {
    if (title.empty()) return L"<empty-title>";
    return L"<omitted-title:" + std::to_wstring(title.size()) + L" chars>";
}

} // namespace vsr

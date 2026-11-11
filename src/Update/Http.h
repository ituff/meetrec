// MeetRec - minimal WinHTTP client for GitHub release checks/downloads.
//
// HTTPS only in production; the API URL can be overridden with
// VSR_UPDATE_API_URL (and asset URLs are whatever the JSON names), which is
// how the updater is exercised against a local mock during qualification.
#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>

#include <Infrastructure/Result.h>

namespace vsr::update {

struct HttpText {
    unsigned long status = 0;
    std::string body;
};

Result<HttpText> HttpGetText(const std::wstring& url);

using DownloadProgress =
    std::function<void(std::uint64_t downloaded, std::uint64_t total)>;

// Streams `url` into `destination` (replacing it). Cancellation is checked
// between reads; a cancelled or failed download leaves no partial file.
Result<void> HttpDownloadFile(const std::wstring& url,
                              const std::filesystem::path& destination,
                              const std::atomic<bool>& cancel,
                              const DownloadProgress& onProgress);

} // namespace vsr::update

#include "Http.h"

#include <windows.h>
#include <winhttp.h>

#include <algorithm>
#include <fstream>
#include <vector>

#include <Infrastructure/Error.h>

#pragma comment(lib, "winhttp.lib")

namespace vsr::update {

namespace {

constexpr wchar_t kUserAgent[] = L"MeetRec-Updater/1.0";
constexpr DWORD kTimeoutMs = 15000;

class InternetHandle {
public:
    explicit InternetHandle(HINTERNET handle = nullptr) : handle_(handle) {}
    ~InternetHandle() { Reset(); }
    InternetHandle(const InternetHandle&) = delete;
    InternetHandle& operator=(const InternetHandle&) = delete;
    InternetHandle(InternetHandle&& other) noexcept : handle_(other.handle_) {
        other.handle_ = nullptr;
    }
    InternetHandle& operator=(InternetHandle&& other) noexcept {
        if (this != &other) {
            Reset(other.handle_);
            other.handle_ = nullptr;
        }
        return *this;
    }

    HINTERNET Get() const noexcept { return handle_; }
    void Reset(HINTERNET handle = nullptr) {
        if (handle_ != nullptr) WinHttpCloseHandle(handle_);
        handle_ = handle;
    }

private:
    HINTERNET handle_ = nullptr;
};

Error HttpError(const wchar_t* what) {
    return ErrorFromWin32(GetLastError(), ErrorCode::InternalState,
                          L"Could not reach the update server.",
                          std::wstring(what) + L" failed");
}

struct ParsedUrl {
    std::wstring host;
    std::wstring path;
    INTERNET_PORT port = 0;
    bool secure = false;
};

Result<ParsedUrl> ParseUrl(const std::wstring& url) {
    URL_COMPONENTS components = {};
    components.dwStructSize = sizeof(components);
    components.dwSchemeLength = static_cast<DWORD>(-1);
    components.dwHostNameLength = static_cast<DWORD>(-1);
    components.dwUrlPathLength = static_cast<DWORD>(-1);
    components.dwExtraInfoLength = static_cast<DWORD>(-1);
    if (!WinHttpCrackUrl(url.c_str(), static_cast<DWORD>(url.size()), 0, &components)) {
        return Error(ErrorCode::InternalState, L"The update address is invalid.",
                     L"WinHttpCrackUrl failed for " + url);
    }
    ParsedUrl parsed;
    parsed.host.assign(components.lpszHostName, components.dwHostNameLength);
    parsed.path.assign(components.lpszUrlPath, components.dwUrlPathLength);
    if (components.dwExtraInfoLength > 0) {
        parsed.path.append(components.lpszExtraInfo, components.dwExtraInfoLength);
    }
    parsed.port = components.nPort;
    parsed.secure = components.nScheme == INTERNET_SCHEME_HTTPS;
    if (parsed.host.empty()) {
        return Error(ErrorCode::InternalState, L"The update address is invalid.",
                     L"URL has no host: " + url);
    }
    return parsed;
}

struct Request {
    InternetHandle session;
    InternetHandle connection;
    InternetHandle request;
};
Result<Request> OpenRequest(const std::wstring& url) {
    auto parsed = ParseUrl(url);
    if (!parsed) return parsed.TakeError();
    const ParsedUrl& target = parsed.Value();

    Request handles;
    handles.session.Reset(WinHttpOpen(kUserAgent, WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                      WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
    if (handles.session.Get() == nullptr) return HttpError(L"WinHttpOpen");
    WinHttpSetTimeouts(handles.session.Get(), static_cast<int>(kTimeoutMs),
                       static_cast<int>(kTimeoutMs), static_cast<int>(kTimeoutMs),
                       static_cast<int>(kTimeoutMs));

    handles.connection.Reset(
        WinHttpConnect(handles.session.Get(), target.host.c_str(), target.port, 0));
    if (handles.connection.Get() == nullptr) return HttpError(L"WinHttpConnect");

    DWORD flags = target.secure ? WINHTTP_FLAG_SECURE : 0;
    handles.request.Reset(WinHttpOpenRequest(handles.connection.Get(), L"GET",
                                             target.path.c_str(), nullptr,
                                             WINHTTP_NO_REFERER,
                                             WINHTTP_DEFAULT_ACCEPT_TYPES, flags));
    if (handles.request.Get() == nullptr) return HttpError(L"WinHttpOpenRequest");

    DWORD redirect = WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS;
    WinHttpSetOption(handles.request.Get(), WINHTTP_OPTION_REDIRECT_POLICY, &redirect,
                     sizeof(redirect));
    WinHttpAddRequestHeaders(handles.request.Get(),
                             L"Accept: application/vnd.github+json\r\n", static_cast<DWORD>(-1),
                             WINHTTP_ADDREQ_FLAG_ADD);

    if (!WinHttpSendRequest(handles.request.Get(), WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                            WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
        return HttpError(L"WinHttpSendRequest");
    }
    if (!WinHttpReceiveResponse(handles.request.Get(), nullptr)) {
        return HttpError(L"WinHttpReceiveResponse");
    }

    DWORD status = 0;
    DWORD size = sizeof(status);
    if (!WinHttpQueryHeaders(handles.request.Get(),
                             WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                             WINHTTP_HEADER_NAME_BY_INDEX, &status, &size,
                             WINHTTP_NO_HEADER_INDEX)) {
        return HttpError(L"WinHttpQueryHeaders");
    }
    if (status != 200) {
        return Error(ErrorCode::InternalState,
                     L"The update server returned an error.",
                     L"HTTP status " + std::to_wstring(status) + L" for " + url);
    }
    return handles;
}

std::uint64_t ContentLength(HINTERNET request) {
    DWORD length = 0;
    DWORD size = sizeof(length);
    if (WinHttpQueryHeaders(request, WINHTTP_QUERY_CONTENT_LENGTH | WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX, &length, &size,
                            WINHTTP_NO_HEADER_INDEX)) {
        return length;
    }
    return 0;
}

} // namespace

Result<HttpText> HttpGetText(const std::wstring& url) {
    auto opened = OpenRequest(url);
    if (!opened) return opened.TakeError();
    Request& handles = opened.Value();

    HttpText result;
    constexpr std::size_t kMaxBytes = 2 * 1024 * 1024;
    std::vector<char> buffer(64 * 1024);
    for (;;) {
        DWORD read = 0;
        if (!WinHttpReadData(handles.request.Get(), buffer.data(),
                             static_cast<DWORD>(buffer.size()), &read)) {
            return HttpError(L"WinHttpReadData");
        }
        if (read == 0) break;
        if (result.body.size() + read > kMaxBytes) {
            return Error(ErrorCode::InternalState, L"The update response was too large.",
                         L"HTTP response exceeded the text cap");
        }
        result.body.append(buffer.data(), read);
    }
    result.status = 200;
    return result;
}

Result<void> HttpDownloadFile(const std::wstring& url,
                              const std::filesystem::path& destination,
                              const std::atomic<bool>& cancel,
                              const DownloadProgress& onProgress) {
    auto opened = OpenRequest(url);
    if (!opened) return opened.TakeError();
    Request& handles = opened.Value();

    const std::uint64_t total = ContentLength(handles.request.Get());
    std::ofstream file(destination, std::ios::binary | std::ios::trunc);
    if (!file.good()) {
        return Error(ErrorCode::OutputNotWritable,
                     L"Could not write the update package.",
                     L"Failed to open " + destination.wstring());
    }

    std::vector<char> buffer(256 * 1024);
    std::uint64_t downloaded = 0;
    for (;;) {
        if (cancel.load()) {
            file.close();
            std::error_code ec;
            std::filesystem::remove(destination, ec);
            return Error(ErrorCode::InternalState, L"The update download was cancelled.",
                         L"Download cancelled by the user");
        }
        DWORD read = 0;
        if (!WinHttpReadData(handles.request.Get(), buffer.data(),
                             static_cast<DWORD>(buffer.size()), &read)) {
            file.close();
            std::error_code ec;
            std::filesystem::remove(destination, ec);
            return HttpError(L"WinHttpReadData");
        }
        if (read == 0) break;
        file.write(buffer.data(), read);
        if (!file.good()) {
            file.close();
            std::error_code ec;
            std::filesystem::remove(destination, ec);
            return Error(ErrorCode::OutputNotWritable,
                         L"Could not write the update package.",
                         L"Write failed for " + destination.wstring());
        }
        downloaded += read;
        if (onProgress) onProgress(downloaded, total);
    }
    file.close();
    if (!file.good()) {
        std::error_code ec;
        std::filesystem::remove(destination, ec);
        return Error(ErrorCode::OutputNotWritable, L"Could not write the update package.",
                     L"Close failed for " + destination.wstring());
    }
    if (downloaded == 0) {
        std::error_code ec;
        std::filesystem::remove(destination, ec);
        return Error(ErrorCode::InternalState, L"The update package was empty.",
                     L"HTTP body contained no bytes");
    }
    return Result<void>{};
}

} // namespace vsr::update

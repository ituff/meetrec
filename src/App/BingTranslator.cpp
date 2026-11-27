#include "BingTranslator.h"

#include <windows.h>
#include <winhttp.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include <Infrastructure/Json.h>
#include <Infrastructure/Strings.h>

#pragma comment(lib, "winhttp.lib")

namespace vsr::app {

namespace {

constexpr wchar_t kUserAgent[] =
    L"Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
    L"(KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36 Edg/120.0.0.0";
constexpr DWORD kTimeoutMs = 15000;
constexpr std::size_t kMaxBodyBytes = 1024 * 1024;
constexpr std::size_t kMaxTextUnits = 1000; // Edge ttranslate limit

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

std::string JsonEscapeAscii(const std::wstring& text) {
    std::string out;
    out.reserve(text.size() * 6 + 4);
    out.push_back('[');
    out.push_back('"');
    std::size_t count = std::min(text.size(), kMaxTextUnits);
    // Never end the slice on a high surrogate (the server would reject the
    // split pair).
    if (count > 0 && text[count - 1] >= 0xD800 && text[count - 1] <= 0xDBFF) {
        --count;
    }
    for (std::size_t i = 0; i < count; ++i) {
        const wchar_t c = text[i];
        switch (c) {
            case L'"': out += "\\\""; break;
            case L'\\': out += "\\\\"; break;
            case L'\b': out += "\\b"; break;
            case L'\f': out += "\\f"; break;
            case L'\n': out += "\\n"; break;
            case L'\r': out += "\\r"; break;
            case L'\t': out += "\\t"; break;
            default:
                if (c >= 0x20 && c <= 0x7E) {
                    out.push_back(static_cast<char>(c));
                } else {
                    char buffer[7] = {};
                    std::snprintf(buffer, sizeof(buffer), "\\u%04X",
                                  static_cast<unsigned>(static_cast<std::uint16_t>(c)));
                    out += buffer;
                }
                break;
        }
    }
    out += "\"]";
    return out;
}

std::wstring HttpsPathFor(const std::string& targetLanguage) {
    return L"/translate/translatetext?from=&to=" +
           Utf8ToWide(targetLanguage) + L"&isEnterpriseClient=false";
}

bool ReadResponse(HINTERNET request, std::string& body) {
    for (;;) {
        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(request, &available)) return false;
        if (available == 0) return true;
        if (body.size() + available > kMaxBodyBytes) return false;
        const std::size_t offset = body.size();
        body.resize(offset + available);
        DWORD read = 0;
        if (!WinHttpReadData(request, body.data() + offset, available, &read)) {
            body.resize(offset);
            return false;
        }
        body.resize(offset + read);
        if (read == 0) return true;
    }
}

TranslationResult Attempt(const std::string& targetLanguage, const std::string& body) {
    TranslationResult result;
    InternetHandle session;
    InternetHandle connection;
    InternetHandle request;

    session.Reset(WinHttpOpen(kUserAgent, WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                              WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
    if (session.Get() == nullptr) {
        result.error = L"WinHttpOpen failed.";
        return result;
    }
    WinHttpSetTimeouts(session.Get(), static_cast<int>(kTimeoutMs),
                       static_cast<int>(kTimeoutMs), static_cast<int>(kTimeoutMs),
                       static_cast<int>(kTimeoutMs));
    connection.Reset(WinHttpConnect(session.Get(), L"edge.microsoft.com",
                                    INTERNET_DEFAULT_HTTPS_PORT, 0));
    if (connection.Get() == nullptr) {
        result.error = L"Could not connect to edge.microsoft.com.";
        return result;
    }
    const std::wstring path = HttpsPathFor(targetLanguage);
    request.Reset(WinHttpOpenRequest(connection.Get(), L"POST", path.c_str(), nullptr,
                                     WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                     WINHTTP_FLAG_SECURE));
    if (request.Get() == nullptr) {
        result.error = L"Could not create the translation request.";
        return result;
    }
    constexpr wchar_t kHeaders[] =
        L"Content-Type: application/json\r\n"
        L"Accept: application/json\r\n";
    if (!WinHttpSendRequest(request.Get(), kHeaders,
                            static_cast<DWORD>(-1),
                            const_cast<char*>(body.data()),
                            static_cast<DWORD>(body.size()),
                            static_cast<DWORD>(body.size()), 0)) {
        result.error = L"Could not send the translation request.";
        return result;
    }
    if (!WinHttpReceiveResponse(request.Get(), nullptr)) {
        result.error = L"No response from the translation service.";
        return result;
    }

    DWORD status = 0;
    DWORD statusSize = sizeof(status);
    if (!WinHttpQueryHeaders(
            request.Get(),
            WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize,
            WINHTTP_NO_HEADER_INDEX)) {
        result.error = L"Could not read the translation response status.";
        return result;
    }
    std::string response;
    if (!ReadResponse(request.Get(), response)) {
        result.error = L"Could not read the translation response.";
        return result;
    }
    if (status != 200) {
        result.error = L"Translation service returned HTTP " +
                       std::to_wstring(status) + L".";
        return result;
    }

    bool parsed = false;
    const json::Value root = json::Parse(response, &parsed);
    if (!parsed || root.GetType() != json::Type::Array || root.GetArray().empty()) {
        result.error = L"The translation response was malformed.";
        return result;
    }
    const json::Value& first = root.GetArray().front();
    const json::Value& detected = first.Find("detectedLanguage");
    if (detected.GetType() == json::Type::Object) {
        const json::Value& language = detected.Find("language");
        if (language.GetType() == json::Type::String) {
            result.detectedLanguage = language.AsString();
        }
    }
    const json::Value& translations = first.Find("translations");
    if (translations.GetType() != json::Type::Array ||
        translations.GetArray().empty()) {
        result.error = L"The translation response contained no text.";
        return result;
    }
    const json::Value& translated = translations.GetArray().front().Find("text");
    if (translated.GetType() != json::Type::String) {
        result.error = L"The translation response contained no text.";
        return result;
    }
    result.text = Utf8ToWide(translated.AsString());
    result.ok = !result.text.empty();
    if (!result.ok) result.error = L"The translation was empty.";
    return result;
}

} // namespace

TranslationResult TranslateWithBing(const std::wstring& text,
                                    const std::string& targetLanguage) {
    TranslationResult result;
    if (text.empty()) return result;
    if (targetLanguage.empty()) {
        result.error = L"No target language was selected.";
        return result;
    }
    for (char c : targetLanguage) {
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_')) {
            result.error = L"Invalid target language.";
            return result;
        }
    }
    const std::string body = JsonEscapeAscii(text);

    // The endpoint occasionally returns a transient 5xx; one retry keeps live
    // captions from losing a sentence to a single blip.
    for (int attempt = 0; attempt < 2; ++attempt) {
        result = Attempt(targetLanguage, body);
        if (result.ok) return result;
        if (attempt == 0) Sleep(250);
    }
    return result;
}

} // namespace vsr::app

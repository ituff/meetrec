#include "Strings.h"

#include <algorithm>
#include <cctype>

namespace vsr {

std::string WideToUtf8(const std::wstring& wide) {
    if (wide.empty()) return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(),
                                         static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<size_t>(size), '\0');
    if (size > 0) {
        WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), static_cast<int>(wide.size()),
                            out.data(), size, nullptr, nullptr);
    }
    return out;
}

std::wstring Utf8ToWide(const std::string& utf8) {
    if (utf8.empty()) return {};
    const int size = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(),
                                         static_cast<int>(utf8.size()), nullptr, 0);
    std::wstring out(static_cast<size_t>(size), L'\0');
    if (size > 0) {
        MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), static_cast<int>(utf8.size()), out.data(),
                            size);
    }
    return out;
}

bool EqualsNoCase(const std::wstring& a, const std::wstring& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        const wchar_t ca = a[i];
        const wchar_t cb = b[i];
        const wchar_t la = (ca >= L'A' && ca <= L'Z') ? static_cast<wchar_t>(ca + 32) : ca;
        const wchar_t lb = (cb >= L'A' && cb <= L'Z') ? static_cast<wchar_t>(cb + 32) : cb;
        if (la != lb) return false;
    }
    return true;
}

std::wstring ToLower(std::wstring s) {
    std::transform(s.begin(), s.end(), s.begin(), [](wchar_t c) {
        return (c >= L'A' && c <= L'Z') ? static_cast<wchar_t>(c + 32) : c;
    });
    return s;
}

std::wstring FileNameOf(const std::wstring& path) {
    const size_t slash = path.find_last_of(L"/\\");
    return slash == std::wstring::npos ? path : path.substr(slash + 1);
}

std::wstring FormatHresult(HRESULT hr) {
    wchar_t buf[16];
    swprintf_s(buf, L"0x%08X", static_cast<unsigned>(hr));
    return buf;
}

} // namespace vsr

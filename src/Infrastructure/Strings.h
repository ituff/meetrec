// Virtual Screen Recorder - string conversion helpers.
#pragma once

#include <windows.h>

#include <string>

namespace vsr {

std::string WideToUtf8(const std::wstring& wide);
std::wstring Utf8ToWide(const std::string& utf8);

// Lowercase ASCII comparison helpers used for identity matching.
bool EqualsNoCase(const std::wstring& a, const std::wstring& b);
std::wstring ToLower(std::wstring s);

// Extracts the file name portion of a path (no filesystem dependency needed
// by callers that already hold plain strings).
std::wstring FileNameOf(const std::wstring& path);

// Formats an HRESULT as "0x%08X".
std::wstring FormatHresult(HRESULT hr);

} // namespace vsr

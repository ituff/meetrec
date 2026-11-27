// MeetRec - free Bing/Edge translation for live caption cues.
//
// Uses the unauthenticated Edge endpoint
//   POST https://edge.microsoft.com/translate/translatetext
// with a JSON array of strings. The service rejects raw UTF-8 CJK bodies with
// HTTP 500, so the request body is ASCII-escaped (\uXXXX) before sending.
//
// This endpoint is unofficial and can change or disappear (the old
// edge.microsoft.com/translate/auth token flow was retired in 2026). Failures
// are non-fatal: the original caption remains visible.
#pragma once

#include <string>

namespace vsr::app {

struct TranslationResult {
    bool ok = false;
    std::wstring text;
    std::string detectedLanguage;
    std::wstring error;
};

// `targetLanguage` uses the Edge/Bing code scheme (en, zh-Hans, ja, ko, fr,
// de, es, ru). Blocks up to the WinHTTP timeouts; call from a worker thread.
TranslationResult TranslateWithBing(const std::wstring& text,
                                    const std::string& targetLanguage);

} // namespace vsr::app

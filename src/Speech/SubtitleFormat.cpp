#include "SubtitleFormat.h"

#include <algorithm>
#include <cwctype>

namespace vsr::speech {

namespace {

std::wstring TwoDigits(std::int64_t value) {
    return value < 10 ? L"0" + std::to_wstring(value) : std::to_wstring(value);
}

std::wstring ThreeDigits(std::int64_t value) {
    std::wstring text = std::to_wstring(value);
    while (text.size() < 3) text.insert(text.begin(), L'0');
    return text;
}

std::wstring Trim(const std::wstring& text) {
    std::size_t begin = 0;
    std::size_t end = text.size();
    while (begin < end && std::iswspace(text[begin])) ++begin;
    while (end > begin && std::iswspace(text[end - 1])) --end;
    return text.substr(begin, end - begin);
}

bool IsHighSurrogate(wchar_t c) {
    return c >= 0xD800 && c <= 0xDBFF;
}

bool IsLowSurrogate(wchar_t c) {
    return c >= 0xDC00 && c <= 0xDFFF;
}

bool IsWideCodePoint(char32_t cp) {
    return (cp >= 0x1100 && cp <= 0x11FF) ||   // Hangul Jamo
           (cp >= 0x2E80 && cp <= 0x303E) ||   // CJK radicals, punctuation
           (cp >= 0x3041 && cp <= 0x33FF) ||   // Kana, CJK compatibility
           (cp >= 0x3400 && cp <= 0x4DBF) ||   // CJK extension A
           (cp >= 0x4E00 && cp <= 0x9FFF) ||   // CJK unified ideographs
           (cp >= 0xA000 && cp <= 0xA4CF) ||   // Yi
           (cp >= 0xAC00 && cp <= 0xD7A3) ||   // Hangul syllables
           (cp >= 0xF900 && cp <= 0xFAFF) ||   // CJK compatibility ideographs
           (cp >= 0xFE30 && cp <= 0xFE4F) ||   // CJK compatibility forms
           (cp >= 0xFF00 && cp <= 0xFF60) ||   // Fullwidth forms
           (cp >= 0xFFE0 && cp <= 0xFFE6) ||
           (cp >= 0x1F300 && cp <= 0x1FAFF) || // Emoji
           (cp >= 0x20000 && cp <= 0x3FFFD);   // CJK extensions B+
}

bool IsBreakAfter(char32_t cp) {
    switch (cp) {
        case L' ':
        case L'\t':
        case L'，':
        case L'。':
        case L'！':
        case L'？':
        case L'、':
        case L'；':
        case L'：':
        case L',':
        case L'.':
        case L'!':
        case L'?':
        case L';':
        case L':':
        case L')':
        case L']':
        case L'}':
        case L'》':
        case L'」':
        case L'』':
        case L'”':
        case L'’':
            return true;
        default:
            return false;
    }
}

struct Glyph {
    std::wstring text;
    char32_t codePoint = 0;
    int width = 1;
    bool breakAfter = false;
};

std::vector<Glyph> ToGlyphs(const std::wstring& text) {
    std::vector<Glyph> glyphs;
    glyphs.reserve(text.size());
    for (std::size_t i = 0; i < text.size();) {
        Glyph glyph;
        const wchar_t c = text[i];
        if (IsHighSurrogate(c) && i + 1 < text.size() && IsLowSurrogate(text[i + 1])) {
            glyph.text.assign(text, i, 2);
            glyph.codePoint =
                0x10000 + ((static_cast<char32_t>(c) - 0xD800) << 10) +
                (static_cast<char32_t>(text[i + 1]) - 0xDC00);
            i += 2;
        } else {
            glyph.text.assign(1, c);
            glyph.codePoint = static_cast<char32_t>(c);
            ++i;
        }
        glyph.width = IsWideCodePoint(glyph.codePoint) ? 2 : 1;
        glyph.breakAfter = IsBreakAfter(glyph.codePoint);
        glyphs.push_back(std::move(glyph));
    }
    return glyphs;
}

} // namespace

std::wstring FormatSrtTimestamp(std::int64_t microseconds) {
    if (microseconds < 0) microseconds = 0;
    const std::int64_t totalMs = microseconds / 1000;
    const std::int64_t ms = totalMs % 1000;
    const std::int64_t totalSeconds = totalMs / 1000;
    const std::int64_t seconds = totalSeconds % 60;
    const std::int64_t totalMinutes = totalSeconds / 60;
    const std::int64_t minutes = totalMinutes % 60;
    const std::int64_t hours = totalMinutes / 60;
    return TwoDigits(hours) + L":" + TwoDigits(minutes) + L":" + TwoDigits(seconds) +
           L"," + ThreeDigits(ms);
}

std::wstring FormatTranscriptClock(std::int64_t microseconds) {
    if (microseconds < 0) microseconds = 0;
    const std::int64_t totalSeconds = microseconds / 1'000'000;
    const std::int64_t seconds = totalSeconds % 60;
    const std::int64_t totalMinutes = totalSeconds / 60;
    const std::int64_t minutes = totalMinutes % 60;
    const std::int64_t hours = totalMinutes / 60;
    return L"[" + TwoDigits(hours) + L":" + TwoDigits(minutes) + L":" +
           TwoDigits(seconds) + L"]";
}

std::wstring SpeakerLabel(bool microphone, bool chinese) {
    if (chinese) return microphone ? L"我" : L"对方";
    return microphone ? L"Me" : L"Them";
}

std::wstring SpeakerPrefix(bool microphone, bool chinese) {
    return SpeakerLabel(microphone, chinese) + (chinese ? L"：" : L": ");
}

std::wstring SanitizeTranscriptText(const std::wstring& text) {
    std::wstring cleaned;
    cleaned.reserve(text.size());
    bool inTag = false;
    for (std::size_t i = 0; i < text.size(); ++i) {
        const wchar_t c = text[i];
        if (!inTag && c == L'<' && i + 1 < text.size() && text[i + 1] == L'|') {
            inTag = true;
            ++i;
            continue;
        }
        if (inTag) {
            if (c == L'|' && i + 1 < text.size() && text[i + 1] == L'>') {
                inTag = false;
                ++i;
            }
            continue;
        }
        if (c == L'\r' || c == L'\n' || c == L'\t') {
            cleaned.push_back(L' ');
        } else {
            cleaned.push_back(c);
        }
    }

    // Collapse runs of whitespace while keeping single separators for Latin.
    std::wstring collapsed;
    collapsed.reserve(cleaned.size());
    bool previousSpace = false;
    for (wchar_t c : cleaned) {
        const bool space = std::iswspace(c) != 0;
        if (space) {
            if (!previousSpace) collapsed.push_back(L' ');
        } else {
            collapsed.push_back(c);
        }
        previousSpace = space;
    }
    return Trim(collapsed);
}

std::vector<std::wstring> WrapSubtitleText(const std::wstring& text,
                                           std::size_t maxUnits) {
    std::vector<std::wstring> lines;
    const std::wstring trimmed = Trim(text);
    if (trimmed.empty()) return lines;
    if (maxUnits == 0) maxUnits = 1;

    const std::vector<Glyph> glyphs = ToGlyphs(trimmed);
    const int limit = static_cast<int>(maxUnits);

    std::vector<std::size_t> line; // indices into glyphs
    int width = 0;
    std::size_t breakIndex = 0; // glyphs to keep when cutting at a break

    auto emit = [&](std::size_t count) {
        std::wstring output;
        for (std::size_t i = 0; i < count; ++i) output += glyphs[line[i]].text;
        output = Trim(output);
        if (!output.empty()) lines.push_back(output);

        line.erase(line.begin(),
                   line.begin() + static_cast<std::ptrdiff_t>(count));
        width = 0;
        breakIndex = 0;
        for (std::size_t i = 0; i < line.size(); ++i) {
            width += glyphs[line[i]].width;
            if (glyphs[line[i]].breakAfter) {
                breakIndex = i + 1;
            }
        }
    };

    for (std::size_t i = 0; i < glyphs.size(); ++i) {
        line.push_back(i);
        width += glyphs[i].width;
        if (glyphs[i].breakAfter) {
            breakIndex = line.size();
        }
        if (width > limit && line.size() > 1) {
            const std::size_t keep =
                breakIndex > 0 && breakIndex < line.size() ? breakIndex : line.size() - 1;
            emit(keep == 0 ? 1 : keep);
        }
    }
    if (!line.empty()) emit(line.size());
    return lines;
}

} // namespace vsr::speech

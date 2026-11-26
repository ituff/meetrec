#include "TranscriptStore.h"

#include <Infrastructure/Strings.h>

#include <algorithm>
#include <sstream>

#include "SubtitleFormat.h"

namespace vsr::speech {

namespace {

constexpr char kUtf8Bom[] = "\xEF\xBB\xBF";

std::filesystem::path WithSuffix(const std::filesystem::path& base,
                                 const wchar_t* suffix) {
    return std::filesystem::path(base.wstring() + suffix);
}

bool Exists(const std::filesystem::path& path) {
    std::error_code ec;
    return std::filesystem::exists(path, ec) && !ec;
}

bool WriteUtf8(const std::filesystem::path& path, const std::wstring& text) {
    // Write the complete file next to the target first; a crash mid-write
    // leaves the .tmp behind instead of a truncated transcript.
    const std::filesystem::path temp = path.wstring() + L".tmp";
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        if (!out.good()) return false;
        out.write(kUtf8Bom, 3);
        const std::string utf8 = WideToUtf8(text);
        out.write(utf8.data(), static_cast<std::streamsize>(utf8.size()));
        out.flush();
        if (!out.good()) {
            out.close();
            std::error_code removeEc;
            std::filesystem::remove(temp, removeEc);
            return false;
        }
    }
    std::error_code ec;
    std::filesystem::rename(temp, path, ec);
    if (ec) {
        std::error_code removeEc;
        std::filesystem::remove(temp, removeEc);
        return false;
    }
    return true;
}

// Chooses <base>.srt / <base>.txt (or a -N suffix) only when neither file
// exists, so a retried finalize cannot overwrite a previous transcript.
std::filesystem::path ChooseBase(const std::filesystem::path& base) {
    for (int attempt = 0; attempt < 1000; ++attempt) {
        const std::filesystem::path candidate =
            attempt == 0
                ? base
                : std::filesystem::path(base.wstring() + L"-" + std::to_wstring(attempt));
        if (!Exists(WithSuffix(candidate, L".srt")) &&
            !Exists(WithSuffix(candidate, L".txt"))) {
            return candidate;
        }
    }
    return base;
}

std::int64_t ClampEnd(const TranscriptSegment& segment) {
    if (segment.endUs <= segment.startUs) {
        return segment.startUs + 500'000;
    }
    return segment.endUs;
}

std::wstring BuildCueText(const TranscriptSegment& segment, bool chinese,
                          std::size_t index) {
    std::wstring text = SanitizeTranscriptText(segment.text);
    if (text.empty()) return {};
    text = SpeakerPrefix(segment.microphone, chinese) + text;

    std::vector<std::wstring> lines = WrapSubtitleText(text);
    if (lines.empty()) return {};

    std::wstring cue;
    cue += std::to_wstring(index);
    cue += L"\r\n";
    cue += FormatSrtTimestamp(segment.startUs);
    cue += L" --> ";
    cue += FormatSrtTimestamp(ClampEnd(segment));
    cue += L"\r\n";
    for (const std::wstring& line : lines) {
        cue += line;
        cue += L"\r\n";
    }
    cue += L"\r\n";
    return cue;
}

} // namespace

TranscriptStore::TranscriptStore(bool chinese) : chinese_(chinese) {}

bool TranscriptStore::OpenPartial(const std::filesystem::path& basePath) {
    ClosePartial();
    if (basePath.empty()) return false;
    std::error_code ec;
    if (!basePath.parent_path().empty()) {
        std::filesystem::create_directories(basePath.parent_path(), ec);
    }
    partialPath_ = WithSuffix(basePath, L".partial.srt");
    partial_.open(partialPath_, std::ios::binary | std::ios::trunc);
    if (!partial_.good()) {
        partial_.close();
        partialPath_.clear();
        return false;
    }
    partial_.write(kUtf8Bom, 3);
    partial_.flush();
    return partial_.good();
}

void TranscriptStore::Add(TranscriptSegment segment) {
    segment.text = SanitizeTranscriptText(segment.text);
    if (segment.text.empty()) return;
    if (segment.startUs < 0) segment.startUs = 0;
    if (segment.endUs <= segment.startUs) {
        segment.endUs = segment.startUs + 500'000;
    }

    segments_.push_back(std::move(segment));
    if (partial_.is_open()) {
        (void)WriteCueToPartial(segments_.back(), segments_.size());
    }
}

bool TranscriptStore::WriteCueToPartial(const TranscriptSegment& segment,
                                        std::size_t index) {
    const std::wstring cue = BuildCueText(segment, chinese_, index);
    if (cue.empty()) return true;
    const std::string utf8 = WideToUtf8(cue);
    partial_.write(utf8.data(), static_cast<std::streamsize>(utf8.size()));
    partial_.flush();
    return partial_.good();
}

void TranscriptStore::ClosePartial() {
    if (partial_.is_open()) {
        partial_.flush();
        partial_.close();
    }
}

std::wstring TranscriptStore::BuildSrtBody(
    const std::vector<TranscriptSegment>& segments, bool chinese) {
    std::wstring body;
    std::size_t index = 1;
    for (const TranscriptSegment& segment : segments) {
        const std::wstring cue = BuildCueText(segment, chinese, index);
        if (cue.empty()) continue;
        body += cue;
        ++index;
    }
    return body;
}

std::wstring TranscriptStore::BuildTxtBody(
    const std::vector<TranscriptSegment>& segments, bool chinese) {
    std::wstring body;
    for (const TranscriptSegment& segment : segments) {
        const std::wstring text = SanitizeTranscriptText(segment.text);
        if (text.empty()) continue;
        body += FormatTranscriptClock(segment.startUs);
        body += L" ";
        body += SpeakerPrefix(segment.microphone, chinese);
        body += text;
        body += L"\r\n";
    }
    return body;
}

TranscriptStore::Written TranscriptStore::Finalize(
    const std::filesystem::path& basePath) {
    ClosePartial();
    Written written;
    if (segments_.empty()) {
        if (!partialPath_.empty()) {
            std::error_code ec;
            std::filesystem::remove(partialPath_, ec);
        }
        partialPath_.clear();
        return written;
    }

    const std::filesystem::path base = ChooseBase(basePath);
    const std::filesystem::path srtPath = WithSuffix(base, L".srt");
    const std::filesystem::path txtPath = WithSuffix(base, L".txt");
    const std::wstring srt = BuildSrtBody(segments_, chinese_);
    const std::wstring txt = BuildTxtBody(segments_, chinese_);

    if (WriteUtf8(srtPath, srt)) written.srt = srtPath;
    if (WriteUtf8(txtPath, txt)) written.txt = txtPath;

    if (!written.srt.empty() && !written.txt.empty() && !partialPath_.empty()) {
        std::error_code ec;
        std::filesystem::remove(partialPath_, ec);
        if (!ec) partialPath_.clear();
    }
    return written;
}

void TranscriptStore::Abandon() {
    ClosePartial();
}

} // namespace vsr::speech

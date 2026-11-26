// MeetRec - meeting transcript store and writer (P1).
//
// Finalized speech segments are appended to <base>.partial.srt immediately, so
// a crash during a recording still leaves a readable transcript up to the last
// cue. On a normal stop the partial file is replaced by <base>.srt and
// <base>.txt (UTF-8 with BOM) without overwriting an existing file.
#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace vsr::speech {

struct TranscriptSegment {
    std::int64_t startUs = 0;
    std::int64_t endUs = 0;
    bool microphone = false;
    std::string language;
    std::wstring text;
};

class TranscriptStore {
public:
    struct Written {
        std::filesystem::path srt;
        std::filesystem::path txt;
    };

    explicit TranscriptStore(bool chinese);

    TranscriptStore(const TranscriptStore&) = delete;
    TranscriptStore& operator=(const TranscriptStore&) = delete;

    // Opens <base>.partial.srt for append, truncating any stale file from a
    // previous run with the same base. `base` has no extension. Returns false
    // when the file cannot be created; segments are still kept in memory.
    bool OpenPartial(const std::filesystem::path& basePath);

    void Add(TranscriptSegment segment);

    std::size_t Count() const noexcept { return segments_.size(); }
    bool HasSegments() const noexcept { return !segments_.empty(); }

    // Closes the partial and writes the final SRT/TXT pair. Returns empty
    // paths when no segment was recorded. Never overwrites an existing file;
    // collisions get a -1, -2, ... suffix.
    Written Finalize(const std::filesystem::path& basePath);

    // Closes the partial writer but leaves the .partial.srt in place for
    // crash recovery (used when the recording ends abnormally).
    void Abandon();

private:
    void ClosePartial();
    bool WriteCueToPartial(const TranscriptSegment& segment, std::size_t index);
    static std::wstring BuildSrtBody(const std::vector<TranscriptSegment>& segments,
                                     bool chinese);
    static std::wstring BuildTxtBody(const std::vector<TranscriptSegment>& segments,
                                     bool chinese);

    bool chinese_;
    std::vector<TranscriptSegment> segments_;
    std::filesystem::path partialPath_;
    std::ofstream partial_;
};

} // namespace vsr::speech

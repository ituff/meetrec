// MeetRec - live caption feed shared by the speech worker and the overlay.
//
// The worker owns recognition and pushes each finalized cue; the overlay's
// timer reads a snapshot. No UI dependency, no callbacks into a window that
// may be closing, and the overlay can always catch up after a stall.
#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

namespace vsr::speech {

struct CaptionCue {
    std::wstring text;
    bool microphone = false;
    std::int64_t startUs = 0;
    std::int64_t endUs = 0;
    std::uint64_t sequence = 0;
    // Filled by the optional live translator; empty until a translation
    // arrives. translationLang records which target the text is for, and
    // translationFailed prevents retrying a cue forever.
    std::wstring translation;
    std::string translationLang;
    bool translationFailed = false;
};

class CaptionFeed {
public:
    void Push(std::wstring text, bool microphone, std::int64_t startUs,
              std::int64_t endUs);

    // Drops all cues (new recording/pause). The sequence still advances so an
    // overlay that was showing text repaints as empty.
    void Clear();

    // Newest `maxCount` cues, oldest first.
    std::vector<CaptionCue> Snapshot(std::size_t maxCount = 2) const;

    // Applies a translation to the cue with this sequence. The global
    // sequence advances so the overlay repaints.
    void SetTranslation(std::uint64_t cueSequence, std::wstring text,
                        std::string lang);
    void MarkTranslationFailed(std::uint64_t cueSequence, std::string lang);
    // Clears translations (used when the target language changes).
    void ClearTranslations();

    std::uint64_t Sequence() const;
    bool Empty() const;

private:
    static constexpr std::size_t kMaxCues = 8;
    mutable std::mutex mutex_;
    std::deque<CaptionCue> cues_;
    std::uint64_t sequence_ = 0;
};

} // namespace vsr::speech

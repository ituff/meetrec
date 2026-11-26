#include "CaptionFeed.h"

#include <algorithm>
#include <utility>

namespace vsr::speech {

void CaptionFeed::Push(std::wstring text, bool microphone, std::int64_t startUs,
                       std::int64_t endUs) {
    if (text.empty()) return;
    std::lock_guard<std::mutex> lock(mutex_);
    CaptionCue cue;
    cue.text = std::move(text);
    cue.microphone = microphone;
    cue.startUs = startUs;
    cue.endUs = endUs;
    cue.sequence = ++sequence_;
    cues_.push_back(std::move(cue));
    while (cues_.size() > kMaxCues) cues_.pop_front();
}

void CaptionFeed::Clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    cues_.clear();
    ++sequence_;
}

std::vector<CaptionCue> CaptionFeed::Snapshot(std::size_t maxCount) const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (maxCount == 0) return {};
    const std::size_t count = std::min(maxCount, cues_.size());
    std::vector<CaptionCue> result;
    result.reserve(count);
    const auto begin = cues_.end() - static_cast<std::ptrdiff_t>(count);
    for (auto it = begin; it != cues_.end(); ++it) result.push_back(*it);
    return result;
}

void CaptionFeed::SetTranslation(std::uint64_t cueSequence, std::wstring text,
                                 std::string lang) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (CaptionCue& cue : cues_) {
        if (cue.sequence != cueSequence) continue;
        cue.translation = std::move(text);
        cue.translationLang = std::move(lang);
        cue.translationFailed = false;
        ++sequence_;
        return;
    }
}

void CaptionFeed::MarkTranslationFailed(std::uint64_t cueSequence,
                                        std::string lang) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (CaptionCue& cue : cues_) {
        if (cue.sequence != cueSequence) continue;
        cue.translation.clear();
        cue.translationLang = std::move(lang);
        cue.translationFailed = true;
        ++sequence_;
        return;
    }
}

void CaptionFeed::ClearTranslations() {
    std::lock_guard<std::mutex> lock(mutex_);
    bool changed = false;
    for (CaptionCue& cue : cues_) {
        if (cue.translation.empty() && cue.translationLang.empty() &&
            !cue.translationFailed) {
            continue;
        }
        cue.translation.clear();
        cue.translationLang.clear();
        cue.translationFailed = false;
        changed = true;
    }
    if (changed) ++sequence_;
}

std::uint64_t CaptionFeed::Sequence() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return sequence_;
}

bool CaptionFeed::Empty() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return cues_.empty();
}

} // namespace vsr::speech

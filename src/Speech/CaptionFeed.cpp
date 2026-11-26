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

std::uint64_t CaptionFeed::Sequence() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return sequence_;
}

bool CaptionFeed::Empty() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return cues_.empty();
}

} // namespace vsr::speech

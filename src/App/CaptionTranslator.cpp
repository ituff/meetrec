#include "CaptionTranslator.h"

#include <Infrastructure/Log.h>
#include <Infrastructure/Strings.h>

#include <utility>

#include "BingTranslator.h"

namespace vsr::app {

namespace {
constexpr std::size_t kMaxQueue = 32;
constexpr std::size_t kCuesPerPoll = 4;
} // namespace

CaptionTranslator::CaptionTranslator(std::shared_ptr<speech::CaptionFeed> feed)
    : feed_(std::move(feed)) {
    worker_ = std::thread([this] { WorkerLoop(); });
}

CaptionTranslator::~CaptionTranslator() {
    Stop();
}

void CaptionTranslator::SetTargetLanguage(const std::string& language) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (target_ == language) return;
        target_ = language;
        queue_.clear();
        inFlight_.clear();
    }
    if (feed_) feed_->ClearTranslations();
    cv_.notify_all();
}

void CaptionTranslator::Poll(bool enabled) {
    if (!feed_) return;
    std::string target;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stopping_) return;
        enabled_ = enabled;
        if (!enabled) {
            queue_.clear();
            inFlight_.clear();
            return;
        }
        target = target_;
    }
    if (!enabled || target.empty()) return;

    const std::vector<speech::CaptionCue> cues = feed_->Snapshot(kCuesPerPoll);
    if (cues.empty()) return;

    std::lock_guard<std::mutex> lock(mutex_);
    if (stopping_ || !enabled_) return;
    for (const speech::CaptionCue& cue : cues) {
        if (cue.translationLang == target) continue; // done or failed for this target
        if (inFlight_.count(cue.sequence) != 0) continue;
        if (queue_.size() >= kMaxQueue) break;
        Job job;
        job.sequence = cue.sequence;
        job.text = cue.text;
        job.target = target;
        queue_.push_back(std::move(job));
        inFlight_.insert(cue.sequence);
    }
    cv_.notify_one();
}

void CaptionTranslator::Stop() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stopping_) return;
        stopping_ = true;
        queue_.clear();
        inFlight_.clear();
    }
    cv_.notify_all();
    if (worker_.joinable()) worker_.join();
}

void CaptionTranslator::WorkerLoop() {
    for (;;) {
        Job job;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [this] { return stopping_ || !queue_.empty(); });
            if (stopping_) return;
            job = std::move(queue_.front());
            queue_.pop_front();
        }

        const TranslationResult result = TranslateWithBing(job.text, job.target);

        bool apply = false;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            inFlight_.erase(job.sequence);
            apply = !stopping_ && enabled_ && target_ == job.target;
        }
        if (!apply) continue;

        if (result.ok) {
            feed_->SetTranslation(job.sequence, result.text, job.target);
        } else {
            feed_->MarkTranslationFailed(job.sequence, job.target);
            log::Warning("Translation", "TRANSLATE_FAILED", L"",
                         L"Live caption translation failed; the original caption is kept.",
                         {{"target", job.target},
                          {"detail", WideToUtf8(result.error)}});
        }
    }
}

} // namespace vsr::app

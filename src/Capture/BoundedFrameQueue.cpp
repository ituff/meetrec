#include "BoundedFrameQueue.h"

namespace vsr::capture {

BoundedFrameQueue::BoundedFrameQueue(std::size_t capacity) : capacity_(capacity) {}

BoundedFrameQueue::~BoundedFrameQueue() {
    Close();
}

bool BoundedFrameQueue::Push(CapturedFrame&& frame) {
    captured_.fetch_add(1, std::memory_order_relaxed);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (closed_) return false;
        if (frames_.size() >= capacity_) {
            // Overflow policy: evict the oldest unencoded frame (ADR-005).
            frames_.pop_front();
            dropped_.fetch_add(1, std::memory_order_relaxed);
        }
        frames_.push_back(std::move(frame));
    }
    available_.notify_one();
    return true;
}

bool BoundedFrameQueue::Pop(CapturedFrame& out, std::chrono::milliseconds timeout) {
    std::unique_lock<std::mutex> lock(mutex_);
    if (!available_.wait_for(lock, timeout, [&] { return !frames_.empty() || closed_; })) {
        return false;
    }
    if (frames_.empty()) return false; // closed and drained
    out = std::move(frames_.front());
    frames_.pop_front();
    return true;
}

void BoundedFrameQueue::Close() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        closed_ = true;
    }
    available_.notify_all();
}

FrameQueueCounters BoundedFrameQueue::Counters() const noexcept {
    FrameQueueCounters c;
    c.captured = captured_.load(std::memory_order_relaxed);
    c.encoded = encoded_.load(std::memory_order_relaxed);
    c.dropped = dropped_.load(std::memory_order_relaxed);
    std::lock_guard<std::mutex> lock(mutex_);
    c.queued = frames_.size();
    return c;
}

} // namespace vsr::capture

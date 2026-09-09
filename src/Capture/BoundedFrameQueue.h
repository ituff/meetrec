// Virtual Screen Recorder - bounded frame queue (FR-043..FR-046, NFR-007;
// ADR-005).
//
// A fixed-capacity, thread-safe handoff between the capture callback thread
// and the encoder worker. Capacity is fixed at construction (three frames per
// design.md §4.6). When full, the OLDEST unencoded frame is evicted and the
// dropped counter increments, so the pipeline never blocks the capture
// callback and memory stays bounded (no unbounded frame queue).
//
// The payload is an opaque shared_ptr so the queue is unit-testable without a
// GPU and keeps no raw texture-ownership semantics of its own.
#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>

namespace vsr::capture {

struct CapturedFrame {
    std::shared_ptr<void> payload; // ID3D11Texture2D wrapped by the producer
    std::uint64_t timestampUs = 0; // monotonic capture timestamp
    std::uint32_t width = 0;
    std::uint32_t height = 0;
};

struct FrameQueueCounters {
    std::uint64_t captured = 0;  // frames seen by the producer
    std::uint64_t queued = 0;    // currently in the queue
    std::uint64_t encoded = 0;   // consumed downstream
    std::uint64_t dropped = 0;   // evicted by the overflow policy
};

class BoundedFrameQueue {
public:
    explicit BoundedFrameQueue(std::size_t capacity);
    ~BoundedFrameQueue();

    BoundedFrameQueue(const BoundedFrameQueue&) = delete;
    BoundedFrameQueue& operator=(const BoundedFrameQueue&) = delete;

    // Producer side. Never blocks: when the queue is full the oldest frame is
    // evicted (dropped). Returns false if the queue is closed.
    bool Push(CapturedFrame&& frame);

    // Consumer side. Waits up to `timeout` for a frame; returns false when no
    // frame arrived within the timeout or the queue is closed and drained.
    bool Pop(CapturedFrame& out, std::chrono::milliseconds timeout);

    // Closes the queue. Push fails immediately; Pop drains remaining frames
    // then fails. Idempotent.
    void Close();

    bool IsClosed() const noexcept { return closed_.load(std::memory_order_acquire); }

    // Marks one queued frame as encoded downstream (counter bookkeeping).
    void NotifyEncoded() noexcept { encoded_.fetch_add(1, std::memory_order_relaxed); }

    FrameQueueCounters Counters() const noexcept;

    std::size_t Capacity() const noexcept { return capacity_; }

private:
    const std::size_t capacity_;
    mutable std::mutex mutex_;
    std::condition_variable available_;
    std::deque<CapturedFrame> frames_;
    std::atomic<bool> closed_{false};

    std::atomic<std::uint64_t> captured_{0};
    std::atomic<std::uint64_t> encoded_{0};
    std::atomic<std::uint64_t> dropped_{0};
};

} // namespace vsr::capture

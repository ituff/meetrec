// Bounded frame queue tests (T023, ADR-005): enqueue, overflow eviction,
// shutdown semantics, and concurrent producer/consumer behavior.
#include "TestFramework.h"
#include <Capture/BoundedFrameQueue.h>

#include <atomic>
#include <thread>

using vsr::capture::BoundedFrameQueue;
using vsr::capture::CapturedFrame;

namespace {

CapturedFrame MakeFrame(std::uint64_t ts, int tag = 0) {
    CapturedFrame f;
    f.payload = std::shared_ptr<int>(new int(tag), [](int* p) { delete p; });
    f.timestampUs = ts;
    f.width = 1920;
    f.height = 1080;
    return f;
}

int TagOf(const CapturedFrame& f) {
    return *static_cast<int*>(f.payload.get());
}

} // namespace

VSR_TEST(FrameQueue, EnqueueAndDequeueInOrder) {
    BoundedFrameQueue q(3);
    VSR_CHECK(q.Push(MakeFrame(1, 1)));
    VSR_CHECK(q.Push(MakeFrame(2, 2)));
    VSR_CHECK(q.Push(MakeFrame(3, 3)));

    CapturedFrame out;
    VSR_CHECK(q.Pop(out, std::chrono::milliseconds(0)));
    VSR_CHECK_EQ(TagOf(out), 1);
    VSR_CHECK(q.Pop(out, std::chrono::milliseconds(0)));
    VSR_CHECK_EQ(TagOf(out), 2);
    VSR_CHECK(q.Pop(out, std::chrono::milliseconds(0)));
    VSR_CHECK_EQ(TagOf(out), 3);
}

VSR_TEST(FrameQueue, OverflowEvictsOldest) {
    BoundedFrameQueue q(3);
    VSR_CHECK(q.Push(MakeFrame(1, 1)));
    VSR_CHECK(q.Push(MakeFrame(2, 2)));
    VSR_CHECK(q.Push(MakeFrame(3, 3)));
    VSR_CHECK(q.Push(MakeFrame(4, 4))); // evicts frame 1 (FR-045)

    const auto counters = q.Counters();
    VSR_CHECK_EQ(counters.captured, 4u);
    VSR_CHECK_EQ(counters.dropped, 1u);
    VSR_CHECK_EQ(counters.queued, 3u);

    CapturedFrame out;
    VSR_CHECK(q.Pop(out, std::chrono::milliseconds(0)));
    VSR_CHECK_EQ(TagOf(out), 2);
}

VSR_TEST(FrameQueue, CloseRejectsPushAndDrainsPop) {
    BoundedFrameQueue q(2);
    VSR_CHECK(q.Push(MakeFrame(1, 7)));
    q.Close();
    VSR_CHECK(!q.Push(MakeFrame(2, 8)));

    CapturedFrame out;
    VSR_CHECK(q.Pop(out, std::chrono::milliseconds(0))); // drain remaining
    VSR_CHECK_EQ(TagOf(out), 7);
    VSR_CHECK(!q.Pop(out, std::chrono::milliseconds(1))); // then fails
}

VSR_TEST(FrameQueue, PopTimesOutWhenEmpty) {
    BoundedFrameQueue q(1);
    CapturedFrame out;
    const auto begin = std::chrono::steady_clock::now();
    VSR_CHECK(!q.Pop(out, std::chrono::milliseconds(25)));
    const auto elapsed = std::chrono::steady_clock::now() - begin;
    VSR_CHECK(elapsed >= std::chrono::milliseconds(20));
}

VSR_TEST(FrameQueue, ConcurrentProducerConsumerKeepsCountersConsistent) {
    BoundedFrameQueue q(3);
    constexpr int kProduce = 5000;
    std::atomic<int> consumed{0};

    std::thread consumer([&] {
        CapturedFrame out;
        for (;;) {
            if (q.Pop(out, std::chrono::milliseconds(50))) {
                q.NotifyEncoded();
                ++consumed;
                continue;
            }
            if (q.IsClosed()) {
                // Drain whatever remains, then stop.
                CapturedFrame drained;
                while (q.Pop(drained, std::chrono::milliseconds(0))) {
                    q.NotifyEncoded();
                    ++consumed;
                }
                break;
            }
        }
    });

    for (int i = 0; i < kProduce; ++i) {
        q.Push(MakeFrame(static_cast<std::uint64_t>(i), i));
    }
    q.Close();
    consumer.join();

    // With the drop-oldest eviction policy, a slow consumer means some frames
    // are legitimately dropped; the invariant is conservation:
    // captured == encoded + dropped + queued(0 after drain).
    const auto counters = q.Counters();
    VSR_CHECK_EQ(counters.captured, static_cast<std::uint64_t>(kProduce));
    VSR_CHECK_EQ(counters.queued, 0u);
    VSR_CHECK_EQ(counters.encoded + counters.dropped,
                 static_cast<std::uint64_t>(kProduce));
}

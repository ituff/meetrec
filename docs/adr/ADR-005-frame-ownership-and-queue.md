# ADR-005: Frame ownership — pooled copy with a three-frame bounded queue

- **Status:** Accepted
- **Date:** 2026-09-09
- **Deciders:** Virtual Screen Recorder project
- **Related requirements:** FR-043..FR-047, NFR-007, NFR-011; design.md §4.6

## Context

WGC hands the app a surface owned by the frame pool: after the callback returns, the pool may
reuse it. The encoder consumes frames asynchronously, so the pipeline must guarantee texture
stability between the capture callback and encoder submission while never letting memory grow
without bound (NFR-007) and never blocking the capture callback on the encoder.

Two ownership strategies were allowed by the design:

1. **Retain the frame object** and submit the pool surface directly, relying on the documented
   safe lifetime of `Direct3D11CaptureFrame`.
2. **Copy** each incoming frame into a small pool of app-owned textures before enqueueing.

## Decision

We will **copy** each frame in the capture callback into a fixed ring of app-owned BGRA
textures (queue capacity 3 + 2 slack) and enqueue those, in a **three-frame bounded queue**:

- `BoundedFrameQueue` is fixed-capacity, non-blocking for the producer: on overflow the oldest
  unencoded frame is evicted and `dropped` increments (FR-045).
- Each enqueued frame carries its **own COM reference** to a ring texture plus the ring slot
  index; releasing the payload returns the slot to the ring. Ring state lives in a
  `shared_ptr<PoolState>`, so a frame consumed after session teardown cannot touch freed state.
- Content-size changes recreate the ring; frames in flight keep their own references and stay
  valid (verified by construction; see `MonitorCaptureSession`).
- Counters (`captured`, `queued`, `encoded`, `dropped`) are atomic and tested for conservation
  (`captured == encoded + dropped + queued`) under concurrent producer/consumer load.
- Pause boundaries discard frames via a watermark so paused intervals never reach the encoder
  (FR-055, FR-071, FR-072).

## Alternatives considered

- **Frame-object retention (option 1)** — rejected: the safe-lifetime contract is versioned and
  opaque ("until the next `TryGetNextFrame`"), which is exactly the class of assumption the
  spike was required to prove; a failure surfaces as corruption in the *middle* of a recording.
  The pooled copy costs one GPU-to-GPU `CopySubresourceRegion` per frame — negligible next to
  the encoder.
- **Unbounded or growable queue** — rejected by NFR-007; backpressure must drop, not queue.
- **Larger queue capacity** — rejected: capacity 3 already decouples callback from encoder
  jitter at 30-60 FPS; larger queues only hide stalls and increase latency.

## Consequences

- (+) Textures are provably stable for the encoder; no dependence on WGC reuse semantics.
- (+) Bounded memory by construction; overflow degrades quality (dropped frames) instead of
  leaking or deadlocking.
- (-) One extra GPU copy per frame; measured in T091 and acceptable within NFR-004.
- (-) A fully stalled encoder degrades to a stream of dropped frames; the UI surfaces the
  dropped counter (FR-046, FR-077).

## Validation evidence

- `BoundedQueueTests` cover normal enqueue, overflow eviction, close/drain, timeout, and a
  5000-frame concurrent producer/consumer conservation check — all passing.
- The 30-minute bounded-memory soak (T023) and two-hour soak (AC-008) are executed during
  release qualification on the reference machine.

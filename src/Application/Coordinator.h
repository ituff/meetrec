// Virtual Screen Recorder - application coordinator (T040, T041, T042, T080).
//
// The coordinator is the sole owner of the recording lifecycle (design.md
// §4.2). It validates commands against the state machine, orders subsystem
// startup and shutdown, runs the capture->queue->convert->encode pipeline on
// a dedicated worker, monitors failure sources (target loss, display loss,
// device loss, disk guard), and publishes immutable status snapshots.
#pragma once

#include <atomic>
#include <chrono>
#include <deque>
#include <filesystem>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include <DisplayControl/SoftwareDisplayDevice.h>
#include <DisplayControl/TopologyService.h>
#include <Infrastructure/Clock.h>
#include <Infrastructure/Error.h>
#include <Infrastructure/Result.h>
#include "AppState.h"
#include "DiskSpaceGuard.h"
#include "EncoderStallDetector.h"
#include "RecordingTimeline.h"
#include "StatusSnapshot.h"
#include <Encoder/AudioCapture.h>
#include <Encoder/EncoderProbe.h>
#include <Encoder/VideoPreset.h>
#include <Encoder/VideoWriterFactory.h>
#include <WindowManagement/PopupTracker.h>

namespace vsr::capture {
class BoundedFrameQueue;
class CaptureSession;
class GraphicsDevice;
class ICaptureSession;
} // namespace vsr::capture
namespace vsr::encoder {
class VideoConverter;
struct EncoderCapability;
} // namespace vsr::encoder
namespace vsr::window {
class WindowPlacementManager;
struct SavedPlacement;
} // namespace vsr::window

namespace vsr::app {

using encoder::QualityPreset;

struct StartRecordingRequest {
    HWND targetWindow = nullptr; // null => record the virtual display only (dev CLI)
    std::filesystem::path outputDirectory;
    QualityPreset preset = QualityPreset::Standard;
    encoder::EncoderPreference encoderPreference = encoder::EncoderPreference::Auto;
    encoder::WriterBackend backend = encoder::WriterBackend::Auto;
    bool captureCursor = false;
    // The two audio sources are independent: either one (or both) creates the
    // AAC track. A missing loopback endpoint degrades to mic-only; a missing
    // microphone leaves the loopback track intact.
    bool captureAudio = false;
    bool captureMicrophone = false;
    // Audio endpoint IDs selected in the UI (empty = Windows default). Call
    // apps often use devices other than the Windows default; capturing the
    // wrong endpoint makes that source silent.
    std::wstring audioOutputDeviceId;
    std::wstring audioInputDeviceId;
};

enum class StopReason { User, TargetLost, DisplayLost, DeviceLost, DiskLow, WriteFailed, Shutdown };

class Coordinator {
public:
    explicit Coordinator(std::shared_ptr<IClock> clock);
    ~Coordinator();

    Coordinator(const Coordinator&) = delete;
    Coordinator& operator=(const Coordinator&) = delete;

    // ---- Commands (validated against the state machine; invalid commands
    // are rejected with VSR-INTERNAL-STATE without side effects). ----

    Result<void> Start(const StartRecordingRequest& request);
    Result<void> Pause();
    Result<void> Resume();
    // Idempotent (FR-073): Stop outside Recording/Paused completes immediately.
    Result<void> Stop(StopReason reason);

    // FR-016/FR-017: display management is only valid when not recording.
    Result<display::MonitorDescriptor> EnsureVirtualDisplay();
    Result<void> DisableVirtualDisplay();

    // Faulted -> Ready recovery.
    Result<void> Reset();

    // Controlled shutdown: stops and finalizes an active recording, then
    // moves to ShuttingDown (bounded waits only, NFR-014).
    Result<void> Shutdown();

    StatusSnapshot GetStatus() const;

    // Preview tap (FR-048): copies the latest captured frame into a BGRA
    // byte buffer; preview cannot affect recording. Returns false when idle.
    bool TryGetPreviewFrameCpu(std::vector<std::uint8_t>& outBgra, UINT& width, UINT& height);

    // Latest per-source peak levels (0..1) for the UI input meters. Stale
    // values are harmless: the UI only reads them while recording.
    void GetAudioLevels(float& loopback, float& microphone) const noexcept;

private:
    // Worker entry for the active recording session.
    void SessionWorker(const StartRecordingRequest& request);
    bool PrepareSession(const StartRecordingRequest& request);
    void ConsumeLoop();
    void TeardownSession();
    // Independent-thread encoder stall watchdog (NFR-014): detects a wedged
    // WriteSample/Finalize that the worker's own in-thread checks cannot see,
    // then escalates through the bounded-abandon stop path.
    void WatchdogLoop();
    // Containment shared by Stop and the watchdog: bounded wait for the
    // worker, detach + resource abandon past the deadline.
    Result<void> WaitForWorkerExit(std::chrono::milliseconds bounded);

    void SetState(AppState next);
    void PublishError(const Error& error);
    void SetStopRequested(StopReason reason);
    // Drains queued audio chunks into the writer (pause/watermark aware).
    void DrainAudio();
    void ReleaseRecordingMutex();
    // Wedged-worker containment: when the bounded stop deadline expires the
    // worker thread is detached while still inside a driver call. The session
    // objects it may touch are deliberately leaked (bounded set) and the
    // target-window restore is performed here instead, so the abandoned
    // thread can never touch freed memory (NFR-014).
    void AbandonSessionResources() noexcept;

    StatusSnapshot BuildSnapshot() const;
    double SampleFps();

    std::shared_ptr<IClock> clock_;

    // State machine core. All transitions guarded by stateMutex_.
    mutable std::mutex stateMutex_;
    AppState state_ = AppState::Initializing;
    Error lastError_{};
    std::wstring correlationId_;
    StatusSnapshot snapshot_; // last known cached facts for the UI

    // Worker lifecycle.
    std::thread worker_;
    // Serializes the join/detach of worker_ between Stop, Shutdown, and the
    // watchdog escalation (std::thread::detach on two threads is UB).
    std::mutex workerJoinMutex_;
    std::thread watchdog_;
    std::atomic<bool> watchdogStop_{false};
    std::atomic<bool> workerAlive_{false};
    std::atomic<bool> sessionAbandoned_{false};
    std::atomic<bool> stopRequested_{false};
    std::atomic<StopReason> stopReason_{StopReason::User};
    std::atomic<bool> paused_{false};

    // Failure monitors (set from watcher callbacks).
    std::atomic<bool> displayLost_{false};
    std::atomic<bool> targetLost_{false};

    // Session objects (owned by the worker between Preparing and Stopping).
    std::unique_ptr<capture::BoundedFrameQueue> queue_;
    std::unique_ptr<capture::GraphicsDevice> device_;
    std::unique_ptr<capture::ICaptureSession> session_;
    std::unique_ptr<encoder::AudioCapture> audioCapture_;
    std::mutex audioMutex_;
    std::deque<encoder::AudioChunk> audioQueue_;
    std::atomic<bool> audioReceived_{false};
    std::atomic<float> loopbackLevel_{0.0f};
    std::atomic<float> micLevel_{0.0f};
    std::uint32_t audioRate_ = 48000;
    std::uint32_t audioChannels_ = 2;
    // Output-time cursor for the audio stream (100ns). Audio timestamps must
    // advance by sample count, not wall clock: AAC artifacts (speed-warp
    // garble) appear when chunk timestamps jitter relative to sample counts.
    std::int64_t audioCursor100Ns_ = -1;
    std::unique_ptr<encoder::VideoConverter> converter_;
    std::unique_ptr<encoder::IVideoWriter> writer_;
    std::unique_ptr<window::SavedPlacement> savedPlacement_;
    std::unique_ptr<encoder::EncoderCapability> encoderCap_;
    display::SoftwareDisplayDevice softwareDevice_;
    display::TopologyWatcher topologyWatcher_;
    window::PopupTracker popupTracker_; // FR-029 / T100
    HANDLE recordingMutex_ = nullptr; // per-user-session recording mutex (FR-074)

    // Timing. timeline_ is written by UI-thread commands (Pause/Resume) and
    // read by the worker, so accesses are guarded.
    mutable std::mutex timelineMutex_;
    RecordingTimeline timeline_;
    DiskSpaceGuard diskGuard_;
    std::atomic<std::int64_t> lastOutputTimestamp_{-1};
    std::atomic<double> lastFps_{0.0};
    std::int64_t fpsSampleEncoded_ = 0;
    std::int64_t fpsSampleTimeMs_ = 0;

    // Output bookkeeping for the snapshot.
    std::filesystem::path partialPath_;
    std::filesystem::path finalPath_;
    QualityPreset preset_ = QualityPreset::Standard;
    bool captureCursor_ = false;
    std::uintmax_t encodedBytes_ = 0;
    std::int64_t nextDiskCheckMs_ = 0;
    std::int64_t nextQueueCheckMs_ = 0;
    std::int64_t nextTargetCheckMs_ = 0;

    // WGC window-capture repair (Session-9 disease: the WGC frame pool
    // delivers one blank frame or nothing and never updates). When the
    // captured count is frozen past the start deadline, the session is
    // rebuilt on the GDI PrintWindow backend; the queue is shared and
    // recording continues.
    bool usingWgcWindowCapture_ = false;
    bool captureFallbackTried_ = false;
    std::uint64_t lastSeenCapturedCount_ = 0;
    std::int64_t lastCapturedChangeMs_ = 0;

    // Monitor liveness is queried on a slow cadence (NFR-001: the 100 ms UI
    // timer must not drive full topology queries).
    static constexpr std::int64_t kMonitorCheckIntervalMs = 2000;
    mutable std::atomic<std::int64_t> lastMonitorCheckMs_{-1'000'000};
    mutable std::atomic<bool> monitorActiveCached_{false};
};

} // namespace vsr::app

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
#include "RecordingTimeline.h"
#include "StatusSnapshot.h"
#include <Encoder/VideoPreset.h>
#include <WindowManagement/PopupTracker.h>

namespace vsr::capture {
class BoundedFrameQueue;
class MonitorCaptureSession;
class GraphicsDevice;
} // namespace vsr::capture
namespace vsr::encoder {
class H264Mp4Writer;
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
    bool captureCursor = false;
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

private:
    // Worker entry for the active recording session.
    void SessionWorker(const StartRecordingRequest& request);
    bool PrepareSession(const StartRecordingRequest& request);
    void ConsumeLoop();
    void TeardownSession();

    void SetState(AppState next);
    void PublishError(const Error& error);
    void SetStopRequested(StopReason reason);
    Result<void> WaitForWorkerExit(std::chrono::milliseconds bounded);
    void ReleaseRecordingMutex();

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
    std::atomic<bool> workerAlive_{false};
    std::atomic<bool> stopRequested_{false};
    std::atomic<StopReason> stopReason_{StopReason::User};
    std::atomic<bool> paused_{false};

    // Failure monitors (set from watcher callbacks).
    std::atomic<bool> displayLost_{false};
    std::atomic<bool> targetLost_{false};

    // Session objects (owned by the worker between Preparing and Stopping).
    std::unique_ptr<capture::BoundedFrameQueue> queue_;
    std::unique_ptr<capture::GraphicsDevice> device_;
    std::unique_ptr<capture::MonitorCaptureSession> session_;
    std::unique_ptr<encoder::VideoConverter> converter_;
    std::unique_ptr<encoder::H264Mp4Writer> writer_;
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

    // Monitor liveness is queried on a slow cadence (NFR-001: the 100 ms UI
    // timer must not drive full topology queries).
    static constexpr std::int64_t kMonitorCheckIntervalMs = 2000;
    mutable std::atomic<std::int64_t> lastMonitorCheckMs_{-1'000'000};
    mutable std::atomic<bool> monitorActiveCached_{false};
};

} // namespace vsr::app

#include "Coordinator.h"

#include <Capture/BoundedFrameQueue.h>
#include <Capture/GraphicsDevice.h>
#include <Capture/CaptureSession.h>
#include <Capture/GdiCaptureSession.h>
#include <shlobj.h>
#include <Encoder/EncoderProbe.h>
#include <Encoder/FfmpegWriter.h>
#include <Encoder/VideoConverter.h>
#include <Encoder/VideoWriterFactory.h>
#include <Infrastructure/Guid.h>
#include <Infrastructure/Log.h>
#include <Infrastructure/Strings.h>
#include <WindowManagement/WindowEnumerator.h>
#include <WindowManagement/WindowPlacement.h>

#include "OutputFile.h"

namespace vsr::app {

using capture::BoundedFrameQueue;
using capture::CapturedFrame;
using capture::GraphicsDevice;
using capture::CaptureSession;
using encoder::EncoderCapability;
using encoder::IVideoWriter;
using encoder::PresetParams;
using encoder::VideoConverter;
using encoder::WriterBackend;
using window::SavedPlacement;
using window::WindowPlacementManager;

namespace {

constexpr auto kQueueCapacity = std::size_t{8};
constexpr auto kStopWait = std::chrono::milliseconds(10'000);
// First-time install of the UMDF driver (PnP configure + WUDFHost cold
// start) measurably exceeds 5 s, so the display wait must budget for it.
constexpr auto kPreparingDisplayWait = std::chrono::milliseconds(15'000);

Error StateError(const char* what) {
    return Error(ErrorCode::InternalState, L"This command is not available in the current state.",
                 std::wstring(what, what + std::char_traits<char>::length(what)) +
                     L" rejected by the state machine",
                 S_OK, true);
}

Error RollbackError(const Error& e, const char* stage) {
    Error copy = e;
    copy.technicalMessage =
        std::wstring(stage, stage + std::char_traits<char>::length(stage)) + L": " +
        copy.technicalMessage;
    return copy;
}

std::int64_t Monotonic100Ns(const IClock& clock) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(clock.MonotonicNow()).count() *
           10'000;
}

} // namespace

Coordinator::Coordinator(std::shared_ptr<IClock> clock) : clock_(std::move(clock)) {
    state_ = AppState::Initializing;
    // Startup checks that can fail (OS version, driver package) run through
    // EnsureVirtualDisplay()/Start() and report typed errors; the machine is
    // considered Ready to attempt a session once constructed.
    state_ = AppState::Ready;
}

Coordinator::~Coordinator() {
    (void)Shutdown();
    // A joinable worker thread at destruction terminates the process
    // (std::terminate); bounded-wait for it even on unusual teardown paths.
    if (worker_.joinable()) {
        (void)WaitForWorkerExit(kStopWait);
    }
}

void Coordinator::SetState(AppState next) {
    std::lock_guard<std::mutex> lock(stateMutex_);
    const AppState current = state_;
    if (current == next) return;
    // The transition table is authoritative; violations indicate a coordinator
    // bug and are logged, then applied so the UI reflects reality.
    if (!IsTransitionAllowed(current, next)) {
        log::Error("Coordinator", "STATE_TRANSITION_REJECTED", correlationId_,
                   std::wstring(L"Illegal transition ") + ToString(current) + L" -> " +
                       ToString(next));
    }
    state_ = next;
    log::Info("Coordinator", "STATE_CHANGED", correlationId_,
              std::wstring(L"State changed to ") + ToString(next));
}

void Coordinator::PublishError(const Error& error) {
    std::lock_guard<std::mutex> lock(stateMutex_);
    lastError_ = error;
    log::Error("Coordinator", "ERROR_PUBLISHED", correlationId_, error.technicalMessage,
               {{"code", ToString(error.code)},
                {"hresult", WideToUtf8(FormatHresult(error.hresult))},
                {"recoverable", error.recoverable ? "1" : "0"}});
}

Result<void> Coordinator::Start(const StartRecordingRequest& request) {
    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        if (!IsCommandAllowed(state_, Command::Start)) {
            return StateError("Start");
        }
        correlationId_ = NewGuidString();
    }
    if (worker_.joinable()) {
        worker_.join(); // defensive: never overlap session workers
    }

    // FR-074: only one recording session per user session.
    if (recordingMutex_ == nullptr) {
        recordingMutex_ = CreateMutexW(nullptr, TRUE, L"Local\\VirtualScreenRecorder.Recording");
        if (recordingMutex_ != nullptr && GetLastError() == ERROR_ALREADY_EXISTS) {
            CloseHandle(recordingMutex_);
            recordingMutex_ = nullptr;
            Error e(ErrorCode::InternalState,
                    L"A recording is already running in this Windows session.",
                    L"Recording session mutex already held", S_OK, true);
            PublishError(e);
            return e;
        }
    }

    stopRequested_ = false;
    stopReason_ = StopReason::User;
    paused_ = false;
    displayLost_ = false;
    targetLost_ = false;
    lastOutputTimestamp_ = -1;
    lastFps_.store(0.0);
    fpsSampleEncoded_ = 0;
    fpsSampleTimeMs_ = 0;
    usingWgcWindowCapture_ = false;
    captureFallbackTried_ = false;
    lastSeenCapturedCount_ = 0;
    lastCapturedChangeMs_ = 0;

    // Fast validations synchronously so the UI gets immediate feedback.
    DWORD targetPid = 0;
    if (request.targetWindow != nullptr) {
        if (!IsWindow(request.targetWindow)) {
            Error e(ErrorCode::TargetInvalid,
                    L"The selected window is no longer available. Pick a window that is still "
                    L"open.",
                    L"Start target window failed the liveness check", S_OK, true);
            PublishError(e);
            return e;
        }
        GetWindowThreadProcessId(request.targetWindow, &targetPid);
        if (targetPid == GetCurrentProcessId()) {
            Error e(ErrorCode::TargetInvalid, L"The recorder itself cannot be recorded.",
                    L"Start rejected: target belongs to the recorder process", S_OK, true);
            PublishError(e);
            return e;
        }
    }

    SetState(AppState::Preparing);

    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        snapshot_ = StatusSnapshot{};
        snapshot_.state = AppState::Preparing;
        snapshot_.correlationId = correlationId_;
        lastError_ = Error{}; // fresh session, fresh error state
        snapshot_.targetHwnd = request.targetWindow;
        snapshot_.targetPid = targetPid;
        if (request.targetWindow != nullptr) {
            snapshot_.targetExeName = window::WindowEnumerator::ExecutableNameForPid(targetPid);
        }
        snapshot_.preset = request.preset;
        snapshot_.captureCursor = request.captureCursor;
        snapshot_.outputDirectory = request.outputDirectory.wstring();
    }

    preset_ = request.preset;
    captureCursor_ = request.captureCursor;

    const StartRecordingRequest requestCopy = request;
    try {
        worker_ = std::thread([this, requestCopy] { SessionWorker(requestCopy); });
    } catch (const std::exception& e) {
        SetState(AppState::Ready);
        Error err = InternalError(L"worker spawn failed: " + Utf8ToWide(e.what()));
        PublishError(err);
        return err;
    } catch (...) {
        SetState(AppState::Ready);
        Error err = InternalError(L"worker spawn failed with an unknown exception");
        PublishError(err);
        return err;
    }
    return Result<void>{};
}

Result<void> Coordinator::Pause() {
    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        if (!IsCommandAllowed(state_, Command::Pause)) {
            return StateError("Pause");
        }
    }
    const std::int64_t now = Monotonic100Ns(*clock_);
    {
        std::lock_guard<std::mutex> lock(timelineMutex_);
        timeline_.PauseAt(now);
    }
    paused_.store(true);
    SetState(AppState::Paused); // Recording -> Paused
    log::Info("Coordinator", "PAUSED", correlationId_, L"Recording paused.");
    return Result<void>{};
}

Result<void> Coordinator::Resume() {
    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        if (!IsCommandAllowed(state_, Command::Resume)) {
            return StateError("Resume");
        }
    }
    const std::int64_t now = Monotonic100Ns(*clock_);
    {
        std::lock_guard<std::mutex> lock(timelineMutex_);
        timeline_.ResumeAt(now);
    }
    paused_.store(false);
    SetState(AppState::Recording); // Paused -> Recording
    log::Info("Coordinator", "RESUMED", correlationId_, L"Recording resumed.");
    return Result<void>{};
}

Result<void> Coordinator::Stop(StopReason reason) {
    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        if (state_ == AppState::Ready || state_ == AppState::Faulted ||
            state_ == AppState::ShuttingDown) {
            // Idempotent no-op outside an active session (FR-073) - but the
            // worker thread must still be reaped (e.g. a Faulted worker exits
            // on its own right after this check); destroying a joinable
            // std::thread terminates the process.
            if (worker_.joinable()) {
                (void)WaitForWorkerExit(kStopWait);
            }
            return Result<void>{};
        }
    }
    SetStopRequested(reason);
    return WaitForWorkerExit(kStopWait);
}

void Coordinator::SetStopRequested(StopReason reason) {
    StopReason current = stopReason_.load();
    // First reason wins; later commands cannot upgrade an automatic stop.
    while (current == StopReason::User &&
           !stopReason_.compare_exchange_weak(current, reason)) {
    }
    stopRequested_.store(true);
}

Result<void> Coordinator::WaitForWorkerExit(std::chrono::milliseconds bounded) {
    // Bounded wait (NFR-014): a worker wedged inside a driver call (e.g. a
    // WriteSample that never returns on a stalled encoder MFT) must not hang
    // Stop or process shutdown. After the deadline the thread is detached and
    // the session resources it may still touch are deliberately leaked so the
    // abandoned thread can never operate on freed memory. Stop, Shutdown, and
    // the stall watchdog can reach this concurrently; the join/detach decision
    // is serialized by workerJoinMutex_.
    const auto deadline = std::chrono::steady_clock::now() + bounded;
    while (workerAlive_.load(std::memory_order_acquire)) {
        if (std::chrono::steady_clock::now() >= deadline) {
            std::lock_guard<std::mutex> joinLock(workerJoinMutex_);
            if (workerAlive_.load(std::memory_order_acquire)) {
                if (worker_.joinable()) worker_.detach();
                {
                    std::lock_guard<std::mutex> lock(stateMutex_);
                    state_ = AppState::Faulted;
                }
                AbandonSessionResources();
                audioCapture_.release(); // bounded queue; the thread self-terminates on Stop
                ReleaseRecordingMutex(); // wedged worker will never release it
                log::Error("Coordinator", "WORKER_ABANDONED", correlationId_,
                           L"Recording worker did not exit in time; abandoned with its session "
                           L"resources (NFR-014 bounded stop).");
                return Error(ErrorCode::InternalState,
                             L"The recording pipeline stopped responding. The application "
                             L"recovered, but the recording file may be incomplete.",
                             L"Worker thread exceeded the stop deadline", S_OK, false);
            }
            break; // the worker exited between the checks; join below
        }
        Sleep(50);
    }
    {
        std::lock_guard<std::mutex> joinLock(workerJoinMutex_);
        if (worker_.joinable()) {
            worker_.join(); // sets workerAlive_ false just before exiting
        }
    }
    return Result<void>{};
}

void Coordinator::AbandonSessionResources() noexcept {
    bool expected = false;
    if (!sessionAbandoned_.compare_exchange_strong(expected, true)) return;

    // NFR-013: restore the target window from this thread — the wedged
    // worker will never reach its own restore step.
    if (savedPlacement_ != nullptr) {
        (void)WindowPlacementManager().Restore(*savedPlacement_);
        savedPlacement_.reset();
    }
    topologyWatcher_.Stop();

    // Deliberate leak of the wedged pipeline (bounded set of objects): the
    // abandoned worker may be blocked inside any of these objects' calls;
    // releasing ownership instead of destroying them keeps the process alive
    // and memory-safe. The OS reclaims everything at process exit.
    (void)writer_.release();
    (void)converter_.release();
    (void)session_.release();
    (void)queue_.release();
    (void)device_.release();
    (void)encoderCap_.release();
    log::Error("Coordinator", "SESSION_ABANDONED", correlationId_,
               L"Session resources abandoned to contain a wedged worker (NFR-014).");
}

Result<display::MonitorDescriptor> Coordinator::EnsureVirtualDisplay() {
    // FR-013/FR-014: locate by stable identity; must be in the extended
    // desktop before recording.
    return softwareDevice_.EnsureDisplayAvailable(kPreparingDisplayWait);
}

Result<void> Coordinator::DisableVirtualDisplay() {
    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        if (!IsCommandAllowed(state_, Command::DisableDisplay)) {
            return StateError("DisableVirtualDisplay");
        }
    }
    softwareDevice_.ReleaseDisplay();
    return Result<void>{};
}

Result<void> Coordinator::Reset() {
    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        if (state_ != AppState::Faulted) {
            return StateError("Reset");
        }
        lastError_ = Error{};
    }
    SetState(AppState::Ready);
    return Result<void>{};
}

Result<void> Coordinator::Shutdown() {
    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        if (state_ == AppState::ShuttingDown) {
            return Result<void>{};
        }
    }
    const bool active = [&] {
        std::lock_guard<std::mutex> lock(stateMutex_);
        switch (state_) {
            case AppState::Recording:
            case AppState::Paused:
            case AppState::Preparing:
            case AppState::Stopping:
                return true;
            default:
                return false;
        }
    }();
    if (active) {
        SetStopRequested(StopReason::Shutdown);
        (void)WaitForWorkerExit(kStopWait);
    }
    SetState(AppState::ShuttingDown);
    return Result<void>{};
}

// ---------------------------------------------------------------------------
// Session worker
// ---------------------------------------------------------------------------

void Coordinator::SessionWorker(const StartRecordingRequest& request) {
    workerAlive_.store(true, std::memory_order_release);
    if (!PrepareSession(request)) {
        // PrepareSession rolled back and published the error.
        bool recoverable = true;
        {
            std::lock_guard<std::mutex> lock(stateMutex_);
            recoverable = lastError_.recoverable;
        }
        SetState(recoverable ? AppState::Ready : AppState::Faulted);
        TeardownSession();
        ReleaseRecordingMutex();
        workerAlive_.store(false, std::memory_order_release);
        return;
    }

    SetState(AppState::Recording); // Preparing -> Recording
    log::Info("Coordinator", "RECORDING_STARTED", correlationId_, L"Recording is active.");

    // Independent stall watchdog for the active phase. Failures to spawn it
    // are non-fatal: the in-thread watermark checks and the bounded external
    // stop remain as backstops.
    watchdogStop_.store(false);
    try {
        watchdog_ = std::thread([this] { WatchdogLoop(); });
    } catch (...) {
        log::Warning("Coordinator", "WATCHDOG_UNAVAILABLE", correlationId_,
                     L"Encoder stall watchdog could not start; continuing without it.");
    }

    ConsumeLoop();

    // Stopping phase: drain, finalize, restore (design.md §4.8 stop order).
    SetState(AppState::Stopping);
    // The watchdog only covers the Recording phase; teardown has its own
    // bounded-stop containment for a Finalize that never returns.
    watchdogStop_.store(true, std::memory_order_release);
    if (watchdog_.joinable()) watchdog_.join();
    TeardownSession();

    const StopReason reason = stopReason_.load();
    if (reason == StopReason::User || reason == StopReason::Shutdown) {
        SetState(AppState::Ready);
    } else {
        SetState(AppState::Faulted);
    }
    ReleaseRecordingMutex();
    workerAlive_.store(false, std::memory_order_release);
}

void Coordinator::ReleaseRecordingMutex() {
    if (recordingMutex_ != nullptr) {
        CloseHandle(recordingMutex_);
        recordingMutex_ = nullptr;
    }
}

bool Coordinator::PrepareSession(const StartRecordingRequest& request) {
    // Rollback order on failure (T080): capture -> window -> writer -> display.
    const PresetParams preset = GetPresetParams(request.preset);
    auto stopNow = [this] { return stopRequested_.load(); };

    // 1. Target surface. Window targets record driverless: the window is
    // parked off-screen and captured directly, with no virtual display and no
    // display driver. Only the display-only dev CLI (--dev-record) still
    // brings up the virtual display.
    const bool windowTarget = request.targetWindow != nullptr;
    display::MonitorDescriptor monitor{};
    if (!windowTarget) {
        auto monitorResult = EnsureVirtualDisplay();
        if (!monitorResult || stopNow()) {
            if (!monitorResult) PublishError(monitorResult.TakeError());
            return false;
        }
        monitor = monitorResult.Value();
    }

    // 2. Output validation and naming (FR-052, FR-053, FR-059).
    const std::uintmax_t requiredBytes =
        DiskSpaceGuard::RequiredBytes(preset.bitrateBps, 60.0) +
        DiskSpaceGuard::kMinimumReserveBytes;
    std::filesystem::path outputDir = request.outputDirectory;
    if (outputDir.empty()) {
        PWSTR videos = nullptr;
        if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Videos, 0, nullptr, &videos))) {
            outputDir = std::filesystem::path(videos) / L"Virtual Screen Recorder";
            CoTaskMemFree(videos);
            std::error_code mkEc;
            std::filesystem::create_directories(outputDir, mkEc);
        }
    }
    auto dirResult = ValidateOutputDirectory(outputDir, requiredBytes);
    if (!dirResult || stopNow()) {
        if (!dirResult) PublishError(dirResult.TakeError());
        return false;
    }
    auto plan = PlanOutputFile(outputDir, *clock_);
    if (!plan || stopNow()) {
        if (!plan) PublishError(plan.TakeError());
        return false;
    }
    partialPath_ = plan.Value().partialPath;
    finalPath_ = plan.Value().finalPath;

    // 3. Encoder availability BEFORE any capture or file creation (FR-063).
    // The ffmpeg backend does not use OS encoder MFTs, so a failed probe
    // must not block it; the MF probe result is still used for the
    // MF-backend fallback selection below.
    const WriterBackend backend = encoder::ResolveBackend(request.backend);
    auto probe = encoder::ProbeEncoders(request.encoderPreference);
    if (!probe && backend != WriterBackend::Ffmpeg) {
        if (stopNow()) return false;
        PublishError(probe.TakeError());
        return false;
    }
    if (probe) {
        encoderCap_ = std::make_unique<EncoderCapability>(probe.Value().selected);
    } else {
        EncoderCapability ffmpegCap{};
        ffmpegCap.name = L"ffmpeg (libx264)";
        encoderCap_ = std::make_unique<EncoderCapability>(ffmpegCap);
    }

    // 4. Graphics device (FR-041).
    auto deviceResult = GraphicsDevice::Create();
    if (!deviceResult || stopNow()) {
        if (!deviceResult) PublishError(deviceResult.TakeError());
        return false;
    }
    device_ = std::make_unique<GraphicsDevice>(deviceResult.Value());

    encoder::AudioFormat audioFormat_{};
    // 4b. System audio format: probed up front so the writer is created with
    // its AAC stream (best effort - failures degrade to video-only).
    if (request.captureAudio) {
        auto fmt = encoder::AudioCapture::QueryLoopbackFormat();
        if (fmt.IsOk()) {
            audioFormat_ = fmt.Value();
            audioRate_ = fmt.Value().sampleRate;
            audioChannels_ = fmt.Value().channels;
        } else {
            log::Warning("Coordinator", "AUDIO_CAPTURE_UNAVAILABLE", correlationId_,
                         fmt.TakeError().technicalMessage);
        audioFormat_ = encoder::AudioFormat{};
        audioRate_ = 0;
        }
    }

    // 5. Target placement. Window targets are captured IN PLACE: moving the
    // target (park or virtual display) made occluded apps stop rendering and
    // the capture record a blank surface. Capture keeps working when other
    // windows cover the target; only minimizing it freezes the frames.
    // The display-only dev path still moves the target onto the virtual
    // display (FR-023..FR-025).

    // 5b. Track new windows of the target process and relocate them onto the
    // virtual display (FR-029 / T100). Display-only dev mode skips this, and
    // window capture cannot include owned popups (they are separate windows),
    // so parked sessions skip it too.
    if (request.targetWindow != nullptr && !windowTarget) {
        auto tracking = popupTracker_.Start(request.targetWindow, monitor.workRect);
        if (!tracking) {
            // Non-fatal (SHOULD requirement): log and continue recording.
            log::Warning("Coordinator", "POPUP_TRACKING_UNAVAILABLE", correlationId_,
                         tracking.TakeError().technicalMessage);
        }
    }

    // 6. Queue + capture session (FR-040..FR-046). Window targets capture
    // through the WGC backend on desktop sessions; inside a remote (RDP)
    // session WGC window capture returns a blank surface, so the GDI
    // PrintWindow backend is used there instead.
    queue_ = std::make_unique<BoundedFrameQueue>(kQueueCapacity);
    const bool remoteSession = GetSystemMetrics(SM_REMOTESESSION) != 0;
    usingWgcWindowCapture_ = windowTarget && !remoteSession;
    using SessionResult = Result<std::unique_ptr<capture::ICaptureSession>>;
    SessionResult sessionResult = SessionResult{Error{ErrorCode::CaptureInitFailed,
                                                      L"", L"capture session not started"}};
    if (windowTarget) {
        if (remoteSession) {
            auto gdi = capture::GdiCaptureSession::Start(*device_, request.targetWindow,
                                                         preset.fps, *queue_, *clock_);
            if (gdi) {
                sessionResult = SessionResult{std::move(gdi.Value())};
            } else {
                sessionResult = SessionResult{gdi.TakeError()};
            }
        } else {
            auto wgc = capture::CaptureSession::Start(*device_, request.targetWindow,
                                                      *queue_, *clock_);
            if (wgc) {
                sessionResult = SessionResult{std::move(wgc.Value())};
            } else {
                sessionResult = SessionResult{wgc.TakeError()};
            }
        }
    } else {
        auto disp = capture::CaptureSession::Start(*device_, monitor.handle, *queue_,
                                                   *clock_);
        if (disp) {
            sessionResult = SessionResult{std::move(disp.Value())};
        } else {
            sessionResult = SessionResult{disp.TakeError()};
        }
    }
    if (!sessionResult) {
        Error e = RollbackError(sessionResult.TakeError(), "capture start");
        if (savedPlacement_ != nullptr) {
            (void)WindowPlacementManager().Restore(*savedPlacement_);
            savedPlacement_.reset();
        }
        PublishError(e);
        return false;
    }
    session_ = std::move(sessionResult.Value());
    session_->SetCursorCaptureEnabled(request.captureCursor);

    // 7. Converter + writer (the .partial.mp4 is created only here, FR-057).
    auto converterResult = VideoConverter::Create(*device_, preset.width, preset.height);
    if (!converterResult) {
        Error e = RollbackError(converterResult.TakeError(), "converter create");
        session_->Stop();
        if (savedPlacement_ != nullptr) {
            (void)WindowPlacementManager().Restore(*savedPlacement_);
            savedPlacement_.reset();
        }
        PublishError(e);
        return false;
    }
    converter_ = std::move(converterResult.Value());

    const encoder::AudioFormat* audioFormatArg =
        request.captureAudio && audioRate_ > 0 ? &audioFormat_ : nullptr;
    auto writerResult = encoder::CreateWriter(backend, *device_, *encoderCap_, preset,
                                              partialPath_, audioFormatArg);
    if (!writerResult && backend == WriterBackend::MediaFoundation && probe) {
        // Encoder-fallback retry (MF backend only): a candidate can pass the
        // capability probe yet fail at activation or the consumption gate
        // (per-session hardware MFT stalls, software MFTs that never encode
        // on some OS builds). Retry once with the next best candidate under
        // the same preference before failing the session.
        const Error firstError = writerResult.TakeError();
        auto alternative =
            encoder::SelectEncoder(probe.Value().candidates, request.encoderPreference,
                                   encoderCap_->name);
        if (alternative) {
            log::Warning("Coordinator", "ENCODER_FALLBACK_RETRY", correlationId_,
                         L"Selected encoder failed to activate; retrying with the next "
                         L"candidate.",
                         {{"failed", WideToUtf8(encoderCap_->name)},
                          {"retry", WideToUtf8(alternative.Value().name)}});
            DeleteFileW(partialPath_.c_str());
            encoderCap_ = std::make_unique<EncoderCapability>(alternative.Value());
            writerResult = encoder::CreateWriter(backend, *device_, *encoderCap_, preset,
                                                 partialPath_, audioFormatArg);
        } else {
            writerResult = Result<std::unique_ptr<IVideoWriter>>(firstError);
        }
    }
    if (!writerResult) {
        Error e = RollbackError(writerResult.TakeError(), "writer create");
        session_->Stop();
        if (savedPlacement_ != nullptr) {
            (void)WindowPlacementManager().Restore(*savedPlacement_);
            savedPlacement_.reset();
        }
        PublishError(e);
        return false;
    }
    writer_ = std::move(writerResult.Value());

    if (request.captureAudio && audioRate_ > 0) {
        auto started = encoder::AudioCapture::Start(
            *clock_, [this](encoder::AudioChunk&& chunk) {
                audioReceived_.store(true, std::memory_order_relaxed);
                std::lock_guard<std::mutex> lock(audioMutex_);
                if (audioQueue_.size() >= 400) audioQueue_.pop_front();
                audioQueue_.push_back(std::move(chunk));
            });
        if (started) {
            audioCapture_ = std::move(started.Value());
            log::Info("Coordinator", "AUDIO_STREAM_ENABLED", correlationId_,
                      L"System audio will be recorded.",
                      {{"sampleRate", std::to_string(audioRate_)},
                       {"channels", std::to_string(audioChannels_)}});
        } else {
            log::Warning("Coordinator", "AUDIO_CAPTURE_UNAVAILABLE", correlationId_,
                         L"System audio capture could not start; recording video only.");
        }
    }

    // 8. Disk guard + topology watcher + timing anchors. The topology watcher
    // guards the virtual display only; parked-window sessions end through the
    // target-liveness poll instead.
    diskGuard_.Begin(partialPath_.parent_path(), preset.bitrateBps);
    {
        std::lock_guard<std::mutex> lock(timelineMutex_);
        timeline_.StartAt(Monotonic100Ns(*clock_));
        audioCursor100Ns_ = -1; // fresh session: re-anchor the audio cursor
    }
    if (!windowTarget) {
        auto watcherResult = topologyWatcher_.Start([this] { displayLost_ = true; });
        if (!watcherResult) {
            // Non-fatal: the consume loop still re-checks the display on events.
            log::Warning("Coordinator", "TOPOLOGY_WATCHER_UNAVAILABLE", correlationId_,
                         L"Topology watcher unavailable; falling back to event-only checks.");
        }
    }

    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        snapshot_.virtualMonitorActive = !windowTarget;
        if (windowTarget) {
            RECT wr{};
            GetWindowRect(request.targetWindow, &wr);
            snapshot_.monitorX = wr.left;
            snapshot_.monitorY = wr.top;
            snapshot_.monitorW = wr.right - wr.left;
            snapshot_.monitorH = wr.bottom - wr.top;
        } else {
            snapshot_.monitorX = monitor.monitorRect.left;
            snapshot_.monitorY = monitor.monitorRect.top;
            snapshot_.monitorW = monitor.monitorRect.right - monitor.monitorRect.left;
            snapshot_.monitorH = monitor.monitorRect.bottom - monitor.monitorRect.top;
        }
        snapshot_.partialPath = partialPath_.wstring();
        snapshot_.finalPath = finalPath_.wstring();
    }

    const std::int64_t nowMs =
        std::chrono::duration_cast<std::chrono::milliseconds>(clock_->MonotonicNow()).count();
    nextDiskCheckMs_ = nowMs + 5'000;
    nextTargetCheckMs_ = nowMs + 500;
    lastCapturedChangeMs_ = nowMs; // repair clock starts at Recording
    encodedBytes_ = 0;
    return true;
}

double Coordinator::SampleFps() {
    const std::uint64_t encoded = writer_ != nullptr ? writer_->WrittenFrames() : 0;
    const std::int64_t nowMs =
        std::chrono::duration_cast<std::chrono::milliseconds>(clock_->MonotonicNow()).count();
    if (fpsSampleTimeMs_ != 0 && nowMs > fpsSampleTimeMs_) {
        const double seconds = static_cast<double>(nowMs - fpsSampleTimeMs_) / 1000.0;
        lastFps_.store(static_cast<double>(encoded -
                                           static_cast<std::uint64_t>(fpsSampleEncoded_)) /
                       seconds);
    }
    fpsSampleEncoded_ = static_cast<std::int64_t>(encoded);
    fpsSampleTimeMs_ = nowMs;
    return lastFps_.load();
}

void Coordinator::DrainAudio() {
    if (audioCapture_ == nullptr || writer_ == nullptr) return;
    std::deque<encoder::AudioChunk> chunks;
    {
        std::lock_guard<std::mutex> lock(audioMutex_);
        chunks.swap(audioQueue_);
    }
    static bool firstChunkLogged = false;
    if (!chunks.empty() && !firstChunkLogged) {
        firstChunkLogged = true;
        log::Info("Coordinator", "AUDIO_FIRST_CHUNK", correlationId_, L"",
                  {{"frames", std::to_string(chunks.front().frames)},
                   {"tsUs", std::to_string(chunks.front().timestampUs)}});
    }
    while (!chunks.empty()) {
        auto chunk = std::move(chunks.front());
        chunks.pop_front();
        const std::int64_t ts100 = static_cast<std::int64_t>(chunk.timestampUs) * 10;
        std::int64_t out = 0;
        {
            std::lock_guard<std::mutex> lock(timelineMutex_);
            if (paused_.load() || ts100 < timeline_.StaleFrameWatermark100Ns()) {
                audioCursor100Ns_ = -1; // re-anchor on the next written chunk
                continue;
            }
            if (audioCursor100Ns_ < 0) {
                // Anchor the audio track on the timeline (pause-aware), then
                // advance strictly by sample count from here on.
                audioCursor100Ns_ = timeline_.OutputTimestamp100Ns(ts100);
            }
            out = audioCursor100Ns_;
        }
        const std::int64_t rate = audioRate_ > 0 ? static_cast<std::int64_t>(audioRate_) : 48000;
        const std::int64_t dur100 =
            static_cast<std::int64_t>(chunk.frames) * 10'000'000 / rate;
        audioCursor100Ns_ = out + dur100;
        auto written =
            writer_->WriteAudioSample(chunk.pcm.data(), chunk.frames, out, dur100);
        if (!written.IsOk()) {
            const Error e = written.TakeError();
            log::Error("Coordinator", "AUDIO_WRITE_FAILED", correlationId_, e.technicalMessage,
                      {{"hresult", WideToUtf8(FormatHresult(e.hresult))}});
            break;
        }
    }
}

void Coordinator::ConsumeLoop() {
    const std::int64_t frameDuration =
        encoder::FrameDuration100Ns(GetPresetParams(preset_).fps);

    for (;;) {
        // 1. Stop command.
        if (stopRequested_.load()) break;

        // 2. Failure monitors (bounded work per iteration).
        const std::int64_t nowMs =
            std::chrono::duration_cast<std::chrono::milliseconds>(clock_->MonotonicNow()).count();

        // Target liveness poll (FR-028 / AC-006).
        if (nowMs >= nextTargetCheckMs_) {
            nextTargetCheckMs_ = nowMs + 500;
            HWND hwnd = nullptr;
            DWORD pid = 0;
            {
                std::lock_guard<std::mutex> lock(stateMutex_);
                hwnd = snapshot_.targetHwnd;
                pid = snapshot_.targetPid;
            }
            if (hwnd != nullptr && !window::WindowEnumerator::IsTargetAlive(hwnd, pid)) {
                targetLost_ = true;
                PublishError(Error(ErrorCode::TargetLost,
                                   L"The recorded application was closed. The recording was "
                                   L"stopped and saved.",
                                   L"Target window no longer alive during recording", S_OK,
                                   true));
                SetStopRequested(StopReason::TargetLost);
                break;
            }
        }

        // Display loss (FR-015 / AC-002 guard): watcher sets the flag; verify
        // against the actual topology to ignore transient churn.
        if (displayLost_.exchange(false)) {
            if (!display::TopologyService::FindVirtualMonitor().IsOk()) {
                PublishError(Error(ErrorCode::DisplayDisconnected,
                                   L"The virtual display was disconnected. The recording was "
                                   L"stopped and saved.",
                                   L"Virtual monitor disappeared during recording", S_OK, true));
                SetStopRequested(StopReason::DisplayLost);
                break;
            }
        }

        // Device loss (FR-012). Only the documented removal codes end the
        // session: the AMD driver under RDP has been observed returning
        // transient errors (e.g. E_INVALIDARG) from GetDeviceRemovedReason
        // while the device keeps working, which is not a removal.
        if (device_ != nullptr && device_->IsValid()) {
            const HRESULT removed = device_->CheckDeviceRemoved();
            const bool deviceGone =
                removed == DXGI_ERROR_DEVICE_REMOVED || removed == DXGI_ERROR_DEVICE_RESET ||
                removed == DXGI_ERROR_DEVICE_HUNG ||
                removed == DXGI_ERROR_DRIVER_INTERNAL_ERROR;
            if (FAILED(removed) && deviceGone) {
                PublishError(ErrorFromHresult(removed, ErrorCode::CaptureDeviceLost,
                                              L"The GPU was reset or disconnected. The recording "
                                              L"was stopped and saved.",
                                              L"D3D device removed during recording", true));
                SetStopRequested(StopReason::DeviceLost);
                break;
            }
        }

        // Disk guard (FR-060 / AC-007).
        if (nowMs >= nextDiskCheckMs_) {
            nextDiskCheckMs_ = nowMs + 5'000;
            auto disk = diskGuard_.Check(encodedBytes_);
            if (!disk) {
                PublishError(disk.TakeError());
                SetStopRequested(StopReason::DiskLow);
                break;
            }
        }

        // Writer health (NFR-007/NFR-014): the backends latch internal
        // failures (stalled encoder MFT, dead ffmpeg feeder); surfacing them
        // here stops the session safely instead of recording nothing.
        if (nowMs >= nextQueueCheckMs_) {
            nextQueueCheckMs_ = nowMs + 3'000;
            if (writer_ != nullptr) {
                auto health = writer_->QueueHealth();
                if (!health) {
                    PublishError(health.TakeError());
                    SetStopRequested(StopReason::WriteFailed);
                    break;
                }
            }
        }

        // WGC window-capture repair: the documented failure mode is a frame
        // pool that delivers a single blank frame (or nothing) and never
        // updates. When the captured count is frozen past the start deadline,
        // the session is rebuilt on the GDI PrintWindow backend (same queue;
        // the recording continues with the documented fallback).
        const auto countersNow = queue_->Counters();
        if (countersNow.captured != lastSeenCapturedCount_) {
            lastSeenCapturedCount_ = countersNow.captured;
            lastCapturedChangeMs_ = nowMs;
        }
        if (usingWgcWindowCapture_ && !captureFallbackTried_ &&
            nowMs - lastCapturedChangeMs_ >= 2'500) {
            captureFallbackTried_ = true;
            {
                HWND hwnd = nullptr;
                {
                    std::lock_guard<std::mutex> lock(stateMutex_);
                    hwnd = snapshot_.targetHwnd;
                }
                log::Warning("Coordinator", "WGC_WINDOW_CAPTURE_IDLE", correlationId_,
                             L"WGC window capture stopped producing frames; rebuilding the "
                             L"session on the GDI backend.",
                             {{"captured", std::to_string(countersNow.captured)}});
                session_->Stop();
                auto gdi = capture::GdiCaptureSession::Start(
                    *device_, hwnd, GetPresetParams(preset_).fps, *queue_, *clock_);
                if (gdi) {
                    session_ = std::move(gdi.Value());
                    session_->SetCursorCaptureEnabled(captureCursor_);
                    usingWgcWindowCapture_ = false;
                    log::Warning("Coordinator", "WGC_TO_GDI_FALLBACK", correlationId_,
                                 L"Window capture continues on the GDI backend.");
                } else {
                    Error e = RollbackError(gdi.TakeError(), "gdi capture repair");
                    PublishError(e);
                    SetStopRequested(StopReason::DeviceLost);
                    break;
                }
            }
        }

        // 3. Consume one frame (bounded wait keeps the loop responsive).
        CapturedFrame raw;
        if (!queue_->Pop(raw, std::chrono::milliseconds(250))) {
            SampleFps();
            DrainAudio();
            continue;
        }

        auto* payload = static_cast<ID3D11Texture2D*>(raw.payload.get());
        queue_->NotifyEncoded();
        if (payload == nullptr) continue;

        const std::int64_t frameCapture100Ns = static_cast<std::int64_t>(raw.timestampUs) * 10;

        // FR-071/FR-072: discard frames captured before the newest pause
        // boundary watermark; while paused, everything is discarded.
        std::int64_t outputTime = 0;
        {
            std::lock_guard<std::mutex> lock(timelineMutex_);
            if (paused_.load() || frameCapture100Ns < timeline_.StaleFrameWatermark100Ns()) {
                continue;
            }
            outputTime = timeline_.OutputTimestamp100Ns(frameCapture100Ns);
        }

        DrainAudio();

        // Convert BGRA -> NV12 and submit with timeline-derived timestamps.
        auto converted = converter_->Convert(payload);
        if (!converted) {
            PublishError(converted.TakeError());
            SetStopRequested(StopReason::DeviceLost);
            break;
        }
        auto submitted = writer_->SubmitFrame(converted.Value(), outputTime, frameDuration);
        if (!submitted) {
            PublishError(submitted.TakeError());
            SetStopRequested(StopReason::WriteFailed);
            break;
        }
        lastOutputTimestamp_.store(outputTime);
        SampleFps();
    }
}

// ---------------------------------------------------------------------------
// Encoder stall watchdog
// ---------------------------------------------------------------------------

void Coordinator::WatchdogLoop() {
    // Covers the stall family the worker's in-thread watermark check cannot
    // see: a WriteSample blocked forever inside a stalled MFT never returns
    // to run the next self-check. This thread samples progress counters and,
    // on a declared stall, escalates through the same bounded-abandon stop
    // path as Stop (process survives, session Faulted, partial file kept).
    constexpr DWORD kCheckIntervalMs = 1'000;
    EncoderStallDetector detector;
    for (;;) {
        Sleep(kCheckIntervalMs);
        if (watchdogStop_.load(std::memory_order_acquire)) return;

        bool active = false;
        {
            std::lock_guard<std::mutex> lock(stateMutex_);
            active = state_ == AppState::Recording;
        }
        if (paused_.load(std::memory_order_relaxed)) active = false;
        // writer_/queue_ live until TeardownSession resets them, which happens
        // only after this thread has been joined (or after an abandon, which
        // releases rather than destroys them — the null guard covers that).
        if (writer_ == nullptr || queue_ == nullptr) return;

        const std::uint64_t encoded = writer_->WrittenFrames();
        const auto counters = queue_->Counters();
        const std::int64_t nowMs =
            std::chrono::duration_cast<std::chrono::milliseconds>(clock_->MonotonicNow()).count();
        if (!detector.Feed(active, encoded, counters.captured, nowMs)) continue;

        log::Error("Coordinator", "ENCODER_STALLED_WATCHDOG", correlationId_,
                   L"Encoder stopped consuming while capture kept producing; escalating to a "
                   L"bounded stop.",
                   {{"encodedFrames", std::to_string(encoded)},
                    {"capturedFrames", std::to_string(counters.captured)},
                    {"droppedFrames", std::to_string(counters.dropped)}});
        PublishError(Error(ErrorCode::EncoderWriteFailed,
                           L"The encoder stopped responding. The recording was stopped safely "
                           L"and the partial file was kept.",
                           L"Encoder stall watchdog fired", S_OK, true));
        SetStopRequested(StopReason::WriteFailed);

        // Give the worker a bounded window to unwind on its own; a WriteSample
        // wedged inside the MFT will not, and the deadline path contains it.
        const auto deadline = std::chrono::steady_clock::now() + kStopWait;
        while (workerAlive_.load(std::memory_order_acquire)) {
            if (watchdogStop_.load(std::memory_order_acquire)) return; // healthy teardown began
            if (std::chrono::steady_clock::now() >= deadline) break;
            Sleep(50);
        }
        if (!workerAlive_.load(std::memory_order_acquire)) return; // worker exited cleanly
        {
            std::lock_guard<std::mutex> joinLock(workerJoinMutex_);
            if (!workerAlive_.load(std::memory_order_acquire)) return;
            if (worker_.joinable()) worker_.detach();
        }
        {
            std::lock_guard<std::mutex> lock(stateMutex_);
            state_ = AppState::Faulted;
        }
        AbandonSessionResources();
        audioCapture_.release();
        ReleaseRecordingMutex();
        log::Error("Coordinator", "WORKER_ABANDONED", correlationId_,
                   L"Recording worker did not exit after the watchdog stop; abandoned with its "
                   L"session resources (NFR-014).");
        return;
    }
}

void Coordinator::TeardownSession() {
    // Stop order (design.md §4.8): stop accepting frames -> close capture ->
    // signal queue completion -> encode remaining frames -> drain -> finalize
    // -> close file -> rename -> restore target -> publish result.
    if (session_ != nullptr) session_->Stop();
    if (queue_ != nullptr) queue_->Close();

    // Drain everything already accepted so the MP4 is complete (FR-056).
    if (queue_ != nullptr && writer_ != nullptr && converter_ != nullptr) {
        const std::int64_t frameDuration =
            encoder::FrameDuration100Ns(GetPresetParams(preset_).fps);
        CapturedFrame raw;
        while (queue_->Pop(raw, std::chrono::milliseconds(0))) {
            queue_->NotifyEncoded();
            auto* payload = static_cast<ID3D11Texture2D*>(raw.payload.get());
            if (payload == nullptr || paused_.load()) continue;
            auto converted = converter_->Convert(payload);
            if (!converted) break;
            std::int64_t outputTime = 0;
            {
                std::lock_guard<std::mutex> lock(timelineMutex_);
                if (static_cast<std::int64_t>(raw.timestampUs) * 10 <
                    timeline_.StaleFrameWatermark100Ns()) {
                    continue;
                }
                outputTime =
                    timeline_.OutputTimestamp100Ns(static_cast<std::int64_t>(raw.timestampUs) * 10);
            }
            (void)writer_->SubmitFrame(converted.Value(), outputTime, frameDuration);
        }
    }

    // Stop audio first and flush its last chunks into the writer before the
    // MP4 is finalized (finalize closes every stream).
    log::Info("Coordinator", "TEARDOWN_AUDIO_STOP", correlationId_, L"");
    if (audioCapture_ != nullptr) audioCapture_->Stop();
    log::Info("Coordinator", "TEARDOWN_AUDIO_DRAIN", correlationId_, L"");
    DrainAudio();
    // WASAPI loopback yields no packets while the render endpoint is idle;
    // a zero-input AAC stream can wedge Finalize, so feed minimal silence
    // when nothing was captured.
    if (writer_ != nullptr && writer_->HasAudio() &&
        !audioReceived_.load(std::memory_order_relaxed)) {
        const std::int64_t silenceFrames = static_cast<std::int64_t>(audioRate_) / 10;
        std::vector<std::int16_t> silence(
            static_cast<std::size_t>(silenceFrames) * audioChannels_, 0);
        const std::int64_t now100 = static_cast<std::int64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(clock_->MonotonicNow())
                .count()) *
            10;
        (void)writer_->WriteAudioSample(silence.data(),
                                        static_cast<std::size_t>(silenceFrames), now100,
                                        silenceFrames * 10 * 1000);
        log::Warning("Coordinator", "AUDIO_IDLE_SILENCE", correlationId_,
                     L"No system audio was active; wrote a silent audio track.");
    }
    log::Info("Coordinator", "TEARDOWN_BEFORE_FINALIZE", correlationId_, L"");

    if (writer_ != nullptr) {
        auto finalized = writer_->Finalize();
        if (finalized.IsOk()) {
            // FR-058: rename where the filesystem permits; keep the partial
            // file otherwise (FR-057).
            std::error_code ec;
            std::filesystem::rename(partialPath_, finalPath_, ec);
            if (ec) {
                PublishError(Error(ErrorCode::FinalizeFailed,
                                   L"The recording file could not be renamed after "
                                   L"finalization. The partial file was kept.",
                                   L"rename failed: " + Utf8ToWide(ec.message()), S_OK, true));
            } else {
                log::Info("Coordinator", "OUTPUT_RENAMED", correlationId_,
                          L"Recording finalized and renamed.",
                          {{"path", WideToUtf8(finalPath_.wstring())}});
            }
        } else {
            PublishError(finalized.TakeError());
        }
    }

    topologyWatcher_.Stop();
    popupTracker_.Stop();

    // Restore the target window (FR-026 / NFR-013).
    if (savedPlacement_ != nullptr) {
        (void)WindowPlacementManager().Restore(*savedPlacement_);
        savedPlacement_.reset();
    }

    // Persist the final pipeline counters into the snapshot before the live
    // objects are destroyed: the UI and the dev CLI read the final
    // captured/encoded/dropped numbers after Stop, and GetStatus only sees
    // live counters while queue_/session_ still exist.
    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        if (queue_ != nullptr) {
            const auto counters = queue_->Counters();
            snapshot_.capturedFrames = counters.captured;
            snapshot_.encodedFrames = counters.encoded;
            snapshot_.droppedFrames = counters.dropped;
        }
        if (session_ != nullptr) {
            const auto stats = session_->Stats();
            snapshot_.captureWidth = stats.lastWidth;
            snapshot_.captureHeight = stats.lastHeight;
        }
    }

    // Release pipeline objects in reverse creation order.
    writer_.reset();
    audioCapture_.reset();
    converter_.reset();
    session_.reset();
    queue_.reset();
    device_.reset();
    encoderCap_.reset();
    partialPath_.clear();
}

StatusSnapshot Coordinator::GetStatus() const {
    StatusSnapshot s;
    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        s = snapshot_;
        s.state = state_;
        s.correlationId = correlationId_;
        s.hasError = lastError_.code != ErrorCode::None;
        s.errorCode = lastError_.code;
        s.errorUserMessage = lastError_.userMessage;
        s.preset = preset_;
        s.captureCursor = captureCursor_;

        if (queue_ != nullptr) {
            const auto counters = queue_->Counters();
            s.capturedFrames = counters.captured;
            s.encodedFrames = counters.encoded;
            s.droppedFrames = counters.dropped;
        }
        if (session_ != nullptr) {
            const auto stats = session_->Stats();
            s.captureWidth = stats.lastWidth;
            s.captureHeight = stats.lastHeight;
        }
        s.approxFps = lastFps_.load();
        {
            std::lock_guard<std::mutex> tlock(timelineMutex_);
            s.activeElapsedMs = timeline_.ActiveElapsed100Ns(Monotonic100Ns(*clock_)) / 10'000;
        }
    }
    // Topology query on a slow cadence, outside the lock; it touches the
    // display stack and would otherwise run on every UI timer tick (NFR-001).
    const std::int64_t nowMs =
        std::chrono::duration_cast<std::chrono::milliseconds>(clock_->MonotonicNow()).count();
    std::int64_t last = lastMonitorCheckMs_.load(std::memory_order_relaxed);
    if (nowMs - last >= kMonitorCheckIntervalMs) {
        const bool active = display::SoftwareDisplayDevice::IsMonitorActive();
        monitorActiveCached_.store(active, std::memory_order_relaxed);
        lastMonitorCheckMs_.compare_exchange_strong(last, nowMs, std::memory_order_relaxed);
    }
    s.virtualMonitorActive = monitorActiveCached_.load(std::memory_order_relaxed);
    return s;
}

bool Coordinator::TryGetPreviewFrameCpu(std::vector<std::uint8_t>& outBgra, UINT& width,
                                        UINT& height) {
    std::lock_guard<std::mutex> lock(stateMutex_);
    if (session_ == nullptr) return false;
    return session_->CopyLatestToCpu(outBgra, width, height);
}

} // namespace vsr::app

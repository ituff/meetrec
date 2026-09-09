#include "Application.h"

#include <cstdio>

#include <objbase.h>

#include <DisplayControl/SoftwareDisplayDevice.h>
#include <DisplayControl/TopologyService.h>
#include <Encoder/VideoPreset.h>
#include <Infrastructure/Log.h>
#include <Infrastructure/Paths.h>
#include <Infrastructure/Settings.h>
#include <Infrastructure/Strings.h>

#include "MainWindow.h"

namespace vsr::app {

namespace {

struct OsVersionInfoW {
    unsigned long dwOSVersionInfoSize;
    unsigned long dwMajorVersion;
    unsigned long dwMinorVersion;
    unsigned long dwBuildNumber;
    unsigned long dwPlatformId;
    wchar_t szCSDVersion[128];
};

bool IsWindows11OrLater() {
    // FR-004: OS gate. Windows 11 reports build >= 22000.
    typedef unsigned long(__stdcall * RtlGetVersionFn)(OsVersionInfoW*);
    OsVersionInfoW info = {};
    info.dwOSVersionInfoSize = sizeof(info);
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    if (ntdll == nullptr) return false;
    auto fn = reinterpret_cast<RtlGetVersionFn>(GetProcAddress(ntdll, "RtlGetVersion"));
    if (fn == nullptr || fn(&info) != 0) return false;
    return info.dwMajorVersion >= 10 && info.dwBuildNumber >= 22000;
}

void ShowStartupError(const std::wstring& text) {
    MessageBoxW(nullptr, text.c_str(), L"Virtual Screen Recorder", MB_OK | MB_ICONERROR);
}

void AttachParentConsole() {
    if (AttachConsole(ATTACH_PARENT_PROCESS)) {
        FILE* out = nullptr;
        FILE* err = nullptr;
        (void)freopen_s(&out, "CONOUT$", "w", stdout);
        (void)freopen_s(&err, "CONOUT$", "w", stderr);
    }
}

} // namespace

Application& Application::Instance() noexcept {
    static Application app;
    return app;
}

Application::Application() = default;

bool Application::InitRuntime() {
    // Logger (FR-093..FR-095).
    LogConfig config;
    config.directory = LogDir();
    Logger::Instance().Configure(config);

    // Settings.
    SettingsStore store;
    bool usedDefaults = false;
    const Settings settings = store.Load(&usedDefaults);
    if (usedDefaults) {
        log::Warning("App", "SETTINGS_DEFAULTS_USED", L"",
                     L"Settings missing or invalid; defaults applied (FR-092).");
    }
    minimizeToTray_ = settings.minimizeToTray;
    outputDirectory_ = settings.outputDirectory;

    if (!IsWindows11OrLater()) {
        Error e(ErrorCode::InitOsUnsupported,
                L"Virtual Screen Recorder requires Windows 11 (x64).",
                L"OS version check failed at startup", S_OK, false);
        log::Error("App", "STARTUP_OS_UNSUPPORTED", L"", e.technicalMessage);
        ShowStartupError(e.userMessage + L"\n\nError code: " + Utf8ToWide(ToString(e.code)));
        return false;
    }

    clock_ = std::make_shared<SystemClock>();
    coordinator_ = std::make_unique<Coordinator>(clock_);
    return true;
}

void Application::ShutdownRuntime() {
    if (coordinator_ != nullptr) {
        (void)coordinator_->Shutdown();
        coordinator_.reset();
    }
    Logger::Instance().Shutdown();
}

bool Application::EnsureSingleInstance() {
    // FR-005 / AC-009: single instance; a second launch activates the first.
    HANDLE mutex = CreateMutexW(nullptr, TRUE, kSingleInstanceMutexName);
    if (mutex == nullptr) return true; // let it run; nothing worse will happen
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        CloseHandle(mutex);
        // Give the first instance a moment, then find and activate it.
        Sleep(150);
        HWND existing = FindWindowW(kMainWindowClassName, nullptr);
        if (existing != nullptr) {
            const UINT msg = RegisterWindowMessageW(L"VsrActivateExistingInstance");
            PostMessageW(existing, msg, 0, 0);
            if (IsIconic(existing)) ShowWindowAsync(existing, SW_RESTORE);
            SetForegroundWindow(existing);
        }
        return false;
    }
    // Ownership released by the OS when the process exits.
    return true;
}

int Application::RunInteractive() {
    if (!EnsureSingleInstance()) {
        return 0;
    }
    if (!InitRuntime()) {
        return 1;
    }
    log::Info("App", "STARTUP_COMPLETE", L"", L"Interactive session starting.");

    mainWindow_ = std::make_unique<MainWindow>(*coordinator_);
    if (!mainWindow_->Create()) {
        ShowStartupError(L"The recorder window could not be created.");
        log::Error("App", "STARTUP_WINDOW_FAILED", L"", L"MainWindow::Create failed.");
        ShutdownRuntime();
        return 1;
    }
    mainWindow_->ShowAndActivate();

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        // IsDialogMessage gives tab/keyboard navigation to the whole window
        // (NFR-041: keyboard accessibility).
        if (!IsDialogMessageW(mainWindow_->Hwnd(), &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    ShutdownRuntime();
    return static_cast<int>(msg.wParam);
}

int Application::RunDevRecord(int seconds, const std::wstring& presetName,
                              const std::wstring& outputDir) {
    // T040: command-line development entry point recording the virtual
    // display for a fixed duration, without UI or window movement.
    AttachParentConsole();
    if (!InitRuntime()) {
        return 1;
    }

    const encoder::QualityPreset preset = encoder::FromString(WideToUtf8(presetName).c_str());
    const encoder::PresetParams params = encoder::GetPresetParams(preset);
    wprintf(L"[dev-record] preset=%s (%ux%u@%u, %u bps), %d seconds\n",
            Utf8ToWide(encoder::ToString(preset)).c_str(), params.width, params.height,
            params.fps, params.bitrateBps, seconds);

    auto display = coordinator_->EnsureVirtualDisplay();
    if (!display) {
        const Error e = display.TakeError();
        wprintf(L"[dev-record] FAILED: %s (code %hs)\n", e.userMessage.c_str(), ToString(e.code));
        ShutdownRuntime();
        return 2;
    }
    wprintf(L"[dev-record] virtual display active at %ld,%ld %ldx%ld\n", display.Value().monitorRect.left,
            display.Value().monitorRect.top,
            display.Value().monitorRect.right - display.Value().monitorRect.left,
            display.Value().monitorRect.bottom - display.Value().monitorRect.top);

    StartRecordingRequest request;
    request.targetWindow = nullptr; // display-only; no window move in dev mode
    request.outputDirectory = outputDir;
    request.preset = preset;
    auto started = coordinator_->Start(request);
    if (!started) {
        const Error e = started.TakeError();
        wprintf(L"[dev-record] FAILED: %s (code %hs)\n", e.userMessage.c_str(), ToString(e.code));
        ShutdownRuntime();
        return 3;
    }

    // Wait while polling status; NFR-005 expects Recording within 5 s.
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
    bool reachedRecording = false;
    while (std::chrono::steady_clock::now() < deadline) {
        const StatusSnapshot snap = coordinator_->GetStatus();
        if (snap.state == AppState::Recording) reachedRecording = true;
        if (snap.state == AppState::Ready || snap.state == AppState::Faulted) break;
        Sleep(200);
    }
    if (!reachedRecording) {
        const StatusSnapshot snap = coordinator_->GetStatus();
        wprintf(L"[dev-record] FAILED to reach Recording state (state=%s) error=%s\n",
                ToString(snap.state), snap.errorUserMessage.c_str());
        (void)coordinator_->Stop(StopReason::User);
        ShutdownRuntime();
        return 4;
    }

    (void)coordinator_->Stop(StopReason::User);
    (void)coordinator_->Shutdown();
    const StatusSnapshot finalSnap = coordinator_->GetStatus();
    if (finalSnap.hasError) {
        wprintf(L"[dev-record] stopped with error: %s (code %hs)\n",
                finalSnap.errorUserMessage.c_str(), ToString(finalSnap.errorCode));
    }
    wprintf(L"[dev-record] output: %s\n",
            (finalSnap.finalPath.empty() ? finalSnap.partialPath : finalSnap.finalPath).c_str());
    wprintf(L"[dev-record] frames captured=%llu encoded=%llu dropped=%llu\n",
            static_cast<unsigned long long>(finalSnap.capturedFrames),
            static_cast<unsigned long long>(finalSnap.encodedFrames),
            static_cast<unsigned long long>(finalSnap.droppedFrames));
    ShutdownRuntime();
    return finalSnap.hasError ? 5 : 0;
}

} // namespace vsr::app

// ---------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------

int wmain(int argc, wchar_t* argv[]) {
    using namespace vsr::app;

    // COM + WinRT share the apartment; the MTA suits the free-threaded frame
    // pool and Media Foundation worker.
    const HRESULT coInit = CoInitializeEx(nullptr, COINIT_MULTITHREADED);

    if (argc >= 2 && wcscmp(argv[1], L"--dev-record") == 0) {
        int seconds = 10;
        std::wstring preset = L"standard";
        std::wstring outputDir;
        for (int i = 2; i + 1 < argc; i += 2) {
            if (wcscmp(argv[i], L"--seconds") == 0) seconds = _wtoi(argv[i + 1]);
            else if (wcscmp(argv[i], L"--preset") == 0) preset = argv[i + 1];
            else if (wcscmp(argv[i], L"--output") == 0) outputDir = argv[i + 1];
        }
        const int code = Application::Instance().RunDevRecord(seconds, preset, outputDir);
        if (coInit == S_OK || coInit == S_FALSE) CoUninitialize();
        return code;
    }

    const int code = Application::Instance().RunInteractive();
    if (coInit == S_OK || coInit == S_FALSE) CoUninitialize();
    return code;
}

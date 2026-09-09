#include "Application.h"

#include <chrono>
#include <cstdio>
#include <thread>
#include <vector>

#include <objbase.h>

#include <Capture/CaptureInterop.h>
#include <Capture/GraphicsDevice.h>
#include <Encoder/H264Mp4Writer.h>
#include <Encoder/VideoConverter.h>
#include <DisplayControl/SoftwareDisplayDevice.h>
#include <DisplayControl/TopologyService.h>
#include <Encoder/EncoderProbe.h>
#include <Encoder/VideoPreset.h>
#include <Infrastructure/Clock.h>
#include <Infrastructure/CrashReporter.h>
#include <Infrastructure/Log.h>
#include <Infrastructure/Paths.h>
#include <Infrastructure/Settings.h>
#include <Infrastructure/Strings.h>

#include "MainWindow.h"

#include <Application/OutputFile.h>

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

bool QueryOsVersion(OsVersionInfoW& info) {
    // FR-004: OS gate. Windows 11 reports build >= 22000.
    typedef unsigned long(__stdcall * RtlGetVersionFn)(OsVersionInfoW*);
    info.dwOSVersionInfoSize = sizeof(info);
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    if (ntdll == nullptr) return false;
    auto fn = reinterpret_cast<RtlGetVersionFn>(GetProcAddress(ntdll, "RtlGetVersion"));
    return fn != nullptr && fn(&info) == 0;
}

bool IsWindows11OrLater() {
    OsVersionInfoW info = {};
    return QueryOsVersion(info) && info.dwMajorVersion >= 10 && info.dwBuildNumber >= 22000;
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

    // T103: crash dumps are strictly opt-in and stay local.
    CrashReporter::Install(settings.enableCrashDumps);

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

std::wstring Application::OsBuildString() {
    OsVersionInfoW info = {};
    if (!QueryOsVersion(info)) return L"unknown Windows version";
    wchar_t buf[96] = {};
    swprintf_s(buf, L"Windows build %lu ( %lu.%lu )", info.dwBuildNumber, info.dwMajorVersion,
               info.dwMinorVersion);
    return buf;
}

int Application::RunSelfCheck() {
    // Support/diagnostics report. Read-only: creates a D3D device and probes
    // encoders, but never creates the virtual display or writes files.
    AttachParentConsole();

    struct Section {
        const wchar_t* name;
        bool ok;
        std::wstring detail;
    };
    std::vector<Section> sections;
    bool coreOk = true;

    // 1. OS gate (FR-004).
    sections.push_back({L"OS version", IsWindows11OrLater(), OsBuildString()});
    coreOk = coreOk && IsWindows11OrLater();

    // 2. Settings (FR-090..FR-092). A missing file is a normal first run.
    {
        SettingsStore store;
        bool usedDefaults = false;
        const Settings settings = store.Load(&usedDefaults);
        const bool fileExists = std::filesystem::exists(SettingsPath());
        std::wstring detail = L"preset=" + Utf8ToWide(ToString(settings.preset)) +
                              L", crashDumps=" + (settings.enableCrashDumps ? L"on" : L"off") +
                              (usedDefaults ? L" (defaults in use)" : L"");
        const bool ok = !usedDefaults || !fileExists;
        if (!ok) detail += L" - settings file is invalid and was replaced by defaults";
        sections.push_back({L"Settings", ok, detail});
    }

    // 3. D3D11 device (FR-041, T020 verification: adapter + feature level).
    {
        auto device = capture::GraphicsDevice::Create();
        if (device) {
            wchar_t fl[32] = {};
            swprintf_s(fl, L"0x%04X", static_cast<unsigned>(device.Value().FeatureLevel()));
            sections.push_back({L"Direct3D 11", true,
                                L"adapter=" + device.Value().AdapterDescription() +
                                    L", featureLevel=" + fl});
        } else {
            const Error e = device.TakeError();
            sections.push_back({L"Direct3D 11", false, e.userMessage});
            coreOk = false;
        }
    }

    // 4. Windows.Graphics.Capture (ADR-003).
    sections.push_back({L"Graphics capture", capture::IsGraphicsCaptureSupported(),
                        capture::IsGraphicsCaptureSupported() ? L"available" : L"unavailable"});

    // 5. Encoder probe (FR-062/FR-063, T030 verification output).
    {
        auto probe = encoder::ProbeEncoders();
        if (probe) {
            std::wstring detail =
                L"selected=" + probe.Value().selected.name +
                (probe.Value().selected.hardware ? L" (hardware" : L" (software") +
                (probe.Value().selected.d3d11Aware ? L", D3D11-aware)" : L")") +
                L", candidates=" + std::to_wstring(probe.Value().discovered.size());
            sections.push_back({L"H.264 encoders", true, detail});
        } else {
            const Error e = probe.TakeError();
            sections.push_back({L"H.264 encoders", false, e.userMessage});
            coreOk = false;
        }
    }

    // 6. Virtual display driver + monitor (FR-013/FR-014).
    {
        const bool adapterPresent = display::SoftwareDisplayDevice::IsAdapterDevicePresent();
        const bool monitorActive = display::SoftwareDisplayDevice::IsMonitorActive();
        sections.push_back({L"Virtual display driver", adapterPresent || monitorActive,
                            std::wstring(L"adapter device ") +
                                (adapterPresent ? L"present" : L"absent") +
                                L", monitor " + (monitorActive ? L"active" : L"inactive")});
    }

    // 7. Topology summary (sanitized).
    {
        const auto summary = display::TopologyService::Summarize();
        std::wstring detail = std::to_wstring(summary.monitors.size()) + L" active monitor(s)";
        for (const auto& m : summary.monitors) {
            detail += L"; " + (m.friendlyName.empty() ? m.gdiDeviceName : m.friendlyName);
            wchar_t geo[64] = {};
            swprintf_s(geo, L" %ldx%ld@%u", m.w, m.h, m.dpiX);
            detail += geo;
            if (m.isVirtual) detail += L" [virtual]";
        }
        sections.push_back({L"Display topology", true, detail});
    }

    // Report.
    wprintf(L"Virtual Screen Recorder self-check\n");
    wprintf(L"app: %s\n", OsBuildString().c_str());
    for (const auto& sec : sections) {
        wprintf(L"[%s] %s: %s\n", sec.ok ? L" OK " : L"FAIL", sec.name, sec.detail.c_str());
    }
    wprintf(L"core capabilities: %s\n", coreOk ? L"OK" : L"MISSING");
    return coreOk ? 0 : 1;
}

int Application::RunEncodeTest(int seconds, const std::wstring& presetName,
                               const std::wstring& outputDir) {
    // T031 verification: encode synthetic GPU frames through the production
    // conversion + encoder + MP4 sink path without any display dependency.
    AttachParentConsole();

    const encoder::QualityPreset preset = encoder::FromString(WideToUtf8(presetName).c_str());
    const encoder::PresetParams params = encoder::GetPresetParams(preset);

    // 1. Encoder probe (FR-063: fail before any file exists).
    auto probe = encoder::ProbeEncoders();
    if (!probe) {
        const Error e = probe.TakeError();
        wprintf(L"[encode-test] FAILED: %s (code %hs)\n  detail: %hs\n", e.userMessage.c_str(),
                ToString(e.code), WideToUtf8(e.technicalMessage).c_str());
        return 2;
    }

    // 2. GPU device.
    auto deviceResult = capture::GraphicsDevice::Create();
    if (!deviceResult) {
        const Error e = deviceResult.TakeError();
        wprintf(L"[encode-test] FAILED: %s (code %hs)\n  detail: %hs\n", e.userMessage.c_str(),
                ToString(e.code), WideToUtf8(e.technicalMessage).c_str());
        return 3;
    }
    const capture::GraphicsDevice& device = deviceResult.Value();

    // 3. Output naming with the production collision-safe planner.
    SystemClock clock;
    auto plan = PlanOutputFile(outputDir, clock);
    if (!plan) {
        const Error e = plan.TakeError();
        wprintf(L"[encode-test] FAILED: %s (code %hs)\n  detail: %hs\n", e.userMessage.c_str(),
                ToString(e.code), WideToUtf8(e.technicalMessage).c_str());
        return 4;
    }

    // 4. Converter + writer.
    auto converter = encoder::VideoConverter::Create(device, params.width, params.height);
    if (!converter) {
        const Error e = converter.TakeError();
        wprintf(L"[encode-test] FAILED: %s (code %hs)\n  detail: %hs\n", e.userMessage.c_str(),
                ToString(e.code), WideToUtf8(e.technicalMessage).c_str());
        return 5;
    }
    encoder::EncoderCapability cap = probe.Value().selected;
    {
        // BISECT: force the CPU staging path to isolate GPU sample submission.
        wchar_t force[4] = {};
        if (GetEnvironmentVariableW(L"VSR_FORCE_CPU_ENCODER", force, 4) > 0 &&
            force[0] == L'1') {
            cap.d3d11Aware = false;
        }
    }
    auto writer = encoder::H264Mp4Writer::Create(device, cap, params, plan.Value().partialPath);
    if (!writer) {
        const Error e = writer.TakeError();
        wprintf(L"[encode-test] FAILED: %s (code %hs)\n  detail: %hs\n", e.userMessage.c_str(),
                ToString(e.code), WideToUtf8(e.technicalMessage).c_str());
        return 6;
    }

    // 5. Synthetic source: animated gradient + moving block, uploaded to a
    // BGRA texture and pushed through the production pipeline.
    Microsoft::WRL::ComPtr<ID3D11Texture2D> source;
    {
        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width = params.width;
        desc.Height = params.height;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        if (FAILED(device.Device()->CreateTexture2D(&desc, nullptr, source.GetAddressOf()))) {
            wprintf(L"[encode-test] FAILED: BGRA source texture creation\n");
            return 7;
        }
    }

    const std::uint32_t frameCount = params.fps * static_cast<std::uint32_t>(seconds);
    const std::int64_t frameDuration = encoder::FrameDuration100Ns(params.fps);
    std::vector<std::uint8_t> pixels(static_cast<size_t>(params.width) * params.height * 4);

    wprintf(L"[encode-test] encoding %u synthetic frames (%ux%u@%u) via %s\n", frameCount,
            params.width, params.height, params.fps, probe.Value().selected.name.c_str());

    // Real-time pacing: the sink writer's WriteSample provides no backpressure,
    // so feeding faster than the nominal rate grows the MFT queue without
    // bound. Production frames arrive at wall-clock cadence from capture; the
    // test paces identically so soak memory reflects production behavior.
    const auto pacingStart = std::chrono::steady_clock::now();
    for (std::uint32_t f = 0; f < frameCount; ++f) {
        // CPU pattern (test-only path; production frames never touch the CPU).
        const std::uint8_t phase = static_cast<std::uint8_t>(f * 255 / (frameCount ? frameCount : 1));
        for (std::uint32_t y = 0; y < params.height; ++y) {
            auto* row = pixels.data() + static_cast<size_t>(y) * params.width * 4;
            for (std::uint32_t x = 0; x < params.width; ++x) {
                row[x * 4 + 0] = static_cast<std::uint8_t>(x * 255 / params.width);      // B
                row[x * 4 + 1] = static_cast<std::uint8_t>(y * 255 / params.height);     // G
                row[x * 4 + 2] = phase;                                                  // R
                row[x * 4 + 3] = 0xFF;                                                   // A
            }
            // Moving block every 30th frame boundary for temporal motion.
            const std::uint32_t bx = (f * 7) % (params.width - 160);
            const std::uint32_t by = (f * 3) % (params.height - 90);
            if (y >= by && y < by + 90 && bx < params.width) {
                for (std::uint32_t x = bx; x < std::min(bx + 160, params.width); ++x) {
                    row[x * 4 + 0] = 0xFF;
                    row[x * 4 + 1] = 0x40;
                    row[x * 4 + 2] = 0x20;
                }
            }
        }

        D3D11_SUBRESOURCE_DATA init = {};
        init.pSysMem = pixels.data();
        init.SysMemPitch = params.width * 4;
        device.Context()->UpdateSubresource(source.Get(), 0, nullptr, pixels.data(),
                                            init.SysMemPitch, 0);

        auto converted = converter.Value()->Convert(source.Get());
        if (!converted) {
            const Error e = converted.TakeError();
            wprintf(L"[encode-test] FAILED at frame %u: %s\n", f, e.technicalMessage.c_str());
            return 8;
        }
        auto submitted = writer.Value()->SubmitFrame(converted.Value(),
                                                     static_cast<std::int64_t>(f) * frameDuration,
                                                     frameDuration);
        if (!submitted) {
            const Error e = submitted.TakeError();
            wprintf(L"[encode-test] FAILED at frame %u: %s\n", f, e.technicalMessage.c_str());
            return 9;
        }

        // Pace to the nominal frame cadence (see pacingStart note above).
        const auto target = pacingStart +
                            std::chrono::microseconds(
                                static_cast<std::int64_t>(f + 1) * 1'000'000 / params.fps);
        if (std::chrono::steady_clock::now() < target) {
            std::this_thread::sleep_until(target);
        }
    }

    if (!writer.Value()->Finalize().IsOk()) {
        wprintf(L"[encode-test] FAILED: finalization\n");
        return 10;
    }
    std::error_code ec;
    std::filesystem::rename(plan.Value().partialPath, plan.Value().finalPath, ec);
    if (ec) {
        wprintf(L"[encode-test] FAILED: rename (%hs)\n", ec.message().c_str());
        return 11;
    }

    wprintf(L"[encode-test] wrote %u frames to %s\n", frameCount,
            plan.Value().finalPath.wstring().c_str());
    wprintf(L"[encode-test] encoder=%s hardware=%d writtenFrames=%llu\n",
            probe.Value().selected.name.c_str(), probe.Value().selected.hardware ? 1 : 0,
            static_cast<unsigned long long>(writer.Value()->WrittenFrames()));
    return 0;
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

    // NFR-023: resolve DLL dependencies only from known-safe locations; the
    // product ships no side-by-side DLLs of its own.
    SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_APPLICATION_DIR |
                             LOAD_LIBRARY_SEARCH_SYSTEM32);

    // COM + WinRT share the apartment; the MTA suits the free-threaded frame
    // pool and Media Foundation worker.
    const HRESULT coInit = CoInitializeEx(nullptr, COINIT_MULTITHREADED);

    if (argc >= 2 && wcscmp(argv[1], L"--self-check") == 0) {
        const int code = Application::Instance().RunSelfCheck();
        if (coInit == S_OK || coInit == S_FALSE) CoUninitialize();
        return code;
    }

    if (argc >= 2 && wcscmp(argv[1], L"--encode-test") == 0) {
        int seconds = 10;
        std::wstring preset = L"standard";
        std::wstring outputDir;
        for (int i = 2; i + 1 < argc; i += 2) {
            if (wcscmp(argv[i], L"--seconds") == 0) seconds = _wtoi(argv[i + 1]);
            else if (wcscmp(argv[i], L"--preset") == 0) preset = argv[i + 1];
            else if (wcscmp(argv[i], L"--output") == 0) outputDir = argv[i + 1];
        }
        const int code = Application::Instance().RunEncodeTest(seconds, preset, outputDir);
        if (coInit == S_OK || coInit == S_FALSE) CoUninitialize();
        return code;
    }

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

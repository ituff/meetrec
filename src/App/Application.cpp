#include "Application.h"

#include <objidl.h>
#include <gdiplus.h>

#include <WindowManagement/WindowPlacement.h>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <algorithm>

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
#include <Transcode/TranscodeService.h>
#include <Update/UpdateApplier.h>
#include <Update/UpdateChecker.h>

#include "Localization.h"
#include "MainWindow.h"
#include "Version.h"

#include <Application/OutputFile.h>

#pragma comment(lib, "gdiplus.lib")

#if __has_include("VersionGenerated.h")
#include "VersionGenerated.h"
#endif

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
    // FR-004: OS gate. Windows 10 2004 (19041) is the oldest supported build; Windows 11 is build >= 22000.
    typedef unsigned long(__stdcall * RtlGetVersionFn)(OsVersionInfoW*);
    info.dwOSVersionInfoSize = sizeof(info);
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    if (ntdll == nullptr) return false;
    auto fn = reinterpret_cast<RtlGetVersionFn>(GetProcAddress(ntdll, "RtlGetVersion"));
    return fn != nullptr && fn(&info) == 0;
}

bool IsSupportedWindows() {
    OsVersionInfoW info = {};
    return QueryOsVersion(info) && info.dwMajorVersion >= 10 && info.dwBuildNumber >= 19041;
}

void ShowStartupError(const std::wstring& text) {
    MessageBoxW(nullptr, text.c_str(), i18n::Text(i18n::Str::AppName),
                MB_OK | MB_ICONERROR);
}

void AttachParentConsole() {
    // Never clobber an existing stdout redirection (pipe or file): terminals
    // and automation capture the CLI output through it. Only attach to the
    // parent console when the GUI subsystem left stdout unusable (a plain
    // double-click/console launch).
    const HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    if (out != nullptr && out != INVALID_HANDLE_VALUE && GetFileType(out) != FILE_TYPE_CHAR) {
        return;
    }
    if (AttachConsole(ATTACH_PARENT_PROCESS)) {
        FILE* outFile = nullptr;
        FILE* errFile = nullptr;
        (void)freopen_s(&outFile, "CONOUT$", "w", stdout);
        (void)freopen_s(&errFile, "CONOUT$", "w", stderr);
    }
}

} // namespace

Application& Application::Instance() noexcept {
    static Application app;
    return app;
}

Application::Application() = default;

void Application::SetOpenCompressorOnStart(bool open,
                                           const std::vector<std::wstring>& files) {
    openCompressorOnStart_ = open;
    openCompressorFiles_ = files;
}

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
    // The UI language follows the Windows display language by default; the
    // settings window can pin zh-CN or en-US explicitly.
    i18n::SetLanguage(i18n::LanguageFromSetting(settings.uiLanguage));

    // T103: crash dumps are strictly opt-in and stay local.
    CrashReporter::Install(settings.enableCrashDumps);

    if (!IsSupportedWindows()) {
        Error e(ErrorCode::InitOsUnsupported,
                L"MeetRec requires Windows 10 version 2004 (x64) or later.",
                L"OS version check failed at startup", S_OK, false);
        log::Error("App", "STARTUP_OS_UNSUPPORTED", L"", e.technicalMessage);
        ShowStartupError(std::wstring(i18n::ErrorTitle(e.code)) + L"\n" +
                         i18n::ErrorMessage(e.code) + L"\n\nError code: " +
                         Utf8ToWide(ToString(e.code)));
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
    sections.push_back({L"OS version", IsSupportedWindows(), OsBuildString()});
    coreOk = coreOk && IsSupportedWindows();

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
    wprintf(L"MeetRec self-check\n");
    wprintf(L"app: %s\n", OsBuildString().c_str());
    for (const auto& sec : sections) {
        wprintf(L"[%s] %s: %s\n", sec.ok ? L" OK " : L"FAIL", sec.name, sec.detail.c_str());
    }
    wprintf(L"core capabilities: %s\n", coreOk ? L"OK" : L"MISSING");
    return coreOk ? 0 : 1;
}

int Application::RunEncodeTest(int seconds, const std::wstring& presetName,
                               const std::wstring& outputDir,
                               encoder::EncoderPreference encoderPreference,
                               encoder::WriterBackend backend) {
    // T031 verification: encode synthetic GPU frames through the production
    // conversion + encoder + MP4 sink path without any display dependency.
    AttachParentConsole();
    (void)setvbuf(stdout, nullptr, _IONBF, 0); // survive hard kills in diagnostics

    const encoder::QualityPreset preset = encoder::FromString(WideToUtf8(presetName).c_str());
    const encoder::PresetParams params = encoder::GetPresetParams(preset);
    const encoder::WriterBackend resolvedBackend = encoder::ResolveBackend(backend);

    // 1. Encoder probe (FR-063: fail before any file exists). The ffmpeg
    // backend does not use OS encoder MFTs; its probe runs in the factory.
    encoder::EncoderCapability cap;
    if (resolvedBackend != encoder::WriterBackend::Ffmpeg) {
        auto probe = encoder::ProbeEncoders(encoderPreference);
        if (!probe) {
            const Error e = probe.TakeError();
            wprintf(L"[encode-test] FAILED: %s (code %hs)\n  detail: %hs\n",
                    e.userMessage.c_str(), ToString(e.code),
                    WideToUtf8(e.technicalMessage).c_str());
            return 2;
        }
        cap = probe.Value().selected;
        {
            // BISECT: force the CPU staging path to isolate GPU sample submission.
            wchar_t force[4] = {};
            if (GetEnvironmentVariableW(L"VSR_FORCE_CPU_ENCODER", force, 4) > 0 &&
                force[0] == L'1') {
                cap.d3d11Aware = false;
            }
        }
    } else {
        cap.name = L"ffmpeg (libx264)";
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
    auto writer = encoder::CreateWriter(backend, device, cap, params,
                                        plan.Value().partialPath, nullptr);
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
            wprintf(L"[encode-test] FAILED: BGRA source texture creation\n");
            return 7;
        }
    }

    const std::uint32_t frameCount = params.fps * static_cast<std::uint32_t>(seconds);
    const std::int64_t frameDuration = encoder::FrameDuration100Ns(params.fps);
    std::vector<std::uint8_t> pixels(static_cast<size_t>(params.width) * params.height * 4);

    // Base gradient is rendered ONCE: regenerating 8 MB per frame on the CPU
    // could not sustain the 60 fps presets, the paced feed fell behind wall
    // time, and the CFR container honestly recorded the overrun as excess
    // duration. Per frame only the moving block is repainted (temporal
    // motion for the validator), then the texture is uploaded.
    for (std::uint32_t y = 0; y < params.height; ++y) {
        auto* row = pixels.data() + static_cast<size_t>(y) * params.width * 4;
        for (std::uint32_t x = 0; x < params.width; ++x) {
            row[x * 4 + 0] = static_cast<std::uint8_t>(x * 255 / params.width);      // B
            row[x * 4 + 1] = static_cast<std::uint8_t>(y * 255 / params.height);     // G
            row[x * 4 + 2] = 0x40;                                                   // R
            row[x * 4 + 3] = 0xFF;                                                   // A
        }
    }

    wprintf(L"[encode-test] encoding %u synthetic frames (%ux%u@%u) via %s (backend %hs)\n",
            frameCount, params.width, params.height, params.fps, cap.name.c_str(),
            encoder::ToString(resolvedBackend));

    // Real-time pacing: the sink writer's WriteSample provides no backpressure,
    // so feeding faster than the nominal rate grows the MFT queue without
    // bound. Production frames arrive at wall-clock cadence from capture; the
    // test paces identically so soak memory reflects production behavior.
    const auto pacingStart = std::chrono::steady_clock::now();
    for (std::uint32_t f = 0; f < frameCount; ++f) {
        // Moving block repaint (test-only temporal motion).
        const std::uint32_t bx = (f * 7) % (params.width - 160);
        const std::uint32_t by = (f * 3) % (params.height - 90);
        for (std::uint32_t y = by; y < std::min(by + 90u, params.height); ++y) {
            auto* row = pixels.data() + static_cast<size_t>(y) * params.width * 4;
            for (std::uint32_t x = bx; x < std::min(bx + 160u, params.width); ++x) {
                row[x * 4 + 0] = 0xFF;
                row[x * 4 + 1] = 0x40;
                row[x * 4 + 2] = 0x20;
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
            wprintf(L"[encode-test] FAILED at frame %u: %s\n", f, e.technicalMessage.c_str());
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
        wprintf(L"[encode-test] FAILED: finalization\n");
        return 10;
    }
    std::error_code ec;
    std::filesystem::rename(plan.Value().partialPath, plan.Value().finalPath, ec);
    if (ec) {
        wprintf(L"[encode-test] FAILED: rename (%hs)\n", ec.message().c_str());
        return 11;
    }

    wprintf(L"[encode-test] wrote %u frames to %s\n", frameCount,
            plan.Value().finalPath.wstring().c_str());
    wprintf(L"[encode-test] encoder=%s hardware=%d writtenFrames=%llu\n", cap.name.c_str(),
            cap.hardware ? 1 : 0,
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

    Gdiplus::GdiplusStartupInput gdiplusInput;
    if (Gdiplus::GdiplusStartup(&gdiplusToken_, &gdiplusInput, nullptr) != Gdiplus::Ok) {
        ShowStartupError(i18n::ErrorMessage(ErrorCode::InitD3dFailed));
        ShutdownRuntime();
        return 1;
    }

    mainWindow_ = std::make_unique<MainWindow>(*coordinator_);
    if (!mainWindow_->Create()) {
        ShowStartupError(i18n::Text(i18n::Str::ErrInternalBody));
        log::Error("App", "STARTUP_WINDOW_FAILED", L"", L"MainWindow::Create failed.");
        mainWindow_.reset();
        ShutdownRuntime();
        Gdiplus::GdiplusShutdown(gdiplusToken_);
        gdiplusToken_ = 0;
        return 1;
    }
    mainWindow_->ShowAndActivate();
    if (openCompressorOnStart_) {
        mainWindow_->ShowCompressWindow();
        if (!openCompressorFiles_.empty()) {
            mainWindow_->QueueCompressFiles(openCompressorFiles_);
        }
    }
    if (openUpdateWindowOnStart_) mainWindow_->SetOpenUpdateWindowOnAvailable(true);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        // The custom-drawn windows implement their own Tab/Enter/arrow
        // navigation (NFR-041); native edit controls subclass their keys.
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    // GDI+ fonts and bitmaps are owned by the windows: destroy them before
    // shutting the graphics subsystem down.
    mainWindow_.reset();
    ShutdownRuntime();
    if (gdiplusToken_ != 0) {
        Gdiplus::GdiplusShutdown(gdiplusToken_);
        gdiplusToken_ = 0;
    }
    return static_cast<int>(msg.wParam);
}

int Application::RunDevRecord(int seconds, const std::wstring& presetName,
                              const std::wstring& outputDir, HWND targetWindow,
                              encoder::EncoderPreference encoderPreference,
                              bool captureAudio, encoder::WriterBackend backend,
                              const std::wstring& audioOutputDeviceId,
                              const std::wstring& audioInputDeviceId,
                              const std::wstring& competeFile,
                              std::uint64_t* droppedFramesOut) {
    // T040: command-line development entry point recording the virtual
    // display for a fixed duration, without UI or window movement.
    AttachParentConsole();
    if (!InitRuntime()) {
        return 1;
    }

    const encoder::QualityPreset preset = encoder::FromString(WideToUtf8(presetName).c_str());
    const encoder::PresetParams params = encoder::GetPresetParams(preset);
    wprintf(L"[dev-record] preset=%s (%ux%u@%u, crf %d, ceiling %u bps, gop %us, "
            L"audio %u kbps/%u ch), %d seconds, audio=%s\n",
            Utf8ToWide(encoder::ToString(preset)).c_str(), params.width, params.height,
            params.fps, params.crf, params.bitrateBps, params.gopSeconds,
            params.audioBitrateBps / 1000, params.audioChannels, seconds,
            captureAudio ? L"on" : L"off");

    // ADR-007 "recording wins": start a real transcode job in this process and
    // suspend it once recording begins, so the competition path and the
    // dropped-frame counter are exercised together.
    std::unique_ptr<transcode::TranscodeService> competitor;
    if (!competeFile.empty()) {
        competitor = std::make_unique<transcode::TranscodeService>();
        if (competitor->Add(std::filesystem::path(competeFile), {}) == 0) {
            wprintf(L"[dev-compete] FAILED: could not queue %s\n", competeFile.c_str());
            ShutdownRuntime();
            return 7;
        }
        competitor->Start(transcode::Quality::Standard, transcode::Resolution::Keep);
        bool running = false;
        for (int i = 0; i < 100 && !running; ++i) {
            const auto snapshot = competitor->Snapshot();
            running = !snapshot.jobs.empty() &&
                      snapshot.jobs.front().state == transcode::JobState::Running;
            if (!running) Sleep(100);
        }
        if (!running) {
            wprintf(L"[dev-compete] FAILED: the transcode job never started\n");
            ShutdownRuntime();
            return 7;
        }
        Sleep(1200);
        const auto snapshot = competitor->Snapshot();
        const double progress =
            snapshot.jobs.empty() ? -1.0 : snapshot.jobs.front().progress;
        wprintf(L"[dev-compete] transcode running before recording (progress %.0f%%)\n",
                progress * 100.0);
        fflush(stdout);
    }

    // Window targets record driverless; only the display-only dev path needs
    // the virtual display to be up.
    if (targetWindow == nullptr) {
        auto display = coordinator_->EnsureVirtualDisplay();
        if (!display) {
            const Error e = display.TakeError();
            wprintf(L"[dev-record] FAILED: %s (code %hs)\n", e.userMessage.c_str(),
                    ToString(e.code));
            ShutdownRuntime();
            return 2;
        }
        wprintf(L"[dev-record] virtual display active at %ld,%ld %ldx%ld\n",
                display.Value().monitorRect.left,
                display.Value().monitorRect.top,
                display.Value().monitorRect.right - display.Value().monitorRect.left,
                display.Value().monitorRect.bottom - display.Value().monitorRect.top);
    }

    StartRecordingRequest request;
    request.targetWindow = targetWindow; // null => display-only dev mode
    request.outputDirectory = outputDir;
    request.preset = preset;
    request.encoderPreference = encoderPreference;
    // Audio stays opt-in (--audio / GUI checkbox): the MS AAC encoder
    // finalize hung on some Insider builds, so automated runs are video-only
    // by default and the failure mode degrades to video-only when present.
    request.captureAudio = captureAudio;
    // Dev runs mix the mic in with the loopback (VSR_MIC_DISABLE=1 forces a
    // system-only run).
    request.captureMicrophone = captureAudio;
    request.audioOutputDeviceId = audioOutputDeviceId;
    request.audioInputDeviceId = audioInputDeviceId;
    request.backend = backend;
    auto started = coordinator_->Start(request);
    if (!started) {
        const Error e = started.TakeError();
        wprintf(L"[dev-record] FAILED: %s (code %hs)\n", e.userMessage.c_str(), ToString(e.code));
        ShutdownRuntime();
        return 3;
    }
    if (competitor) {
        // Worst-case UI timing: the compressor's 200 ms poll runs after the
        // recording has already started.
        Sleep(200);
        competitor->Suspend();
        const auto suspended = competitor->Snapshot();
        wprintf(L"[dev-compete] transcode suspended after 200 ms of recording; job=%hs\n",
                suspended.jobs.empty() ? "none"
                                       : transcode::ToString(suspended.jobs.front().state));
        fflush(stdout);
    }

    // Wait for the Recording state; the requested duration counts from the
    // FIRST Recording sample, so pipeline start-up (encoder probe, writer
    // activation, capture bring-up) does not eat into the recorded content.
    // NFR-005 expects Recording within 5 s; the wait itself is bounded for
    // wedged starts.
    const auto startWait = std::chrono::steady_clock::now();
    bool reachedRecording = false;
    auto recordStart = startWait;
    while (std::chrono::steady_clock::now() <
           startWait + std::chrono::seconds(15 + seconds)) {
        const StatusSnapshot snap = coordinator_->GetStatus();
        if (snap.state == AppState::Recording) {
            reachedRecording = true;
            recordStart = std::chrono::steady_clock::now();
            break;
        }
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
    wprintf(L"[dev-record] recording active; holding for %d s\n", seconds);
    fflush(stdout);

    while (std::chrono::steady_clock::now() < recordStart + std::chrono::seconds(seconds)) {
        const StatusSnapshot snap = coordinator_->GetStatus();
        if (snap.state == AppState::Ready || snap.state == AppState::Faulted) break;
        Sleep(200);
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
    if (competitor) {
        const auto after = competitor->Snapshot();
        const bool requeued = !after.jobs.empty() &&
                              after.jobs.front().state == transcode::JobState::Queued;
        wprintf(L"[dev-compete] transcode after recording: job=%hs (requeued=%s)\n",
                after.jobs.empty() ? "none"
                                   : transcode::ToString(after.jobs.front().state),
                requeued ? L"yes" : L"no");
        if (!requeued) {
            wprintf(L"[dev-compete] FAILED: the transcode job was not requeued\n");
            ShutdownRuntime();
            return 7;
        }
    }
    if (droppedFramesOut != nullptr) *droppedFramesOut = finalSnap.droppedFrames;
    ShutdownRuntime();
    return finalSnap.hasError ? 5 : 0;
}

} // namespace vsr::app

namespace vsr::app {

// --- Driverless dev test window (T-E2E-WINDOW) ------------------------------
//
// A self-contained animated window owned by a CHILD process so the recorder
// can capture a real, cross-process window without violating the
// "never record our own process" guard. The window is created parked
// off-screen (invisible on every monitor, still composed) and invalidates at
// the preset frame rate, so WGC delivers ~fps frames for media validation.

namespace {

constexpr wchar_t kDevTestWindowClass[] = L"VsrDevTestWindow";

LRESULT CALLBACK DevTestWindowProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE:
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        return 0;
    case WM_TIMER: {
        const auto n = reinterpret_cast<std::uint64_t*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (n != nullptr) ++*n;
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps = {};
        HDC dc = BeginPaint(hwnd, &ps);
        RECT rc = {};
        GetClientRect(hwnd, &rc);
        const auto* counter =
            reinterpret_cast<const std::uint64_t*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        const std::uint64_t n = counter != nullptr ? *counter : 0;
        const COLORREF color = RGB((n * 37) & 0xFF, (n * 89) & 0xFF, (n * 151) & 0xFF);
        HBRUSH brush = CreateSolidBrush(color);
        FillRect(dc, &rc, brush);
        DeleteObject(brush);
        wchar_t text[32] = {};
        swprintf(text, 32, L"frame %llu", static_cast<unsigned long long>(n));
        SetTextColor(dc, RGB(255, 255, 255));
        SetBkMode(dc, TRANSPARENT);
        DrawTextW(dc, text, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcW(hwnd, msg, wp, lp);
    }
}

int RunDevTestWindow(unsigned fps, int seconds, const std::wstring& title) {
    const HINSTANCE instance = GetModuleHandleW(nullptr);
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = DevTestWindowProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kDevTestWindowClass;
    if (!RegisterClassExW(&wc)) return 30;

    // In-place capture: the test window stays visible on-screen (bottom-right
    // of the work area) so the recorded content is real.
    RECT work = {};
    const HMONITOR primary = MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO info = {};
    info.cbSize = sizeof(info);
    if (GetMonitorInfoW(primary, &info)) work = info.rcWork;
    const RECT park = window::ComputeOnScreenCornerRect(RECT{0, 0, 960, 540}, work);

    const HWND hwnd = CreateWindowExW(
        WS_EX_NOACTIVATE, kDevTestWindowClass, title.c_str(), WS_OVERLAPPEDWINDOW,
        park.left, park.top, park.right - park.left, park.bottom - park.top,
        nullptr, nullptr, instance, nullptr);
    if (hwnd == nullptr) return 31;
    // Frame counter storage rides in GWLP_USERDATA (set on WM_CREATE).
    static std::uint64_t frameCounter = 0;
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&frameCounter));
    ShowWindow(hwnd, SW_SHOWNOACTIVATE);

    const UINT interval = fps > 0 ? static_cast<UINT>(1000 / fps) : 33u;
    SetTimer(hwnd, 1, interval, nullptr);

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
    MSG msg = {};
    while (std::chrono::steady_clock::now() < deadline) {
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                KillTimer(hwnd, 1);
                return 0;
            }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        Sleep(5);
    }
    KillTimer(hwnd, 1);
    return 0;
}

} // namespace

int Application::RunDevRecordWindow(int seconds, const std::wstring& presetName,
                                    const std::wstring& outputDir,
                                    encoder::EncoderPreference encoderPreference,
                                    bool captureAudio, encoder::WriterBackend backend,
                                    const std::wstring& audioOutputDeviceId,
                                    const std::wstring& audioInputDeviceId,
                                    const std::wstring& competeFile,
                                    std::uint64_t* droppedFramesOut) {
    wprintf(L"[dev-record-window] starting\n");
    fflush(stdout);
    AttachParentConsole();
    wprintf(L"[dev-record-window] console attached\n");
    fflush(stdout);
    const encoder::QualityPreset preset = encoder::FromString(WideToUtf8(presetName).c_str());
    const encoder::PresetParams params = encoder::GetPresetParams(preset);

    wchar_t exePath[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    wprintf(L"[dev-record-window] exe resolved\n");
    fflush(stdout);
    const std::wstring title =
        std::wstring(kDevTestWindowClass) + L"-" + std::to_wstring(GetCurrentProcessId());
    std::wstring cmd = std::wstring(L"\"") + exePath + L"\" --dev-test-window --fps " +
                       std::to_wstring(params.fps) + L" --seconds " +
                       std::to_wstring(seconds + 10) + L" --title \"" + title + L"\"";

    STARTUPINFOW si = {};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi = {};
    if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si,
                        &pi)) {
        wprintf(L"[dev-record-window] FAILED: could not spawn the test window process "
                L"(error %lu)\n",
                GetLastError());
        return 20;
    }
    wprintf(L"[dev-record-window] child spawned (pid %lu)\n", pi.dwProcessId);
    fflush(stdout);
    CloseHandle(pi.hThread);
    const HANDLE child = pi.hProcess;

    HWND hwnd = nullptr;
    for (int i = 0; i < 100 && hwnd == nullptr; ++i) {
        hwnd = FindWindowW(nullptr, title.c_str());
        if (hwnd == nullptr) Sleep(100);
    }
    if (hwnd == nullptr) {
        wprintf(L"[dev-record-window] FAILED: the test window never appeared\n");
        TerminateProcess(child, 1);
        CloseHandle(child);
        return 21;
    }

    const int code = RunDevRecord(seconds, presetName, outputDir, hwnd, encoderPreference,
                                  captureAudio, backend, audioOutputDeviceId,
                                  audioInputDeviceId, competeFile, droppedFramesOut);

    PostMessageW(hwnd, WM_CLOSE, 0, 0);
    WaitForSingleObject(child, 5000);
    CloseHandle(child);
    return code;
}

int Application::RunDevCompete(const std::wstring& competeFile, int seconds,
                               const std::wstring& presetName,
                               const std::wstring& outputDir) {
    AttachParentConsole();
    std::uint64_t baselineDrops = 0;
    std::uint64_t competingDrops = 0;

    wprintf(L"[dev-compete] phase 1/2: baseline recording (no transcode)\n");
    fflush(stdout);
    const int baseline = RunDevRecordWindow(seconds, presetName, outputDir,
                                            encoder::EncoderPreference::Auto, false,
                                            encoder::WriterBackend::Auto, {}, {}, {},
                                            &baselineDrops);
    if (baseline != 0) {
        wprintf(L"[dev-compete] FAILED: baseline recording returned %d\n", baseline);
        return baseline;
    }

    wprintf(L"[dev-compete] phase 2/2: recording with a transcode job\n");
    fflush(stdout);
    const int competing = RunDevRecordWindow(seconds, presetName, outputDir,
                                             encoder::EncoderPreference::Auto, false,
                                             encoder::WriterBackend::Auto, {}, {},
                                             competeFile, &competingDrops);
    if (competing != 0) {
        wprintf(L"[dev-compete] FAILED: competition recording returned %d\n", competing);
        return competing;
    }

    const std::uint64_t tolerance = std::max<std::uint64_t>(3, baselineDrops / 4);
    wprintf(L"[dev-compete] drops baseline=%llu competing=%llu tolerance=%llu\n",
            static_cast<unsigned long long>(baselineDrops),
            static_cast<unsigned long long>(competingDrops),
            static_cast<unsigned long long>(tolerance));
    if (competingDrops > baselineDrops + tolerance) {
        wprintf(L"[dev-compete] FAILED: the transcode added dropped frames\n");
        return 6;
    }
    if (competingDrops == 0) {
        wprintf(L"[dev-compete] PASS: zero dropped frames with transcode suspended\n");
    } else {
        wprintf(L"[dev-compete] PASS: no extra drops vs baseline; the baseline loss is "
                L"pre-existing capture/encoder backpressure\n");
    }
    return 0;
}

int Application::RunCompressTest(const std::wstring& input, const std::wstring& presetName,
                                 const std::wstring& outputDir) {
    AttachParentConsole();
    (void)setvbuf(stdout, nullptr, _IONBF, 0);

    const transcode::Quality quality =
        transcode::QualityFromString(WideToUtf8(presetName).c_str());
    transcode::TranscodeService service;
    if (service.Add(std::filesystem::path(input),
                    outputDir.empty() ? std::filesystem::path()
                                      : std::filesystem::path(outputDir)) == 0) {
        wprintf(L"[compress] FAILED: could not queue %s\n", input.c_str());
        return 2;
    }
    service.Start(quality, transcode::Resolution::Keep);

    const ULONGLONG deadline = GetTickCount64() + 6ull * 60ull * 60ull * 1000ull;
    double lastPercent = -1.0;
    for (;;) {
        Sleep(200);
        const transcode::ServiceSnapshot snapshot = service.Snapshot();
        if (snapshot.jobs.empty()) return 2;
        const transcode::JobSnapshot& job = snapshot.jobs.front();
        if (job.state == transcode::JobState::Running && job.progress >= 0.0 &&
            std::abs(job.progress - lastPercent) >= 0.05) {
            lastPercent = job.progress;
            wprintf(L"[compress] %d%%\n", static_cast<int>(job.progress * 100.0));
        }
        if (job.state == transcode::JobState::Completed) {
            wprintf(L"[compress] OK: %s (%llu bytes)\n", job.outputPath.c_str(),
                    static_cast<unsigned long long>(job.outputBytes));
            return 0;
        }
        if (job.state == transcode::JobState::Failed) {
            wprintf(L"[compress] FAILED: %s\n", job.error.c_str());
            return 1;
        }
        if (GetTickCount64() > deadline) {
            wprintf(L"[compress] FAILED: timed out\n");
            return 3;
        }
    }
}

int Application::RunUpdateCheck() {
    AttachParentConsole();
    auto check = update::CheckForUpdate(VSR_APP_VERSION_UTF8);
    if (!check) {
        wprintf(L"[update] FAILED: %s\n", check.GetError().userMessage.c_str());
        return 1;
    }
    const auto& release = check.Value().release;
    wprintf(L"[update] current=%s latest=%s available=%s\n", VSR_APP_VERSION_W,
            Utf8ToWide(release.version.ToString()).c_str(),
            check.Value().available ? L"yes" : L"no");
    if (!release.zipUrl.empty()) {
        wprintf(L"[update] package=%s\n", release.zipUrl.c_str());
    }
    return 0;
}

int Application::RunUpdateSelfTest() {
    AttachParentConsole();
    auto check = update::CheckForUpdate(VSR_APP_VERSION_UTF8);
    if (!check) {
        wprintf(L"[update-self-test] FAILED: %s\n", check.GetError().userMessage.c_str());
        return 1;
    }
    if (!check.Value().available) {
        wprintf(L"[update-self-test] no update available (current %s)\n",
                VSR_APP_VERSION_W);
        return 0;
    }
    const auto& release = check.Value().release;
    std::atomic<bool> cancel{false};
    const std::filesystem::path destination =
        update::UpdatesDirectory() / Utf8ToWide(release.version.ToString());
    auto downloaded = update::DownloadVerified(
        release, destination, cancel, [](std::uint64_t done, std::uint64_t total) {
            if (total > 0 && done == total) {
                wprintf(L"[update-self-test] downloaded %llu bytes\n",
                        static_cast<unsigned long long>(done));
            }
        });
    if (!downloaded) {
        wprintf(L"[update-self-test] FAILED: %s\n",
                downloaded.GetError().userMessage.c_str());
        return 1;
    }

    wchar_t exePath[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    const std::filesystem::path exe = exePath;
    auto script = update::WriteApplyScript(downloaded.Value(), exe.parent_path(), exe,
                                           GetCurrentProcessId());
    if (!script) {
        wprintf(L"[update-self-test] FAILED: %s\n", script.GetError().userMessage.c_str());
        return 1;
    }
    auto launched = update::LaunchApplyScript(script.Value());
    if (!launched) {
        wprintf(L"[update-self-test] FAILED: %s\n",
                launched.GetError().userMessage.c_str());
        return 1;
    }
    wprintf(L"[update-self-test] staged %s; exiting for the updater\n",
            downloaded.Value().c_str());
    return 0;
}

} // namespace vsr::app

// ---------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------

namespace {

struct DevRecordOptions {
    int seconds = 10;
    std::wstring preset = L"standard";
    std::wstring outputDir;
    vsr::encoder::EncoderPreference encoder = vsr::encoder::EncoderPreference::Auto;
    vsr::encoder::WriterBackend backend = vsr::encoder::WriterBackend::Auto;
    bool audio = false;
    std::wstring audioOutId; // empty = Windows default endpoint
    std::wstring audioInId;
};

// Shared parser for the recording CLI modes. `--audio` is a bare boolean
// flag; the rest are name/value pairs and tolerate any order.
DevRecordOptions ParseDevRecordOptions(int argc, wchar_t* argv[]) {
    DevRecordOptions o;
    for (int i = 2; i < argc; ++i) {
        if (wcscmp(argv[i], L"--audio") == 0) {
            o.audio = true;
            continue;
        }
        if (i + 1 >= argc) break;
        if (wcscmp(argv[i], L"--seconds") == 0) {
            o.seconds = _wtoi(argv[i + 1]);
            ++i;
        } else if (wcscmp(argv[i], L"--preset") == 0) {
            o.preset = argv[i + 1];
            ++i;
        } else if (wcscmp(argv[i], L"--output") == 0) {
            o.outputDir = argv[i + 1];
            ++i;
        } else if (wcscmp(argv[i], L"--audio-out") == 0) {
            o.audioOutId = argv[i + 1];
            ++i;
        } else if (wcscmp(argv[i], L"--audio-in") == 0) {
            o.audioInId = argv[i + 1];
            ++i;
        } else if (wcscmp(argv[i], L"--encoder") == 0) {
            o.encoder = vsr::encoder::EncoderPreferenceFromString(vsr::WideToUtf8(argv[i + 1]));
            ++i;
        } else if (wcscmp(argv[i], L"--backend") == 0) {
            o.backend = vsr::encoder::WriterBackendFromString(vsr::WideToUtf8(argv[i + 1]));
            ++i;
        }
    }
    return o;
}

} // namespace

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
        const DevRecordOptions o = ParseDevRecordOptions(argc, argv);
        const int code = Application::Instance().RunEncodeTest(o.seconds, o.preset, o.outputDir,
                                                               o.encoder, o.backend);
        if (coInit == S_OK || coInit == S_FALSE) CoUninitialize();
        return code;
    }

    if (argc >= 2 && wcscmp(argv[1], L"--update-check") == 0) {
        const int code = Application::Instance().RunUpdateCheck();
        if (coInit == S_OK || coInit == S_FALSE) CoUninitialize();
        return code;
    }

    if (argc >= 2 && wcscmp(argv[1], L"--update-self-test") == 0) {
        const int code = Application::Instance().RunUpdateSelfTest();
        if (coInit == S_OK || coInit == S_FALSE) CoUninitialize();
        return code;
    }

    if (argc >= 3 && wcscmp(argv[1], L"--compress") == 0) {
        std::wstring preset = L"standard";
        std::wstring outputDir;
        for (int i = 3; i + 1 < argc; i += 2) {
            if (wcscmp(argv[i], L"--preset") == 0) preset = argv[i + 1];
            else if (wcscmp(argv[i], L"--output") == 0) outputDir = argv[i + 1];
        }
        const int code = Application::Instance().RunCompressTest(argv[2], preset, outputDir);
        if (coInit == S_OK || coInit == S_FALSE) CoUninitialize();
        return code;
    }

    if (argc >= 2 && wcscmp(argv[1], L"--version") == 0) {
        wprintf(L"MeetRec %s\n", VSR_APP_VERSION_W);
#ifdef VSR_BUILD_COMMIT
        wprintf(L"build: %s\n", VSR_BUILD_COMMIT);
#endif
        if (coInit == S_OK || coInit == S_FALSE) CoUninitialize();
        return 0;
    }

    if (argc >= 2 && wcscmp(argv[1], L"--dev-record") == 0) {
        const DevRecordOptions o = ParseDevRecordOptions(argc, argv);
        const int code = Application::Instance().RunDevRecord(o.seconds, o.preset, o.outputDir,
                                                              nullptr, o.encoder, o.audio,
                                                              o.backend, o.audioOutId,
                                                              o.audioInId);
        if (coInit == S_OK || coInit == S_FALSE) CoUninitialize();
        return code;
    }

    if (argc >= 3 && wcscmp(argv[1], L"--dev-compete") == 0) {
        // ADR-007 verification: baseline vs. recording with a transcode job.
        const DevRecordOptions o = ParseDevRecordOptions(argc, argv);
        const int code = Application::Instance().RunDevCompete(
            argv[2], o.seconds, o.preset, o.outputDir);
        if (coInit == S_OK || coInit == S_FALSE) CoUninitialize();
        return code;
    }

    // Child helper for --dev-record-window: renders the animated test window
    // and exits when the parent closes it (or after its own timeout).
    if (argc >= 2 && wcscmp(argv[1], L"--dev-test-window") == 0) {
        unsigned fps = 30;
        int seconds = 30;
        std::wstring title = L"VsrDevTestWindow";
        for (int i = 2; i + 1 < argc; i += 2) {
            if (wcscmp(argv[i], L"--fps") == 0) fps = static_cast<unsigned>(_wtoi(argv[i + 1]));
            else if (wcscmp(argv[i], L"--seconds") == 0) seconds = _wtoi(argv[i + 1]);
            else if (wcscmp(argv[i], L"--title") == 0) title = argv[i + 1];
        }
        const int code = vsr::app::RunDevTestWindow(fps, seconds, title);
        if (coInit == S_OK || coInit == S_FALSE) CoUninitialize();
        return code;
    }

    if (argc >= 2 && wcscmp(argv[1], L"--dev-record-window") == 0) {
        const DevRecordOptions o = ParseDevRecordOptions(argc, argv);
        const int code = Application::Instance().RunDevRecordWindow(o.seconds, o.preset,
                                                                    o.outputDir, o.encoder,
                                                                    o.audio, o.backend,
                                                                    o.audioOutId, o.audioInId);
        if (coInit == S_OK || coInit == S_FALSE) CoUninitialize();
        return code;
    }

    if (argc >= 2 && wcscmp(argv[1], L"--compress-window") == 0) {
        // Development/QA: start with the compressor window open; any further
        // arguments are files to queue.
        std::vector<std::wstring> files;
        for (int i = 2; i < argc; ++i) files.emplace_back(argv[i]);
        Application::Instance().SetOpenCompressorOnStart(true, files);
    }

    if (argc >= 2 && wcscmp(argv[1], L"--update-window") == 0) {
        // Development/QA: open the update window when a check finds an update.
        Application::Instance().SetOpenUpdateWindowOnStart(true);
    }

    const int code = Application::Instance().RunInteractive();
    if (coInit == S_OK || coInit == S_FALSE) CoUninitialize();
    return code;
}

// Virtual Screen Recorder - Win32 application shell (FR-003..FR-006, T070,
// T071; design.md §4.1).
//
// Responsibilities: process startup/shutdown, COM + WinRT init, Per-Monitor V2
// DPI awareness (manifest), single-instance activation, logger and settings
// bootstrap, and the `--dev-record` headless development entry point (T040).
// The UI never touches capture, encoder, display, or driver objects directly;
// everything routes through the coordinator.
#pragma once

#include <memory>

#include <Application/Coordinator.h>

namespace vsr::app {

constexpr wchar_t kMainWindowClassName[] = L"VsrMainWindow";
constexpr wchar_t kSingleInstanceMutexName[] = L"Local\\VirtualScreenRecorder.SingleInstance";

class MainWindow;

class Application {
public:
    static Application& Instance() noexcept;

    // Runs the interactive app. Returns the process exit code.
    int RunInteractive();
    // Runs the headless development recording entry point (T040). With
    // `targetWindow` set, records that window driverless (parked off-screen);
    // with null, brings up the virtual display and records it (dev path).
    // `captureAudio` opts the run into the system-audio (WASAPI loopback ->
    // AAC) stream.
    int RunDevRecord(int seconds, const std::wstring& presetName,
                     const std::wstring& outputDir, HWND targetWindow = nullptr,
                     encoder::EncoderPreference encoder = encoder::EncoderPreference::Auto,
                     bool captureAudio = false);
    // Driverless E2E entry: spawns a self-contained animated test window in a
    // child process, records it through the full capture pipeline, then closes
    // the child. Exit 0 on a finalized MP4.
    int RunDevRecordWindow(int seconds, const std::wstring& presetName,
                           const std::wstring& outputDir,
                           encoder::EncoderPreference encoder = encoder::EncoderPreference::Auto,
                           bool captureAudio = false);
    // Prints a capability report (OS, GPU, capture, encoder, virtual display,
    // topology, settings) and exits. Support/diagnostics tool; satisfies the
    // T020/T030 "diagnostic output" verification items. Never mutates state.
    int RunSelfCheck();

    // T031 verification: encodes synthetic GPU frames through the full
    // VideoConverter -> H.264 -> MP4 pipeline for a fixed duration at the
    // chosen preset. No capture or driver involvement. Returns the output
    // path via the process exit log; exit 0 on a finalized MP4.
    int RunEncodeTest(int seconds, const std::wstring& presetName,
                      const std::wstring& outputDir,
                      encoder::EncoderPreference encoder = encoder::EncoderPreference::Auto);

    // "Windows 11 build 26100" style string for diagnostics.
    static std::wstring OsBuildString();

    Coordinator& GetCoordinator() noexcept { return *coordinator_; }

    bool WantsMinimizeToTray() const noexcept { return minimizeToTray_; }
    void SetMinimizeToTray(bool v) noexcept { minimizeToTray_ = v; }
    std::wstring OutputDirectory() const { return outputDirectory_; }
    void SetOutputDirectory(const std::wstring& dir) { outputDirectory_ = dir; }

private:
    Application();

    bool InitRuntime();
    void ShutdownRuntime();
    bool EnsureSingleInstance();

    std::shared_ptr<IClock> clock_;
    std::unique_ptr<Coordinator> coordinator_;
    std::unique_ptr<MainWindow> mainWindow_;
    bool minimizeToTray_ = true;
    std::wstring outputDirectory_;
};

} // namespace vsr::app

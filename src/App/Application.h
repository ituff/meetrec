// Virtual Screen Recorder - Win32 application shell (FR-003..FR-006, T070,
// T071; design.md §4.1).
//
// Responsibilities: process startup/shutdown, COM + WinRT init, Per-Monitor V2
// DPI awareness (manifest), single-instance activation, logger and settings
// bootstrap, and the `--dev-record` headless development entry point (T040).
// The UI never touches capture, encoder, display, or driver objects directly;
// everything routes through the coordinator.
#pragma once

#include <cstdint>
#include <memory>
#include <vector>

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
                     bool captureAudio = false,
                     encoder::WriterBackend backend = encoder::WriterBackend::Auto,
                     const std::wstring& audioOutputDeviceId = {},
                     const std::wstring& audioInputDeviceId = {},
                     const std::wstring& competeFile = {},
                     std::uint64_t* droppedFramesOut = nullptr);
    // Driverless E2E entry: spawns a self-contained animated test window in a
    // child process, records it through the full capture pipeline, then closes
    // the child. Exit 0 on a finalized MP4. `competeFile` runs a transcode job
    // in the same process and suspends it when recording starts (ADR-007
    // "recording wins" verification).
    int RunDevRecordWindow(int seconds, const std::wstring& presetName,
                           const std::wstring& outputDir,
                           encoder::EncoderPreference encoder = encoder::EncoderPreference::Auto,
                           bool captureAudio = false,
                           encoder::WriterBackend backend = encoder::WriterBackend::Auto,
                           const std::wstring& audioOutputDeviceId = {},
                           const std::wstring& audioInputDeviceId = {},
                           const std::wstring& competeFile = {},
                           std::uint64_t* droppedFramesOut = nullptr);
    // ADR-007 verification: records a baseline run and then a run with a
    // transcode job, comparing dropped frames and asserting the job was
    // requeued when recording started.
    int RunDevCompete(const std::wstring& competeFile, int seconds,
                      const std::wstring& presetName,
                      const std::wstring& outputDir);
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
                      encoder::EncoderPreference encoder = encoder::EncoderPreference::Auto,
                      encoder::WriterBackend backend = encoder::WriterBackend::Auto);

    // ADR-007 qualification: compresses one existing file through the same
    // service the Compress window uses, printing the output path. Exit 0 on a
    // completed MP4. Development/support entry point.
    int RunCompressTest(const std::wstring& input, const std::wstring& presetName,
                        const std::wstring& outputDir);

    // ADR-008 qualification: update check and a full check/download/apply run
    // (the app exits so the generated script can replace it).
    int RunUpdateCheck();
    int RunUpdateSelfTest();

    // "Windows build 26100" style string for diagnostics.
    static std::wstring OsBuildString();

    Coordinator& GetCoordinator() noexcept { return *coordinator_; }

    bool WantsMinimizeToTray() const noexcept { return minimizeToTray_; }
    void SetMinimizeToTray(bool v) noexcept { minimizeToTray_ = v; }
    // Development/QA entry support: open the compressor window at startup and
    // optionally queue the given files.
    void SetOpenCompressorOnStart(bool open,
                                  const std::vector<std::wstring>& files = {});
    // Development/QA: open the update window as soon as a check finds one.
    void SetOpenUpdateWindowOnStart(bool open) noexcept { openUpdateWindowOnStart_ = open; }
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
    ULONG_PTR gdiplusToken_ = 0;
    bool minimizeToTray_ = true;
    bool openCompressorOnStart_ = false;
    bool openUpdateWindowOnStart_ = false;
    std::vector<std::wstring> openCompressorFiles_;
    std::wstring outputDirectory_;
};

} // namespace vsr::app

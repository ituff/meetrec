#pragma once

// Virtual Screen Recorder - native controller window (FR-070, FR-075..FR-077,
// NFR-040..NFR-044; T070, T072).

#include <atomic>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <Application/Coordinator.h>
#include <WindowManagement/WindowEnumerator.h>
#include "TrayIcon.h"

namespace vsr::app {

class PreviewPanel;

class MainWindow {
public:
    explicit MainWindow(Coordinator& coordinator);
    ~MainWindow();

    MainWindow(const MainWindow&) = delete;
    MainWindow& operator=(const MainWindow&) = delete;

    // Creates the window (hidden until ShowAndActivate).
    bool Create();
    void ShowAndActivate();
    HWND Hwnd() const noexcept { return hwnd_; }

    // Forces a status refresh (also used by tray commands).
    void RefreshUi();

private:
    static LRESULT CALLBACK WndProcThunk(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
    LRESULT WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

    void OnCreate();
    void OnTimer();
    void OnDpiChanged(UINT newDpi);
    void LayoutControls(UINT dpi);
    void ApplyCommandStates(const StatusSnapshot& snap);

    void OnStartClicked();
    void OnPauseResumeClicked();
    void OnStopClicked();
    void OnBrowseClicked();
    void OnOpenOutputFolder();
    void OnDiagnosticsClicked();
    void OnDisplayToggleClicked();
    void ReapDisplayOperation();
    void OnStopClickedAsync();
    void ReapStopOperation();
    void RefreshWindowList();
    // Rebuilds the audio device combo items; empty preferred IDs keep the
    // Windows default entry selected.
    void RefreshAudioDeviceCombos(const std::wstring& preferredOutput = {},
                                  const std::wstring& preferredInput = {});
    std::wstring SelectedAudioDeviceId(bool output) const;
    void SaveSettings();

    Coordinator& coordinator_;
    TrayIcon tray_;
    std::unique_ptr<PreviewPanel> preview_;
    HWND hwnd_ = nullptr;
    UINT dpi_ = 96;
    UINT timerId_ = 1;
    UINT activateMessage_ = 0;
    UINT taskbarCreatedMessage_ = 0;
    int pauseButtonMode_ = 0; // 0 = Pause, 1 = Resume
    bool lastVirtualActive_ = false;
    UINT lastTrayKey_ = 0;
    std::atomic<bool> displayOpRunning_{false};
    std::atomic<bool> displayOpDone_{false};
    std::thread displayOp_;   // bounded display connect/disconnect work
    std::atomic<bool> stopOpRunning_{false};
    std::atomic<bool> stopOpDone_{false};
    std::thread stopOp_;      // bounded stop/finalize work (FR-075)
    HFONT uiFont_ = nullptr;
    std::vector<window::WindowInfo> windows_;
    // Endpoint IDs behind the audio device combo items; index 0 is the
    // Windows default (empty ID). Parallel to the combo item indexes.
    std::vector<std::wstring> audioOutputIds_;
    std::vector<std::wstring> audioInputIds_;
};

} // namespace vsr::app

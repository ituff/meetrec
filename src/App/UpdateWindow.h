// MeetRec - progress window for a verified update download (ADR-008).
//
// Owns the download worker, shows progress/cancel, and hands the verified zip
// to MainWindow, which writes the apply script and exits the app.
#pragma once

#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <thread>
#include <vector>

#include <Update/ReleaseInfo.h>

#include "UiDraw.h"

namespace vsr::app {

class UpdateWindow {
public:
    // Invoked on the UI thread once the verified zip is ready. The callback
    // must launch the updater and close the main window.
    using ReadyCallback = std::function<void(const std::filesystem::path&)>;

    UpdateWindow(HWND owner, update::ReleaseInfo release, ReadyCallback onReady);
    ~UpdateWindow();

    UpdateWindow(const UpdateWindow&) = delete;
    UpdateWindow& operator=(const UpdateWindow&) = delete;

    void Show();
    HWND Hwnd() const noexcept { return hwnd_; }

private:
    enum class State { Downloading, Ready, Failed, Cancelled };
    enum class Hit { None, Close, Cancel, ApplyNow };
    struct HitTarget {
        Hit id = Hit::None;
        Gdiplus::RectF rect;
    };

    struct Layout {
        Gdiplus::RectF titleBar;
        Gdiplus::RectF title;
        Gdiplus::RectF close;
        Gdiplus::RectF heading;
        Gdiplus::RectF status;
        Gdiplus::RectF progress;
        Gdiplus::RectF detail;
        Gdiplus::RectF button;
    };

    static LRESULT CALLBACK WndProcThunk(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
    LRESULT WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

    bool CreateWindowIfNeeded();
    void Invalidate();
    void UpdateLayout();
    void Paint(HDC targetDc);
    void Draw(Gdiplus::Graphics& graphics);
    void StartDownload();
    void OnDownloadDone(bool ok, const std::filesystem::path& path,
                        const std::wstring& error);
    void PollProgress();
    void RequestClose();
    Hit HitTest(POINT point) const;
    void OnDestroy();

    HWND owner_ = nullptr;
    HWND hwnd_ = nullptr;
    update::ReleaseInfo release_;
    ReadyCallback onReady_;
    std::thread worker_;
    std::atomic<bool> cancel_{false};
    std::atomic<std::uint64_t> downloaded_{0};
    std::atomic<std::uint64_t> total_{0};

    State state_ = State::Downloading;
    std::wstring error_;
    std::filesystem::path zipPath_;

    UINT dpi_ = 96;
    ui::Fonts fonts_;
    Layout layout_;
    Hit hover_ = Hit::None;
    Hit pressed_ = Hit::None;
    std::vector<HitTarget> hits_;
    HDC memDc_ = nullptr;
    HBITMAP memBmp_ = nullptr;
    int memWidth_ = 0;
    int memHeight_ = 0;
    UINT pollTimer_ = 1;
    UINT applyTimer_ = 2;
    bool trackingMouse_ = false;
};

} // namespace vsr::app

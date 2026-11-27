// MeetRec - modal progress window for the in-app speech model download.
//
// Owns the download worker, shows phased progress (download/verify/extract/
// install), and supports cancellation. The caller receives the result once the
// worker finishes; the window stays open showing success/failure until the
// user closes it.
#pragma once

#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "ModelDownloader.h"
#include "UiDraw.h"

namespace vsr::app {

class ModelDownloadWindow {
public:
    using DoneCallback = std::function<void(const ModelInstallResult&)>;

    ModelDownloadWindow(HWND owner, DoneCallback onDone);
    ~ModelDownloadWindow();

    ModelDownloadWindow(const ModelDownloadWindow&) = delete;
    ModelDownloadWindow& operator=(const ModelDownloadWindow&) = delete;

    void Show();
    bool IsOpen() const noexcept { return hwnd_ != nullptr; }

private:
    enum class State { Downloading, Success, Failed, Cancelled };
    enum class Hit { None, CloseButton, Action };

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
        Gdiplus::RectF action;
    };

    static LRESULT CALLBACK WndProcThunk(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
    LRESULT WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

    bool EnsureWindow();
    void Start();
    void Poll();
    void ApplyResult();
    void RequestCancel();
    void RequestClose();
    void UpdateLayout();
    void Paint(HDC targetDc);
    void Draw(Gdiplus::Graphics& graphics);
    void ReleaseBackBuffer();
    void HitTestBuild();
    Hit HitTest(POINT point) const;
    void Activate(Hit hit);
    std::wstring StatusText() const;
    std::wstring DetailText() const;
    std::wstring ActionLabel() const;
    ui::ButtonStyle ActionStyle() const;

    HWND owner_ = nullptr;
    HWND hwnd_ = nullptr;
    DoneCallback onDone_;
    std::thread worker_;
    std::atomic<bool> cancel_{false};
    std::atomic<bool> cancelRequested_{false};
    std::atomic<bool> done_{false};
    std::atomic<int> phase_{0};
    std::atomic<int> percent_{0};
    std::atomic<std::uint64_t> downloaded_{0};
    std::atomic<std::uint64_t> total_{0};
    std::mutex resultMutex_;
    ModelInstallResult result_;
    State state_ = State::Downloading;
    bool closeAfterCancel_ = false;
    bool callbackInvoked_ = false;

    UINT dpi_ = 96;
    ui::Fonts fonts_;
    Layout layout_;
    std::vector<HitTarget> hits_;
    Hit hover_ = Hit::None;
    Hit pressed_ = Hit::None;
    HDC memDc_ = nullptr;
    HBITMAP memBmp_ = nullptr;
    int memWidth_ = 0;
    int memHeight_ = 0;
    UINT timer_ = 1;
    bool trackingMouse_ = false;
};

} // namespace vsr::app

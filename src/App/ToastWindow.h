// MeetRec - non-activating status toast shown above the notification area.
#pragma once

#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>

#include <string>

#include "UiDraw.h"

namespace vsr::app {

class ToastWindow {
public:
    ToastWindow() = default;
    ~ToastWindow();

    ToastWindow(const ToastWindow&) = delete;
    ToastWindow& operator=(const ToastWindow&) = delete;

    void Show(HWND owner, const std::wstring& title, const std::wstring& line1,
              const std::wstring& line2, bool warning);
    // Live elapsed time while recording/paused.
    void SetSecondLine(const std::wstring& line2);
    void Hide();
    bool IsVisible() const;
    HWND Hwnd() const noexcept { return hwnd_; }

private:
    static LRESULT CALLBACK WndProcThunk(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
    LRESULT WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

    void EnsureWindow();
    void Position();
    void Paint(HDC targetDc = nullptr);
    void ReleaseBackBuffer();

    HWND hwnd_ = nullptr;
    HWND owner_ = nullptr;
    UINT dpi_ = 96;
    ui::Fonts fonts_;
    std::wstring title_;
    std::wstring line1_;
    std::wstring line2_;
    bool warning_ = false;
    bool closeHover_ = false;
    bool trackingMouse_ = false;
    UINT hideTimerId_ = 1;
    HDC memDc_ = nullptr;
    HBITMAP memBmp_ = nullptr;
    int memWidth_ = 0;
    int memHeight_ = 0;
    Gdiplus::RectF closeRect_;
};

} // namespace vsr::app

// MeetRec - modal window picker (search + eligible window list).
#pragma once

#include <windows.h>

#include <string>
#include <vector>

#include <WindowManagement/WindowEnumerator.h>

#include "UiDraw.h"

namespace vsr::app {

class WindowPicker {
public:
    // `windows` is borrowed for the lifetime of the dialog; the owner keeps
    // the icon handles alive until the result message is processed.
    WindowPicker(HWND owner, std::vector<window::WindowInfo> windows, HWND current,
                 UINT resultMessage);
    ~WindowPicker();

    WindowPicker(const WindowPicker&) = delete;
    WindowPicker& operator=(const WindowPicker&) = delete;

    bool Show();
    HWND Hwnd() const noexcept { return hwnd_; }

private:
    enum class Hot { None, Close, Select, Cancel, Filter };

    static LRESULT CALLBACK WndProcThunk(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
    static LRESULT CALLBACK ChildKeyProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp,
                                         UINT_PTR id, DWORD_PTR ref);
    LRESULT WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

    void LayoutChildren();
    void RebuildList();
    void Paint(HDC targetDc = nullptr);
    void Accept();
    void Finish(int index);
    void SetHot(Hot hot);
    void DrawTitleBar(Gdiplus::Graphics& graphics);
    void DrawFooter(Gdiplus::Graphics& graphics);

    HWND hwnd_ = nullptr;
    HWND owner_ = nullptr;
    HWND edit_ = nullptr;
    HWND list_ = nullptr;
    UINT resultMessage_ = 0;
    UINT dpi_ = 96;
    HWND current_ = nullptr;
    std::vector<window::WindowInfo> windows_;
    std::vector<size_t> filtered_;
    ui::Fonts fonts_;
    HFONT editFont_ = nullptr;
    HFONT listFont_ = nullptr;
    HBRUSH whiteBrush_ = nullptr;
    Hot hot_ = Hot::None;
    bool trackingMouse_ = false;
    RECT closeRect_ = {};
    RECT selectRect_ = {};
    RECT cancelRect_ = {};
    RECT filterRect_ = {};
    bool filterCommon_ = false;
    HDC memDc_ = nullptr;
    HBITMAP memBmp_ = nullptr;
    int memWidth_ = 0;
    int memHeight_ = 0;
};

} // namespace vsr::app

#include "TrayIcon.h"

namespace vsr::app {

namespace {

// Fallback icon size when SM_CXSMICON is unavailable.
constexpr int kIconPx = 16;

COLORREF StateColor(const StatusSnapshot& snap) {
    switch (snap.state) {
        case AppState::Recording:
            return RGB(214, 48, 49); // red: recording active (FR-085)
        case AppState::Paused:
            return RGB(254, 211, 48); // amber: paused
        case AppState::Faulted:
            return RGB(87, 87, 87); // dark gray: faulted
        default:
            return RGB(116, 185, 255); // blue: ready/idle
    }
}

} // namespace

TrayIcon::~TrayIcon() {
    Remove();
}

void TrayIcon::Create(HWND owner, UINT callbackMessage) {
    data_.cbSize = sizeof(data_);
    data_.hWnd = owner;
    data_.uID = 1;
    data_.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    data_.uCallbackMessage = callbackMessage;
    data_.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    lstrcpynW(data_.szTip, L"Virtual Screen Recorder", ARRAYSIZE(data_.szTip));
    if (Shell_NotifyIconW(NIM_ADD, &data_)) {
        added_ = true;
    }
}

HICON TrayIcon::MakeStateIcon(const StatusSnapshot& snap) {
    // Runtime-drawn state icons avoid binary assets; a filled circle whose
    // color encodes the state (FR-085).
    const int size = GetSystemMetrics(SM_CXSMICON);
    const int px = size > 0 ? size : kIconPx;

    HDC screen = GetDC(nullptr);
    HDC mem = CreateCompatibleDC(screen);
    HBITMAP bmp = CreateCompatibleBitmap(screen, px, px);
    HGDIOBJ oldBmp = SelectObject(mem, bmp);
    HBRUSH brush = CreateSolidBrush(StateColor(snap));
    HGDIOBJ oldBrush = SelectObject(mem, brush);
    RECT full = {0, 0, px, px};
    FillRect(mem, &full, brush);
    SelectObject(mem, oldBrush);
    DeleteObject(brush);
    SelectObject(mem, oldBmp);
    DeleteDC(mem);
    ReleaseDC(nullptr, screen);

    HICON icon = nullptr;
    ICONINFO info = {};
    info.fIcon = TRUE;
    info.hbmColor = bmp;
    info.hbmMask = CreateBitmap(px, px, 1, 1, nullptr);
    if (info.hbmMask != nullptr) {
        icon = CreateIconIndirect(&info);
        DeleteObject(info.hbmMask);
    }
    DeleteObject(bmp);
    return icon;
}

void TrayIcon::Update(const StatusSnapshot& snap) {
    if (!added_) return;

    // FR-083: tooltip shows recording state and elapsed time while recording.
    wchar_t tip[128] = L"Virtual Screen Recorder";
    switch (snap.state) {
        case AppState::Recording: {
            const std::int64_t s = snap.activeElapsedMs / 1000;
            swprintf_s(tip, L"Recording %02d:%02d:%02d", static_cast<int>(s / 3600),
                       static_cast<int>((s / 60) % 60), static_cast<int>(s % 60));
            break;
        }
        case AppState::Paused:
            swprintf_s(tip, L"Paused at %02d:%02d",
                       static_cast<int>(snap.activeElapsedMs / 60000),
                       static_cast<int>((snap.activeElapsedMs / 1000) % 60));
            break;
        case AppState::Faulted:
            lstrcpynW(tip, L"Virtual Screen Recorder - error", ARRAYSIZE(tip));
            break;
        default:
            break;
    }
    lstrcpynW(data_.szTip, tip, ARRAYSIZE(data_.szTip));

    const UINT stateKey = static_cast<UINT>(snap.state);
    if (stateKey != lastIconState_) {
        if (data_.hIcon != nullptr) {
            DestroyIcon(data_.hIcon);
            data_.hIcon = nullptr;
        }
        data_.hIcon = MakeStateIcon(snap);
        lastIconState_ = stateKey;
    }
    Shell_NotifyIconW(NIM_MODIFY, &data_);
}

void TrayIcon::Remove() {
    if (added_) {
        Shell_NotifyIconW(NIM_DELETE, &data_);
        added_ = false;
    }
    if (data_.hIcon != nullptr) {
        DestroyIcon(data_.hIcon);
        data_.hIcon = nullptr;
    }
}

} // namespace vsr::app

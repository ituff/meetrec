#include "TrayIcon.h"

#include <objidl.h>
#include <gdiplus.h>

#include "Localization.h"
#include "UiDraw.h"

namespace vsr::app {

namespace {

constexpr int kIconResource = 1;

Gdiplus::Color StateColor(const StatusSnapshot& snap) {
    switch (snap.state) {
        case AppState::Recording:
            return Gdiplus::Color(255, 229, 52, 44);
        case AppState::Paused:
            return Gdiplus::Color(255, 233, 162, 27);
        case AppState::Faulted:
            return Gdiplus::Color(255, 107, 114, 128);
        default:
            return Gdiplus::Color(255, 0, 100, 234);
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
    data_.hIcon = LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(kIconResource));
    lstrcpynW(data_.szTip, i18n::Text(i18n::Str::TrayReady), ARRAYSIZE(data_.szTip));
    if (Shell_NotifyIconW(NIM_ADD, &data_)) {
        added_ = true;
    }
}

HICON TrayIcon::MakeStateIcon(const StatusSnapshot& snap) {
    const int size = GetSystemMetrics(SM_CXSMICON);
    const int px = size > 0 ? size : 16;
    const float scale = static_cast<float>(px) / 16.0f;

    Gdiplus::Bitmap bitmap(px, px, PixelFormat32bppARGB);
    {
        Gdiplus::Graphics graphics(&bitmap);
        graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        graphics.SetCompositingMode(Gdiplus::CompositingModeSourceOver);
        graphics.Clear(Gdiplus::Color(0, 0, 0, 0));
        ui::DrawLogo(graphics, Gdiplus::RectF(0.5f, 0.5f, px - 1.0f, px - 1.0f),
                     3.0f * scale);

        // State badge in the lower-right corner with a white ring; shape and
        // color both change so the tray is not color-only (NFR-043).
        const float badge = std::max(5.0f, 6.5f * scale);
        const Gdiplus::RectF ring(px - badge - 1.0f, px - badge - 1.0f, badge, badge);
        ui::DrawCircle(graphics, ring, Gdiplus::Color(255, 255, 255, 255),
                       Gdiplus::Color(0, 0, 0, 0), 0.0f);
        const Gdiplus::RectF inner(ring.X + 1.0f, ring.Y + 1.0f, ring.Width - 2.0f,
                                   ring.Height - 2.0f);
        ui::DrawCircle(graphics, inner, StateColor(snap),
                       Gdiplus::Color(0, 0, 0, 0), 0.0f);
    }

    HBITMAP color = nullptr;
    if (bitmap.GetHBITMAP(Gdiplus::Color(0, 0, 0, 0), &color) != Gdiplus::Ok ||
        color == nullptr) {
        return LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(kIconResource));
    }
    HBITMAP mask = CreateBitmap(px, px, 1, 1, nullptr);
    ICONINFO info = {};
    info.fIcon = TRUE;
    info.hbmColor = color;
    info.hbmMask = mask;
    HICON icon = CreateIconIndirect(&info);
    if (mask != nullptr) DeleteObject(mask);
    DeleteObject(color);
    return icon != nullptr
               ? icon
               : LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(kIconResource));
}

void TrayIcon::Update(const StatusSnapshot& snap) {
    if (!added_) return;

    std::wstring tip;
    const std::wstring elapsed = i18n::FormatDuration(snap.activeElapsedMs);
    switch (snap.state) {
        case AppState::Recording:
            tip = i18n::Format(i18n::Str::TrayRecordingTooltip, {elapsed});
            break;
        case AppState::Paused:
            tip = i18n::Format(i18n::Str::TrayPausedTooltip, {elapsed});
            break;
        case AppState::Faulted:
            tip = i18n::Text(i18n::Str::TrayErrorTooltip);
            break;
        default:
            tip = i18n::Text(i18n::Str::TrayReady);
            break;
    }
    lstrcpynW(data_.szTip, tip.c_str(), ARRAYSIZE(data_.szTip));

    const UINT stateKey = static_cast<UINT>(snap.state);
    bool changed = false;
    if (stateKey != lastIconState_) {
        HICON newIcon = MakeStateIcon(snap);
        if (newIcon != nullptr) {
            if (data_.hIcon != nullptr) DestroyIcon(data_.hIcon);
            data_.hIcon = newIcon;
        }
        lastIconState_ = stateKey;
        changed = true;
    }
    if (tip != lastTip_) {
        lastTip_ = tip;
        changed = true;
    }
    if (changed) Shell_NotifyIconW(NIM_MODIFY, &data_);
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
    lastIconState_ = 0xFFFFFFFF;
    lastTip_.clear();
}

} // namespace vsr::app

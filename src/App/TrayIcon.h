#pragma once

// MeetRec - notification area icon (FR-080..FR-085, T071).
//
// The icon is the product mark with a state badge; the tooltip and menu use
// the same localized strings as the window.

#include <windows.h>
#include <shellapi.h>

#include <string>

#include <Application/StatusSnapshot.h>

namespace vsr::app {

class TrayIcon {
public:
    TrayIcon() = default;
    ~TrayIcon();

    TrayIcon(const TrayIcon&) = delete;
    TrayIcon& operator=(const TrayIcon&) = delete;

    void Create(HWND owner, UINT callbackMessage);
    void Update(const StatusSnapshot& snap);
    void Remove();

private:
    HICON MakeStateIcon(const StatusSnapshot& snap);
    NOTIFYICONDATAW data_ = {};
    bool added_ = false;
    UINT lastIconState_ = 0xFFFFFFFF;
    std::wstring lastTip_;
};

} // namespace vsr::app

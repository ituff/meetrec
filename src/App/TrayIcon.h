#pragma once

// Virtual Screen Recorder - notification area icon (FR-080..FR-085, T071).

#include <windows.h>
#include <shellapi.h>

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
};

} // namespace vsr::app

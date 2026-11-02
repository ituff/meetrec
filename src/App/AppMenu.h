// MeetRec - owner-drawn native popup menus.
//
// Windows owns the popup mechanics (keyboard, scrolling, dismissal, shadows,
// rounded corners on Windows 11; Windows 10 uses square native menus); the
// items are painted in the product theme.
#pragma once

#include <windows.h>

#include <string>
#include <vector>

#include "UiDraw.h"

namespace vsr::app::menu {

struct Item {
    std::wstring label;
    std::wstring right;             // secondary text, right aligned
    ui::Icon icon = ui::Icon::None; // menu bitmap area
    UINT command = 0;
    bool enabled = true;
    bool checked = false;
    bool danger = false;
    bool separator = false;

    // Filled in by Show(); consumed by HandleMeasureItem.
    int measuredWidth = 0;
    int measuredHeight = 0;
};

// Displays the items at the screen point. Returns the chosen command, or 0.
// The item vector must outlive the call (it does: TrackPopupMenu is blocking).
int Show(HWND owner, POINT screenPoint, std::vector<Item>& items,
         const ui::Fonts& fonts, bool alignRight);

// WM_MEASUREITEM / WM_DRAWITEM helpers; the owner window forwards them.
bool HandleMeasureItem(MEASUREITEMSTRUCT* measure, const ui::Fonts& fonts);
bool HandleDrawItem(DRAWITEMSTRUCT* draw, const ui::Fonts& fonts);

} // namespace vsr::app::menu

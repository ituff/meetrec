#include "AppMenu.h"

#include <algorithm>

namespace vsr::app::menu {

namespace {

void MeasureItems(HWND owner, std::vector<Item>& items, const ui::Fonts& fonts) {
    HDC screen = GetDC(nullptr);
    if (screen == nullptr) return;
    {
        Gdiplus::Graphics graphics(screen);
        const UINT dpi = owner != nullptr ? GetDpiForWindow(owner) : 96;
        for (Item& item : items) {
            if (item.separator) {
                item.measuredWidth = 0;
                item.measuredHeight = ui::Scale(9, dpi);
                continue;
            }
            const Gdiplus::SizeF label =
                ui::MeasureText(graphics, fonts, item.label, ui::FontRole::Body);
            const Gdiplus::SizeF right =
                ui::MeasureText(graphics, fonts, item.right, ui::FontRole::Small);
            int width = ui::Scale(48, dpi) + static_cast<int>(label.Width);
            if (!item.right.empty()) {
                width += ui::Scale(16, dpi) + static_cast<int>(right.Width);
            }
            if (item.checked) width += ui::Scale(26, dpi);
            item.measuredWidth = width;
            item.measuredHeight = ui::Scale(37, dpi);
        }
    }
    ReleaseDC(nullptr, screen);
}

} // namespace

int Show(HWND owner, POINT screenPoint, std::vector<Item>& items, const ui::Fonts& fonts,
         bool alignRight) {
    if (items.empty()) return 0;
    MeasureItems(owner, items, fonts);

    HMENU menu = CreatePopupMenu();
    if (menu == nullptr) return 0;

    for (Item& item : items) {
        MENUITEMINFOW info = {};
        info.cbSize = sizeof(info);
        info.fMask = MIIM_FTYPE | MIIM_ID | MIIM_DATA | MIIM_STATE;
        info.wID = item.command;
        info.fState = item.enabled ? MFS_ENABLED : MFS_DISABLED;
        info.dwItemData = reinterpret_cast<ULONG_PTR>(&item);
        if (item.separator) {
            info.fType = MFT_SEPARATOR;
            info.fState = MFS_ENABLED;
        } else {
            info.fType = MFT_OWNERDRAW;
            info.fMask |= MIIM_STRING;
            info.dwTypeData = const_cast<wchar_t*>(item.label.c_str());
            info.cch = static_cast<UINT>(item.label.size());
        }
        InsertMenuItemW(menu, GetMenuItemCount(menu), TRUE, &info);
    }

    // Ownership of an open popup menu requires the foreground window dance;
    // without it the menu stays visible when the user clicks elsewhere.
    SetForegroundWindow(owner);
    int command = TrackPopupMenuEx(
        menu,
        TPM_RETURNCMD | TPM_NONOTIFY | TPM_TOPALIGN |
            (alignRight ? TPM_RIGHTALIGN : TPM_LEFTALIGN),
        screenPoint.x, screenPoint.y, owner, nullptr);
    DestroyMenu(menu);
    PostMessageW(owner, WM_NULL, 0, 0);
    return command;
}

bool HandleMeasureItem(MEASUREITEMSTRUCT* measure, const ui::Fonts& fonts) {
    (void)fonts;
    if (measure == nullptr || measure->CtlType != ODT_MENU || measure->itemData == 0) {
        return false;
    }
    auto* item = reinterpret_cast<Item*>(measure->itemData);
    measure->itemWidth = static_cast<UINT>(item->measuredWidth);
    measure->itemHeight = static_cast<UINT>(item->measuredHeight);
    return true;
}

bool HandleDrawItem(DRAWITEMSTRUCT* draw, const ui::Fonts& fonts) {
    if (draw == nullptr || draw->CtlType != ODT_MENU || draw->itemData == 0) return false;
    auto* item = reinterpret_cast<Item*>(draw->itemData);
    if (item->separator) return false;

    Gdiplus::Graphics graphics(draw->hDC);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    graphics.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);
    const Gdiplus::RectF rect(static_cast<float>(draw->rcItem.left),
                              static_cast<float>(draw->rcItem.top),
                              static_cast<float>(draw->rcItem.right - draw->rcItem.left),
                              static_cast<float>(draw->rcItem.bottom - draw->rcItem.top));
    const bool selected = (draw->itemState & ODS_SELECTED) != 0;
    ui::FillRect(graphics, rect,
                 selected ? ui::colors::PrimarySoft() : ui::colors::Surface());

    const UINT dpi = fonts.Dpi();
    const float iconSize = ui::ScaleF(16.0f, dpi);
    const float iconX = rect.X + ui::ScaleF(12.0f, dpi);
    const float iconY = rect.Y + (rect.Height - iconSize) / 2.0f;
    Gdiplus::Color textColor = ui::colors::Text();
    if (!item->enabled) {
        textColor = ui::colors::TextTertiary();
    } else if (item->danger) {
        textColor = ui::colors::Danger();
    }
    if (item->icon != ui::Icon::None) {
        ui::DrawIcon(graphics, item->icon, Gdiplus::RectF(iconX, iconY, iconSize, iconSize),
                     textColor, ui::ScaleF(1.5f, dpi));
    }

    const float textLeft = rect.X + ui::ScaleF(38.0f, dpi);
    float textRight = rect.GetRight() - ui::ScaleF(12.0f, dpi);
    if (item->checked) textRight -= ui::ScaleF(24.0f, dpi);
    if (!item->right.empty()) {
        const Gdiplus::SizeF rightSize =
            ui::MeasureText(graphics, fonts, item->right, ui::FontRole::Small);
        const Gdiplus::RectF rightRect(textRight - rightSize.Width, rect.Y,
                                       rightSize.Width, rect.Height);
        ui::DrawText(graphics, fonts, item->right, rightRect, ui::FontRole::Small,
                     ui::colors::TextSecondary(), ui::Align::Far, ui::VAlign::Center, false);
        textRight -= rightSize.Width + ui::ScaleF(16.0f, dpi);
    }
    ui::DrawText(graphics, fonts, item->label,
                 Gdiplus::RectF(textLeft, rect.Y, std::max(0.0f, textRight - textLeft),
                                rect.Height),
                 ui::FontRole::Body, textColor, ui::Align::Near, ui::VAlign::Center, true);

    if (item->checked) {
        const float checkSize = ui::ScaleF(14.0f, dpi);
        ui::DrawIcon(graphics,
                     ui::Icon::Check,
                     Gdiplus::RectF(rect.GetRight() - ui::ScaleF(26.0f, dpi),
                                    rect.Y + (rect.Height - checkSize) / 2.0f, checkSize,
                                    checkSize),
                     ui::colors::Primary(), ui::ScaleF(1.8f, dpi));
    }
    return true;
}

} // namespace vsr::app::menu

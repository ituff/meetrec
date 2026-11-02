#include "WindowPicker.h"

#include <commctrl.h>
#include <dwmapi.h>
#include <windowsx.h>

#include <algorithm>
#include <cwctype>

#include <Infrastructure/Strings.h>

#include "Application.h"

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "dwmapi.lib")

namespace vsr::app {

namespace {

constexpr wchar_t kPickerClassName[] = L"VsrWindowPicker";
constexpr int kSearchId = 101;
constexpr int kListId = 102;
constexpr int kAppIconResource = 1;

bool ContainsNoCase(const std::wstring& haystack, const std::wstring& needle) {
    if (needle.empty()) return true;
    return ToLower(haystack).find(ToLower(needle)) != std::wstring::npos;
}

// The "meeting apps only" filter keeps the common clients people actually
// record. Executable names are the primary signal; title keywords catch web
// clients and renamed/embedded builds.
bool IsCommonMeetingApp(const window::WindowInfo& info) {
    const std::wstring exe = ToLower(info.exeName);
    static const wchar_t* kExecutables[] = {
        L"ms-teams.exe",       L"teams.exe",          L"zoom.exe",
        L"wemeetapp.exe",      L"wemeet.exe",         L"tencentmeeting.exe",
        L"voovmeeting.exe",    L"dingtalk.exe",       L"feishu.exe",
        L"lark.exe",           L"wxwork.exe",         L"skype.exe",
        L"webexmta.exe",       L"webex.exe",          L"ciscocollabhost.exe",
        L"gotomeeting.exe",    L"bluejeans.exe",      L"hwcloudmeeting.exe",
        L"huaweicloudmeeting.exe"};
    for (const wchar_t* candidate : kExecutables) {
        if (exe == candidate) return true;
    }

    const std::wstring title = ToLower(info.title);
    static const wchar_t* kTitleHints[] = {
        L"microsoft teams", L"zoom meeting", L"腾讯会议", L"webex",
        L"飞书会议",        L"钉钉会议",     L"voov",     L"google meet",
        L"meet -",          L"gotomeeting",  L"bluejeans", L"skype"};
    for (const wchar_t* hint : kTitleHints) {
        if (title.find(hint) != std::wstring::npos) return true;
    }
    return false;
}

} // namespace

WindowPicker::WindowPicker(HWND owner, std::vector<window::WindowInfo> windows,
                           HWND current, UINT resultMessage)
    : owner_(owner), resultMessage_(resultMessage), current_(current),
      windows_(std::move(windows)) {}

WindowPicker::~WindowPicker() {
    if (hwnd_ != nullptr) {
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }
    if (editFont_ != nullptr) DeleteObject(editFont_);
    // listFont_ aliases editFont_ (native controls share the UI font).
    if (whiteBrush_ != nullptr) DeleteObject(whiteBrush_);
    if (memDc_ != nullptr) DeleteDC(memDc_);
    if (memBmp_ != nullptr) DeleteObject(memBmp_);
    fonts_.Destroy();
}

bool WindowPicker::Show() {
    if (hwnd_ != nullptr) {
        ShowWindow(hwnd_, SW_SHOW);
        SetForegroundWindow(hwnd_);
        return true;
    }
    HINSTANCE instance = GetModuleHandleW(nullptr);
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = &WindowPicker::WndProcThunk;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = kPickerClassName;
    wc.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(kAppIconResource));
    RegisterClassExW(&wc);

    dpi_ = owner_ != nullptr ? GetDpiForWindow(owner_) : GetDpiForSystem();
    if (dpi_ == 0) dpi_ = 96;
    fonts_.Create(dpi_, i18n::CurrentLanguage());
    editFont_ = CreateFontW(-ui::Scale(13, dpi_), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
                            i18n::CurrentLanguage() == i18n::Language::ZhCn
                                ? L"Microsoft YaHei UI"
                                : L"Segoe UI");
    listFont_ = editFont_;
    whiteBrush_ = CreateSolidBrush(RGB(255, 255, 255));

    MONITORINFO info = {};
    info.cbSize = sizeof(info);
    GetMonitorInfoW(MonitorFromWindow(owner_, MONITOR_DEFAULTTONEAREST), &info);
    const int workWidth = info.rcWork.right - info.rcWork.left;
    const int workHeight = info.rcWork.bottom - info.rcWork.top;
    const int width = std::min(ui::Scale(620, dpi_), workWidth - ui::Scale(32, dpi_));
    const int height = std::min(ui::Scale(500, dpi_), workHeight - ui::Scale(32, dpi_));
    const int x = info.rcWork.left + (workWidth - width) / 2;
    const int y = info.rcWork.top + (workHeight - height) / 2;

    const std::wstring title =
        std::wstring(i18n::Text(i18n::Str::AppName)) + L" - " +
        i18n::Text(i18n::Str::TargetPickerTitle);
    hwnd_ = CreateWindowExW(WS_EX_TOOLWINDOW, kPickerClassName, title.c_str(),
                            WS_POPUP | WS_CLIPCHILDREN, x, y, width, height, owner_,
                            nullptr, instance, this);
    if (hwnd_ == nullptr) return false;
    SetWindowLongPtrW(hwnd_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
    DWORD corner = DWMWCP_ROUND;
    DwmSetWindowAttribute(hwnd_, DWMWA_WINDOW_CORNER_PREFERENCE, &corner, sizeof(corner));

    edit_ = CreateWindowExW(0, L"EDIT", L"",
                            WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_LEFT, 0, 0, 0,
                            0, hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kSearchId)), instance,
                            nullptr);
    list_ = CreateWindowExW(0, L"LISTBOX", L"",
                            WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_NOTIFY |
                                LBS_OWNERDRAWFIXED | LBS_NOINTEGRALHEIGHT | LBS_HASSTRINGS,
                            0, 0, 0, 0, hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kListId)), instance,
                            nullptr);
    if (edit_ == nullptr || list_ == nullptr) {
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
        return false;
    }
    SendMessageW(edit_, WM_SETFONT, reinterpret_cast<WPARAM>(editFont_), TRUE);
    SendMessageW(list_, WM_SETFONT, reinterpret_cast<WPARAM>(listFont_), TRUE);
    SendMessageW(edit_, EM_SETCUEBANNER, TRUE,
                 reinterpret_cast<LPARAM>(i18n::Text(i18n::Str::TargetPickerSearch)));
    SetWindowSubclass(edit_, &WindowPicker::ChildKeyProc, 1,
                      reinterpret_cast<DWORD_PTR>(this));
    SetWindowSubclass(list_, &WindowPicker::ChildKeyProc, 2,
                      reinterpret_cast<DWORD_PTR>(this));

    LayoutChildren();
    RebuildList();
    ShowWindow(hwnd_, SW_SHOW);
    SetForegroundWindow(hwnd_);
    SetFocus(edit_);
    return true;
}

void WindowPicker::LayoutChildren() {
    RECT rc = {};
    GetClientRect(hwnd_, &rc);
    const auto S = [&](int value) { return ui::Scale(value, dpi_); };
    const int width = rc.right - rc.left;
    const int height = rc.bottom - rc.top;
    MoveWindow(edit_, S(52), S(62), width - S(72), S(22), TRUE);
    filterRect_ = RECT{S(21), S(98), width - S(21), S(121)};
    MoveWindow(list_, S(21), S(128), width - S(42), height - S(204), TRUE);
    closeRect_ = RECT{width - S(44), 0, width, S(44)};
    selectRect_ = RECT{width - S(20) - S(150), height - S(56), width - S(20),
                       height - S(16)};
    cancelRect_ = RECT{selectRect_.left - S(12) - S(110), selectRect_.top,
                       selectRect_.left - S(12), selectRect_.bottom};
}

void WindowPicker::RebuildList() {
    if (list_ == nullptr) return;
    wchar_t query[256] = {};
    if (edit_ != nullptr) GetWindowTextW(edit_, query, ARRAYSIZE(query));
    const std::wstring search = query;
    ListBox_ResetContent(list_);
    filtered_.clear();
    int selected = -1;
    for (size_t i = 0; i < windows_.size(); ++i) {
        const window::WindowInfo& info = windows_[i];
        if (filterCommon_ && !IsCommonMeetingApp(info)) continue;
        if (!ContainsNoCase(info.title, search) && !ContainsNoCase(info.exeName, search)) {
            continue;
        }
        const int item = ListBox_AddString(list_, info.title.c_str());
        if (item == LB_ERR) continue;
        ListBox_SetItemData(list_, item, static_cast<LPARAM>(i));
        if (info.hwnd == current_) selected = item;
        filtered_.push_back(i);
    }
    if (filtered_.empty()) {
        ShowWindow(list_, SW_HIDE);
    } else {
        ShowWindow(list_, SW_SHOW);
        if (selected < 0) selected = 0;
        ListBox_SetCurSel(list_, selected);
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void WindowPicker::Accept() {
    if (filtered_.empty()) return;
    const int item = ListBox_GetCurSel(list_);
    if (item == LB_ERR) return;
    const LPARAM data = ListBox_GetItemData(list_, item);
    if (data == LB_ERR) return;
    Finish(static_cast<int>(data));
}

void WindowPicker::Finish(int index) {
    if (hwnd_ == nullptr) return;
    HWND window = hwnd_;
    hwnd_ = nullptr;
    if (resultMessage_ != 0 && owner_ != nullptr && IsWindow(owner_)) {
        // index + 1: 0 means cancelled, 1.. means the filtered window index.
        PostMessageW(owner_, resultMessage_, static_cast<WPARAM>(index + 1), 0);
    }
    DestroyWindow(window);
}

void WindowPicker::SetHot(Hot hot) {
    if (hot_ == hot) return;
    hot_ = hot;
    InvalidateRect(hwnd_, nullptr, FALSE);
}

LRESULT CALLBACK WindowPicker::WndProcThunk(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
    }
    auto* self = reinterpret_cast<WindowPicker*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    return self != nullptr ? self->WndProc(hwnd, msg, wp, lp)
                           : DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT CALLBACK WindowPicker::ChildKeyProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp,
                                            UINT_PTR id, DWORD_PTR ref) {
    auto* self = reinterpret_cast<WindowPicker*>(ref);
    if (self != nullptr && msg == WM_KEYDOWN) {
        if (wp == VK_RETURN) {
            self->Accept();
            return 0;
        }
        if (wp == VK_ESCAPE) {
            self->Finish(-1);
            return 0;
        }
        if (wp == VK_DOWN && id == 1 && !self->filtered_.empty()) {
            SetFocus(self->list_);
            ListBox_SetCurSel(self->list_, 0);
            return 0;
        }
    }
    return DefSubclassProc(hwnd, msg, wp, lp);
}

LRESULT WindowPicker::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_NCHITTEST: {
            POINT screen = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            POINT client = screen;
            ScreenToClient(hwnd, &client);
            RECT title = {0, 0, 0, 0};
            RECT rc = {};
            GetClientRect(hwnd, &rc);
            title.right = rc.right;
            title.bottom = ui::Scale(44, dpi_);
            if (client.y >= 0 && client.y < title.bottom) {
                if (client.x >= closeRect_.left && client.x < closeRect_.right) {
                    return HTCLIENT;
                }
                return HTCAPTION;
            }
            return HTCLIENT;
        }
        case WM_SIZE:
            LayoutChildren();
            return 0;
        case WM_ACTIVATE:
            // The picker stays open until the user selects a window or
            // cancels; the disabled owner cannot be used anyway, and closing
            // on a transient deactivation made the dialog disappear when
            // another app briefly stole focus.
            return 0;
        case WM_PAINT: {
            PAINTSTRUCT ps = {};
            HDC dc = BeginPaint(hwnd, &ps);
            Paint(dc);
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_PRINTCLIENT:
            Paint(reinterpret_cast<HDC>(wp));
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_MOUSEMOVE: {
            if (!trackingMouse_) {
                TRACKMOUSEEVENT track = {};
                track.cbSize = sizeof(track);
                track.dwFlags = TME_LEAVE;
                track.hwndTrack = hwnd;
                TrackMouseEvent(&track);
                trackingMouse_ = true;
            }
            POINT point = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            if (point.x >= closeRect_.left && point.x < closeRect_.right &&
                point.y >= closeRect_.top && point.y < closeRect_.bottom) {
                SetHot(Hot::Close);
                SetCursor(LoadCursorW(nullptr, IDC_HAND));
            } else if (point.x >= filterRect_.left && point.x < filterRect_.right &&
                       point.y >= filterRect_.top && point.y < filterRect_.bottom) {
                SetHot(Hot::Filter);
                SetCursor(LoadCursorW(nullptr, IDC_HAND));
            } else if (point.x >= selectRect_.left && point.x < selectRect_.right &&
                       point.y >= selectRect_.top && point.y < selectRect_.bottom) {
                SetHot(Hot::Select);
                SetCursor(LoadCursorW(nullptr, IDC_HAND));
            } else if (point.x >= cancelRect_.left && point.x < cancelRect_.right &&
                       point.y >= cancelRect_.top && point.y < cancelRect_.bottom) {
                SetHot(Hot::Cancel);
                SetCursor(LoadCursorW(nullptr, IDC_HAND));
            } else {
                SetHot(Hot::None);
                SetCursor(LoadCursorW(nullptr, IDC_ARROW));
            }
            return 0;
        }
        case WM_MOUSELEAVE:
            trackingMouse_ = false;
            SetHot(Hot::None);
            return 0;
        case WM_LBUTTONUP: {
            POINT point = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            if (point.x >= closeRect_.left && point.x < closeRect_.right &&
                point.y >= closeRect_.top && point.y < closeRect_.bottom) {
                Finish(-1);
            } else if (point.x >= filterRect_.left && point.x < filterRect_.right &&
                       point.y >= filterRect_.top && point.y < filterRect_.bottom) {
                filterCommon_ = !filterCommon_;
                RebuildList();
                InvalidateRect(hwnd_, nullptr, FALSE);
            } else if (point.x >= selectRect_.left && point.x < selectRect_.right &&
                       point.y >= selectRect_.top && point.y < selectRect_.bottom) {
                Accept();
            } else if (point.x >= cancelRect_.left && point.x < cancelRect_.right &&
                       point.y >= cancelRect_.top && point.y < cancelRect_.bottom) {
                Finish(-1);
            }
            return 0;
        }
        case WM_KEYDOWN:
            if (wp == VK_ESCAPE) {
                Finish(-1);
                return 0;
            }
            if (wp == VK_RETURN) {
                Accept();
                return 0;
            }
            if (wp == VK_DOWN && !filtered_.empty()) {
                SetFocus(list_);
                ListBox_SetCurSel(list_, 0);
                return 0;
            }
            break;
        case WM_COMMAND: {
            const int id = LOWORD(wp);
            const int code = HIWORD(wp);
            if (id == kSearchId && code == EN_CHANGE) {
                RebuildList();
                return 0;
            }
            if (id == kListId && code == LBN_SELCHANGE) {
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            if (id == kListId && code == LBN_DBLCLK) {
                Accept();
                return 0;
            }
            break;
        }
        case WM_MEASUREITEM: {
            auto* measure = reinterpret_cast<MEASUREITEMSTRUCT*>(lp);
            if (measure != nullptr && measure->CtlID == kListId) {
                measure->itemHeight = static_cast<UINT>(ui::Scale(52, dpi_));
                return TRUE;
            }
            break;
        }
        case WM_DRAWITEM: {
            auto* draw = reinterpret_cast<DRAWITEMSTRUCT*>(lp);
            if (draw == nullptr || draw->CtlID != kListId || draw->itemID == -1) break;
            const LPARAM data = ListBox_GetItemData(list_, static_cast<int>(draw->itemID));
            if (data == LB_ERR || static_cast<size_t>(data) >= windows_.size()) return TRUE;
            const window::WindowInfo& info = windows_[static_cast<size_t>(data)];
            Gdiplus::Graphics graphics(draw->hDC);
            graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
            graphics.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);
            const Gdiplus::RectF rect(static_cast<float>(draw->rcItem.left),
                                      static_cast<float>(draw->rcItem.top),
                                      static_cast<float>(draw->rcItem.right -
                                                         draw->rcItem.left),
                                      static_cast<float>(draw->rcItem.bottom -
                                                         draw->rcItem.top));
            const bool selected = (draw->itemState & ODS_SELECTED) != 0;
            ui::FillRect(graphics, rect,
                         selected ? ui::colors::PrimarySoft() : ui::colors::Surface());
            const auto S = [&](float value) { return ui::ScaleF(value, dpi_); };
            const float iconSize = S(26);
            const Gdiplus::RectF iconRect(rect.X + S(12),
                                          rect.Y + (rect.Height - iconSize) / 2.0f,
                                          iconSize, iconSize);
            if (info.icon != nullptr) {
                HDC hdc = graphics.GetHDC();
                DrawIconEx(hdc, static_cast<int>(iconRect.X), static_cast<int>(iconRect.Y),
                           info.icon, static_cast<int>(iconSize),
                           static_cast<int>(iconSize), 0, nullptr, DI_NORMAL);
                graphics.ReleaseHDC(hdc);
            } else {
                ui::FillRoundRect(graphics, iconRect, S(6), ui::colors::PrimarySoft());
                ui::DrawText(graphics, fonts_, ui::InitialFor(info.exeName), iconRect,
                             ui::FontRole::BodyStrong, ui::colors::Primary(),
                             ui::Align::Center, ui::VAlign::Center, false);
            }
            const float textX = iconRect.GetRight() + S(14);
            ui::DrawText(graphics, fonts_, info.title,
                         Gdiplus::RectF(textX, rect.Y + S(6), rect.GetRight() - textX - S(12),
                                        S(22)),
                         ui::FontRole::BodyStrong, ui::colors::Text(), ui::Align::Near,
                         ui::VAlign::Center, true);
            ui::DrawText(graphics, fonts_, info.exeName,
                         Gdiplus::RectF(textX, rect.Y + S(27),
                                        rect.GetRight() - textX - S(12), S(18)),
                         ui::FontRole::Small, ui::colors::TextSecondary(), ui::Align::Near,
                         ui::VAlign::Center, true);
            return TRUE;
        }
        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORLISTBOX: {
            HDC dc = reinterpret_cast<HDC>(wp);
            SetTextColor(dc, RGB(31, 35, 41));
            SetBkColor(dc, RGB(255, 255, 255));
            return reinterpret_cast<LRESULT>(whiteBrush_);
        }
        case WM_CLOSE:
            Finish(-1);
            return 0;
        case WM_DESTROY:
            hwnd_ = nullptr;
            return 0;
        default:
            break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

void WindowPicker::Paint(HDC targetDc) {
    RECT rc = {};
    GetClientRect(hwnd_, &rc);
    const int width = rc.right - rc.left;
    const int height = rc.bottom - rc.top;
    if (width <= 0 || height <= 0) return;
    if (memDc_ == nullptr || memWidth_ != width || memHeight_ != height) {
        if (memDc_ != nullptr) {
            DeleteDC(memDc_);
            memDc_ = nullptr;
        }
        if (memBmp_ != nullptr) {
            DeleteObject(memBmp_);
            memBmp_ = nullptr;
        }
        HDC screen = GetDC(hwnd_);
        memDc_ = CreateCompatibleDC(screen);
        memBmp_ = CreateCompatibleBitmap(screen, width, height);
        ReleaseDC(hwnd_, screen);
        if (memDc_ == nullptr || memBmp_ == nullptr) return;
        SelectObject(memDc_, memBmp_);
        memWidth_ = width;
        memHeight_ = height;
    }

    {
        Gdiplus::Graphics graphics(memDc_);
        graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        graphics.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);
        const auto S = [&](float value) { return ui::ScaleF(value, dpi_); };
        ui::FillRect(graphics, Gdiplus::RectF(0, 0, static_cast<float>(width),
                                               static_cast<float>(height)),
                     ui::colors::Window());
        DrawTitleBar(graphics);

        // Search field frame.
        const Gdiplus::RectF search(S(20), S(54), static_cast<float>(width) - S(40), S(38));
        ui::FillRoundRectWithBorder(graphics, search, S(8), ui::colors::Surface(),
                                    ui::colors::BorderStrong());
        if (edit_ != nullptr && GetWindowTextLengthW(edit_) == 0) {
            ui::DrawIcon(graphics, ui::Icon::Search,
                         Gdiplus::RectF(search.X + S(11), search.Y + S(11), S(16), S(16)),
                         ui::colors::TextTertiary(), S(1.4f));
            ui::DrawText(graphics, fonts_, i18n::Text(i18n::Str::TargetPickerSearch),
                         Gdiplus::RectF(search.X + S(34), search.Y,
                                        search.Width - S(46), search.Height),
                         ui::FontRole::Body, ui::colors::TextTertiary(), ui::Align::Near,
                         ui::VAlign::Center, true);
        } else {
            ui::DrawIcon(graphics, ui::Icon::Search,
                         Gdiplus::RectF(search.X + S(11), search.Y + S(11), S(16), S(16)),
                         ui::colors::TextSecondary(), S(1.4f));
        }

        // "Meeting apps only" filter.
        const Gdiplus::RectF filterBox(S(23), S(101), S(17), S(17));
        ui::DrawCheckBox(graphics, filterBox, filterCommon_, true,
                         hot_ == Hot::Filter, dpi_);
        ui::DrawText(graphics, fonts_, i18n::Text(i18n::Str::TargetPickerFilter),
                     Gdiplus::RectF(S(47), S(98),
                                    static_cast<float>(width) - S(70), S(23)),
                     ui::FontRole::Body,
                     hot_ == Hot::Filter ? ui::colors::Text()
                                         : ui::colors::TextSecondary(),
                     ui::Align::Near, ui::VAlign::Center, false);

        // List card.
        const Gdiplus::RectF listCard(S(20), S(127),
                                      static_cast<float>(width) - S(40),
                                      static_cast<float>(height) - S(203));
        ui::FillRoundRectWithBorder(graphics, listCard, S(8), ui::colors::Surface(),
                                    ui::colors::Border());
        if (filtered_.empty()) {
            const bool noWindows = windows_.empty();
            const bool filterHidEverything =
                filterCommon_ && !noWindows &&
                (edit_ == nullptr || GetWindowTextLengthW(edit_) == 0);
            const std::wstring message = i18n::Text(
                noWindows ? i18n::Str::TargetPickerEmpty
                          : (filterHidEverything ? i18n::Str::TargetPickerNoMeetingApps
                                                 : i18n::Str::TargetPickerNoMatch));
            ui::DrawTextWrapped(
                graphics, fonts_, message,
                Gdiplus::RectF(listCard.X + S(30), listCard.Y + listCard.Height / 2.0f -
                                                       S(26),
                               listCard.Width - S(60), S(52)),
                ui::FontRole::Body, ui::colors::TextTertiary(), ui::Align::Center);
        }

        ui::DrawLine(graphics, 0, static_cast<float>(height) - S(64),
                     static_cast<float>(width), static_cast<float>(height) - S(64),
                     ui::colors::Divider(), 1.0f);
        DrawFooter(graphics);
    }

    HDC dc = targetDc != nullptr ? targetDc : GetDC(hwnd_);
    BitBlt(dc, 0, 0, width, height, memDc_, 0, 0, SRCCOPY);
    if (targetDc == nullptr) ReleaseDC(hwnd_, dc);
}

void WindowPicker::DrawTitleBar(Gdiplus::Graphics& graphics) {
    const auto S = [&](float value) { return ui::ScaleF(value, dpi_); };
    RECT rc = {};
    GetClientRect(hwnd_, &rc);
    ui::FillRect(graphics,
                 Gdiplus::RectF(0, 0, static_cast<float>(rc.right), S(44)),
                 ui::colors::TitleBar());
    ui::DrawLine(graphics, 0, S(43.5f), static_cast<float>(rc.right), S(43.5f),
                 ui::colors::Divider(), 1.0f);
    ui::DrawLogo(graphics, Gdiplus::RectF(S(16), S(12), S(20), S(20)), S(5));
    ui::DrawText(graphics, fonts_, i18n::Text(i18n::Str::TargetPickerTitle),
                 Gdiplus::RectF(S(44), S(8), static_cast<float>(rc.right) - S(110), S(28)),
                 ui::FontRole::BodyStrong, ui::colors::Text(), ui::Align::Near,
                 ui::VAlign::Center, true);
    if (hot_ == Hot::Close) {
        ui::FillRoundRect(graphics,
                          Gdiplus::RectF(S(4) + static_cast<float>(rc.right) - S(44), S(8),
                                         S(36), S(28)),
                          S(6), ui::colors::HoverTint());
    }
    ui::DrawIcon(graphics, ui::Icon::Close,
                 Gdiplus::RectF(static_cast<float>(rc.right) - S(34), S(15), S(14), S(14)),
                 ui::colors::TextSecondary(), S(1.3f));
}

void WindowPicker::DrawFooter(Gdiplus::Graphics& graphics) {
    const auto S = [&](float value) { return ui::ScaleF(value, dpi_); };
    const Gdiplus::RectF select(static_cast<float>(selectRect_.left),
                                static_cast<float>(selectRect_.top),
                                static_cast<float>(selectRect_.right - selectRect_.left),
                                static_cast<float>(selectRect_.bottom - selectRect_.top));
    const Gdiplus::RectF cancel(static_cast<float>(cancelRect_.left),
                                static_cast<float>(cancelRect_.top),
                                static_cast<float>(cancelRect_.right - cancelRect_.left),
                                static_cast<float>(cancelRect_.bottom - cancelRect_.top));
    const bool canSelect = !filtered_.empty();
    ui::DrawButton(graphics, fonts_, select, i18n::Text(i18n::Str::TargetPickerSelect),
                   ui::ButtonStyle::Primary, hot_ == Hot::Select, false, canSelect,
                   ui::Icon::Check);
    ui::DrawButton(graphics, fonts_, cancel, i18n::Text(i18n::Str::TargetPickerCancel),
                   ui::ButtonStyle::Secondary, hot_ == Hot::Cancel, false, true);
    (void)S;
}

} // namespace vsr::app

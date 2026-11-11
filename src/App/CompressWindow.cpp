#include "CompressWindow.h"

#include <windowsx.h>

#include <dwmapi.h>
#include <shellapi.h>
#include <shobjidl.h>

#include <algorithm>
#include <cmath>
#include <cwchar>
#include <filesystem>
#include <utility>
#include <vector>

#include <Infrastructure/Log.h>
#include <Infrastructure/Strings.h>

#include "AppMenu.h"
#include "Localization.h"

#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "dwmapi.lib")

namespace vsr::app {

namespace {

constexpr wchar_t kCompressClassName[] = L"VsrCompressWindow";
constexpr int kAppIconResource = 1;
constexpr UINT kCmdQualityLow = 4101;
constexpr UINT kCmdQualityStandard = 4102;
constexpr UINT kCmdQualityHigh = 4103;
constexpr UINT kCmdResolutionKeep = 4111;
constexpr UINT kCmdResolution720 = 4112;
constexpr UINT kCmdResolution1080 = 4113;
constexpr float kRowHeight = 64.0f;
constexpr UINT kPollMs = 250;

bool Contains(const Gdiplus::RectF& rect, POINT point) {
    return static_cast<float>(point.x) >= rect.X &&
           static_cast<float>(point.x) < rect.X + rect.Width &&
           static_cast<float>(point.y) >= rect.Y &&
           static_cast<float>(point.y) < rect.Y + rect.Height;
}

bool CursorIn(HWND hwnd, const Gdiplus::RectF& rect) {
    POINT point = {};
    if (!GetCursorPos(&point)) return false;
    ScreenToClient(hwnd, &point);
    return Contains(rect, point);
}

std::wstring BytesText(std::uint64_t bytes) {
    if (bytes == 0) return i18n::Text(i18n::Str::NotAvailable);
    return i18n::FormatBytes(bytes);
}

std::wstring FileNameText(const std::wstring& path) {
    return std::filesystem::path(path).filename().wstring();
}

bool HasQueued(const std::vector<transcode::JobSnapshot>& jobs) {
    return std::any_of(jobs.begin(), jobs.end(), [](const transcode::JobSnapshot& job) {
        return job.state == transcode::JobState::Queued;
    });
}

bool HasFailed(const std::vector<transcode::JobSnapshot>& jobs) {
    return std::any_of(jobs.begin(), jobs.end(), [](const transcode::JobSnapshot& job) {
        return job.state == transcode::JobState::Failed;
    });
}

bool AllCompleted(const std::vector<transcode::JobSnapshot>& jobs) {
    return !jobs.empty() &&
           std::all_of(jobs.begin(), jobs.end(), [](const transcode::JobSnapshot& job) {
               return job.state == transcode::JobState::Completed;
           });
}

} // namespace

CompressWindow::CompressWindow(HWND owner, std::function<bool()> recorderBusy)
    : owner_(owner), recorderBusy_(std::move(recorderBusy)) {}

CompressWindow::~CompressWindow() {
    service_.Shutdown();
    if (memBmp_ != nullptr) DeleteObject(memBmp_);
    if (memDc_ != nullptr) DeleteDC(memDc_);
    memBmp_ = nullptr;
    memDc_ = nullptr;
}

void CompressWindow::Show() {
    if (!CreateWindowIfNeeded()) return;
    ShowWindow(hwnd_, SW_SHOWNORMAL);
    SetForegroundWindow(hwnd_);
    ::SetFocus(hwnd_);
    Poll();
}

void CompressWindow::Invalidate() {
    if (hwnd_ != nullptr) InvalidateRect(hwnd_, nullptr, FALSE);
}

void CompressWindow::OnLanguageChanged() {
    if (hwnd_ == nullptr) return;
    fonts_.Create(dpi_, i18n::CurrentLanguage());
    UpdateLayout();
    RebuildRows();
    Invalidate();
}

void CompressWindow::QueueFiles(const std::vector<std::wstring>& files) {
    AddFiles(files);
}

bool CompressWindow::CreateWindowIfNeeded() {
    if (hwnd_ != nullptr) return true;
    HINSTANCE instance = GetModuleHandleW(nullptr);
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.style = CS_DBLCLKS;
    wc.lpfnWndProc = &CompressWindow::WndProcThunk;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = kCompressClassName;
    wc.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(kAppIconResource));
    RegisterClassExW(&wc);

    dpi_ = owner_ != nullptr ? GetDpiForWindow(owner_) : GetDpiForSystem();
    if (dpi_ == 0) dpi_ = 96;
    fonts_.Create(dpi_, i18n::CurrentLanguage());

    MONITORINFO info = {};
    info.cbSize = sizeof(info);
    GetMonitorInfoW(MonitorFromWindow(owner_, MONITOR_DEFAULTTONEAREST), &info);
    const int workWidth = info.rcWork.right - info.rcWork.left;
    const int workHeight = info.rcWork.bottom - info.rcWork.top;
    const int width = std::min(ui::Scale(780, dpi_), workWidth - ui::Scale(32, dpi_));
    const int height = std::min(ui::Scale(680, dpi_), workHeight - ui::Scale(32, dpi_));
    const int x = info.rcWork.left + (workWidth - width) / 2;
    const int y = info.rcWork.top + (workHeight - height) / 2;

    const std::wstring title = std::wstring(i18n::Text(i18n::Str::AppName)) + L" - " +
                               i18n::Text(i18n::Str::CompressTitle);
    hwnd_ = CreateWindowExW(WS_EX_TOOLWINDOW, kCompressClassName, title.c_str(),
                            WS_POPUP | WS_CLIPCHILDREN, x, y, width, height, owner_,
                            nullptr, instance, this);
    if (hwnd_ == nullptr) {
        log::Error("UI", "COMPRESS_WINDOW_FAILED", L"",
                   L"CreateWindowExW failed for the compress window.",
                   {{"error", std::to_string(GetLastError())}});
        return false;
    }
    SetWindowLongPtrW(hwnd_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
    DWORD corner = DWMWCP_ROUND;
    DwmSetWindowAttribute(hwnd_, DWMWA_WINDOW_CORNER_PREFERENCE, &corner, sizeof(corner));
    DragAcceptFiles(hwnd_, TRUE);
    UpdateLayout();
    RebuildRows();
    SetTimer(hwnd_, timerId_, kPollMs, nullptr);
    return true;
}

void CompressWindow::OnDestroy() {
    if (hwnd_ != nullptr) {
        KillTimer(hwnd_, timerId_);
        DragAcceptFiles(hwnd_, FALSE);
    }
    if (memBmp_ != nullptr) {
        DeleteObject(memBmp_);
        memBmp_ = nullptr;
    }
    if (memDc_ != nullptr) {
        DeleteDC(memDc_);
        memDc_ = nullptr;
    }
    memWidth_ = 0;
    memHeight_ = 0;
    fonts_.Destroy();
    hwnd_ = nullptr;
    hover_ = Hit::None;
    pressed_ = Hit::None;
    focus_ = Hit::None;
    jobs_.clear();
    rows_.clear();
    hits_.clear();
    lastSignature_.clear();
}

void CompressWindow::OnDpiChanged(UINT dpi) {
    dpi_ = dpi;
    fonts_.Create(dpi_, i18n::CurrentLanguage());
    UpdateLayout();
    RebuildRows();
    Invalidate();
}

LRESULT CALLBACK CompressWindow::WndProcThunk(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
    }
    auto* self = reinterpret_cast<CompressWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    return self != nullptr ? self->WndProc(hwnd, msg, wp, lp)
                           : DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT CompressWindow::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_NCCALCSIZE:
        case WM_NCPAINT:
            return 0;
        case WM_NCACTIVATE:
            return TRUE;
        case WM_NCHITTEST: {
            POINT screen = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            POINT client = screen;
            ScreenToClient(hwnd, &client);
            if (Contains(layout_.titleBar, client) &&
                !Contains(layout_.minimize, client) && !Contains(layout_.close, client)) {
                return HTCAPTION;
            }
            return HTCLIENT;
        }
        case WM_GETMINMAXINFO: {
            auto* info = reinterpret_cast<MINMAXINFO*>(lp);
            if (info != nullptr) {
                info->ptMinTrackSize.x = ui::Scale(720, dpi_);
                info->ptMinTrackSize.y = ui::Scale(600, dpi_);
            }
            return 0;
        }
        case WM_DPICHANGED: {
            auto* suggested = reinterpret_cast<RECT*>(lp);
            SetWindowPos(hwnd, nullptr, suggested->left, suggested->top,
                         suggested->right - suggested->left,
                         suggested->bottom - suggested->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            OnDpiChanged(HIWORD(wp));
            return 0;
        }
        case WM_SIZE:
            UpdateLayout();
            RebuildRows();
            Invalidate();
            return 0;
        case WM_ERASEBKGND:
            return 1;
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
        case WM_TIMER:
            if (wp == timerId_) Poll();
            return 0;
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
            const Hit hit = HitTest(point);
            if (hit != hover_) {
                hover_ = hit;
                Invalidate();
            } else if (Contains(layout_.list, point) || !rows_.empty()) {
                Invalidate();
            }
            SetCursor(LoadCursorW(nullptr, (hit != Hit::None || Contains(layout_.list, point))
                                           ? IDC_HAND
                                           : IDC_ARROW));
            return 0;
        }
        case WM_MOUSELEAVE:
            trackingMouse_ = false;
            if (hover_ != Hit::None) {
                hover_ = Hit::None;
                Invalidate();
            }
            return 0;
        case WM_LBUTTONDOWN: {
            ::SetFocus(hwnd);
            POINT point = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            const Hit hit = HitTest(point);
            if (hit != Hit::None) {
                pressed_ = hit;
                FocusControl(hit, false);
                SetCapture(hwnd);
                Invalidate();
            } else {
                keyboardFocus_ = false;
                focus_ = Hit::None;
                Invalidate();
            }
            return 0;
        }
        case WM_LBUTTONUP: {
            const Hit pressed = pressed_;
            pressed_ = Hit::None;
            if (GetCapture() == hwnd) ReleaseCapture();
            POINT point = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            for (const RowLayout& row : rows_) {
                if (Contains(row.action, point)) {
                    if (const transcode::JobSnapshot* job = FindJob(row.id)) {
                        if (row.canOpen || row.canRemove) RowAction(*job);
                    }
                    return 0;
                }
            }
            if (pressed != Hit::None && HitTest(point) == pressed) Activate(pressed);
            Invalidate();
            return 0;
        }
        case WM_CAPTURECHANGED:
            pressed_ = Hit::None;
            Invalidate();
            return 0;
        case WM_LBUTTONDBLCLK: {
            POINT point = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            for (const RowLayout& row : rows_) {
                if (Contains(row.rect, point)) {
                    if (const transcode::JobSnapshot* job = FindJob(row.id)) {
                        if (job->state == transcode::JobState::Completed) {
                            OpenOutput(*job, true);
                        }
                    }
                    break;
                }
            }
            return 0;
        }
        case WM_MOUSEWHEEL:
            OnWheel(GET_WHEEL_DELTA_WPARAM(wp));
            return 0;
        case WM_KEYDOWN: {
            const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
            if (wp == VK_TAB) {
                CycleFocus(shift);
                return 0;
            }
            if (wp == VK_ESCAPE) {
                if (keyboardFocus_ && focus_ != Hit::None) {
                    focus_ = Hit::None;
                    keyboardFocus_ = false;
                    Invalidate();
                } else {
                    RequestClose();
                }
                return 0;
            }
            if ((wp == VK_RETURN || wp == VK_SPACE) && focus_ != Hit::None) {
                if (IsHitEnabled(focus_)) Activate(focus_);
                return 0;
            }
            break;
        }
        case WM_DROPFILES: {
            HDROP drop = reinterpret_cast<HDROP>(wp);
            const UINT count = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
            std::vector<std::wstring> files;
            files.reserve(count);
            for (UINT i = 0; i < count; ++i) {
                const UINT length = DragQueryFileW(drop, i, nullptr, 0);
                std::vector<wchar_t> buffer(static_cast<std::size_t>(length) + 1, L'\0');
                if (DragQueryFileW(drop, i, buffer.data(),
                                   static_cast<UINT>(buffer.size())) > 0) {
                    files.emplace_back(buffer.data());
                }
            }
            DragFinish(drop);
            AddFiles(files);
            SetForegroundWindow(hwnd);
            return 0;
        }
        case WM_MEASUREITEM:
            if (menu::HandleMeasureItem(reinterpret_cast<MEASUREITEMSTRUCT*>(lp), fonts_)) {
                return TRUE;
            }
            break;
        case WM_DRAWITEM:
            if (menu::HandleDrawItem(reinterpret_cast<DRAWITEMSTRUCT*>(lp), fonts_)) {
                return TRUE;
            }
            break;
        case WM_CLOSE:
            RequestClose();
            return 0;
        case WM_DESTROY:
            OnDestroy();
            return 0;
        default:
            break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

void CompressWindow::UpdateLayout() {
    RECT rc = {};
    GetClientRect(hwnd_, &rc);
    const float width = static_cast<float>(rc.right - rc.left);
    const float height = static_cast<float>(rc.bottom - rc.top);
    const float pad = ui::ScaleF(20.0f, dpi_);
    const float titleHeight = ui::ScaleF(44.0f, dpi_);

    layout_.titleBar = Gdiplus::RectF(0.0f, 0.0f, width, titleHeight);
    layout_.title = Gdiplus::RectF(pad + ui::ScaleF(32.0f, dpi_), 0.0f,
                                   width * 0.5f, titleHeight);
    layout_.minimize = Gdiplus::RectF(width - ui::ScaleF(88.0f, dpi_),
                                      ui::ScaleF(8.0f, dpi_), ui::ScaleF(32.0f, dpi_),
                                      ui::ScaleF(28.0f, dpi_));
    layout_.close = Gdiplus::RectF(width - ui::ScaleF(52.0f, dpi_),
                                   ui::ScaleF(8.0f, dpi_), ui::ScaleF(32.0f, dpi_),
                                   ui::ScaleF(28.0f, dpi_));

    const float headerY = titleHeight + ui::ScaleF(14.0f, dpi_);
    layout_.add = Gdiplus::RectF(pad, headerY, ui::ScaleF(132.0f, dpi_),
                                 ui::ScaleF(36.0f, dpi_));
    layout_.clear = Gdiplus::RectF(width - pad - ui::ScaleF(96.0f, dpi_), headerY,
                                   ui::ScaleF(96.0f, dpi_), ui::ScaleF(36.0f, dpi_));

    const float footerHeight = ui::ScaleF(84.0f, dpi_);
    const float settingsHeight = ui::ScaleF(148.0f, dpi_);
    const float listTop = headerY + ui::ScaleF(36.0f, dpi_) + ui::ScaleF(14.0f, dpi_);
    const float listBottom = height - footerHeight - settingsHeight - ui::ScaleF(10.0f, dpi_);
    layout_.list = Gdiplus::RectF(pad, listTop, width - pad * 2.0f,
                                  std::max(ui::ScaleF(120.0f, dpi_),
                                           listBottom - listTop));

    const float settingsY = layout_.list.GetBottom() + ui::ScaleF(14.0f, dpi_);
    const float fieldHeight = ui::ScaleF(36.0f, dpi_);
    layout_.qualityLabel = Gdiplus::RectF(pad, settingsY + ui::ScaleF(9.0f, dpi_),
                                          ui::ScaleF(56.0f, dpi_), ui::ScaleF(18.0f, dpi_));
    layout_.qualityField = Gdiplus::RectF(pad + ui::ScaleF(62.0f, dpi_), settingsY,
                                          ui::ScaleF(178.0f, dpi_), fieldHeight);
    const float resolutionX = layout_.qualityField.GetRight() + ui::ScaleF(18.0f, dpi_);
    layout_.resolutionLabel = Gdiplus::RectF(resolutionX,
                                             settingsY + ui::ScaleF(9.0f, dpi_),
                                             ui::ScaleF(60.0f, dpi_),
                                             ui::ScaleF(18.0f, dpi_));
    layout_.resolutionField = Gdiplus::RectF(resolutionX + ui::ScaleF(66.0f, dpi_),
                                             settingsY, ui::ScaleF(178.0f, dpi_),
                                             fieldHeight);

    const float secondRowY = settingsY + ui::ScaleF(50.0f, dpi_);
    const float outputX = pad + ui::ScaleF(62.0f, dpi_);
    const float chooseWidth = ui::ScaleF(84.0f, dpi_);
    const float resetWidth = ui::ScaleF(70.0f, dpi_);
    const float outputRight = width - pad - chooseWidth - resetWidth -
                              ui::ScaleF(16.0f, dpi_);
    layout_.outputLabel = Gdiplus::RectF(pad, secondRowY + ui::ScaleF(9.0f, dpi_),
                                         ui::ScaleF(56.0f, dpi_), ui::ScaleF(18.0f, dpi_));
    layout_.outputField = Gdiplus::RectF(outputX, secondRowY,
                                         std::max(ui::ScaleF(120.0f, dpi_),
                                                  outputRight - outputX),
                                         fieldHeight);
    layout_.outputChoose = Gdiplus::RectF(layout_.outputField.GetRight() +
                                              ui::ScaleF(8.0f, dpi_),
                                          secondRowY, chooseWidth, fieldHeight);
    layout_.outputReset = Gdiplus::RectF(layout_.outputChoose.GetRight() +
                                             ui::ScaleF(8.0f, dpi_),
                                         secondRowY, resetWidth, fieldHeight);
    layout_.estimate = Gdiplus::RectF(pad, secondRowY + ui::ScaleF(46.0f, dpi_),
                                      width - pad * 2.0f, ui::ScaleF(20.0f, dpi_));

    const float footerTop = height - footerHeight;
    layout_.footer = Gdiplus::RectF(0.0f, footerTop, width, footerHeight);
    layout_.startButton = Gdiplus::RectF(width - pad - ui::ScaleF(140.0f, dpi_),
                                         footerTop + ui::ScaleF(22.0f, dpi_),
                                         ui::ScaleF(140.0f, dpi_),
                                         ui::ScaleF(40.0f, dpi_));
    layout_.cancelButton = Gdiplus::RectF(
        layout_.startButton.X - ui::ScaleF(112.0f, dpi_),
        layout_.startButton.Y, ui::ScaleF(104.0f, dpi_), ui::ScaleF(40.0f, dpi_));
    layout_.status = Gdiplus::RectF(pad, footerTop + ui::ScaleF(24.0f, dpi_),
                                    std::max(ui::ScaleF(80.0f, dpi_),
                                             layout_.cancelButton.X - pad -
                                                 ui::ScaleF(16.0f, dpi_)),
                                    ui::ScaleF(36.0f, dpi_));

    hits_.clear();
    hits_.push_back({Hit::Minimize, layout_.minimize});
    hits_.push_back({Hit::Close, layout_.close});
    hits_.push_back({Hit::Add, layout_.add});
    hits_.push_back({Hit::Clear, layout_.clear});
    hits_.push_back({Hit::Quality, layout_.qualityField});
    hits_.push_back({Hit::Resolution, layout_.resolutionField});
    hits_.push_back({Hit::OutputChoose, layout_.outputChoose});
    hits_.push_back({Hit::OutputReset, layout_.outputReset});
    hits_.push_back({Hit::Start, layout_.startButton});
    hits_.push_back({Hit::Cancel, layout_.cancelButton});
}

void CompressWindow::RebuildRows() {
    rows_.clear();
    if (hwnd_ == nullptr) return;
    const float rowHeight = ui::ScaleF(kRowHeight, dpi_);
    const float contentHeight = static_cast<float>(jobs_.size()) * rowHeight;
    const float maxScroll = std::max(0.0f, contentHeight - layout_.list.Height);
    scrollOffset_ = std::clamp(scrollOffset_, 0.0f, maxScroll);
    rows_.reserve(jobs_.size());
    for (std::size_t i = 0; i < jobs_.size(); ++i) {
        const transcode::JobSnapshot& job = jobs_[i];
        RowLayout row;
        row.id = job.id;
        row.rect = Gdiplus::RectF(layout_.list.X,
                                  layout_.list.Y + static_cast<float>(i) * rowHeight -
                                      scrollOffset_,
                                  layout_.list.Width, rowHeight);
        const bool busy = job.state == transcode::JobState::Running ||
                          job.state == transcode::JobState::Probing;
        if (job.state == transcode::JobState::Completed) {
            row.canOpen = true;
            row.action = Gdiplus::RectF(row.rect.GetRight() - ui::ScaleF(110.0f, dpi_),
                                        row.rect.Y + ui::ScaleF(18.0f, dpi_),
                                        ui::ScaleF(94.0f, dpi_),
                                        ui::ScaleF(28.0f, dpi_));
        } else {
            row.canRemove = !busy;
            row.action = Gdiplus::RectF(row.rect.GetRight() - ui::ScaleF(40.0f, dpi_),
                                        row.rect.Y + ui::ScaleF(18.0f, dpi_),
                                        ui::ScaleF(24.0f, dpi_),
                                        ui::ScaleF(24.0f, dpi_));
        }
        rows_.push_back(row);
    }
}

void CompressWindow::Paint(HDC targetDc) {
    RECT rc = {};
    GetClientRect(hwnd_, &rc);
    const int width = rc.right - rc.left;
    const int height = rc.bottom - rc.top;
    if (width <= 0 || height <= 0) return;

    if (memDc_ == nullptr) memDc_ = CreateCompatibleDC(targetDc);
    if (memBmp_ == nullptr || memWidth_ != width || memHeight_ != height) {
        if (memBmp_ != nullptr) DeleteObject(memBmp_);
        memBmp_ = CreateCompatibleBitmap(targetDc, width, height);
        memWidth_ = width;
        memHeight_ = height;
    }
    if (memDc_ == nullptr || memBmp_ == nullptr) return;

    HGDIOBJ old = SelectObject(memDc_, memBmp_);
    {
        Gdiplus::Graphics graphics(memDc_);
        Draw(graphics, width, height);
    }
    BitBlt(targetDc, 0, 0, width, height, memDc_, 0, 0, SRCCOPY);
    SelectObject(memDc_, old);
}

void CompressWindow::Draw(Gdiplus::Graphics& graphics, int width, int height) {
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    graphics.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);
    graphics.Clear(ui::colors::Window());
    (void)width;
    (void)height;
    DrawTitleBar(graphics);
    DrawHeader(graphics);
    DrawList(graphics);
    DrawSettings(graphics);
    DrawFooter(graphics);

    if (keyboardFocus_ && focus_ != Hit::None) {
        for (const HitTarget& target : hits_) {
            if (target.id == focus_) {
                DrawFocusRing(graphics, target.rect);
                break;
            }
        }
    }
}

void CompressWindow::DrawTitleBar(Gdiplus::Graphics& graphics) {
    ui::FillRect(graphics, layout_.titleBar, ui::colors::TitleBar());
    ui::DrawLine(graphics, 0.0f, layout_.titleBar.GetBottom() - 1.0f,
                 layout_.titleBar.GetRight(), layout_.titleBar.GetBottom() - 1.0f,
                 ui::colors::Border());

    const float logoSize = ui::ScaleF(22.0f, dpi_);
    ui::DrawLogo(graphics,
                 Gdiplus::RectF(ui::ScaleF(20.0f, dpi_),
                                (layout_.titleBar.Height - logoSize) / 2.0f, logoSize,
                                logoSize),
                 ui::ScaleF(6.0f, dpi_));
    ui::DrawText(graphics, fonts_, i18n::Text(i18n::Str::CompressTitle),
                 Gdiplus::RectF(ui::ScaleF(52.0f, dpi_), 0.0f,
                                layout_.title.Width, layout_.title.Height),
                 ui::FontRole::BodyStrong, ui::colors::Text(), ui::Align::Near,
                 ui::VAlign::Center, true);

    const auto drawChrome = [&](const Gdiplus::RectF& rect, ui::Icon icon,
                                bool hover) {
        if (hover) {
            ui::FillRoundRect(graphics, rect, ui::ScaleF(6.0f, dpi_),
                              ui::colors::HoverTint());
        }
        const float size = ui::ScaleF(14.0f, dpi_);
        ui::DrawIcon(graphics, icon,
                     Gdiplus::RectF(rect.X + (rect.Width - size) / 2.0f,
                                    rect.Y + (rect.Height - size) / 2.0f, size, size),
                     hover ? ui::colors::Text() : ui::colors::TextSecondary(),
                     ui::ScaleF(1.6f, dpi_));
    };
    drawChrome(layout_.minimize, ui::Icon::Minimize, hover_ == Hit::Minimize);
    drawChrome(layout_.close, ui::Icon::Close, hover_ == Hit::Close);
}

void CompressWindow::DrawHeader(Gdiplus::Graphics& graphics) {
    ui::DrawButton(graphics, fonts_, layout_.add, i18n::Text(i18n::Str::CompressAdd),
                   ui::ButtonStyle::Secondary, hover_ == Hit::Add, pressed_ == Hit::Add,
                   true, ui::Icon::Video);
    ui::DrawButton(graphics, fonts_, layout_.clear,
                   i18n::Text(i18n::Str::CompressClear), ui::ButtonStyle::Ghost,
                   hover_ == Hit::Clear, pressed_ == Hit::Clear, !jobs_.empty());
}

void CompressWindow::DrawList(Gdiplus::Graphics& graphics) {
    ui::FillRoundRectWithBorder(graphics, layout_.list, ui::ScaleF(10.0f, dpi_),
                                ui::colors::Surface(), ui::colors::Border());
    if (jobs_.empty()) {
        DrawEmptyState(graphics);
        return;
    }

    graphics.SetClip(Gdiplus::RectF(layout_.list.X + 1.0f, layout_.list.Y + 1.0f,
                                    layout_.list.Width - 2.0f,
                                    layout_.list.Height - 2.0f));
    for (const RowLayout& row : rows_) {
        if (row.rect.GetBottom() < layout_.list.Y ||
            row.rect.Y > layout_.list.GetBottom()) {
            continue;
        }
        const transcode::JobSnapshot* job = FindJob(row.id);
        if (job == nullptr) continue;
        const bool hovered = Contains(row.rect, [&] {
            POINT point = {};
            GetCursorPos(&point);
            ScreenToClient(hwnd_, &point);
            return point;
        }());
        DrawRow(graphics, *job, row, hovered);
    }
    graphics.ResetClip();

    const float rowHeight = ui::ScaleF(kRowHeight, dpi_);
    const float contentHeight = static_cast<float>(jobs_.size()) * rowHeight;
    if (contentHeight > layout_.list.Height) {
        const float trackX = layout_.list.GetRight() - ui::ScaleF(7.0f, dpi_);
        const float trackTop = layout_.list.Y + ui::ScaleF(8.0f, dpi_);
        const float trackHeight = layout_.list.Height - ui::ScaleF(16.0f, dpi_);
        const float maxScroll = contentHeight - layout_.list.Height;
        const float thumbHeight = std::max(ui::ScaleF(24.0f, dpi_),
                                           trackHeight * layout_.list.Height /
                                               contentHeight);
        const float thumbTop = trackTop +
                               (trackHeight - thumbHeight) *
                                   (maxScroll > 0.0f ? scrollOffset_ / maxScroll : 0.0f);
        ui::FillRoundRect(graphics,
                          Gdiplus::RectF(trackX, thumbTop,
                                         ui::ScaleF(3.0f, dpi_), thumbHeight),
                          ui::ScaleF(1.5f, dpi_), ui::colors::BorderStrong());
    }
}

void CompressWindow::DrawEmptyState(Gdiplus::Graphics& graphics) {
    const float listCenterX = layout_.list.X + layout_.list.Width / 2.0f;
    const float listCenterY = layout_.list.Y + layout_.list.Height / 2.0f;
    const float iconSize = ui::ScaleF(40.0f, dpi_);
    ui::DrawIcon(graphics, ui::Icon::Video,
                 Gdiplus::RectF(listCenterX - iconSize / 2.0f,
                                listCenterY - ui::ScaleF(64.0f, dpi_), iconSize,
                                iconSize),
                 ui::colors::TextTertiary(), ui::ScaleF(2.0f, dpi_));
    ui::DrawText(graphics, fonts_, i18n::Text(i18n::Str::CompressEmptyTitle),
                 Gdiplus::RectF(layout_.list.X, listCenterY - ui::ScaleF(14.0f, dpi_),
                                layout_.list.Width, ui::ScaleF(24.0f, dpi_)),
                 ui::FontRole::BodyStrong, ui::colors::Text(), ui::Align::Center,
                 ui::VAlign::Center, true);
    ui::DrawTextWrapped(graphics, fonts_, i18n::Text(i18n::Str::CompressEmptyHint),
                        Gdiplus::RectF(layout_.list.X + ui::ScaleF(60.0f, dpi_),
                                       listCenterY + ui::ScaleF(18.0f, dpi_),
                                       layout_.list.Width - ui::ScaleF(120.0f, dpi_),
                                       ui::ScaleF(48.0f, dpi_)),
                        ui::FontRole::Small, ui::colors::TextSecondary(),
                        ui::Align::Center);
}

void CompressWindow::DrawRow(Gdiplus::Graphics& graphics,
                             const transcode::JobSnapshot& job, const RowLayout& row,
                             bool hovered) {
    if (hovered) {
        ui::FillRect(graphics, row.rect, ui::colors::HoverTint());
    }

    const float iconSize = ui::ScaleF(38.0f, dpi_);
    const Gdiplus::RectF iconRect(row.rect.X + ui::ScaleF(14.0f, dpi_),
                                  row.rect.Y + (row.rect.Height - iconSize) / 2.0f,
                                  iconSize, iconSize);
    ui::FillRoundRect(graphics, iconRect, ui::ScaleF(9.0f, dpi_),
                      ui::colors::PrimarySoft());
    const float glyph = ui::ScaleF(18.0f, dpi_);
    ui::DrawIcon(graphics, ui::Icon::Video,
                 Gdiplus::RectF(iconRect.X + (iconSize - glyph) / 2.0f,
                                iconRect.Y + (iconSize - glyph) / 2.0f, glyph, glyph),
                 ui::colors::Primary(), ui::ScaleF(1.8f, dpi_));

    const float textLeft = row.rect.X + ui::ScaleF(64.0f, dpi_);
    const float textRight = row.action.X - ui::ScaleF(12.0f, dpi_);
    const float nameWidth = std::max(ui::ScaleF(60.0f, dpi_),
                                     textRight - textLeft - ui::ScaleF(150.0f, dpi_));
    ui::DrawText(graphics, fonts_, FileNameText(job.inputPath),
                 Gdiplus::RectF(textLeft, row.rect.Y + ui::ScaleF(10.0f, dpi_),
                                nameWidth, ui::ScaleF(20.0f, dpi_)),
                 ui::FontRole::BodyStrong, ui::colors::Text(), ui::Align::Near,
                 ui::VAlign::Center, true);

    Gdiplus::Color subColor = ui::colors::TextSecondary();
    if (job.state == transcode::JobState::Failed) subColor = ui::colors::DangerText();
    ui::DrawText(graphics, fonts_, RowSubtitle(job),
                 Gdiplus::RectF(textLeft, row.rect.Y + ui::ScaleF(32.0f, dpi_),
                                std::max(ui::ScaleF(80.0f, dpi_),
                                         textRight - textLeft),
                                ui::ScaleF(18.0f, dpi_)),
                 ui::FontRole::Small, subColor, ui::Align::Near, ui::VAlign::Center,
                 true);

    Gdiplus::Color statusColor = ui::colors::TextSecondary();
    if (job.state == transcode::JobState::Completed) statusColor = ui::colors::Success();
    else if (job.state == transcode::JobState::Failed) statusColor = ui::colors::DangerText();
    else if (job.state == transcode::JobState::Running) statusColor = ui::colors::Primary();
    const float statusRight = row.canOpen ? row.action.X - ui::ScaleF(8.0f, dpi_)
                                          : row.rect.GetRight() - ui::ScaleF(44.0f, dpi_);
    ui::DrawText(graphics, fonts_, RowStatus(job),
                 Gdiplus::RectF(statusRight - ui::ScaleF(150.0f, dpi_),
                                row.rect.Y + ui::ScaleF(10.0f, dpi_),
                                ui::ScaleF(150.0f, dpi_), ui::ScaleF(20.0f, dpi_)),
                 ui::FontRole::SmallStrong, statusColor, ui::Align::Far,
                 ui::VAlign::Center, true);

    if (job.state == transcode::JobState::Running) {
        const float barLeft = textLeft;
        const float barRight = row.rect.GetRight() - ui::ScaleF(16.0f, dpi_);
        const float barY = row.rect.GetBottom() - ui::ScaleF(11.0f, dpi_);
        const float barWidth = std::max(ui::ScaleF(40.0f, dpi_), barRight - barLeft);
        ui::FillRoundRect(graphics,
                          Gdiplus::RectF(barLeft, barY, barWidth,
                                         ui::ScaleF(4.0f, dpi_)),
                          ui::ScaleF(2.0f, dpi_), ui::colors::Border());
        if (job.progress > 0.0) {
            const float fill = barWidth * static_cast<float>(job.progress);
            ui::FillRoundRect(graphics,
                              Gdiplus::RectF(barLeft, barY,
                                             std::max(ui::ScaleF(4.0f, dpi_), fill),
                                             ui::ScaleF(4.0f, dpi_)),
                              ui::ScaleF(2.0f, dpi_), ui::colors::Primary());
        }
    }

    if (row.canOpen) {
        ui::DrawButton(graphics, fonts_, row.action,
                       i18n::Text(i18n::Str::CompressOpenFile),
                       ui::ButtonStyle::Ghost, CursorIn(hwnd_, row.action), false, true,
                       ui::Icon::File);
    } else {
        if (CursorIn(hwnd_, row.action) && row.canRemove) {
            ui::FillRoundRect(graphics, row.action, ui::ScaleF(6.0f, dpi_),
                              ui::colors::HoverTint());
        }
        const float size = ui::ScaleF(12.0f, dpi_);
        ui::DrawIcon(graphics, ui::Icon::Close,
                     Gdiplus::RectF(row.action.X + (row.action.Width - size) / 2.0f,
                                    row.action.Y + (row.action.Height - size) / 2.0f,
                                    size, size),
                     row.canRemove ? ui::colors::TextSecondary()
                                   : ui::colors::TextTertiary(),
                     ui::ScaleF(1.6f, dpi_));
    }

    if (&job != &jobs_.back()) {
        const float dividerY = row.rect.GetBottom() - 0.5f;
        ui::DrawLine(graphics, row.rect.X + ui::ScaleF(14.0f, dpi_), dividerY,
                     row.rect.GetRight() - ui::ScaleF(14.0f, dpi_), dividerY,
                     ui::colors::Divider());
    }
}

void CompressWindow::DrawSettings(Gdiplus::Graphics& graphics) {
    ui::DrawText(graphics, fonts_, i18n::Text(i18n::Str::CompressQuality),
                 layout_.qualityLabel, ui::FontRole::Small,
                 ui::colors::TextSecondary(), ui::Align::Near, ui::VAlign::Center,
                 true);
    DrawField(graphics, layout_.qualityField, QualityLabel(),
              hover_ == Hit::Quality, focus_ == Hit::Quality, ui::Icon::ChevronDown);
    ui::DrawText(graphics, fonts_, i18n::Text(i18n::Str::CompressResolution),
                 layout_.resolutionLabel, ui::FontRole::Small,
                 ui::colors::TextSecondary(), ui::Align::Near, ui::VAlign::Center,
                 true);
    DrawField(graphics, layout_.resolutionField, ResolutionLabel(),
              hover_ == Hit::Resolution, focus_ == Hit::Resolution,
              ui::Icon::ChevronDown);

    ui::DrawText(graphics, fonts_, i18n::Text(i18n::Str::CompressOutput),
                 layout_.outputLabel, ui::FontRole::Small,
                 ui::colors::TextSecondary(), ui::Align::Near, ui::VAlign::Center,
                 true);
    const std::wstring outputText =
        outputDir_.empty() ? i18n::Text(i18n::Str::CompressOutputSame)
                           : ui::CompactPath(outputDir_, 56);
    DrawField(graphics, layout_.outputField, outputText, false, false,
              ui::Icon::Folder);
    ui::DrawButton(graphics, fonts_, layout_.outputChoose,
                   i18n::Text(i18n::Str::CompressOutputChoose),
                   ui::ButtonStyle::Ghost, hover_ == Hit::OutputChoose,
                   pressed_ == Hit::OutputChoose, true);
    ui::DrawButton(graphics, fonts_, layout_.outputReset,
                   i18n::Text(i18n::Str::CompressOutputReset),
                   ui::ButtonStyle::Ghost, hover_ == Hit::OutputReset,
                   pressed_ == Hit::OutputReset, !outputDir_.empty());

    std::uint64_t totalEstimate = 0;
    bool anyEstimate = false;
    bool smallSource = false;
    for (const transcode::JobSnapshot& job : jobs_) {
        if (job.state == transcode::JobState::Completed ||
            job.state == transcode::JobState::Failed) {
            continue;
        }
        if (job.estimatedBytes > 0) {
            totalEstimate += job.estimatedBytes;
            anyEstimate = true;
        }
        if (job.sourceAlreadySmall) smallSource = true;
    }
    std::wstring estimateText;
    ui::FontRole role = ui::FontRole::Small;
    Gdiplus::Color color = ui::colors::TextSecondary();
    if (smallSource && jobs_.size() == 1) {
        estimateText = i18n::Text(i18n::Str::CompressSmallSource);
        color = ui::colors::Warning();
    } else if (anyEstimate) {
        estimateText = i18n::Format(i18n::Str::CompressTotalEstimate,
                                    {std::to_wstring(jobs_.size()),
                                     i18n::FormatBytes(totalEstimate)});
    }
    if (!estimateText.empty()) {
        ui::DrawText(graphics, fonts_, estimateText, layout_.estimate, role, color,
                     ui::Align::Near, ui::VAlign::Center, true);
    }
}

void CompressWindow::DrawFooter(Gdiplus::Graphics& graphics) {
    ui::DrawLine(graphics, 0.0f, layout_.footer.Y, layout_.footer.GetRight(),
                 layout_.footer.Y, ui::colors::Border());

    ui::DrawText(graphics, fonts_, StatusText(), layout_.status,
                 ui::FontRole::Small,
                 toolError_.empty() ? ui::colors::TextSecondary()
                                    : ui::colors::DangerText(),
                 ui::Align::Near, ui::VAlign::Center, true);
    const bool running = running_ || AnyActiveJob();
    if (running) {
        ui::DrawButton(graphics, fonts_, layout_.cancelButton,
                       i18n::Text(i18n::Str::CompressCancel),
                       ui::ButtonStyle::Secondary, hover_ == Hit::Cancel,
                       pressed_ == Hit::Cancel, true);
    }
    const bool startEnabled = StartEnabled();
    ui::DrawButton(graphics, fonts_, layout_.startButton,
                   i18n::Text(i18n::Str::CompressStart),
                   ui::ButtonStyle::Primary, hover_ == Hit::Start,
                   pressed_ == Hit::Start, startEnabled);
}

void CompressWindow::DrawField(Gdiplus::Graphics& graphics,
                               const Gdiplus::RectF& rect, const std::wstring& text,
                               bool hover, bool focused, ui::Icon icon) {
    ui::FillRoundRectWithBorder(graphics, rect, ui::ScaleF(8.0f, dpi_),
                                hover ? ui::colors::ControlHover()
                                      : ui::colors::ControlBg(),
                                focused ? ui::colors::Primary()
                                        : ui::colors::BorderStrong(),
                                focused ? 1.6f : 1.0f);
    const float textRight = rect.GetRight() -
                            (icon != ui::Icon::None ? ui::ScaleF(30.0f, dpi_)
                                                    : ui::ScaleF(12.0f, dpi_));
    ui::DrawText(graphics, fonts_, text,
                 Gdiplus::RectF(rect.X + ui::ScaleF(12.0f, dpi_), rect.Y,
                                std::max(0.0f, textRight - rect.X -
                                                   ui::ScaleF(12.0f, dpi_)),
                                rect.Height),
                 ui::FontRole::Body, ui::colors::Text(), ui::Align::Near,
                 ui::VAlign::Center, true);
    if (icon != ui::Icon::None) {
        const float size = ui::ScaleF(14.0f, dpi_);
        ui::DrawIcon(graphics, icon,
                     Gdiplus::RectF(rect.GetRight() - ui::ScaleF(24.0f, dpi_),
                                    rect.Y + (rect.Height - size) / 2.0f, size, size),
                     ui::colors::TextTertiary(), ui::ScaleF(1.5f, dpi_));
    }
}

void CompressWindow::DrawFocusRing(Gdiplus::Graphics& graphics,
                                   const Gdiplus::RectF& rect) {
    ui::StrokeRoundRect(graphics,
                        Gdiplus::RectF(rect.X - ui::ScaleF(3.0f, dpi_),
                                       rect.Y - ui::ScaleF(3.0f, dpi_),
                                       rect.Width + ui::ScaleF(6.0f, dpi_),
                                       rect.Height + ui::ScaleF(6.0f, dpi_)),
                        ui::ScaleF(10.0f, dpi_), ui::colors::Primary(),
                        ui::ScaleF(1.5f, dpi_));
}

CompressWindow::Hit CompressWindow::HitTest(POINT point) const {
    for (const HitTarget& target : hits_) {
        if (Contains(target.rect, point)) return target.id;
    }
    return Hit::None;
}

void CompressWindow::FocusControl(Hit hit, bool keyboard) {
    focus_ = hit;
    keyboardFocus_ = keyboard;
    Invalidate();
}

void CompressWindow::CycleFocus(bool backwards) {
    static const Hit order[] = {Hit::Add,        Hit::Clear,       Hit::Quality,
                                Hit::Resolution, Hit::OutputChoose, Hit::OutputReset,
                                Hit::Start,      Hit::Cancel};
    constexpr std::size_t count = sizeof(order) / sizeof(order[0]);
    std::size_t start = 0;
    for (std::size_t i = 0; i < count; ++i) {
        if (order[i] == focus_) {
            start = i;
            break;
        }
    }
    for (std::size_t step = 1; step <= count; ++step) {
        const std::size_t index =
            backwards ? (start + count - step) % count : (start + step) % count;
        if (IsHitEnabled(order[index])) {
            FocusControl(order[index], true);
            return;
        }
    }
}

bool CompressWindow::IsHitEnabled(Hit hit) const {
    switch (hit) {
        case Hit::Add:
        case Hit::Quality:
        case Hit::Resolution:
        case Hit::OutputChoose:
        case Hit::Close:
        case Hit::Minimize:
            return true;
        case Hit::Clear:
            return !jobs_.empty();
        case Hit::OutputReset:
            return !outputDir_.empty();
        case Hit::Start:
            return StartEnabled();
        case Hit::Cancel:
            return running_ || AnyActiveJob();
        case Hit::None:
            return false;
    }
    return false;
}

void CompressWindow::Activate(Hit hit) {
    switch (hit) {
        case Hit::Close:
            RequestClose();
            break;
        case Hit::Minimize:
            ShowWindow(hwnd_, SW_MINIMIZE);
            break;
        case Hit::Add:
            AddFromDialog();
            break;
        case Hit::Clear:
            for (const transcode::JobSnapshot& job : jobs_) {
                if (job.state != transcode::JobState::Running &&
                    job.state != transcode::JobState::Probing) {
                    service_.Remove(job.id);
                }
            }
            RefreshSnapshot();
            break;
        case Hit::Quality:
            ShowQualityMenu();
            break;
        case Hit::Resolution:
            ShowResolutionMenu();
            break;
        case Hit::OutputChoose:
            ChooseOutputFolder();
            break;
        case Hit::OutputReset:
            outputDir_.clear();
            service_.SetOutputDirectory(std::filesystem::path());
            RefreshSnapshot();
            break;
        case Hit::Start:
            if (StartEnabled()) {
                service_.Start(quality_, resolution_);
                RefreshSnapshot();
            }
            break;
        case Hit::Cancel:
            service_.CancelCurrent();
            RefreshSnapshot();
            break;
        case Hit::None:
            break;
    }
}

void CompressWindow::Poll() {
    if (hwnd_ == nullptr) return;
    const bool busy = RecordingBusy();
    const transcode::ServiceSnapshot before = service_.Snapshot();
    if (busy && !before.suspended) service_.Suspend();
    else if (!busy && before.suspended) service_.Resume();
    RefreshSnapshot();
}

void CompressWindow::RefreshSnapshot() {
    const transcode::ServiceSnapshot snapshot = service_.Snapshot();
    std::wstring signature;
    signature.reserve(snapshot.jobs.size() * 64 + 64);
    signature += snapshot.active ? L'A' : L'a';
    signature += snapshot.suspended ? L'S' : L's';
    signature += snapshot.running ? L'R' : L'r';
    signature += snapshot.toolError;
    for (const transcode::JobSnapshot& job : snapshot.jobs) {
        signature += std::to_wstring(job.id);
        signature += L',';
        signature += std::to_wstring(static_cast<int>(job.state));
        signature += L',';
        signature += std::to_wstring(static_cast<int>(job.progress * 100.0));
        signature += job.probeFailed ? L'P' : L'p';
        signature += job.outputPath;
        signature += job.error;
        signature += std::to_wstring(job.estimatedBytes);
        signature += job.sourceAlreadySmall ? L'S' : L's';
        signature += std::to_wstring(job.outputBytes);
        signature += L';';
    }
    if (signature == lastSignature_) return;
    lastSignature_ = signature;
    jobs_ = snapshot.jobs;
    active_ = snapshot.active;
    suspended_ = snapshot.suspended;
    running_ = snapshot.running;
    toolError_ = snapshot.toolError;
    RebuildRows();
    Invalidate();
}

void CompressWindow::OnWheel(int delta) {
    scrollOffset_ -= static_cast<float>(delta) / static_cast<float>(WHEEL_DELTA) *
                     ui::ScaleF(kRowHeight, dpi_);
    RebuildRows();
    Invalidate();
}

void CompressWindow::AddFromDialog() {
    IFileOpenDialog* dialog = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_PPV_ARGS(&dialog));
    if (FAILED(hr) || dialog == nullptr) return;
    DWORD options = 0;
    dialog->GetOptions(&options);
    dialog->SetOptions(options | FOS_ALLOWMULTISELECT | FOS_FILEMUSTEXIST |
                       FOS_FORCEFILESYSTEM | FOS_DONTADDTORECENT);
    COMDLG_FILTERSPEC filter[] = {
        {i18n::Text(i18n::Str::CompressFilter),
         L"*.mp4;*.mkv;*.mov;*.avi;*.wmv;*.flv;*.m4v;*.ts;*.webm;*.mpg;*.mpeg;*.3gp"}};
    dialog->SetFileTypes(1, filter);
    dialog->SetTitle(i18n::Text(i18n::Str::CompressAdd));
    if (dialog->Show(hwnd_) != S_OK) {
        dialog->Release();
        return;
    }

    IShellItemArray* results = nullptr;
    if (FAILED(dialog->GetResults(&results)) || results == nullptr) {
        dialog->Release();
        return;
    }
    DWORD count = 0;
    results->GetCount(&count);
    std::vector<std::wstring> files;
    files.reserve(count);
    for (DWORD i = 0; i < count; ++i) {
        IShellItem* item = nullptr;
        if (FAILED(results->GetItemAt(i, &item)) || item == nullptr) continue;
        PWSTR path = nullptr;
        if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)) && path != nullptr) {
            files.emplace_back(path);
            CoTaskMemFree(path);
        }
        item->Release();
    }
    results->Release();
    dialog->Release();
    AddFiles(files);
}

void CompressWindow::AddFiles(const std::vector<std::wstring>& files) {
    for (const std::wstring& file : files) {
        if (!file.empty()) service_.Add(std::filesystem::path(file), outputDir_);
    }
    RefreshSnapshot();
}

void CompressWindow::ChooseOutputFolder() {
    IFileOpenDialog* dialog = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_PPV_ARGS(&dialog));
    if (FAILED(hr) || dialog == nullptr) return;
    DWORD options = 0;
    dialog->GetOptions(&options);
    dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM |
                       FOS_DONTADDTORECENT);
    dialog->SetTitle(i18n::Text(i18n::Str::CompressChooseFolder));
    if (dialog->Show(hwnd_) != S_OK) {
        dialog->Release();
        return;
    }
    IShellItem* result = nullptr;
    if (SUCCEEDED(dialog->GetResult(&result)) && result != nullptr) {
        PWSTR path = nullptr;
        if (SUCCEEDED(result->GetDisplayName(SIGDN_FILESYSPATH, &path)) &&
            path != nullptr) {
            outputDir_ = path;
            CoTaskMemFree(path);
            service_.SetOutputDirectory(std::filesystem::path(outputDir_));
            RefreshSnapshot();
        }
        result->Release();
    }
    dialog->Release();
}

void CompressWindow::ShowQualityMenu() {
    std::vector<menu::Item> items;
    items.push_back({i18n::Text(i18n::Str::CompressQualityLow), L"", ui::Icon::None,
                     kCmdQualityLow, true, quality_ == transcode::Quality::Low, false,
                     false});
    items.push_back({i18n::Text(i18n::Str::CompressQualityStandard), L"",
                     ui::Icon::None, kCmdQualityStandard, true,
                     quality_ == transcode::Quality::Standard, false, false});
    items.push_back({i18n::Text(i18n::Str::CompressQualityHigh), L"", ui::Icon::None,
                     kCmdQualityHigh, true, quality_ == transcode::Quality::High, false,
                     false});
    POINT point = {static_cast<LONG>(layout_.qualityField.X),
                   static_cast<LONG>(layout_.qualityField.GetBottom())};
    ClientToScreen(hwnd_, &point);
    const UINT command = static_cast<UINT>(menu::Show(hwnd_, point, items, fonts_, false));
    switch (command) {
        case kCmdQualityLow: quality_ = transcode::Quality::Low; break;
        case kCmdQualityStandard: quality_ = transcode::Quality::Standard; break;
        case kCmdQualityHigh: quality_ = transcode::Quality::High; break;
        default: return;
    }
    service_.SetPreset(quality_, resolution_);
    RefreshSnapshot();
}

void CompressWindow::ShowResolutionMenu() {
    std::vector<menu::Item> items;
    items.push_back({i18n::Text(i18n::Str::CompressResKeep), L"", ui::Icon::None,
                     kCmdResolutionKeep, true, resolution_ == transcode::Resolution::Keep,
                     false, false});
    items.push_back({i18n::Text(i18n::Str::CompressRes720), L"", ui::Icon::None,
                     kCmdResolution720, true, resolution_ == transcode::Resolution::P720,
                     false, false});
    items.push_back({i18n::Text(i18n::Str::CompressRes1080), L"", ui::Icon::None,
                     kCmdResolution1080, true,
                     resolution_ == transcode::Resolution::P1080, false, false});
    POINT point = {static_cast<LONG>(layout_.resolutionField.X),
                   static_cast<LONG>(layout_.resolutionField.GetBottom())};
    ClientToScreen(hwnd_, &point);
    const UINT command = static_cast<UINT>(menu::Show(hwnd_, point, items, fonts_, false));
    switch (command) {
        case kCmdResolutionKeep: resolution_ = transcode::Resolution::Keep; break;
        case kCmdResolution720: resolution_ = transcode::Resolution::P720; break;
        case kCmdResolution1080: resolution_ = transcode::Resolution::P1080; break;
        default: return;
    }
    service_.SetPreset(quality_, resolution_);
    RefreshSnapshot();
}

void CompressWindow::OpenOutput(const transcode::JobSnapshot& job, bool selectFile) {
    if (job.outputPath.empty()) return;
    if (selectFile) {
        ShellExecuteW(nullptr, L"open", job.outputPath.c_str(), nullptr, nullptr,
                      SW_SHOWNORMAL);
    } else {
        ShellExecuteW(nullptr, L"open",
                      std::filesystem::path(job.outputPath).parent_path().c_str(),
                      nullptr, nullptr, SW_SHOWNORMAL);
    }
}

void CompressWindow::RowAction(const transcode::JobSnapshot& job) {
    if (job.state == transcode::JobState::Completed) {
        OpenOutput(job, true);
        return;
    }
    if (job.state == transcode::JobState::Running ||
        job.state == transcode::JobState::Probing) {
        return;
    }
    service_.Remove(job.id);
    RefreshSnapshot();
}

void CompressWindow::RequestClose() {
    if (running_ || AnyActiveJob()) {
        const int choice = MessageBoxW(hwnd_, i18n::Text(i18n::Str::CompressCloseRunningBody),
                                       i18n::Text(i18n::Str::CompressCloseRunningTitle),
                                       MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2);
        if (choice != IDYES) return;
    }
    service_.CancelCurrent();
    DestroyWindow(hwnd_);
}

const transcode::JobSnapshot* CompressWindow::FindJob(std::uint64_t id) const {
    for (const transcode::JobSnapshot& job : jobs_) {
        if (job.id == id) return &job;
    }
    return nullptr;
}

std::wstring CompressWindow::StatusText() const {
    if (!toolError_.empty()) return i18n::Text(i18n::Str::CompressNoFfmpeg);
    if (RecordingBusy()) return i18n::Text(i18n::Str::CompressRecordingHint);
    if (suspended_) return i18n::Text(i18n::Str::CompressStatusSuspended);
    if (running_) {
        std::size_t index = 0;
        for (std::size_t i = 0; i < jobs_.size(); ++i) {
            if (jobs_[i].state == transcode::JobState::Running ||
                jobs_[i].state == transcode::JobState::Probing) {
                index = i;
                break;
            }
        }
        std::wstring text = i18n::Format(
            i18n::Str::CompressStatusRunning,
            {std::to_wstring(index + 1), std::to_wstring(jobs_.size())});
        const std::wstring remaining = RemainingText();
        if (!remaining.empty()) text += L" · " + remaining;
        return text;
    }
    if (jobs_.empty()) return i18n::Text(i18n::Str::CompressStatusIdle);
    if (HasQueued(jobs_)) return i18n::Text(i18n::Str::CompressStatusIdle);
    if (HasFailed(jobs_)) {
        std::size_t failed = 0;
        for (const transcode::JobSnapshot& job : jobs_) {
            if (job.state == transcode::JobState::Failed) ++failed;
        }
        return i18n::Format(i18n::Str::CompressStatusFailed,
                            {std::to_wstring(failed)});
    }
    if (AllCompleted(jobs_)) return i18n::Text(i18n::Str::CompressStatusDone);
    return i18n::Text(i18n::Str::CompressStatusIdle);
}

std::wstring CompressWindow::RemainingText() const {
    for (const transcode::JobSnapshot& job : jobs_) {
        if (job.state != transcode::JobState::Running) continue;
        if (job.durationMs <= 0 || job.speed <= 0.01) return L"";
        const std::int64_t remainingMs = static_cast<std::int64_t>(
            static_cast<double>(job.durationMs - job.processedMs) / job.speed);
        if (remainingMs <= 0) return L"";
        return i18n::Format(i18n::Str::CompressRemaining,
                            {i18n::FormatDuration(remainingMs)});
    }
    return L"";
}

std::wstring CompressWindow::RowSubtitle(const transcode::JobSnapshot& job) const {
    if (!job.probed && !job.probeFailed) {
        return i18n::Text(i18n::Str::CompressProbing);
    }
    if (job.probeFailed) return i18n::Text(i18n::Str::CompressProbeFailed);
    if (job.state == transcode::JobState::Failed && !job.error.empty()) return job.error;
    if (job.state == transcode::JobState::Completed) {
        return BytesText(job.outputBytes) + L" · " + FileNameText(job.outputPath);
    }
    std::wstring text;
    if (job.media.width > 0 && job.media.height > 0) {
        text = std::to_wstring(job.media.width) + L"×" +
               std::to_wstring(job.media.height) + L" · ";
    }
    text += i18n::FormatDuration(job.durationMs);
    if (job.sourceBytes > 0) text += L" · " + i18n::FormatBytes(job.sourceBytes);
    if (!job.media.videoCodec.empty()) {
        text += L" · " + Utf8ToWide(job.media.videoCodec);
    }
    return text;
}

std::wstring CompressWindow::RowStatus(const transcode::JobSnapshot& job) const {
    switch (job.state) {
        case transcode::JobState::Queued:
            return i18n::Text(i18n::Str::CompressQueued);
        case transcode::JobState::Probing:
            return i18n::Text(i18n::Str::CompressProbing);
        case transcode::JobState::Running: {
            std::wstring percent = L"...";
            if (job.progress >= 0.0) {
                percent = std::to_wstring(
                              static_cast<int>(job.progress * 100.0 + 0.5)) +
                          L"%";
            }
            return i18n::Format(i18n::Str::CompressRunning, {percent});
        }
        case transcode::JobState::Completed:
            return i18n::Text(i18n::Str::CompressDone);
        case transcode::JobState::Failed:
            return i18n::Text(i18n::Str::CompressFailedLabel);
    }
    return L"";
}

std::wstring CompressWindow::QualityLabel() const {
    switch (quality_) {
        case transcode::Quality::Low:
            return i18n::Text(i18n::Str::CompressQualityLow);
        case transcode::Quality::High:
            return i18n::Text(i18n::Str::CompressQualityHigh);
        case transcode::Quality::Standard:
            return i18n::Text(i18n::Str::CompressQualityStandard);
    }
    return i18n::Text(i18n::Str::CompressQualityStandard);
}

std::wstring CompressWindow::ResolutionLabel() const {
    switch (resolution_) {
        case transcode::Resolution::P720:
            return i18n::Text(i18n::Str::CompressRes720);
        case transcode::Resolution::P1080:
            return i18n::Text(i18n::Str::CompressRes1080);
        case transcode::Resolution::Keep:
            return i18n::Text(i18n::Str::CompressResKeep);
    }
    return i18n::Text(i18n::Str::CompressResKeep);
}

bool CompressWindow::RecordingBusy() const {
    return recorderBusy_ ? recorderBusy_() : false;
}

bool CompressWindow::StartEnabled() const {
    if (!toolError_.empty()) return false;
    if (RecordingBusy() || suspended_ || running_) return false;
    if (!HasQueued(jobs_)) return false;
    return true;
}

bool CompressWindow::AnyActiveJob() const {
    return std::any_of(jobs_.begin(), jobs_.end(),
                       [](const transcode::JobSnapshot& job) {
                           return job.state == transcode::JobState::Running ||
                                  job.state == transcode::JobState::Probing;
                       });
}

} // namespace vsr::app

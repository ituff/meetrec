#include "MainWindow.h"

#include <windows.h>
#include <windowsx.h>

#include <shellapi.h>
#include <shlobj.h>

#include <Application/Diagnostics.h>
#include <Capture/GraphicsDevice.h>
#include <DisplayControl/SoftwareDisplayDevice.h>
#include <Encoder/EncoderProbe.h>
#include <Encoder/VideoPreset.h>
#include <Infrastructure/Log.h>
#include <Infrastructure/Paths.h>
#include <Infrastructure/Settings.h>
#include <Infrastructure/Strings.h>

#include "Application.h"
#include "PreviewPanel.h"

namespace vsr::app {

namespace {

// Control identifiers (keyboard accessible via WS_TABSTOP, NFR-041).
constexpr int ID_LIST_WINDOWS = 201;
constexpr int ID_BTN_REFRESH = 202;
constexpr int ID_EDIT_OUTDIR = 203;
constexpr int ID_BTN_BROWSE = 204;
constexpr int ID_COMBO_PRESET = 205;
constexpr int ID_CHK_CURSOR = 206;
constexpr int ID_BTN_START = 207;
constexpr int ID_BTN_PAUSE = 208;
constexpr int ID_BTN_STOP = 209;
constexpr int ID_BTN_OPENFOLDER = 210;
constexpr int ID_BTN_DIAG = 211;
constexpr int ID_PREVIEW_TOGGLE = 212;
constexpr int ID_TXT_STATE = 213;
constexpr int ID_TXT_DURATION = 214;
constexpr int ID_TXT_FPS = 215;
constexpr int ID_TXT_DROPPED = 216;
constexpr int ID_TXT_ERROR = 217;
constexpr int ID_TXT_OUTPUT = 218;
constexpr int ID_TXT_TARGET = 219;
constexpr int ID_BTN_DISPLAY = 220;
constexpr int ID_TXT_DISPLAY = 221;
constexpr int ID_COMBO_ENCODER = 222;
constexpr int ID_CHK_AUDIO = 223;

constexpr UINT WM_APP_ACTIVATE = WM_APP + 1;
constexpr UINT WM_APP_TRAY = WM_APP + 2;
// Carries a std::wstring* (heap) in lParam: the folder picked by the STA
// browse thread; may be an empty string when the dialog was cancelled.
constexpr UINT WM_APP_BROWSE_RESULT = WM_APP + 3;

// Tray menu commands (FR-082).
constexpr int TRAY_OPEN = 3001;
constexpr int TRAY_PAUSE_RESUME = 3002;
constexpr int TRAY_STOP = 3003;
constexpr int TRAY_EXIT = 3004;

int Scale(int value, UINT dpi) {
    return MulDiv(value, static_cast<int>(dpi), 96);
}

void PlaceControl(HWND hwnd, int x, int y, int w, int h, UINT dpi) {
    SetWindowPos(hwnd, nullptr, Scale(x, dpi), Scale(y, dpi), Scale(w, dpi), Scale(h, dpi),
                 SWP_NOZORDER | SWP_NOACTIVATE);
}

} // namespace

MainWindow::MainWindow(Coordinator& coordinator) : coordinator_(coordinator) {}

MainWindow::~MainWindow() {
    if (uiFont_ != nullptr) DeleteObject(uiFont_);
}

bool MainWindow::Create() {
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = &MainWindow::WndProcThunk;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.lpszClassName = kMainWindowClassName;
    wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    RegisterClassExW(&wc);

    dpi_ = GetDpiForSystem();
    const int w = Scale(760, dpi_);
    const int h = Scale(640, dpi_);
    hwnd_ = CreateWindowExW(0, kMainWindowClassName, L"Virtual Screen Recorder",
                            WS_OVERLAPPEDWINDOW & ~WS_MAXIMIZEBOX, CW_USEDEFAULT, CW_USEDEFAULT, w,
                            h, nullptr, nullptr, GetModuleHandleW(nullptr), this);
    if (hwnd_ == nullptr) {
        return false;
    }
    // Child controls need a valid hwnd_, which only exists after
    // CreateWindowExW returns; WM_CREATE runs too early (all children would
    // fail with ERROR_TLW_WITH_WSCHILD).
    OnCreate();

    // The window itself was sized before it existed, using the system DPI,
    // while OnCreate lays out children with GetDpiForWindow; reconcile once
    // here (afterwards WM_DPICHANGED keeps them in sync).
    const UINT windowDpi = GetDpiForWindow(hwnd_);
    if (windowDpi != 0 && windowDpi != dpi_) {
        OnDpiChanged(windowDpi);
        SetWindowPos(hwnd_, nullptr, 0, 0, Scale(760, windowDpi), Scale(640, windowDpi),
                     SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    }
    return true;
}

void MainWindow::ShowAndActivate() {
    if (hwnd_ != nullptr) {
        ShowWindow(hwnd_, SW_SHOWNORMAL);
        UpdateWindow(hwnd_);
    }
}

void MainWindow::RefreshUi() {
    OnTimer();
}

LRESULT CALLBACK MainWindow::WndProcThunk(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
    }
    auto* self = reinterpret_cast<MainWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    return self != nullptr ? self->WndProc(hwnd, msg, wp, lp) : DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT MainWindow::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_TIMER:
            if (wp == timerId_) OnTimer();
            return 0;
        case WM_DPICHANGED: {
            const UINT newDpi = HIWORD(wp);
            auto* suggested = reinterpret_cast<RECT*>(lp);
            SetWindowPos(hwnd, nullptr, suggested->left, suggested->top,
                         suggested->right - suggested->left, suggested->bottom - suggested->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            OnDpiChanged(newDpi);
            return 0;
        }
        case WM_COMMAND: {
            const int id = LOWORD(wp);
            const int code = HIWORD(wp);
            switch (id) {
                case ID_BTN_REFRESH:
                    if (code == BN_CLICKED) RefreshWindowList();
                    return 0;
                case ID_BTN_START:
                    if (code == BN_CLICKED) OnStartClicked();
                    return 0;
                case ID_BTN_PAUSE:
                    if (code == BN_CLICKED) OnPauseResumeClicked();
                    return 0;
                case ID_BTN_STOP:
                    if (code == BN_CLICKED) OnStopClicked();
                    return 0;
                case ID_BTN_BROWSE:
                    if (code == BN_CLICKED) OnBrowseClicked();
                    return 0;
                case ID_BTN_OPENFOLDER:
                    if (code == BN_CLICKED) OnOpenOutputFolder();
                    return 0;
                case ID_BTN_DIAG:
                    if (code == BN_CLICKED) OnDiagnosticsClicked();
                    return 0;
                case ID_BTN_DISPLAY:
                    if (code == BN_CLICKED) OnDisplayToggleClicked();
                    return 0;
                case ID_PREVIEW_TOGGLE:
                    if (code == BN_CLICKED && preview_ != nullptr) {
                        // Live preview toggle; disabling releases panel state
                        // and never touches capture (FR-048).
                        const bool show =
                            IsDlgButtonChecked(hwnd, ID_PREVIEW_TOGGLE) == BST_CHECKED;
                        preview_->SetEnabled(show);
                        if (!preview_->IsEnabled()) preview_->Clear();
                        if (preview_->Hwnd() != nullptr) {
                            ShowWindow(preview_->Hwnd(), show ? SW_SHOW : SW_HIDE);
                        }
                    }
                    return 0;
                case ID_LIST_WINDOWS:
                    if (code == LBN_DBLCLK) RefreshWindowList();
                    return 0;
                default:
                    return 0;
            }
        }
        case WM_MEASUREITEM: {
            // Owner-draw target list (FR-021): row height fits the icon.
            auto* mis = reinterpret_cast<MEASUREITEMSTRUCT*>(lp);
            if (mis != nullptr && mis->CtlID == ID_LIST_WINDOWS) {
                mis->itemHeight = static_cast<UINT>(Scale(24, dpi_));
                return TRUE;
            }
            return DefWindowProcW(hwnd, msg, wp, lp);
        }
        case WM_DRAWITEM: {
            auto* dis = reinterpret_cast<DRAWITEMSTRUCT*>(lp);
            if (dis == nullptr || dis->CtlID != ID_LIST_WINDOWS ||
                dis->itemID >= windows_.size()) {
                return DefWindowProcW(hwnd, msg, wp, lp);
            }
            const auto& info = windows_[dis->itemID];

            // Selection / focus background.
            const bool selected = (dis->itemState & ODS_SELECTED) != 0;
            FillRect(dis->hDC, &dis->rcItem,
                     GetSysColorBrush(selected ? COLOR_HIGHLIGHT : COLOR_WINDOW));
            SetBkMode(dis->hDC, TRANSPARENT);
            SetTextColor(dis->hDC, selected ? GetSysColor(COLOR_HIGHLIGHTTEXT)
                                            : GetSysColor(COLOR_WINDOWTEXT));

            // Icon at the left (FR-021).
            const int iconPx = Scale(16, dpi_);
            const int pad = Scale(4, dpi_);
            if (info.icon != nullptr) {
                DrawIconEx(dis->hDC, dis->rcItem.left + pad,
                           dis->rcItem.top + ((dis->rcItem.bottom - dis->rcItem.top) - iconPx) / 2,
                           info.icon, iconPx, iconPx, 0, nullptr, DI_NORMAL);
            }

            // "exe - title" single line, vertically centered (NFR-040 DPI text).
            wchar_t line[768] = {};
            _snwprintf_s(line, ARRAYSIZE(line), _TRUNCATE, L"%s - %s", info.exeName.c_str(),
                         info.title.c_str());
            RECT textRect = dis->rcItem;
            textRect.left += pad * 2 + iconPx;
            HGDIOBJ oldFont = SelectObject(dis->hDC, uiFont_);
            DrawTextW(dis->hDC, line, -1, &textRect,
                      DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
            SelectObject(dis->hDC, oldFont);

            if ((dis->itemState & ODS_FOCUS) != 0) {
                RECT focus = dis->rcItem;
                InflateRect(&focus, -1, -1);
                DrawFocusRect(dis->hDC, &focus);
            }
            return TRUE;
        }
        case WM_APP_BROWSE_RESULT: {
            auto* picked = reinterpret_cast<std::wstring*>(lp);
            EnableWindow(GetDlgItem(hwnd, ID_BTN_BROWSE), TRUE);
            if (picked != nullptr) {
                if (!picked->empty()) {
                    SetWindowTextW(GetDlgItem(hwnd, ID_EDIT_OUTDIR), picked->c_str());
                }
                delete picked;
            }
            return 0;
        }
        case WM_APP_ACTIVATE:
            ShowAndActivate();
            return 0;
        case WM_APP_TRAY: {
            // FR-082: tray menu exposes Open, Pause/Resume, Stop, Exit; only
            // one of Pause/Resume applies per state, the other is hidden.
            const StatusSnapshot menuSnap = coordinator_.GetStatus();
            if (lp == WM_RBUTTONUP || lp == WM_CONTEXTMENU) {
                HMENU menu = CreatePopupMenu();
                AppendMenuW(menu, MF_STRING, TRAY_OPEN, L"Open");
                const bool active = menuSnap.state == AppState::Recording ||
                                    menuSnap.state == AppState::Paused;
                if (menuSnap.state == AppState::Recording) {
                    AppendMenuW(menu, MF_STRING, TRAY_PAUSE_RESUME, L"Pause");
                } else if (menuSnap.state == AppState::Paused) {
                    AppendMenuW(menu, MF_STRING, TRAY_PAUSE_RESUME, L"Resume");
                }
                AppendMenuW(menu, MF_STRING | (active ? 0 : MF_GRAYED), TRAY_STOP, L"Stop");
                AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
                AppendMenuW(menu, MF_STRING, TRAY_EXIT, L"Exit");
                POINT pt;
                GetCursorPos(&pt);
                SetForegroundWindow(hwnd_);
                const int cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY, pt.x, pt.y, 0,
                                               hwnd_, nullptr);
                DestroyMenu(menu);
                if (cmd == TRAY_OPEN) {
                    ShowAndActivate();
                } else if (cmd == TRAY_PAUSE_RESUME) {
                    if (menuSnap.state == AppState::Recording) (void)coordinator_.Pause();
                    else (void)coordinator_.Resume();
                    RefreshUi();
                } else if (cmd == TRAY_STOP) {
                    (void)coordinator_.Stop(StopReason::User);
                    RefreshUi();
                } else if (cmd == TRAY_EXIT) {
                    // FR-084: exit during recording finalizes first.
                    if (active) {
                        const int choice = MessageBoxW(
                            hwnd_,
                            L"A recording is active. Stop the recording, finalize the file, and "
                            L"exit?\n\n(The recording cannot continue after exit.)",
                            L"Virtual Screen Recorder", MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2);
                        if (choice != IDYES) return 0;
                        (void)coordinator_.Stop(StopReason::User);
                    }
                    DestroyWindow(hwnd_);
                }
            } else if (lp == WM_LBUTTONDBLCLK) {
                ShowAndActivate();
            }
            return 0;
        }
        case WM_CLOSE: {
            const StatusSnapshot snap = coordinator_.GetStatus();
            const bool active = snap.state == AppState::Recording ||
                                snap.state == AppState::Paused ||
                                snap.state == AppState::Preparing;
            if (active) {
                // FR-081 / NFR-044: closing during recording hides to tray.
                if (Application::Instance().WantsMinimizeToTray()) {
                    ShowWindow(hwnd, SW_HIDE);
                    return 0;
                }
                const int choice = MessageBoxW(
                    hwnd_,
                    L"A recording is active. Stop the recording and close the window?",
                    L"Virtual Screen Recorder", MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2);
                if (choice != IDYES) return 0;
                (void)coordinator_.Stop(StopReason::User);
            }
            DestroyWindow(hwnd);
            return 0;
        }
        case WM_DESTROY:
            KillTimer(hwnd, timerId_);
            if (displayOp_.joinable()) displayOp_.join();
            if (stopOp_.joinable()) stopOp_.join();
            for (const auto& w : windows_) {
                if (w.icon != nullptr) DestroyIcon(w.icon);
            }
            windows_.clear();
            tray_.Remove();
            SaveSettings(); // FR-052: remember folder/preset for the next run
            PostQuitMessage(0);
            return 0;
        default:
            // Single-instance activation (FR-005) uses a registered message.
            if (activateMessage_ != 0 && msg == activateMessage_) {
                ShowAndActivate();
                return 0;
            }
            // Explorer restart (T071): recreate the tray icon.
            if (taskbarCreatedMessage_ != 0 && msg == taskbarCreatedMessage_) {
                tray_.Remove();
                tray_.Create(hwnd, WM_APP_TRAY);
                return 0;
            }
            return DefWindowProcW(hwnd, msg, wp, lp);
    }
}

void MainWindow::OnCreate() {
    dpi_ = GetDpiForWindow(hwnd_);
    auto* inst = GetModuleHandleW(nullptr);

    HFONT font = CreateFontW(-Scale(9, dpi_), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                             DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                             CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    uiFont_ = font;

    auto makeControl = [&](const wchar_t* cls, const wchar_t* text, DWORD style, int id) {
        HWND control = CreateWindowExW(0, cls, text, WS_CHILD | WS_VISIBLE | WS_TABSTOP | style, 0,
                                       0, 0, 0, hwnd_, reinterpret_cast<HMENU>(
                                                       static_cast<INT_PTR>(id)),
                                       inst, nullptr);
        SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(uiFont_), TRUE);
        return control;
    };

    makeControl(L"BUTTON", L"Target application:", BS_GROUPBOX, 0);
    makeControl(L"LISTBOX", nullptr,
                LBS_NOTIFY | LBS_HASSTRINGS | LBS_OWNERDRAWFIXED | LBS_NOINTEGRALHEIGHT |
                    WS_VSCROLL,
                ID_LIST_WINDOWS);
    makeControl(L"BUTTON", L"Refresh", BS_PUSHBUTTON, ID_BTN_REFRESH);
    makeControl(L"BUTTON", L"Output folder:", BS_GROUPBOX, 0);
    makeControl(L"EDIT", nullptr, ES_AUTOHSCROLL | WS_BORDER, ID_EDIT_OUTDIR);
    makeControl(L"BUTTON", L"Browse...", BS_PUSHBUTTON, ID_BTN_BROWSE);
    makeControl(L"BUTTON", L"Open folder", BS_PUSHBUTTON, ID_BTN_OPENFOLDER);
    makeControl(L"BUTTON", L"Quality:", BS_GROUPBOX, 0);
    makeControl(L"COMBOBOX", nullptr, CBS_DROPDOWNLIST | CBS_HASSTRINGS, ID_COMBO_PRESET);
    makeControl(L"COMBOBOX", nullptr, CBS_DROPDOWNLIST | CBS_HASSTRINGS, ID_COMBO_ENCODER);
    makeControl(L"BUTTON", L"Capture mouse cursor (when supported)", BS_AUTOCHECKBOX,
                ID_CHK_CURSOR);
    makeControl(L"BUTTON", L"Record system audio", BS_AUTOCHECKBOX, ID_CHK_AUDIO);
    makeControl(L"BUTTON", L"Show preview", BS_AUTOCHECKBOX, ID_PREVIEW_TOGGLE);
    makeControl(L"BUTTON", L"Start Recording", BS_DEFPUSHBUTTON, ID_BTN_START);
    makeControl(L"BUTTON", L"Pause", BS_PUSHBUTTON, ID_BTN_PAUSE);
    makeControl(L"BUTTON", L"Stop", BS_PUSHBUTTON, ID_BTN_STOP);
    makeControl(L"BUTTON", L"Diagnostics...", BS_PUSHBUTTON, ID_BTN_DIAG);
    makeControl(L"STATIC", L"State: Ready", SS_LEFT, ID_TXT_STATE);
    makeControl(L"STATIC", L"Duration: 00:00:00", SS_LEFT, ID_TXT_DURATION);
    makeControl(L"STATIC", L"FPS: -", SS_LEFT, ID_TXT_FPS);
    makeControl(L"STATIC", L"Dropped: 0", SS_LEFT, ID_TXT_DROPPED);
    makeControl(L"STATIC", L"Target: -", SS_LEFT, ID_TXT_TARGET);
    makeControl(L"STATIC", L"Output: -", SS_LEFT, ID_TXT_OUTPUT);
    makeControl(L"STATIC", L"", SS_LEFT, ID_TXT_ERROR);
    makeControl(L"BUTTON", L"Connect display", BS_PUSHBUTTON, ID_BTN_DISPLAY);
    makeControl(L"STATIC", L"Virtual display: checking...", SS_LEFT, ID_TXT_DISPLAY);

    // Preset combo entries (FR-051).
    HWND combo = GetDlgItem(hwnd_, ID_COMBO_PRESET);
    ComboBox_AddString(combo, L"Low - 1280x720 @ 30 FPS");
    ComboBox_AddString(combo, L"Standard - 1920x1080 @ 30 FPS");
    ComboBox_AddString(combo, L"High - 1920x1080 @ 60 FPS");
    ComboBox_SetCurSel(combo, 1);

    // Encoder preference combo (auto / hardware / software).
    HWND encoderCombo = GetDlgItem(hwnd_, ID_COMBO_ENCODER);
    ComboBox_AddString(encoderCombo, L"Encoder: Auto (hardware first)");
    ComboBox_AddString(encoderCombo, L"Encoder: Hardware only");
    ComboBox_AddString(encoderCombo, L"Encoder: Software (slower, most compatible)");
    ComboBox_SetCurSel(encoderCombo, 0);

    // Preview panel (T072).
    preview_ = std::make_unique<PreviewPanel>(coordinator_);
    preview_->Create(hwnd_, inst);

    // Load persisted settings into the controls (FR-052).
    SettingsStore store;
    const Settings settings = store.Load(nullptr);
    if (!settings.outputDirectory.empty()) {
        SetWindowTextW(GetDlgItem(hwnd_, ID_EDIT_OUTDIR), settings.outputDirectory.c_str());
    } else {
        SetWindowTextW(GetDlgItem(hwnd_, ID_EDIT_OUTDIR),
                       Application::Instance().OutputDirectory().c_str());
    }
    ComboBox_SetCurSel(combo, settings.preset == QualityPresetName::Low      ? 0
                                  : settings.preset == QualityPresetName::High ? 2
                                                                               : 1);
    ComboBox_SetCurSel(encoderCombo, settings.encoderPreference == "hardware" ? 1
                                      : settings.encoderPreference == "software" ? 2
                                      : 0);
    CheckDlgButton(hwnd_, ID_CHK_CURSOR, settings.captureCursor ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(hwnd_, ID_CHK_AUDIO,
                   settings.recordSystemAudio ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(hwnd_, ID_PREVIEW_TOGGLE, settings.showPreview ? BST_CHECKED : BST_UNCHECKED);
    preview_->SetEnabled(settings.showPreview);
    if (preview_->Hwnd() != nullptr) {
        ShowWindow(preview_->Hwnd(), settings.showPreview ? SW_SHOW : SW_HIDE);
    }

    LayoutControls(dpi_);
    RefreshWindowList();

    // Tray + timers (FR-080).
    tray_.Create(hwnd_, WM_APP_TRAY);
    SetTimer(hwnd_, timerId_, 100, nullptr);

    // Re-create the tray icon after Explorer restarts (T071) and accept
    // second-instance activation (FR-005).
    activateMessage_ = RegisterWindowMessageW(L"VsrActivateExistingInstance");
    taskbarCreatedMessage_ = RegisterWindowMessageW(L"TaskbarCreated");
}

void MainWindow::OnDpiChanged(UINT newDpi) {
    dpi_ = newDpi;
    if (uiFont_ != nullptr) DeleteObject(uiFont_);
    uiFont_ = CreateFontW(-Scale(9, dpi_), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                          DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                          CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    for (int id = ID_LIST_WINDOWS; id <= ID_TXT_ERROR; ++id) {
        HWND control = GetDlgItem(hwnd_, id);
        if (control != nullptr) SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(uiFont_), TRUE);
    }
    LayoutControls(dpi_);
}

void MainWindow::LayoutControls(UINT dpi) {
    const int margin = 12;
    int y = margin;

    PlaceControl(GetDlgItem(hwnd_, ID_LIST_WINDOWS), margin, y, 460, 200, dpi);
    PlaceControl(GetDlgItem(hwnd_, ID_BTN_REFRESH), 480, y, 120, 24, dpi);
    y += 210;

    PlaceControl(GetDlgItem(hwnd_, ID_EDIT_OUTDIR), margin, y, 340, 24, dpi);
    PlaceControl(GetDlgItem(hwnd_, ID_BTN_BROWSE), 360, y, 90, 24, dpi);
    PlaceControl(GetDlgItem(hwnd_, ID_BTN_OPENFOLDER), 456, y, 100, 24, dpi);
    y += 34;

    PlaceControl(GetDlgItem(hwnd_, ID_COMBO_PRESET), margin, y, 240, 24, dpi);
    PlaceControl(GetDlgItem(hwnd_, ID_COMBO_ENCODER), 290, y, 240, 24, dpi);
    y += 34;

    PlaceControl(GetDlgItem(hwnd_, ID_CHK_CURSOR), margin, y, 260, 22, dpi);
    PlaceControl(GetDlgItem(hwnd_, ID_CHK_AUDIO), margin, y + 26, 260, 22, dpi);
    PlaceControl(GetDlgItem(hwnd_, ID_PREVIEW_TOGGLE), 290, y, 120, 22, dpi);
    PlaceControl(GetDlgItem(hwnd_, ID_BTN_DIAG), 290, y + 30, 120, 24, dpi);
    PlaceControl(GetDlgItem(hwnd_, ID_BTN_DISPLAY), 420, y + 30, 150, 24, dpi);
    // 66 clears the Diagnostics/display row above; 36 would overlap it with
    // the command row (Diagnostics is covered by the Stop button).
    y += 66;

    // Command row.
    PlaceControl(GetDlgItem(hwnd_, ID_BTN_START), margin, y, 150, 36, dpi);
    PlaceControl(GetDlgItem(hwnd_, ID_BTN_PAUSE), 172, y, 110, 36, dpi);
    PlaceControl(GetDlgItem(hwnd_, ID_BTN_STOP), 292, y, 110, 36, dpi);
    y += 46;

    // Preview occupies the right side of the command/status block (x>=480,
    // clear of every other control). Hidden entirely when preview is off.
    if (preview_ != nullptr && preview_->Hwnd() != nullptr) {
        PlaceControl(preview_->Hwnd(), 480, y, 260, 150, dpi);
    }

    // Status rows stay left of the preview area.
    PlaceControl(GetDlgItem(hwnd_, ID_TXT_STATE), margin, y, 300, 20, dpi);
    PlaceControl(GetDlgItem(hwnd_, ID_TXT_DURATION), 320, y, 140, 20, dpi);
    y += 24;
    PlaceControl(GetDlgItem(hwnd_, ID_TXT_FPS), margin, y, 220, 20, dpi);
    PlaceControl(GetDlgItem(hwnd_, ID_TXT_DROPPED), 240, y, 220, 20, dpi);
    y += 24;
    PlaceControl(GetDlgItem(hwnd_, ID_TXT_DISPLAY), margin, y, 460, 20, dpi);
    y += 22;
    PlaceControl(GetDlgItem(hwnd_, ID_TXT_TARGET), margin, y, 460, 20, dpi);
    y += 22;
    PlaceControl(GetDlgItem(hwnd_, ID_TXT_OUTPUT), margin, y, 460, 20, dpi);
    y += 22;
    PlaceControl(GetDlgItem(hwnd_, ID_TXT_ERROR), margin, y, 460, 40, dpi);
}

void MainWindow::ApplyCommandStates(const StatusSnapshot& snap) {
    // Commands are enabled exactly when the state machine allows them
    // (spec.md §8, FR-070).
    const bool canStart = IsCommandAllowed(snap.state, Command::Start);
    const bool canPause = IsCommandAllowed(snap.state, Command::Pause);
    const bool canResume = IsCommandAllowed(snap.state, Command::Resume);
    const bool canStop = IsCommandAllowed(snap.state, Command::Stop);
    EnableWindow(GetDlgItem(hwnd_, ID_BTN_START), canStart ? TRUE : FALSE);
    EnableWindow(GetDlgItem(hwnd_, ID_BTN_PAUSE), (canPause || canResume) ? TRUE : FALSE);
    EnableWindow(GetDlgItem(hwnd_, ID_BTN_STOP), canStop ? TRUE : FALSE);

    // Pause button becomes Resume when paused.
    pauseButtonMode_ = canResume ? 1 : 0;
    SetWindowTextW(GetDlgItem(hwnd_, ID_BTN_PAUSE), canResume ? L"Resume" : L"Pause");

    // FR-016/FR-017: display connect/disconnect only while idle; hidden
    // while an async connect is still running.
    const bool stopPending = stopOpRunning_.load();
    EnableWindow(GetDlgItem(hwnd_, ID_BTN_DISPLAY),
                 (IsCommandAllowed(snap.state, Command::EnableDisplay) && !displayOpRunning_ &&
                  !stopPending)
                     ? TRUE
                     : FALSE);
    // While a stop is pending, all recording commands are in flight (Stopping
    // transition); keep Start/Pause disabled via the snapshot state itself.
    if (stopPending) {
        EnableWindow(GetDlgItem(hwnd_, ID_BTN_STOP), FALSE);
    }
}

void MainWindow::OnTimer() {
    ReapDisplayOperation();
    ReapStopOperation();
    const StatusSnapshot snap = coordinator_.GetStatus();
    ApplyCommandStates(snap);

    wchar_t buf[512] = {};
    swprintf_s(buf, L"State: %s", ToString(snap.state));
    SetWindowTextW(GetDlgItem(hwnd_, ID_TXT_STATE), buf);

    const std::int64_t sec = snap.activeElapsedMs / 1000;
    swprintf_s(buf, L"Duration: %02d:%02d:%02d", static_cast<int>(sec / 3600),
               static_cast<int>((sec / 60) % 60), static_cast<int>(sec % 60));
    SetWindowTextW(GetDlgItem(hwnd_, ID_TXT_DURATION), buf);

    swprintf_s(buf, L"FPS: %.0f", snap.approxFps);
    SetWindowTextW(GetDlgItem(hwnd_, ID_TXT_FPS), buf);
    swprintf_s(buf, L"Dropped: %llu", static_cast<unsigned long long>(snap.droppedFrames));
    SetWindowTextW(GetDlgItem(hwnd_, ID_TXT_DROPPED), buf);

    // Target is identified by executable name only in the status line; the
    // full window title is never displayed here to avoid document leakage in
    // screen shares of the recorder itself (FR-094 aligned choice).
    swprintf_s(buf, L"Target: %s", snap.targetExeName.empty() ? L"-" : snap.targetExeName.c_str());
    SetWindowTextW(GetDlgItem(hwnd_, ID_TXT_TARGET), buf);

    const std::wstring outPath = snap.finalPath.empty() ? snap.partialPath : snap.finalPath;
    swprintf_s(buf, L"Output: %s", outPath.empty() ? L"-" : outPath.c_str());
    SetWindowTextW(GetDlgItem(hwnd_, ID_TXT_OUTPUT), buf);

    // Virtual display line + adaptive button label (FR-016).
    if (snap.virtualMonitorActive) {
        swprintf_s(buf, L"Virtual display: active (%ldx%ld)", snap.monitorW, snap.monitorH);
    } else {
        swprintf_s(buf, L"Virtual display: inactive");
    }
    SetWindowTextW(GetDlgItem(hwnd_, ID_TXT_DISPLAY), buf);
    if (snap.virtualMonitorActive != lastVirtualActive_) {
        lastVirtualActive_ = snap.virtualMonitorActive;
        SetWindowTextW(GetDlgItem(hwnd_, ID_BTN_DISPLAY),
                       lastVirtualActive_ ? L"Disconnect display" : L"Connect display");
    }

    if (snap.hasError) {
        swprintf_s(buf, L"Error [%hs]: %s", ToString(snap.errorCode),
                   snap.errorUserMessage.c_str());
    } else {
        buf[0] = L'\0';
    }
    SetWindowTextW(GetDlgItem(hwnd_, ID_TXT_ERROR), buf);

    // Throttle tray updates: notify only when state or the displayed second
    // changes instead of every 100 ms tick (NFR-001).
    const UINT trayKey = static_cast<UINT>(snap.state) * 100000u +
                         static_cast<UINT>(snap.activeElapsedMs / 1000);
    if (trayKey != lastTrayKey_) {
        lastTrayKey_ = trayKey;
        tray_.Update(snap);
    }
    if (preview_ != nullptr && preview_->IsEnabled()) preview_->Tick();
}

void MainWindow::OnStartClicked() {
    StartRecordingRequest request;
    const int sel = ListBox_GetCurSel(GetDlgItem(hwnd_, ID_LIST_WINDOWS));
    if (sel >= 0 && sel < static_cast<int>(windows_.size())) {
        request.targetWindow = windows_[static_cast<size_t>(sel)].hwnd;
    } else {
        MessageBoxW(hwnd_,
                    L"Select an application window to record.\n\nNote: minimizing the target "
                    L"application may freeze its rendering during recording (Windows may stop "
                    L"redrawing minimized apps).",
                    L"Virtual Screen Recorder", MB_OK | MB_ICONINFORMATION);
        return;
    }

    wchar_t dir[MAX_PATH * 2] = {};
    GetWindowTextW(GetDlgItem(hwnd_, ID_EDIT_OUTDIR), dir, ARRAYSIZE(dir));
    request.outputDirectory = dir;

    const int preset = ComboBox_GetCurSel(GetDlgItem(hwnd_, ID_COMBO_PRESET));
    request.preset = preset == 0   ? QualityPreset::Low
                     : preset == 2 ? QualityPreset::High
                                   : QualityPreset::Standard;
    const int encoderPref = ComboBox_GetCurSel(GetDlgItem(hwnd_, ID_COMBO_ENCODER));
    request.encoderPreference = encoderPref == 1   ? encoder::EncoderPreference::Hardware
                                : encoderPref == 2 ? encoder::EncoderPreference::Software
                                                   : encoder::EncoderPreference::Auto;
    request.captureCursor =
        IsDlgButtonChecked(hwnd_, ID_CHK_CURSOR) == BST_CHECKED;
    request.captureAudio =
        IsDlgButtonChecked(hwnd_, ID_CHK_AUDIO) == BST_CHECKED;

    auto result = coordinator_.Start(request);
    if (!result) {
        MessageBoxW(hwnd_, result.GetError().userMessage.c_str(), L"Cannot start recording",
                    MB_OK | MB_ICONWARNING);
    }
    RefreshUi();
}

void MainWindow::OnPauseResumeClicked() {
    if (pauseButtonMode_ == 1) {
        (void)coordinator_.Resume();
    } else {
        (void)coordinator_.Pause();
    }
    RefreshUi();
}

void MainWindow::OnStopClicked() {
    OnStopClickedAsync();
}

void MainWindow::OnStopClickedAsync() {
    // FR-075 / NFR-006: finalization of a long recording can take up to the
    // bounded 10 s; the UI must stay responsive, so Stop runs on a worker and
    // the timer reaps it. Pause/Resume stay synchronous (no I/O).
    if (stopOpRunning_.load()) return;
    if (stopOp_.joinable()) stopOp_.join();

    stopOpRunning_.store(true);
    stopOpDone_.store(false);
    ApplyCommandStates(coordinator_.GetStatus()); // disable command buttons

    try {
        stopOp_ = std::thread([this] {
            (void)coordinator_.Stop(StopReason::User);
            stopOpDone_.store(true); // last touch: the timer takes over
        });
    } catch (...) {
        stopOpRunning_.store(false);
        stopOpDone_.store(false);
        // Synchronous fallback keeps the command effective (FR-073).
        (void)coordinator_.Stop(StopReason::User);
        RefreshUi();
    }
}

void MainWindow::ReapStopOperation() {
    if (!stopOpDone_.load()) return;
    if (stopOp_.joinable()) stopOp_.join();
    stopOpRunning_.store(false);
    stopOpDone_.store(false);
    RefreshUi();
}

void MainWindow::OnBrowseClicked() {
    // The common file dialog requires an STA thread; the UI thread is MTA
    // (wmain initializes COINIT_MULTITHREADED for the media stack) and
    // IFileDialog::Show deadlocks there. Run the dialog on a short-lived STA
    // thread and post the picked folder back as a message.
    EnableWindow(GetDlgItem(hwnd_, ID_BTN_BROWSE), FALSE);
    std::thread([this] {
        std::wstring picked;
        const HRESULT hrInit = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        IFileDialog* dialog = nullptr;
        if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                       IID_PPV_ARGS(&dialog)))) {
            DWORD options = 0;
            dialog->GetOptions(&options);
            dialog->SetOptions(options | FOS_PICKFOLDERS);
            if (SUCCEEDED(dialog->Show(nullptr))) {
                IShellItem* item = nullptr;
                if (SUCCEEDED(dialog->GetResult(&item)) && item != nullptr) {
                    PWSTR path = nullptr;
                    if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)) &&
                        path != nullptr) {
                        picked.assign(path);
                        CoTaskMemFree(path);
                    }
                    item->Release();
                }
            }
            dialog->Release();
        }
        if (hrInit == S_OK || hrInit == S_FALSE) CoUninitialize();
        PostMessageW(hwnd_, WM_APP_BROWSE_RESULT, 0,
                     reinterpret_cast<LPARAM>(new std::wstring(std::move(picked))));
    }).detach();
}

void MainWindow::OnOpenOutputFolder() {
    wchar_t dir[MAX_PATH * 2] = {};
    GetWindowTextW(GetDlgItem(hwnd_, ID_EDIT_OUTDIR), dir, ARRAYSIZE(dir));
    if (dir[0] != L'\0') {
        ShellExecuteW(hwnd_, L"open", dir, nullptr, nullptr, SW_SHOWNORMAL);
    }
}

void MainWindow::OnDiagnosticsClicked() {
    DiagnosticsContext context;
    context.appVersion = L"1.0.0-mvp";
    context.osBuild = L"Windows 11";
    auto pkg = CreateDiagnosticsPackage(context);
    if (pkg) {
        MessageBoxW(hwnd_,
                    (L"Diagnostics package created:\n" + pkg.Value().wstring()).c_str(),
                    L"Diagnostics", MB_OK | MB_ICONINFORMATION);
    } else {
        MessageBoxW(hwnd_, L"Diagnostics package could not be created. The log folder is "
                           L"available at %LOCALAPPDATA%\\VirtualScreenRecorder\\Logs.",
                    L"Diagnostics", MB_OK | MB_ICONWARNING);
    }
    SaveSettings();
}

void MainWindow::OnDisplayToggleClicked() {
    // FR-016: connect/disconnect the virtual display while idle. The work
    // can take a few seconds (device creation + monitor arrival wait), so it
    // runs on a worker; OnTimer reaps it and refreshes the UI.
    if (displayOpRunning_.load()) return;
    if (displayOp_.joinable()) displayOp_.join(); // reap a finished prior op

    displayOpRunning_.store(true);
    displayOpDone_.store(false);
    const bool connect = !lastVirtualActive_;
    try {
        displayOp_ = std::thread([this, connect] {
            if (connect) {
                (void)coordinator_.EnsureVirtualDisplay();
            } else {
                (void)coordinator_.DisableVirtualDisplay();
            }
            displayOpDone_.store(true); // last touch: the timer takes over
        });
    } catch (...) {
        displayOpRunning_.store(false);
        displayOpDone_.store(false);
        MessageBoxW(hwnd_, L"Could not start the display operation.", L"Virtual Screen Recorder",
                    MB_OK | MB_ICONWARNING);
        return;
    }
    ApplyCommandStates(coordinator_.GetStatus()); // reflect the disabled button
}

void MainWindow::ReapDisplayOperation() {
    if (!displayOpDone_.load()) return;
    if (displayOp_.joinable()) displayOp_.join();
    displayOpRunning_.store(false);
    displayOpDone_.store(false);
    RefreshUi();
}

void MainWindow::RefreshWindowList() {
    // Refresh keeps a confirmed selection stable when the same window is
    // still present (FR-022 exclusions applied inside the enumerator).
    HWND previouslySelected = nullptr;
    const int sel = ListBox_GetCurSel(GetDlgItem(hwnd_, ID_LIST_WINDOWS));
    if (sel >= 0 && sel < static_cast<int>(windows_.size())) {
        previouslySelected = windows_[static_cast<size_t>(sel)].hwnd;
    }

    HWND list = GetDlgItem(hwnd_, ID_LIST_WINDOWS);
    ListBox_ResetContent(list);
    // Icons are loaded fresh per refresh; release the previous batch so a
    // long-running session does not leak GDI handles.
    for (const auto& w : windows_) {
        if (w.icon != nullptr) DestroyIcon(w.icon);
    }
    windows_ = window::WindowEnumerator().Enumerate();

    int restoreIndex = -1;
    for (size_t i = 0; i < windows_.size(); ++i) {
        const auto& w = windows_[i];
        const std::wstring line = w.exeName + L"  -  " + w.title;
        ListBox_AddString(list, line.c_str());
        ListBox_SetItemData(list, static_cast<int>(i), reinterpret_cast<DWORD_PTR>(w.hwnd));
        if (w.hwnd == previouslySelected) restoreIndex = static_cast<int>(i);
    }
    if (restoreIndex >= 0) {
        ListBox_SetCurSel(list, restoreIndex);
    }
}

void MainWindow::SaveSettings() {
    SettingsStore store;
    store.Load(nullptr); // populates the unknown-field preservation set

    Settings settings;
    wchar_t dir[MAX_PATH * 2] = {};
    GetWindowTextW(GetDlgItem(hwnd_, ID_EDIT_OUTDIR), dir, ARRAYSIZE(dir));
    settings.outputDirectory = dir;
    const int preset = ComboBox_GetCurSel(GetDlgItem(hwnd_, ID_COMBO_PRESET));
    settings.preset = preset == 0   ? QualityPresetName::Low
                      : preset == 2 ? QualityPresetName::High
                                    : QualityPresetName::Standard;
    const int encoderPrefSave = ComboBox_GetCurSel(GetDlgItem(hwnd_, ID_COMBO_ENCODER));
    settings.encoderPreference = encoderPrefSave == 1   ? "hardware"
                                : encoderPrefSave == 2 ? "software"
                                                       : "auto";
    settings.recordSystemAudio =
        IsDlgButtonChecked(hwnd_, ID_CHK_AUDIO) == BST_CHECKED;
    settings.showPreview = IsDlgButtonChecked(hwnd_, ID_PREVIEW_TOGGLE) == BST_CHECKED;
    settings.captureCursor = IsDlgButtonChecked(hwnd_, ID_CHK_CURSOR) == BST_CHECKED;
    settings.minimizeToTray = Application::Instance().WantsMinimizeToTray();
    settings.restoreTargetWindow = true;
    if (store.Save(settings)) {
        Application::Instance().SetOutputDirectory(settings.outputDirectory);
    } else {
        log::Warning("UI", "SETTINGS_SAVE_FAILED", L"", L"Saving settings.json failed.");
    }
}

} // namespace vsr::app

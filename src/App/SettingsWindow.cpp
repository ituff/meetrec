#include "SettingsWindow.h"

#include <commctrl.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <shlobj.h>
#include <windowsx.h>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "shell32.lib")

#include <algorithm>
#include <filesystem>
#include <thread>
#include <vector>

#include <Application/Diagnostics.h>
#include <Infrastructure/Log.h>
#include <Infrastructure/Paths.h>
#include <Infrastructure/Strings.h>

#include "AppMenu.h"
#include "Application.h"

#if __has_include("VersionGenerated.h")
#include "VersionGenerated.h"
#endif

namespace vsr::app {

namespace {

constexpr wchar_t kSettingsClassName[] = L"VsrSettingsWindow";
constexpr UINT WM_APP_SETTINGS_BROWSE = WM_APP + 7;
constexpr int kPathEditId = 1001;
constexpr int kAppIconResource = 1;
constexpr wchar_t kAuthorSiteUrl[] = L"https://www.msone.top";
constexpr wchar_t kAuthorGithubUrl[] = L"https://github.com/ituff";

constexpr UINT kCmdLanguageBase = 3000;
constexpr UINT kCmdQualityBase = 3010;
constexpr UINT kCmdEncoderBase = 3020;
constexpr UINT kCmdOutputDeviceBase = 3030;
constexpr UINT kCmdInputDeviceBase = 3040;

bool InRect(const RECT& rect, POINT point) {
    return point.x >= rect.left && point.x < rect.right && point.y >= rect.top &&
           point.y < rect.bottom;
}

} // namespace

SettingsWindow::SettingsWindow(HWND owner, std::function<void()> onChanged)
    : owner_(owner), onChanged_(std::move(onChanged)) {}

SettingsWindow::~SettingsWindow() {
    if (hwnd_ != nullptr) {
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }
    if (editFont_ != nullptr) DeleteObject(editFont_);
    if (whiteBrush_ != nullptr) DeleteObject(whiteBrush_);
    if (memDc_ != nullptr) DeleteDC(memDc_);
    if (memBmp_ != nullptr) DeleteObject(memBmp_);
    fonts_.Destroy();
}

std::wstring SettingsWindow::DefaultOutputDirectory() {
    std::filesystem::path videos;
    PWSTR raw = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Videos, 0, nullptr, &raw)) && raw != nullptr) {
        videos = raw;
        CoTaskMemFree(raw);
    } else {
        wchar_t profile[MAX_PATH] = {};
        if (GetEnvironmentVariableW(L"USERPROFILE", profile, MAX_PATH) > 0) {
            videos = std::filesystem::path(profile) / L"Videos";
        } else {
            videos = std::filesystem::path(L"C:\\");
        }
    }
    videos /= L"MeetRec";
    std::error_code ec;
    std::filesystem::create_directories(videos, ec);
    return videos.wstring();
}

void SettingsWindow::Show(Page page) {
    if (hwnd_ != nullptr) {
        ShowPage(page);
        ShowWindow(hwnd_, SW_SHOWNORMAL);
        SetForegroundWindow(hwnd_);
        return;
    }
    page_ = page;
    CreateWindowIfNeeded();
    if (hwnd_ == nullptr) return;
    EnableWindow(owner_, FALSE);
    ShowPage(page);
    ShowWindow(hwnd_, SW_SHOWNORMAL);
    SetForegroundWindow(hwnd_);
    SetFocus(hwnd_);
}

void SettingsWindow::CreateWindowIfNeeded() {
    if (hwnd_ != nullptr) return;
    HINSTANCE instance = GetModuleHandleW(nullptr);
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = &SettingsWindow::WndProcThunk;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = kSettingsClassName;
    wc.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(kAppIconResource));
    RegisterClassExW(&wc);

    dpi_ = owner_ != nullptr ? GetDpiForWindow(owner_) : GetDpiForSystem();
    if (dpi_ == 0) dpi_ = 96;
    LoadSettings();
    fonts_.Create(dpi_, i18n::CurrentLanguage());
    editFont_ = CreateFontW(-ui::Scale(13, dpi_), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
                            i18n::CurrentLanguage() == i18n::Language::ZhCn
                                ? L"Microsoft YaHei UI"
                                : L"Segoe UI");
    whiteBrush_ = CreateSolidBrush(RGB(255, 255, 255));

    MONITORINFO info = {};
    info.cbSize = sizeof(info);
    GetMonitorInfoW(MonitorFromWindow(owner_, MONITOR_DEFAULTTONEAREST), &info);
    const int workWidth = info.rcWork.right - info.rcWork.left;
    const int workHeight = info.rcWork.bottom - info.rcWork.top;
    const int width = std::min(ui::Scale(620, dpi_), workWidth - ui::Scale(32, dpi_));
    const int height = std::min(ui::Scale(520, dpi_), workHeight - ui::Scale(32, dpi_));
    const int x = info.rcWork.left + (workWidth - width) / 2;
    const int y = info.rcWork.top + (workHeight - height) / 2;
    const std::wstring title = std::wstring(i18n::Text(i18n::Str::AppName)) + L" - " +
                               i18n::Text(i18n::Str::SettingsTitle);
    hwnd_ = CreateWindowExW(WS_EX_TOOLWINDOW, kSettingsClassName, title.c_str(),
                            WS_POPUP | WS_CLIPCHILDREN, x, y, width, height, owner_,
                            nullptr, instance, this);
    if (hwnd_ == nullptr) {
        log::Error("UI", "SETTINGS_WINDOW_FAILED", L"",
                   L"CreateWindowExW failed for the settings window.",
                   {{"error", std::to_string(GetLastError())}});
        return;
    }
    SetWindowLongPtrW(hwnd_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
    DWORD corner = DWMWCP_ROUND;
    DwmSetWindowAttribute(hwnd_, DWMWA_WINDOW_CORNER_PREFERENCE, &corner, sizeof(corner));

    pathEdit_ = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | ES_AUTOHSCROLL, 0, 0, 0, 0,
                                hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kPathEditId)), instance,
                                nullptr);
    if (pathEdit_ != nullptr) {
        SendMessageW(pathEdit_, WM_SETFONT, reinterpret_cast<WPARAM>(editFont_), TRUE);
        SetWindowSubclass(pathEdit_, &SettingsWindow::EditSubclassProc, 1,
                          reinterpret_cast<DWORD_PTR>(this));
    }

    RefreshAudioDevices();
    UpdateLayout();
    SetWindowTextW(pathEdit_, settings_.outputDirectory.c_str());
    SetWindowTextW(hwnd_, title.c_str());
}

void SettingsWindow::OnDpiChanged(UINT dpi) {
    dpi_ = dpi;
    fonts_.Create(dpi_, i18n::CurrentLanguage());
    if (editFont_ != nullptr) DeleteObject(editFont_);
    editFont_ = CreateFontW(-ui::Scale(13, dpi_), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
                            i18n::CurrentLanguage() == i18n::Language::ZhCn
                                ? L"Microsoft YaHei UI"
                                : L"Segoe UI");
    if (pathEdit_ != nullptr) {
        SendMessageW(pathEdit_, WM_SETFONT, reinterpret_cast<WPARAM>(editFont_), TRUE);
    }
    UpdateLayout();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void SettingsWindow::LoadSettings() {
    SettingsStore store;
    settings_ = store.Load(nullptr);
    if (settings_.outputDirectory.empty()) {
        settings_.outputDirectory = DefaultOutputDirectory();
    }
    std::error_code ec;
    std::filesystem::create_directories(settings_.outputDirectory, ec);
    i18n::SetLanguage(i18n::LanguageFromSetting(settings_.uiLanguage));
}

void SettingsWindow::SaveAndNotify() {
    FlushPathEdit();
    if (!settings_.outputDirectory.empty()) {
        std::error_code ec;
        std::filesystem::create_directories(settings_.outputDirectory, ec);
    }
    SettingsStore store;
    store.Save(settings_);
    if (onChanged_) onChanged_();
}

void SettingsWindow::FlushPathEdit() {
    if (pathEdit_ == nullptr || loadingEdit_) return;
    wchar_t text[1024] = {};
    GetWindowTextW(pathEdit_, text, ARRAYSIZE(text));
    settings_.outputDirectory = text;
}

void SettingsWindow::ApplyLanguage() {
    i18n::SetLanguage(i18n::LanguageFromSetting(settings_.uiLanguage));
    fonts_.Create(dpi_, i18n::CurrentLanguage());
    if (editFont_ != nullptr) DeleteObject(editFont_);
    editFont_ = CreateFontW(-ui::Scale(13, dpi_), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
                            i18n::CurrentLanguage() == i18n::Language::ZhCn
                                ? L"Microsoft YaHei UI"
                                : L"Segoe UI");
    if (pathEdit_ != nullptr) {
        SendMessageW(pathEdit_, WM_SETFONT, reinterpret_cast<WPARAM>(editFont_), TRUE);
    }
    const std::wstring title = std::wstring(i18n::Text(i18n::Str::AppName)) + L" - " +
                               i18n::Text(i18n::Str::SettingsTitle);
    SetWindowTextW(hwnd_, title.c_str());
    UpdateLayout();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void SettingsWindow::RefreshAudioDevices() {
    audio::ComScope com;
    outputDevices_ = audio::EnumerateEndpoints(eRender);
    inputDevices_ = audio::EnumerateEndpoints(eCapture);
    defaultOutputName_ = audio::DefaultEndpointName(eRender);
    defaultInputName_ = audio::DefaultEndpointName(eCapture);
}

std::wstring SettingsWindow::DeviceLabel(bool output) const {
    const auto& devices = output ? outputDevices_ : inputDevices_;
    const std::wstring& id =
        output ? settings_.audioOutputDeviceId : settings_.audioInputDeviceId;
    if (!id.empty()) {
        for (const auto& device : devices) {
            if (device.id == id) return device.name;
        }
    }
    const std::wstring& defaultName = output ? defaultOutputName_ : defaultInputName_;
    if (defaultName.empty()) return i18n::Text(i18n::Str::DefaultDevicePrefix);
    return i18n::Format(i18n::Str::DefaultDeviceLabel, {defaultName});
}

void SettingsWindow::ShowPage(Page page) {
    page_ = page;
    if (page == Page::Audio) RefreshAudioDevices();
    if (pathEdit_ != nullptr) {
        if (page == Page::General) {
            FlushPathEdit();
            loadingEdit_ = true;
            SetWindowTextW(pathEdit_, settings_.outputDirectory.c_str());
            loadingEdit_ = false;
            ShowWindow(pathEdit_, SW_SHOW);
        } else {
            FlushPathEdit();
            ShowWindow(pathEdit_, SW_HIDE);
        }
    }
    UpdateLayout();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void SettingsWindow::UpdateLayout() {
    RECT rc = {};
    GetClientRect(hwnd_, &rc);
    const float width = static_cast<float>(rc.right - rc.left);
    const float height = static_cast<float>(rc.bottom - rc.top);
    if (width <= 0.0f || height <= 0.0f) return;
    const auto S = [&](int value) { return ui::ScaleF(static_cast<float>(value), dpi_); };

    Layout layout;
    layout.titleBar = Gdiplus::RectF(0, 0, width, S(44));
    layout.logo = Gdiplus::RectF(S(16), S(11), S(22), S(22));
    layout.title = Gdiplus::RectF(S(46), S(6), width - S(140), S(32));
    layout.close = Gdiplus::RectF(width - S(44), 0, S(44), S(44));
    layout.minimize = Gdiplus::RectF(width - S(88), 0, S(44), S(44));
    for (int i = 0; i < 4; ++i) {
        layout.nav[i] = Gdiplus::RectF(S(12), S(60) + S(46) * static_cast<float>(i),
                                       S(128), S(40));
    }
    const float contentX = S(164);
    const float contentWidth = width - contentX - S(20);
    layout.content = Gdiplus::RectF(contentX, S(60), contentWidth, height - S(76));

    const float contentTop = S(64);
    if (page_ == Page::General) {
        layout.saveLabel = Gdiplus::RectF(contentX, contentTop, contentWidth, S(18));
        layout.saveField = Gdiplus::RectF(contentX, contentTop + S(24),
                                          contentWidth - S(90), S(34));
        layout.browse = Gdiplus::RectF(contentX + contentWidth - S(80),
                                       contentTop + S(24), S(80), S(34));
        layout.fileNameLabel =
            Gdiplus::RectF(contentX, contentTop + S(72), contentWidth, S(18));
        layout.fileNameField =
            Gdiplus::RectF(contentX, contentTop + S(96), contentWidth, S(34));
        layout.fileNameNote =
            Gdiplus::RectF(contentX, contentTop + S(136), contentWidth, S(18));
        layout.afterLabel =
            Gdiplus::RectF(contentX, contentTop + S(170), contentWidth, S(18));
        layout.notificationBox =
            Gdiplus::RectF(contentX, contentTop + S(196), S(18), S(18));
        layout.notificationLabel =
            Gdiplus::RectF(contentX + S(30), contentTop + S(194), contentWidth, S(22));
        layout.autoOpenBox =
            Gdiplus::RectF(contentX, contentTop + S(224), S(18), S(18));
        layout.autoOpenLabel =
            Gdiplus::RectF(contentX + S(30), contentTop + S(222), contentWidth, S(22));
        layout.trayBox = Gdiplus::RectF(contentX, contentTop + S(252), S(18), S(18));
        layout.trayLabel =
            Gdiplus::RectF(contentX + S(30), contentTop + S(250), contentWidth, S(22));
        layout.trayNote =
            Gdiplus::RectF(contentX + S(30), contentTop + S(272), contentWidth, S(18));
        layout.languageLabel =
            Gdiplus::RectF(contentX, contentTop + S(306), contentWidth, S(18));
        layout.languageField =
            Gdiplus::RectF(contentX, contentTop + S(330), S(240), S(34));
    } else if (page_ == Page::Audio) {
        layout.audioHint = Gdiplus::RectF(contentX, contentTop, contentWidth, S(42));
        for (int i = 0; i < 2; ++i) {
            const float y = contentTop + S(54) + S(108) * static_cast<float>(i);
            layout.audioCard[i] = Gdiplus::RectF(contentX, y, contentWidth, S(98));
            layout.audioTitle[i] =
                Gdiplus::RectF(contentX + S(16), y + S(10), contentWidth - S(100), S(26));
            layout.audioToggle[i] = Gdiplus::RectF(
                contentX + contentWidth - S(58), y + S(10), S(42), S(24));
            layout.audioDevice[i] =
                Gdiplus::RectF(contentX + S(16), y + S(48), contentWidth - S(32), S(32));
        }
    } else if (page_ == Page::Recording) {
        layout.recordingHint = Gdiplus::RectF(contentX, contentTop, contentWidth, S(40));
        layout.qualityLabel =
            Gdiplus::RectF(contentX, contentTop + S(50), contentWidth, S(18));
        layout.qualityField =
            Gdiplus::RectF(contentX, contentTop + S(74), S(300), S(34));
        layout.encoderLabel =
            Gdiplus::RectF(contentX, contentTop + S(118), contentWidth, S(18));
        layout.encoderField =
            Gdiplus::RectF(contentX, contentTop + S(142), S(300), S(34));
        layout.cursorBox =
            Gdiplus::RectF(contentX, contentTop + S(194), S(18), S(18));
        layout.cursorLabel =
            Gdiplus::RectF(contentX + S(30), contentTop + S(192), contentWidth, S(22));
        layout.previewBox =
            Gdiplus::RectF(contentX, contentTop + S(226), S(18), S(18));
        layout.previewLabel =
            Gdiplus::RectF(contentX + S(30), contentTop + S(224), contentWidth, S(22));
    } else {
        layout.aboutLogo = Gdiplus::RectF(contentX, contentTop, S(44), S(44));
        layout.aboutTitle =
            Gdiplus::RectF(contentX + S(60), contentTop - S(2), contentWidth - S(60), S(28));
        layout.aboutVersion =
            Gdiplus::RectF(contentX + S(60), contentTop + S(24), contentWidth - S(60), S(18));
        layout.aboutBuild =
            Gdiplus::RectF(contentX + S(60), contentTop + S(44), contentWidth - S(60), S(18));
        layout.aboutDescription =
            Gdiplus::RectF(contentX, contentTop + S(72), contentWidth, S(58));
        layout.aboutAuthorLabel =
            Gdiplus::RectF(contentX, contentTop + S(138), contentWidth, S(18));
        layout.aboutAuthorName =
            Gdiplus::RectF(contentX, contentTop + S(158), contentWidth, S(20));
        layout.aboutAuthorSite =
            Gdiplus::RectF(contentX, contentTop + S(180), contentWidth, S(20));
        layout.aboutAuthorGithub =
            Gdiplus::RectF(contentX, contentTop + S(200), contentWidth, S(20));
        layout.aboutDiagnostics =
            Gdiplus::RectF(contentX, contentTop + S(236), S(160), S(38));
        layout.aboutLogs =
            Gdiplus::RectF(contentX + S(172), contentTop + S(236), S(160), S(38));
        layout.aboutLicense =
            Gdiplus::RectF(contentX, contentTop + S(286), contentWidth, S(34));
    }
    layout_ = layout;

    if (pathEdit_ != nullptr) {
        if (page_ == Page::General) {
            MoveWindow(pathEdit_, static_cast<int>(layout_.saveField.X) + ui::Scale(10, dpi_),
                       static_cast<int>(layout_.saveField.Y) + ui::Scale(7, dpi_),
                       static_cast<int>(layout_.saveField.Width) - ui::Scale(20, dpi_),
                       ui::Scale(20, dpi_), TRUE);
            ShowWindow(pathEdit_, SW_SHOW);
        } else {
            ShowWindow(pathEdit_, SW_HIDE);
        }
    }

    BuildHits();
}

void SettingsWindow::BuildHits() {
    hits_.clear();
    auto add = [&](Hit id, const Gdiplus::RectF& rect) {
        if (IsHitEnabled(id)) hits_.push_back(HitTarget{id, rect});
    };
    add(Hit::NavGeneral, layout_.nav[0]);
    add(Hit::NavAudio, layout_.nav[1]);
    add(Hit::NavRecording, layout_.nav[2]);
    add(Hit::NavAbout, layout_.nav[3]);

    if (page_ == Page::General) {
        add(Hit::Browse, layout_.browse);
        add(Hit::Notification,
            Gdiplus::RectF(layout_.notificationBox.X, layout_.notificationBox.Y,
                           layout_.notificationLabel.GetRight() -
                               layout_.notificationBox.X,
                           layout_.notificationBox.Height));
        add(Hit::AutoOpen,
            Gdiplus::RectF(layout_.autoOpenBox.X, layout_.autoOpenBox.Y,
                           layout_.autoOpenLabel.GetRight() - layout_.autoOpenBox.X,
                           layout_.autoOpenBox.Height));
        add(Hit::MinimizeTray,
            Gdiplus::RectF(layout_.trayBox.X, layout_.trayBox.Y,
                           layout_.trayLabel.GetRight() - layout_.trayBox.X,
                           layout_.trayBox.Height));
        add(Hit::Language, layout_.languageField);
    } else if (page_ == Page::Audio) {
        add(Hit::SystemAudio, layout_.audioToggle[0]);
        add(Hit::OutputDevice, layout_.audioDevice[0]);
        add(Hit::Mic, layout_.audioToggle[1]);
        add(Hit::InputDevice, layout_.audioDevice[1]);
    } else if (page_ == Page::Recording) {
        add(Hit::Quality, layout_.qualityField);
        add(Hit::Encoder, layout_.encoderField);
        add(Hit::Cursor,
            Gdiplus::RectF(layout_.cursorBox.X, layout_.cursorBox.Y,
                           layout_.cursorLabel.GetRight() - layout_.cursorBox.X,
                           layout_.cursorBox.Height));
        add(Hit::Preview,
            Gdiplus::RectF(layout_.previewBox.X, layout_.previewBox.Y,
                           layout_.previewLabel.GetRight() - layout_.previewBox.X,
                           layout_.previewBox.Height));
    } else {
        add(Hit::Diagnostics, layout_.aboutDiagnostics);
        add(Hit::Logs, layout_.aboutLogs);
        add(Hit::AuthorSite, layout_.aboutAuthorSite);
        add(Hit::AuthorGithub, layout_.aboutAuthorGithub);
    }
    add(Hit::MinimizeButton, layout_.minimize);
    add(Hit::CloseButton, layout_.close);

    auto stillValid = [&](Hit id) {
        return std::any_of(hits_.begin(), hits_.end(),
                           [id](const HitTarget& hit) { return hit.id == id; });
    };
    if (hover_ != Hit::None && !stillValid(hover_)) hover_ = Hit::None;
    if (pressed_ != Hit::None && !stillValid(pressed_)) pressed_ = Hit::None;
    if (focus_ != Hit::None && !stillValid(focus_)) focus_ = Hit::None;
}

SettingsWindow::Hit SettingsWindow::HitTest(POINT point) const {
    for (const HitTarget& hit : hits_) {
        if (static_cast<float>(point.x) >= hit.rect.X &&
            static_cast<float>(point.x) < hit.rect.GetRight() &&
            static_cast<float>(point.y) >= hit.rect.Y &&
            static_cast<float>(point.y) < hit.rect.GetBottom()) {
            return hit.id;
        }
    }
    return Hit::None;
}

bool SettingsWindow::IsHitEnabled(Hit id) const {
    switch (id) {
        case Hit::None:
            return false;
        default:
            return true;
    }
}

void SettingsWindow::SetHover(Hit hit) {
    if (hover_ == hit) return;
    hover_ = hit;
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void SettingsWindow::Activate(Hit id) {
    switch (id) {
        case Hit::CloseButton:
            PostMessageW(hwnd_, WM_CLOSE, 0, 0);
            break;
        case Hit::MinimizeButton:
            ShowWindow(hwnd_, SW_MINIMIZE);
            break;
        case Hit::NavGeneral:
            ShowPage(Page::General);
            break;
        case Hit::NavAudio:
            ShowPage(Page::Audio);
            break;
        case Hit::NavRecording:
            ShowPage(Page::Recording);
            break;
        case Hit::NavAbout:
            ShowPage(Page::About);
            break;
        case Hit::Browse:
            BrowseForFolder();
            break;
        case Hit::Notification:
            settings_.showCompletionNotification = !settings_.showCompletionNotification;
            SaveAndNotify();
            InvalidateRect(hwnd_, nullptr, FALSE);
            break;
        case Hit::AutoOpen:
            settings_.autoOpenOutputFolder = !settings_.autoOpenOutputFolder;
            SaveAndNotify();
            InvalidateRect(hwnd_, nullptr, FALSE);
            break;
        case Hit::MinimizeTray:
            settings_.minimizeToTray = !settings_.minimizeToTray;
            SaveAndNotify();
            InvalidateRect(hwnd_, nullptr, FALSE);
            break;
        case Hit::Language:
            ShowLanguageMenu();
            break;
        case Hit::SystemAudio:
            settings_.recordSystemAudio = !settings_.recordSystemAudio;
            SaveAndNotify();
            InvalidateRect(hwnd_, nullptr, FALSE);
            break;
        case Hit::Mic:
            settings_.recordMicrophone = !settings_.recordMicrophone;
            SaveAndNotify();
            InvalidateRect(hwnd_, nullptr, FALSE);
            break;
        case Hit::OutputDevice:
            ShowDeviceMenu(true);
            break;
        case Hit::InputDevice:
            ShowDeviceMenu(false);
            break;
        case Hit::Quality:
            ShowQualityMenu();
            break;
        case Hit::Encoder:
            ShowEncoderMenu();
            break;
        case Hit::Cursor:
            settings_.captureCursor = !settings_.captureCursor;
            SaveAndNotify();
            InvalidateRect(hwnd_, nullptr, FALSE);
            break;
        case Hit::Preview:
            settings_.showPreview = !settings_.showPreview;
            SaveAndNotify();
            InvalidateRect(hwnd_, nullptr, FALSE);
            break;
        case Hit::Diagnostics:
            ShowDiagnostics();
            break;
        case Hit::Logs:
            OpenLogs();
            break;
        case Hit::AuthorSite:
            ShellExecuteW(hwnd_, L"open", kAuthorSiteUrl, nullptr, nullptr, SW_SHOWNORMAL);
            break;
        case Hit::AuthorGithub:
            ShellExecuteW(hwnd_, L"open", kAuthorGithubUrl, nullptr, nullptr, SW_SHOWNORMAL);
            break;
        default:
            break;
    }
}

void SettingsWindow::CycleFocus(bool backwards) {
    if (hits_.empty()) return;
    int index = -1;
    for (size_t i = 0; i < hits_.size(); ++i) {
        if (hits_[i].id == focus_) {
            index = static_cast<int>(i);
            break;
        }
    }
    if (index < 0) {
        index = backwards ? static_cast<int>(hits_.size()) - 1 : 0;
    } else {
        index += backwards ? -1 : 1;
        if (index < 0) index = static_cast<int>(hits_.size()) - 1;
        if (index >= static_cast<int>(hits_.size())) index = 0;
    }
    focus_ = hits_[static_cast<size_t>(index)].id;
    keyboardFocus_ = true;
    SetFocus(hwnd_);
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void SettingsWindow::ShowLanguageMenu() {
    int current = 0;
    if (settings_.uiLanguage == "zh-CN") current = 1;
    else if (settings_.uiLanguage == "en-US") current = 2;
    std::vector<menu::Item> items;
    items.push_back({i18n::Text(i18n::Str::LanguageSystem), L"", ui::Icon::Globe,
                     kCmdLanguageBase, true, current == 0, false, false});
    items.push_back({i18n::Text(i18n::Str::LanguageChinese), L"", ui::Icon::Globe,
                     kCmdLanguageBase + 1, true, current == 1, false, false});
    items.push_back({i18n::Text(i18n::Str::LanguageEnglish), L"", ui::Icon::Globe,
                     kCmdLanguageBase + 2, true, current == 2, false, false});
    POINT point = {static_cast<LONG>(layout_.languageField.X),
                   static_cast<LONG>(layout_.languageField.GetBottom())};
    ClientToScreen(hwnd_, &point);
    const UINT command =
        static_cast<UINT>(menu::Show(hwnd_, point, items, fonts_, false));
    if (command < kCmdLanguageBase) return;
    const UINT index = command - kCmdLanguageBase;
    settings_.uiLanguage = index == 1 ? "zh-CN" : (index == 2 ? "en-US" : "system");
    SaveAndNotify();
    ApplyLanguage();
}

void SettingsWindow::ShowQualityMenu() {
    int current = settings_.preset == QualityPresetName::Low
                      ? 0
                      : (settings_.preset == QualityPresetName::High ? 2 : 1);
    std::vector<menu::Item> items;
    items.push_back({i18n::Text(i18n::Str::QualityLow), L"", ui::Icon::None,
                     kCmdQualityBase, true, current == 0, false, false});
    items.push_back({i18n::Text(i18n::Str::QualityStandard), L"", ui::Icon::None,
                     kCmdQualityBase + 1, true, current == 1, false, false});
    items.push_back({i18n::Text(i18n::Str::QualityHigh), L"", ui::Icon::None,
                     kCmdQualityBase + 2, true, current == 2, false, false});
    POINT point = {static_cast<LONG>(layout_.qualityField.X),
                   static_cast<LONG>(layout_.qualityField.GetBottom())};
    ClientToScreen(hwnd_, &point);
    const UINT command =
        static_cast<UINT>(menu::Show(hwnd_, point, items, fonts_, false));
    if (command < kCmdQualityBase) return;
    settings_.preset = command - kCmdQualityBase == 0
                           ? QualityPresetName::Low
                           : (command - kCmdQualityBase == 2 ? QualityPresetName::High
                                                             : QualityPresetName::Standard);
    SaveAndNotify();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void SettingsWindow::ShowEncoderMenu() {
    const int current = settings_.encoderPreference == "hardware"
                            ? 1
                            : (settings_.encoderPreference == "software" ? 2 : 0);
    std::vector<menu::Item> items;
    items.push_back({i18n::Text(i18n::Str::EncoderAuto), L"", ui::Icon::None,
                     kCmdEncoderBase, true, current == 0, false, false});
    items.push_back({i18n::Text(i18n::Str::EncoderHardware), L"", ui::Icon::None,
                     kCmdEncoderBase + 1, true, current == 1, false, false});
    items.push_back({i18n::Text(i18n::Str::EncoderSoftware), L"", ui::Icon::None,
                     kCmdEncoderBase + 2, true, current == 2, false, false});
    POINT point = {static_cast<LONG>(layout_.encoderField.X),
                   static_cast<LONG>(layout_.encoderField.GetBottom())};
    ClientToScreen(hwnd_, &point);
    const UINT command =
        static_cast<UINT>(menu::Show(hwnd_, point, items, fonts_, false));
    if (command < kCmdEncoderBase) return;
    const UINT index = command - kCmdEncoderBase;
    settings_.encoderPreference = index == 1 ? "hardware" : (index == 2 ? "software" : "auto");
    SaveAndNotify();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void SettingsWindow::ShowDeviceMenu(bool output) {
    RefreshAudioDevices();
    const auto& devices = output ? outputDevices_ : inputDevices_;
    const std::wstring& currentId =
        output ? settings_.audioOutputDeviceId : settings_.audioInputDeviceId;
    const std::wstring defaultName = output ? defaultOutputName_ : defaultInputName_;
    std::wstring defaultLabel = i18n::Text(i18n::Str::DefaultDevicePrefix);
    if (!defaultName.empty()) {
        defaultLabel = i18n::Format(i18n::Str::DefaultDeviceLabel, {defaultName});
    }
    const UINT base = output ? kCmdOutputDeviceBase : kCmdInputDeviceBase;
    std::vector<menu::Item> items;
    items.push_back({defaultLabel, L"", ui::Icon::None, base, true, currentId.empty(),
                     false, false});
    for (size_t i = 0; i < devices.size(); ++i) {
        items.push_back({devices[i].name, L"", ui::Icon::None,
                         base + static_cast<UINT>(i + 1), true,
                         devices[i].id == currentId, false, false});
    }
    if (devices.empty() && defaultName.empty()) {
        items.clear();
        items.push_back({i18n::Text(i18n::Str::AudioNoDevices), L"", ui::Icon::None, 0,
                         false, false, false, false});
    }
    const Gdiplus::RectF field = output ? layout_.audioDevice[0] : layout_.audioDevice[1];
    POINT point = {static_cast<LONG>(field.X), static_cast<LONG>(field.GetBottom())};
    ClientToScreen(hwnd_, &point);
    const UINT command =
        static_cast<UINT>(menu::Show(hwnd_, point, items, fonts_, false));
    if (command < base) return;
    const size_t index = command - base;
    std::wstring id;
    if (index > 0 && index <= devices.size()) id = devices[index - 1].id;
    if (output) settings_.audioOutputDeviceId = id;
    else settings_.audioInputDeviceId = id;
    SaveAndNotify();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void SettingsWindow::BrowseForFolder() {
    if (browseRunning_) return;
    browseRunning_ = true;
    const HWND owner = hwnd_;
    const std::wstring initial = settings_.outputDirectory;
    std::thread([owner, initial] {
        std::wstring picked;
        const HRESULT hrInit = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        IFileDialog* dialog = nullptr;
        if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                       IID_PPV_ARGS(&dialog))) &&
            dialog != nullptr) {
            DWORD options = 0;
            dialog->GetOptions(&options);
            dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
            dialog->SetTitle(i18n::Text(i18n::Str::BrowseForFolder));
            if (!initial.empty()) {
                IShellItem* folder = nullptr;
                if (SUCCEEDED(SHCreateItemFromParsingName(initial.c_str(), nullptr,
                                                          IID_PPV_ARGS(&folder))) &&
                    folder != nullptr) {
                    dialog->SetFolder(folder);
                    folder->Release();
                }
            }
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
        auto* payload = new std::wstring(std::move(picked));
        if (!PostMessageW(owner, WM_APP_SETTINGS_BROWSE, 0,
                          reinterpret_cast<LPARAM>(payload))) {
            delete payload;
        }
    }).detach();
}

void SettingsWindow::ShowDiagnostics() {
    DiagnosticsContext context;
    context.appVersion = L"1.1.0.0";
    context.osBuild = Application::OsBuildString();
    auto package = CreateDiagnosticsPackage(context);
    if (package) {
        const std::wstring body =
            i18n::Format(i18n::Str::DiagnosticsCreatedBody, {package.Value().wstring()});
        MessageBoxW(hwnd_, body.c_str(), i18n::Text(i18n::Str::DiagnosticsCreated),
                    MB_OK | MB_ICONINFORMATION);
    } else {
        const std::wstring body =
            i18n::Format(i18n::Str::DiagnosticsFailedBody, {LogDir().wstring()});
        MessageBoxW(hwnd_, body.c_str(), i18n::Text(i18n::Str::DiagnosticsFailed),
                    MB_OK | MB_ICONWARNING);
    }
}

void SettingsWindow::OpenLogs() {
    const std::wstring path = LogDir().wstring();
    ShellExecuteW(hwnd_, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

LRESULT CALLBACK SettingsWindow::WndProcThunk(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
    }
    auto* self = reinterpret_cast<SettingsWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    return self != nullptr ? self->WndProc(hwnd, msg, wp, lp)
                           : DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT CALLBACK SettingsWindow::EditSubclassProc(HWND hwnd, UINT msg, WPARAM wp,
                                                  LPARAM lp, UINT_PTR id, DWORD_PTR ref) {
    (void)id;
    auto* self = reinterpret_cast<SettingsWindow*>(ref);
    if (self != nullptr && msg == WM_KEYDOWN) {
        const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
        if (wp == VK_ESCAPE) {
            self->SaveAndNotify();
            PostMessageW(self->hwnd_, WM_CLOSE, 0, 0);
            return 0;
        }
        if (wp == VK_RETURN) {
            self->SaveAndNotify();
            SetFocus(self->hwnd_);
            return 0;
        }
        if (wp == VK_TAB) {
            self->SaveAndNotify();
            SetFocus(self->hwnd_);
            self->CycleFocus(shift);
            return 0;
        }
    }
    return DefSubclassProc(hwnd, msg, wp, lp);
}

LRESULT SettingsWindow::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_NCCALCSIZE:
            return 0;
        case WM_NCACTIVATE:
            return TRUE;
        case WM_NCPAINT:
            return 0;
        case WM_NCHITTEST: {
            POINT screen = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            POINT client = screen;
            ScreenToClient(hwnd, &client);
            const int titleHeight = ui::Scale(44, dpi_);
            if (client.y >= 0 && client.y < titleHeight) {
                const int width = static_cast<int>(layout_.close.GetRight());
                if (client.x < width - ui::Scale(88, dpi_)) return HTCAPTION;
            }
            return HTCLIENT;
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
            SetHover(HitTest(point));
            SetCursor(LoadCursorW(nullptr, hover_ != Hit::None ? IDC_HAND : IDC_ARROW));
            return 0;
        }
        case WM_MOUSELEAVE:
            trackingMouse_ = false;
            SetHover(Hit::None);
            return 0;
        case WM_LBUTTONDOWN: {
            SetFocus(hwnd);
            POINT point = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            const Hit hit = HitTest(point);
            if (hit != Hit::None) {
                pressed_ = hit;
                focus_ = hit;
                keyboardFocus_ = false;
                SetCapture(hwnd);
                InvalidateRect(hwnd, nullptr, FALSE);
            } else {
                focus_ = Hit::None;
                keyboardFocus_ = false;
            }
            return 0;
        }
        case WM_LBUTTONUP: {
            // ReleaseCapture synchronously delivers WM_CAPTURECHANGED, which
            // clears pressed_; capture the value first.
            const Hit pressed = pressed_;
            pressed_ = Hit::None;
            if (GetCapture() == hwnd) ReleaseCapture();
            if (pressed == Hit::None) return 0;
            POINT point = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            const Hit hit = HitTest(point);
            if (hit == pressed) Activate(pressed);
            else InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        case WM_CAPTURECHANGED:
            pressed_ = Hit::None;
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        case WM_KEYDOWN: {
            const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
            if (wp == VK_TAB) {
                CycleFocus(shift);
                return 0;
            }
            if (wp == VK_ESCAPE) {
                if (focus_ != Hit::None) {
                    focus_ = Hit::None;
                    keyboardFocus_ = false;
                    InvalidateRect(hwnd, nullptr, FALSE);
                } else {
                    PostMessageW(hwnd, WM_CLOSE, 0, 0);
                }
                return 0;
            }
            if (wp == VK_RETURN || wp == VK_SPACE) {
                if (focus_ != Hit::None) Activate(focus_);
                return 0;
            }
            break;
        }
        case WM_COMMAND: {
            const int id = LOWORD(wp);
            const int code = HIWORD(wp);
            if (id == kPathEditId && code == EN_CHANGE && !loadingEdit_) {
                FlushPathEdit();
                return 0;
            }
            if (id == kPathEditId && code == EN_KILLFOCUS) {
                SaveAndNotify();
                return 0;
            }
            break;
        }
        case WM_CTLCOLOREDIT: {
            HDC dc = reinterpret_cast<HDC>(wp);
            SetTextColor(dc, RGB(31, 35, 41));
            SetBkColor(dc, RGB(255, 255, 255));
            return reinterpret_cast<LRESULT>(whiteBrush_);
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
        case WM_APP_SETTINGS_BROWSE: {
            browseRunning_ = false;
            auto* picked = reinterpret_cast<std::wstring*>(lp);
            if (picked != nullptr) {
                if (!picked->empty()) {
                    settings_.outputDirectory = *picked;
                    loadingEdit_ = true;
                    SetWindowTextW(pathEdit_, picked->c_str());
                    loadingEdit_ = false;
                    SaveAndNotify();
                    InvalidateRect(hwnd_, nullptr, FALSE);
                }
                delete picked;
            }
            return 0;
        }
        case WM_CLOSE:
            SaveAndNotify();
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            hwnd_ = nullptr;
            pathEdit_ = nullptr;
            EnableWindow(owner_, TRUE);
            if (owner_ != nullptr && IsWindow(owner_)) SetForegroundWindow(owner_);
            if (onChanged_) onChanged_();
            return 0;
        default:
            break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

void SettingsWindow::Paint(HDC targetDc) {
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
        Draw(graphics, width, height);
    }
    HDC dc = targetDc != nullptr ? targetDc : GetDC(hwnd_);
    BitBlt(dc, 0, 0, width, height, memDc_, 0, 0, SRCCOPY);
    if (targetDc == nullptr) ReleaseDC(hwnd_, dc);
}

void SettingsWindow::Draw(Gdiplus::Graphics& graphics, int width, int height) {
    ui::FillRect(graphics, Gdiplus::RectF(0, 0, static_cast<float>(width),
                                           static_cast<float>(height)),
                 ui::colors::Window());
    DrawTitleBar(graphics);
    DrawNav(graphics);
    if (page_ == Page::General) DrawGeneral(graphics);
    else if (page_ == Page::Audio) DrawAudio(graphics);
    else if (page_ == Page::Recording) DrawRecording(graphics);
    else DrawAbout(graphics);

    if (keyboardFocus_ && focus_ != Hit::None) {
        for (const HitTarget& hit : hits_) {
            if (hit.id == focus_) {
                ui::StrokeRoundRect(graphics, Gdiplus::RectF(hit.rect.X - 2, hit.rect.Y - 2,
                                                             hit.rect.Width + 4,
                                                             hit.rect.Height + 4),
                                    6.0f, ui::colors::Primary(), 2.0f);
                break;
            }
        }
    }
}

void SettingsWindow::DrawTitleBar(Gdiplus::Graphics& graphics) {
    RECT rc = {};
    GetClientRect(hwnd_, &rc);
    const auto S = [&](float value) { return ui::ScaleF(value, dpi_); };
    ui::FillRect(graphics, layout_.titleBar, ui::colors::TitleBar());
    ui::DrawLine(graphics, 0, layout_.titleBar.GetBottom() - 0.5f,
                 static_cast<float>(rc.right), layout_.titleBar.GetBottom() - 0.5f,
                 ui::colors::Divider(), 1.0f);
    ui::DrawLogo(graphics, layout_.logo, S(5));
    ui::DrawText(graphics, fonts_,
                 std::wstring(i18n::Text(i18n::Str::AppName)) + L" - " +
                     i18n::Text(i18n::Str::SettingsTitle),
                 layout_.title, ui::FontRole::Title, ui::colors::Text(), ui::Align::Near,
                 ui::VAlign::Center, true);

    auto caption = [&](const Gdiplus::RectF& rect, ui::Icon icon, Hit id, bool danger) {
        const bool hover = hover_ == id;
        if (hover || pressed_ == id) {
            ui::FillRoundRect(graphics,
                              Gdiplus::RectF(rect.X + S(4), rect.Y + S(8),
                                             rect.Width - S(8), rect.Height - S(16)),
                              S(6), danger ? ui::colors::Danger() : ui::colors::HoverTint());
        }
        const Gdiplus::Color color =
            danger && hover ? ui::colors::White() : ui::colors::TextSecondary();
        ui::DrawIcon(graphics, icon,
                     Gdiplus::RectF(rect.X + (rect.Width - S(14)) / 2,
                                    rect.Y + (rect.Height - S(14)) / 2, S(14), S(14)),
                     color, S(1.3f));
    };
    caption(layout_.minimize, ui::Icon::Minimize, Hit::MinimizeButton, false);
    caption(layout_.close, ui::Icon::Close, Hit::CloseButton, true);
}

void SettingsWindow::DrawNav(Gdiplus::Graphics& graphics) {
    const auto S = [&](float value) { return ui::ScaleF(value, dpi_); };
    const struct {
        const wchar_t* label;
        ui::Icon icon;
        Page page;
        Hit hit;
    } items[] = {
        {i18n::Text(i18n::Str::NavGeneral), ui::Icon::Gear, Page::General, Hit::NavGeneral},
        {i18n::Text(i18n::Str::NavAudio), ui::Icon::Speaker, Page::Audio, Hit::NavAudio},
        {i18n::Text(i18n::Str::NavRecording), ui::Icon::Video, Page::Recording,
         Hit::NavRecording},
        {i18n::Text(i18n::Str::NavAbout), ui::Icon::Info, Page::About, Hit::NavAbout}};
    for (int i = 0; i < 4; ++i) {
        const Gdiplus::RectF rect = layout_.nav[i];
        const bool selected = page_ == items[i].page;
        if (selected) {
            ui::FillRoundRect(graphics, rect, S(8), ui::colors::PrimarySoft());
        } else if (hover_ == items[i].hit) {
            ui::FillRoundRect(graphics, rect, S(8), ui::colors::HoverTint());
        }
        const float iconSize = S(17);
        ui::DrawIcon(graphics, items[i].icon,
                     Gdiplus::RectF(rect.X + S(12), rect.Y + (rect.Height - iconSize) / 2.0f,
                                    iconSize, iconSize),
                     selected ? ui::colors::Primary() : ui::colors::TextSecondary(), S(1.4f));
        ui::DrawText(graphics, fonts_, items[i].label,
                     Gdiplus::RectF(rect.X + S(40), rect.Y, rect.Width - S(48),
                                    rect.Height),
                     ui::FontRole::BodyStrong,
                     selected ? ui::colors::Primary() : ui::colors::Text(),
                     ui::Align::Near, ui::VAlign::Center, false);
    }
}

void SettingsWindow::DrawField(Gdiplus::Graphics& graphics, const Gdiplus::RectF& rect,
                               const std::wstring& text, bool hover, bool chevron) {
    const auto S = [&](float value) { return ui::ScaleF(value, dpi_); };
    ui::FillRoundRectWithBorder(graphics, rect, S(6), ui::colors::Surface(),
                                hover ? ui::colors::Primary() : ui::colors::Border());
    ui::DrawText(graphics, fonts_, text,
                 Gdiplus::RectF(rect.X + S(10), rect.Y,
                                rect.Width - S(chevron ? 34.0f : 20.0f), rect.Height),
                 ui::FontRole::Small, ui::colors::Text(), ui::Align::Near,
                 ui::VAlign::Center, true);
    if (chevron) {
        ui::DrawIcon(graphics, ui::Icon::ChevronDown,
                     Gdiplus::RectF(rect.GetRight() - S(22),
                                    rect.Y + (rect.Height - S(12)) / 2.0f, S(12), S(12)),
                     ui::colors::TextSecondary(), S(1.3f));
    }
}

void SettingsWindow::DrawCheckRow(Gdiplus::Graphics& graphics, const Gdiplus::RectF& box,
                                  const Gdiplus::RectF& label, const std::wstring& text,
                                  bool checked, Hit id) {
    ui::DrawCheckBox(graphics, box, checked, true, hover_ == id, dpi_);
    ui::DrawText(graphics, fonts_, text, label, ui::FontRole::Body, ui::colors::Text(),
                 ui::Align::Near, ui::VAlign::Center, false);
}

void SettingsWindow::DrawToggleRow(Gdiplus::Graphics& graphics, const Gdiplus::RectF& card,
                                   const Gdiplus::RectF& title,
                                   const Gdiplus::RectF& toggle,
                                   const std::wstring& label, bool on, Hit id) {
    const auto S = [&](float value) { return ui::ScaleF(value, dpi_); };
    ui::FillRoundRectWithBorder(graphics, card, S(10), ui::colors::Surface(),
                                ui::colors::Border());
    ui::DrawText(graphics, fonts_, label, title, ui::FontRole::BodyStrong,
                 ui::colors::Text(), ui::Align::Near, ui::VAlign::Center, true);
    ui::DrawToggleSwitch(graphics, toggle, on, true, hover_ == id, dpi_);
}

void SettingsWindow::DrawGeneral(Gdiplus::Graphics& graphics) {
    const auto S = [&](float value) { return ui::ScaleF(value, dpi_); };
    ui::DrawText(graphics, fonts_, i18n::Text(i18n::Str::SaveLocationLabel),
                 layout_.saveLabel, ui::FontRole::BodyStrong, ui::colors::Text(),
                 ui::Align::Near, ui::VAlign::Center, false);
    DrawField(graphics, layout_.saveField, settings_.outputDirectory,
              hover_ == Hit::Browse, false);
    ui::DrawButton(graphics, fonts_, layout_.browse,
                   i18n::Text(i18n::Str::BrowseEllipsis), ui::ButtonStyle::Secondary,
                   hover_ == Hit::Browse, pressed_ == Hit::Browse, true);

    ui::DrawText(graphics, fonts_, i18n::Text(i18n::Str::FileNameFormat),
                 layout_.fileNameLabel, ui::FontRole::BodyStrong, ui::colors::Text(),
                 ui::Align::Near, ui::VAlign::Center, false);
    DrawField(graphics, layout_.fileNameField, L"Recording_yyyyMMdd_HHmmss.mp4", false,
              false);
    ui::DrawText(graphics, fonts_, i18n::Text(i18n::Str::FileNameFormatNote),
                 layout_.fileNameNote, ui::FontRole::Small, ui::colors::TextSecondary(),
                 ui::Align::Near, ui::VAlign::Center, true);

    ui::DrawText(graphics, fonts_, i18n::Text(i18n::Str::AfterRecording),
                 layout_.afterLabel, ui::FontRole::BodyStrong, ui::colors::Text(),
                 ui::Align::Near, ui::VAlign::Center, false);
    DrawCheckRow(graphics, layout_.notificationBox, layout_.notificationLabel,
                 i18n::Text(i18n::Str::ShowCompletionNotification),
                 settings_.showCompletionNotification, Hit::Notification);
    DrawCheckRow(graphics, layout_.autoOpenBox, layout_.autoOpenLabel,
                 i18n::Text(i18n::Str::AutoOpenOutputFolder),
                 settings_.autoOpenOutputFolder, Hit::AutoOpen);
    DrawCheckRow(graphics, layout_.trayBox, layout_.trayLabel,
                 i18n::Text(i18n::Str::MinimizeToTray), settings_.minimizeToTray,
                 Hit::MinimizeTray);
    ui::DrawText(graphics, fonts_, i18n::Text(i18n::Str::MinimizeToTrayNote),
                 layout_.trayNote, ui::FontRole::Small, ui::colors::TextSecondary(),
                 ui::Align::Near, ui::VAlign::Center, true);

    ui::DrawText(graphics, fonts_, i18n::Text(i18n::Str::InterfaceLanguage),
                 layout_.languageLabel, ui::FontRole::BodyStrong, ui::colors::Text(),
                 ui::Align::Near, ui::VAlign::Center, false);
    std::wstring languageLabel;
    if (settings_.uiLanguage == "zh-CN") languageLabel = i18n::Text(i18n::Str::LanguageChinese);
    else if (settings_.uiLanguage == "en-US") languageLabel = i18n::Text(i18n::Str::LanguageEnglish);
    else languageLabel = i18n::Text(i18n::Str::LanguageSystem);
    DrawField(graphics, layout_.languageField, languageLabel, hover_ == Hit::Language);
    (void)S;
}

void SettingsWindow::DrawAudio(Gdiplus::Graphics& graphics) {
    ui::DrawTextWrapped(graphics, fonts_, i18n::Text(i18n::Str::AudioSettingsHint),
                        layout_.audioHint, ui::FontRole::Small,
                        ui::colors::TextSecondary(), ui::Align::Near);
    for (int i = 0; i < 2; ++i) {
        const bool output = i == 0;
        DrawToggleRow(graphics, layout_.audioCard[i], layout_.audioTitle[i],
                      layout_.audioToggle[i],
                      i18n::Text(output ? i18n::Str::AudioSystemDefault
                                        : i18n::Str::AudioMicDefault),
                      output ? settings_.recordSystemAudio : settings_.recordMicrophone,
                      output ? Hit::SystemAudio : Hit::Mic);
        DrawField(graphics, layout_.audioDevice[i], DeviceLabel(output),
                  hover_ == (output ? Hit::OutputDevice : Hit::InputDevice));
    }
    const Gdiplus::RectF hint(layout_.audioCard[1].X, layout_.audioCard[1].GetBottom() + 12,
                              layout_.audioCard[1].Width, 50);
    ui::DrawTextWrapped(graphics, fonts_, i18n::Text(i18n::Str::AudioDeviceHint), hint,
                        ui::FontRole::Small, ui::colors::TextSecondary(),
                        ui::Align::Near);
}

void SettingsWindow::DrawRecording(Gdiplus::Graphics& graphics) {
    ui::DrawTextWrapped(graphics, fonts_, i18n::Text(i18n::Str::RecordingSettingsHint),
                        layout_.recordingHint, ui::FontRole::Small,
                        ui::colors::TextSecondary(), ui::Align::Near);
    ui::DrawText(graphics, fonts_, i18n::Text(i18n::Str::Quality), layout_.qualityLabel,
                 ui::FontRole::BodyStrong, ui::colors::Text(), ui::Align::Near,
                 ui::VAlign::Center, false);
    std::wstring quality = i18n::Text(i18n::Str::QualityStandard);
    if (settings_.preset == QualityPresetName::Low) quality = i18n::Text(i18n::Str::QualityLow);
    else if (settings_.preset == QualityPresetName::High) quality = i18n::Text(i18n::Str::QualityHigh);
    DrawField(graphics, layout_.qualityField, quality, hover_ == Hit::Quality);

    ui::DrawText(graphics, fonts_, i18n::Text(i18n::Str::Encoder), layout_.encoderLabel,
                 ui::FontRole::BodyStrong, ui::colors::Text(), ui::Align::Near,
                 ui::VAlign::Center, false);
    std::wstring encoder = i18n::Text(i18n::Str::EncoderAuto);
    if (settings_.encoderPreference == "hardware") encoder = i18n::Text(i18n::Str::EncoderHardware);
    else if (settings_.encoderPreference == "software") encoder = i18n::Text(i18n::Str::EncoderSoftware);
    DrawField(graphics, layout_.encoderField, encoder, hover_ == Hit::Encoder);

    DrawCheckRow(graphics, layout_.cursorBox, layout_.cursorLabel,
                 i18n::Text(i18n::Str::CaptureCursor), settings_.captureCursor,
                 Hit::Cursor);
    DrawCheckRow(graphics, layout_.previewBox, layout_.previewLabel,
                 i18n::Text(i18n::Str::PreviewDefault), settings_.showPreview,
                 Hit::Preview);
}

void SettingsWindow::DrawAbout(Gdiplus::Graphics& graphics) {
    const auto S = [&](float value) { return ui::ScaleF(value, dpi_); };
    ui::DrawLogo(graphics, layout_.aboutLogo, S(10));
    ui::DrawText(graphics, fonts_, i18n::Text(i18n::Str::AppName), layout_.aboutTitle,
                 ui::FontRole::Big, ui::colors::Text(), ui::Align::Near,
                 ui::VAlign::Center, false);
    ui::DrawText(graphics, fonts_,
                 std::wstring(i18n::Text(i18n::Str::AboutVersion)) + L" 1.1.0.0",
                 layout_.aboutVersion, ui::FontRole::Small, ui::colors::TextSecondary(),
                 ui::Align::Near, ui::VAlign::Center, false);
    std::wstring build = std::wstring(i18n::Text(i18n::Str::AboutBuild)) + L" ";
#ifdef VSR_BUILD_COMMIT
    build += VSR_BUILD_COMMIT;
#else
    build += L"-";
#endif
    ui::DrawText(graphics, fonts_, build, layout_.aboutBuild, ui::FontRole::Small,
                 ui::colors::TextSecondary(), ui::Align::Near, ui::VAlign::Center, true);
    ui::DrawTextWrapped(graphics, fonts_, i18n::Text(i18n::Str::AboutMadeFor),
                        layout_.aboutDescription, ui::FontRole::Body,
                        ui::colors::TextSecondary(), ui::Align::Near);
    ui::DrawText(graphics, fonts_, i18n::Text(i18n::Str::AboutAuthor),
                 layout_.aboutAuthorLabel, ui::FontRole::BodyStrong,
                 ui::colors::TextSecondary(), ui::Align::Near, ui::VAlign::Center, false);
    ui::DrawText(graphics, fonts_, L"Allen Cheng", layout_.aboutAuthorName,
                 ui::FontRole::Body, ui::colors::Text(), ui::Align::Near,
                 ui::VAlign::Center, false);
    ui::DrawText(graphics, fonts_, kAuthorSiteUrl, layout_.aboutAuthorSite,
                 ui::FontRole::Body,
                 hover_ == Hit::AuthorSite ? ui::colors::PrimaryHover()
                                           : ui::colors::Primary(),
                 ui::Align::Near, ui::VAlign::Center, false);
    ui::DrawText(graphics, fonts_, kAuthorGithubUrl, layout_.aboutAuthorGithub,
                 ui::FontRole::Body,
                 hover_ == Hit::AuthorGithub ? ui::colors::PrimaryHover()
                                             : ui::colors::Primary(),
                 ui::Align::Near, ui::VAlign::Center, false);
    ui::DrawButton(graphics, fonts_, layout_.aboutDiagnostics,
                   i18n::Text(i18n::Str::AboutDiagnostics), ui::ButtonStyle::Secondary,
                   hover_ == Hit::Diagnostics, pressed_ == Hit::Diagnostics, true,
                   ui::Icon::Diagnostics);
    ui::DrawButton(graphics, fonts_, layout_.aboutLogs,
                   i18n::Text(i18n::Str::AboutLogs), ui::ButtonStyle::Secondary,
                   hover_ == Hit::Logs, pressed_ == Hit::Logs, true,
                   ui::Icon::Folder);
    ui::DrawTextWrapped(graphics, fonts_, i18n::Text(i18n::Str::AboutLicense),
                        layout_.aboutLicense, ui::FontRole::Small,
                        ui::colors::TextTertiary(), ui::Align::Near);
}

} // namespace vsr::app

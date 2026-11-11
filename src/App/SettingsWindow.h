// MeetRec - settings window (General / Audio / Recording / About).
#pragma once

#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>

#include <functional>
#include <string>
#include <vector>

#include <Infrastructure/Settings.h>

#include "AudioDevices.h"
#include "UiDraw.h"

namespace vsr::app {

class SettingsWindow {
public:
    enum class Page { General = 0, Audio = 1, Recording = 2, About = 3 };

    SettingsWindow(HWND owner, std::function<void()> onChanged);
    ~SettingsWindow();

    SettingsWindow(const SettingsWindow&) = delete;
    SettingsWindow& operator=(const SettingsWindow&) = delete;

    void Show(Page page);
    void ShowPage(Page page);
    bool IsOpen() const { return hwnd_ != nullptr; }
    HWND Hwnd() const noexcept { return hwnd_; }

private:
    enum class Hit {
        None,
        CloseButton,
        MinimizeButton,
        NavGeneral,
        NavAudio,
        NavRecording,
        NavAbout,
        Browse,
        Notification,
        AutoOpen,
        MinimizeTray,
        AutoUpdate,
        Language,
        SystemAudio,
        Mic,
        OutputDevice,
        InputDevice,
        Quality,
        Encoder,
        Cursor,
        Preview,
        Diagnostics,
        Logs,
        AuthorSite,
        AuthorGithub,
    };

    struct HitTarget {
        Hit id = Hit::None;
        Gdiplus::RectF rect;
    };

    struct Layout {
        Gdiplus::RectF titleBar;
        Gdiplus::RectF logo;
        Gdiplus::RectF title;
        Gdiplus::RectF minimize;
        Gdiplus::RectF close;
        Gdiplus::RectF nav[4];
        Gdiplus::RectF content;

        Gdiplus::RectF saveLabel;
        Gdiplus::RectF saveField;
        Gdiplus::RectF browse;
        Gdiplus::RectF fileNameLabel;
        Gdiplus::RectF fileNameField;
        Gdiplus::RectF fileNameNote;
        Gdiplus::RectF afterLabel;
        Gdiplus::RectF notificationBox;
        Gdiplus::RectF notificationLabel;
        Gdiplus::RectF autoOpenBox;
        Gdiplus::RectF autoOpenLabel;
        Gdiplus::RectF trayBox;
        Gdiplus::RectF trayLabel;
        Gdiplus::RectF trayNote;
        Gdiplus::RectF autoUpdateBox;
        Gdiplus::RectF autoUpdateLabel;
        Gdiplus::RectF autoUpdateNote;
        Gdiplus::RectF languageLabel;
        Gdiplus::RectF languageField;

        Gdiplus::RectF audioHint;
        Gdiplus::RectF audioCard[2];
        Gdiplus::RectF audioTitle[2];
        Gdiplus::RectF audioToggle[2];
        Gdiplus::RectF audioDevice[2];

        Gdiplus::RectF recordingHint;
        Gdiplus::RectF qualityLabel;
        Gdiplus::RectF qualityField;
        Gdiplus::RectF encoderLabel;
        Gdiplus::RectF encoderField;
        Gdiplus::RectF cursorBox;
        Gdiplus::RectF cursorLabel;
        Gdiplus::RectF previewBox;
        Gdiplus::RectF previewLabel;

        Gdiplus::RectF aboutLogo;
        Gdiplus::RectF aboutTitle;
        Gdiplus::RectF aboutVersion;
        Gdiplus::RectF aboutBuild;
        Gdiplus::RectF aboutDescription;
        Gdiplus::RectF aboutAuthorLabel;
        Gdiplus::RectF aboutAuthorName;
        Gdiplus::RectF aboutAuthorSite;
        Gdiplus::RectF aboutAuthorGithub;
        Gdiplus::RectF aboutDiagnostics;
        Gdiplus::RectF aboutLogs;
        Gdiplus::RectF aboutLicense;
    };

    static LRESULT CALLBACK WndProcThunk(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
    static LRESULT CALLBACK EditSubclassProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp,
                                             UINT_PTR id, DWORD_PTR ref);
    LRESULT WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

    void CreateWindowIfNeeded();
    void OnCreate();
    void OnDpiChanged(UINT dpi);
    void LoadSettings();
    void SaveAndNotify();
    void ApplyLanguage();
    void RefreshAudioDevices();
    void UpdateLayout();
    void BuildHits();
    Hit HitTest(POINT point) const;
    bool IsHitEnabled(Hit id) const;
    void SetHover(Hit hit);
    void Activate(Hit id);
    void CycleFocus(bool backwards);
    void Paint(HDC targetDc = nullptr);
    void Draw(Gdiplus::Graphics& graphics, int width, int height);
    void DrawTitleBar(Gdiplus::Graphics& graphics);
    void DrawNav(Gdiplus::Graphics& graphics);
    void DrawGeneral(Gdiplus::Graphics& graphics);
    void DrawAudio(Gdiplus::Graphics& graphics);
    void DrawRecording(Gdiplus::Graphics& graphics);
    void DrawAbout(Gdiplus::Graphics& graphics);
    void DrawField(Gdiplus::Graphics& graphics, const Gdiplus::RectF& rect,
                   const std::wstring& text, bool hover, bool chevron = true);
    void DrawCheckRow(Gdiplus::Graphics& graphics, const Gdiplus::RectF& box,
                      const Gdiplus::RectF& label, const std::wstring& text, bool checked,
                      Hit id);
    void DrawToggleRow(Gdiplus::Graphics& graphics, const Gdiplus::RectF& card,
                       const Gdiplus::RectF& title, const Gdiplus::RectF& toggle,
                       const std::wstring& label, bool on, Hit id);
    void ShowLanguageMenu();
    void ShowQualityMenu();
    void ShowEncoderMenu();
    void ShowDeviceMenu(bool output);
    void BrowseForFolder();
    void ShowDiagnostics();
    void OpenLogs();

    static std::wstring DefaultOutputDirectory();
    std::wstring DeviceLabel(bool output) const;
    void FlushPathEdit();

    HWND hwnd_ = nullptr;
    HWND owner_ = nullptr;
    HWND pathEdit_ = nullptr;
    std::function<void()> onChanged_;
    Page page_ = Page::General;
    UINT dpi_ = 96;
    ui::Fonts fonts_;
    Settings settings_;
    std::vector<audio::EndpointItem> outputDevices_;
    std::vector<audio::EndpointItem> inputDevices_;
    std::wstring defaultOutputName_;
    std::wstring defaultInputName_;
    std::vector<std::string> languageValues_;
    Layout layout_;
    std::vector<HitTarget> hits_;
    Hit hover_ = Hit::None;
    Hit pressed_ = Hit::None;
    Hit focus_ = Hit::None;
    bool keyboardFocus_ = false;
    bool trackingMouse_ = false;
    bool loadingEdit_ = false;
    bool browseRunning_ = false;
    HFONT editFont_ = nullptr;
    HBRUSH whiteBrush_ = nullptr;
    HDC memDc_ = nullptr;
    HBITMAP memBmp_ = nullptr;
    int memWidth_ = 0;
    int memHeight_ = 0;
};

} // namespace vsr::app

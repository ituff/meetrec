#pragma once

// MeetRec - main recorder window.
//
// The UI is custom drawn in the product theme (see docs/design) while the
// command semantics stay exactly those of the application state machine:
// Start is only valid in Ready, Pause in Recording, Resume in Paused, Stop in
// Recording/Paused, and closing during recording hides to the tray.

#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>

#include <atomic>
#include <filesystem>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <Application/Coordinator.h>
#include <Update/ReleaseInfo.h>
#include <WindowManagement/WindowEnumerator.h>

#include "AudioDevices.h"
#include "Localization.h"
#include "TrayIcon.h"
#include "UiDraw.h"

namespace vsr::app {

class PreviewPanel;
class SettingsWindow;
class WindowPicker;
class ToastWindow;
class CompressWindow;
class UpdateWindow;

class MainWindow {
public:
    explicit MainWindow(Coordinator& coordinator);
    ~MainWindow();

    MainWindow(const MainWindow&) = delete;
    MainWindow& operator=(const MainWindow&) = delete;

    bool Create();
    void ShowAndActivate();
    HWND Hwnd() const noexcept { return hwnd_; }
    void RefreshUi();
    // Opens (or activates) the compressor window from the menus and the
    // --compress-window development entry.
    void ShowCompressWindow();
    void QueueCompressFiles(const std::vector<std::wstring>& files);
    void SetOpenUpdateWindowOnAvailable(bool open) noexcept {
        openUpdateWindowOnAvailable_ = open;
    }

private:
    enum class Page { Main, Result, Error };

    enum class Hit {
        None,
        SettingsButton,
        MoreButton,
        MinimizeButton,
        MaximizeButton,
        CloseButton,
        TargetCard,
        AudioHelp,
        SystemAudioToggle,
        MicToggle,
        OutputDevice,
        InputDevice,
        Quality,
        SavePath,
        Browse,
        PreviewToggle,
        Splitter,
        Start,
        PauseResume,
        Stop,
        ResultOpenFile,
        ResultOpenFolder,
        ResultAgain,
        ErrorRetry,
        ErrorHelp,
        ErrorDiagnostics,
    };

    struct HitTarget {
        Hit id = Hit::None;
        Gdiplus::RectF rect;
    };

    struct Layout {
        Gdiplus::RectF titleBar;
        Gdiplus::RectF logo;
        Gdiplus::RectF appTitle;
        Gdiplus::RectF tagline;
        Gdiplus::RectF settings;
        Gdiplus::RectF more;
        Gdiplus::RectF minimize;
        Gdiplus::RectF maximize;
        Gdiplus::RectF close;

        Gdiplus::RectF targetBadge;
        Gdiplus::RectF targetTitle;
        Gdiplus::RectF targetCard;
        Gdiplus::RectF targetIcon;
        Gdiplus::RectF targetName;
        Gdiplus::RectF targetSub;
        Gdiplus::RectF targetChange;

        Gdiplus::RectF audioBadge;
        Gdiplus::RectF audioTitle;
        Gdiplus::RectF audioHelp;
        Gdiplus::RectF audioCard[2];
        Gdiplus::RectF audioIcon[2];
        Gdiplus::RectF audioLabel[2];
        Gdiplus::RectF audioToggle[2];
        Gdiplus::RectF audioDevice[2];
        Gdiplus::RectF audioMeter[2];

        Gdiplus::RectF otherBadge;
        Gdiplus::RectF otherTitle;
        Gdiplus::RectF qualityIcon;
        Gdiplus::RectF qualityLabel;
        Gdiplus::RectF qualityValue;
        Gdiplus::RectF saveIcon;
        Gdiplus::RectF saveLabel;
        Gdiplus::RectF saveValue;
        Gdiplus::RectF saveBrowse;

        Gdiplus::RectF previewTitle;
        Gdiplus::RectF previewCheckBox;
        Gdiplus::RectF previewCheckLabel;
        Gdiplus::RectF previewCard;
        Gdiplus::RectF previewCaption;
        Gdiplus::RectF tipBox;
        Gdiplus::RectF tipIcon;
        // Draggable divider between the settings column and the preview.
        Gdiplus::RectF splitter;

        Gdiplus::RectF footer;
        Gdiplus::RectF statusIcon;
        Gdiplus::RectF statusTitle;
        Gdiplus::RectF statusSub;
        Gdiplus::RectF primaryButton;
        Gdiplus::RectF secondaryButton;
        Gdiplus::RectF stopButton;

        Gdiplus::RectF resultArea;
        Gdiplus::RectF resultIcon;
        Gdiplus::RectF resultTitle;
        Gdiplus::RectF resultSub;
        Gdiplus::RectF resultThumb;
        Gdiplus::RectF resultPlay;
        Gdiplus::RectF resultDetails;
        Gdiplus::RectF resultPrimary;
        Gdiplus::RectF resultSecondary;
        Gdiplus::RectF resultTertiary;

        Gdiplus::RectF errorArea;
        Gdiplus::RectF errorIcon;
        Gdiplus::RectF errorTitle;
        Gdiplus::RectF errorSub;
        Gdiplus::RectF errorBox;
        Gdiplus::RectF errorRetry;
        Gdiplus::RectF errorHelp;
        Gdiplus::RectF errorDiag;
    };

    struct ThumbnailPayload {
        int generation = 0;
        bool failed = false;
        std::vector<std::uint8_t> bgra;
        UINT width = 0;
        UINT height = 0;
    };

    static LRESULT CALLBACK WndProcThunk(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
    LRESULT WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

    void OnCreate();
    void OnTimer();
    void OnDpiChanged(UINT newDpi);
    void RecreateFonts();
    void UpdateLayout();
    void UpdatePreviewChild();
    void InvalidateAll();
    void Paint(HDC targetDc = nullptr);
    void Draw(Gdiplus::Graphics& graphics, int width, int height);
    void DrawTitleBar(Gdiplus::Graphics& graphics);
    void DrawMainPage(Gdiplus::Graphics& graphics);
    void DrawTargetSection(Gdiplus::Graphics& graphics, bool enabled);
    void DrawAudioSection(Gdiplus::Graphics& graphics, bool enabled);
    void DrawOtherSection(Gdiplus::Graphics& graphics, bool enabled);
    void DrawPreviewColumn(Gdiplus::Graphics& graphics, bool enabled);
    void DrawFooter(Gdiplus::Graphics& graphics, const StatusSnapshot& snap);
    void DrawResultPage(Gdiplus::Graphics& graphics);
    void DrawErrorPage(Gdiplus::Graphics& graphics);
    void DrawAudioCard(Gdiplus::Graphics& graphics, int index, bool enabled);
    void DrawSpinner(Gdiplus::Graphics& graphics, const Gdiplus::RectF& rect, float angle);
    void DrawStatusButton(Gdiplus::Graphics& graphics, const Gdiplus::RectF& rect,
                          const std::wstring& label, ui::ButtonStyle style, ui::Icon icon,
                          Hit id, bool enabled);

    void BuildHitTargets();
    Hit HitTest(POINT clientPoint) const;
    bool IsHitEnabled(Hit id) const;
    void SetHover(Hit hit);
    void Activate(Hit id);
    void CycleFocus(bool backwards);
    void SetFocusHit(Hit hit, bool keyboard);

    void ShowSettingsPage(int page);
    void StartUpdateCheck(bool manual);
    void ShowUpdateDialog();
    void OpenUpdateWindow();
    void ApplyUpdateAndExit(const std::filesystem::path& zipPath);
    void ShowMoreMenu();
    void ShowTrayMenu();
    void ShowAudioHelp();
    void ShowTargetPicker();
    void OnPickerResult(int index);
    void SelectTarget(size_t index);
    void ClearTarget();

    void OnStartClicked();
    void OnPauseResumeClicked();
    void OnStopClicked();
    void OnStopAsync();
    void ReapStopOperation();
    void OnBrowseClicked();
    void OnOpenOutputFolder();
    void OnDiagnosticsClicked();
    void OnToggleSystemAudio();
    void OnToggleMicrophone();
    void OnDeviceMenu(bool output);
    void OnQualityMenu();
    void OnTogglePreview();
    void OnResultOpenFile();
    void OnResultOpenFolder();
    void OnResultRecordAgain();
    void OnErrorRetry();
    void OnErrorHelp();
    void OpenPath(const std::wstring& path, bool selectFile = false);

    void LoadSettings();
    void ApplySettingsFromStore();
    void PersistSettings();
    void RefreshWindowList();
    void RefreshAudioDevices(const std::wstring& preferredOutput = {},
                             const std::wstring& preferredInput = {});
    std::wstring SelectedAudioDeviceId(bool output) const;
    void SelectAudioDevice(bool output, const std::wstring& id);
    std::wstring AudioDeviceLabel(bool output) const;
    std::wstring QualityLabel() const;
    int QualityIndex() const;
    void SetQualityIndex(int index);
    int EncoderIndex() const;
    void SetEncoderIndex(int index);
    std::wstring EncoderLabel() const;

    void UpdateTray();
    // Keeps the idle system/microphone level meters alive while a target is
    // selected, and releases the endpoints before recording starts.
    void RefreshLevelMonitor();
    void ShowToast(const std::wstring& title, const std::wstring& line1,
                   const std::wstring& line2, bool warning, bool liveElapsed = false);
    void EnterResultPage(const StatusSnapshot& snap);
    void EnterErrorPage(const StatusSnapshot& snap);
    void EnterErrorPage(ErrorCode code, const std::wstring& detail);
    void EnterMainPage();
    void EnterCompact(int logicalHeight);
    void ExitCompact();
    void StartPreviewForTarget();
    void RequestThumbnail(HWND target);
    void FinishRecording(const StatusSnapshot& snap);

    static bool CaptureWindowThumbnail(HWND window, std::vector<std::uint8_t>& outBgra,
                                       UINT& outWidth, UINT& outHeight);
    std::wstring ElapsedText() const;
    std::wstring ActiveTargetName() const;
    std::wstring FileSizeText() const;
    bool IsActiveState(AppState state) const;
    void UpdateWindowTitle();

    Coordinator& coordinator_;
    TrayIcon tray_;
    std::unique_ptr<PreviewPanel> preview_;
    std::unique_ptr<SettingsWindow> settings_;
    std::unique_ptr<WindowPicker> picker_;
    std::unique_ptr<ToastWindow> toast_;
    std::unique_ptr<CompressWindow> compressor_;
    std::unique_ptr<UpdateWindow> updateWindow_;

    HWND hwnd_ = nullptr;
    UINT dpi_ = 96;
    ui::Fonts fonts_;
    HDC memDc_ = nullptr;
    HBITMAP memBmp_ = nullptr;
    int memWidth_ = 0;
    int memHeight_ = 0;
    UINT timerId_ = 1;
    UINT activateMessage_ = 0;
    UINT taskbarCreatedMessage_ = 0;
    Layout layout_;
    std::vector<HitTarget> hits_;
    Hit hover_ = Hit::None;
    Hit pressed_ = Hit::None;
    Hit focus_ = Hit::None;
    bool keyboardFocus_ = false;
    Page page_ = Page::Main;
    AppState previousState_ = AppState::Initializing;
    StatusSnapshot lastSnapshot_;

    // Settings mirrored into the UI.
    std::wstring outputDirectory_;
    QualityPreset preset_ = QualityPreset::Standard;
    encoder::EncoderPreference encoderPreference_ = encoder::EncoderPreference::Auto;
    bool systemAudio_ = false;
    bool microphone_ = false;
    bool showPreview_ = true;
    bool captureCursor_ = false;
    bool minimizeToTray_ = true;
    bool showNotification_ = true;
    bool autoOpenFolder_ = false;
    bool autoCheckUpdates_ = true;
    long long lastUpdateCheckUnix_ = 0;
    std::string lastNotifiedUpdateVersion_;
    std::string uiLanguage_ = "system";
    float previewRatio_ = 0.40f;
    float layoutAvailableWidth_ = 0.0f;
    float layoutRightWidth_ = 0.0f;
    bool draggingSplitter_ = false;
    int splitterDragStartX_ = 0;
    float splitterDragStartLeft_ = 0.0f;
    float splitterDragAvailable_ = 0.0f;

    // Targets and audio endpoints.
    std::vector<window::WindowInfo> windows_;
    HWND target_ = nullptr;
    DWORD targetPid_ = 0;
    std::wstring targetTitle_;
    std::wstring targetExe_;
    std::unique_ptr<Gdiplus::Bitmap> targetIcon_;
    std::vector<audio::EndpointItem> outputDevices_;
    std::vector<audio::EndpointItem> inputDevices_;
    int outputSelection_ = 0;
    int inputSelection_ = 0;
    std::wstring defaultOutputName_;
    std::wstring defaultInputName_;

    // Last result.
    std::wstring resultPath_;
    std::wstring resultName_;
    std::int64_t resultDurationMs_ = 0;
    std::uint64_t resultBytes_ = 0;
    bool resultHadError_ = false;
    ErrorCode resultErrorCode_ = ErrorCode::None;
    bool resultTargetLost_ = false;
    std::vector<std::uint8_t> resultThumb_;
    UINT resultThumbWidth_ = 0;
    UINT resultThumbHeight_ = 0;

    // Error page.
    ErrorCode errorCode_ = ErrorCode::None;
    std::wstring errorDetail_;

    // Runtime visuals.
    float systemLevel_ = 0.0f;
    float micLevel_ = 0.0f;
    float spinnerAngle_ = 0.0f;
    std::int64_t lastActiveElapsedMs_ = 0;
    UINT lastTrayKey_ = 0;
    std::wstring lastToastKey_;
    bool toastLiveElapsed_ = false;

    // Idle level monitor: a shared-mode WASAPI capture (system loopback and/or
    // microphone) used only for the meters. It is stopped before a recording
    // session opens the same endpoints.
    SystemClock levelMonitorClock_;
    std::unique_ptr<encoder::AudioCapture> levelMonitor_;
    std::wstring levelMonitorKey_;
    ULONGLONG levelMonitorRetryTick_ = 0;
    std::atomic<float> previewLoopbackLevel_{0.0f};
    std::atomic<float> previewMicLevel_{0.0f};
    bool trackingMouse_ = false;
    bool previewChildShown_ = false;
    bool thumbnailPending_ = false;
    bool thumbnailFailed_ = false;
    ULONGLONG thumbnailRequestTick_ = 0;

    // Async stop: finalization can take seconds and must not block painting.
    std::atomic<bool> stopOpRunning_{false};
    std::atomic<bool> stopOpDone_{false};
    std::thread stopOp_;
    std::atomic<bool> browseRunning_{false};

    // Update checks run off the UI thread; the result arrives as a posted
    // message, so the window never blocks on GitHub.
    std::atomic<bool> updateCheckRunning_{false};
    bool updateAvailable_ = false;
    update::ReleaseInfo updateRelease_;
    std::wstring updateVersion_;
    ULONGLONG nextUpdateCheckTick_ = 0;
    bool pendingUpdateToast_ = false;
    bool openUpdateWindowOnAvailable_ = false;

    // Window the user last chose; a thumbnail refresh runs off the UI thread.
    std::shared_ptr<std::atomic<int>> thumbGeneration_;
    bool compact_ = false;
    RECT normalRect_ = {};
    bool normalRectValid_ = false;
};

} // namespace vsr::app

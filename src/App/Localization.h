// MeetRec - UI localization (zh-CN / en-US).
//
// Every user-visible string in the application UI goes through Text() or
// Format(); no UI string is embedded in window code. The language follows the
// Windows display language by default and can be overridden in Settings.
#pragma once

#include <string>
#include <vector>

#include <Infrastructure/Error.h>

namespace vsr::app::i18n {

enum class Language { ZhCn, EnUs };

// Localized UI strings. IDs are stable; entries are grouped by surface.
enum class Str {
    AppName,
    AppTagline,

    // Window chrome
    Settings,
    More,
    Minimize,
    Maximize,
    Restore,
    Close,
    About,
    Exit,

    // Main window - sections
    SectionTarget,
    SectionTargetActive,
    ChangeWindow,
    SectionAudio,
    AudioHelp,
    SectionOther,
    RecordSystemAudio,
    RecordMicrophone,
    Quality,
    SaveLocation,
    Preview,
    ShowPreview,
    PreviewHint,
    MinimizeHint,
    AudioDeviceHint,
    AudioNoDevices,

    // Main window - target card
    NoTarget,
    ChooseWindow,
    NoWindows,
    Rescan,
    TargetUnavailable,
    TargetPickerTitle,
    TargetPickerHint,
    TargetPickerSearch,
    TargetPickerEmpty,
    TargetPickerNoMatch,
    TargetPickerSelect,
    TargetPickerCancel,
    TargetPickerFilter,
    TargetPickerNoMeetingApps,

    // Main window - quality options
    QualityLow,
    QualityStandard,
    QualityHigh,

    // Main window - footer and states
    Ready,
    ReadyHint,
    ReadyNoTargetHint,
    Recording,
    Paused,
    PausedHint,
    Preparing,
    PreparingHint,
    Saving,
    SavingHint,
    StartRecording,
    Pause,
    Resume,
    StopAndSave,
    PreviewOff,
    PreviewEmpty,
    PreviewLoading,
    PreviewRecordingEmpty,
    PreviewUnavailable,
    DefaultDevicePrefix,
    DefaultDeviceLabel,
    SampleRateLabel,

    // Result card
    RecordingComplete,
    ResultSavedHint,
    FileName,
    Duration,
    FileSize,
    SavedIn,
    OpenFile,
    OpenFolder,
    RecordAgain,
    StoppedTitle,
    StoppedHint,
    StoppedErrorTitle,
    StoppedErrorHint,
    NotSavedHint,

    // Error card
    RecordingFailed,
    StartFailedHint,
    ErrorCodeLabel,
    ErrorDetailLabel,
    ViewHelp,
    CreateDiagnostics,
    Retry,
    DiagnosticsCreated,
    DiagnosticsCreatedBody,
    DiagnosticsFailed,
    DiagnosticsFailedBody,
    HelpTitle,
    HelpBody,
    ErrorSavedTitle,
    ErrorSavedHint,
    EmptyRecordingHint,

    // Error titles/messages by stable code
    ErrInitOsUnsupportedTitle,
    ErrInitOsUnsupportedBody,
    ErrInitDriverMissingTitle,
    ErrInitDriverMissingBody,
    ErrInitD3dTitle,
    ErrInitD3dBody,
    ErrDisplayNotFoundTitle,
    ErrDisplayNotFoundBody,
    ErrDisplayDisconnectedTitle,
    ErrDisplayDisconnectedBody,
    ErrTargetInvalidTitle,
    ErrTargetInvalidBody,
    ErrTargetLostTitle,
    ErrTargetLostBody,
    ErrCaptureInitTitle,
    ErrCaptureInitBody,
    ErrDeviceLostTitle,
    ErrDeviceLostBody,
    ErrEncoderUnavailableTitle,
    ErrEncoderUnavailableBody,
    ErrWriteFailedTitle,
    ErrWriteFailedBody,
    ErrOutputNotWritableTitle,
    ErrOutputNotWritableBody,
    ErrDiskLowTitle,
    ErrDiskLowBody,
    ErrFinalizeTitle,
    ErrFinalizeBody,
    ErrInternalTitle,
    ErrInternalBody,

    // Settings window
    SettingsTitle,
    NavGeneral,
    NavAudio,
    NavRecording,
    NavAbout,
    SaveLocationLabel,
    FileNameFormat,
    FileNameFormatNote,
    AfterRecording,
    ShowCompletionNotification,
    AutoOpenOutputFolder,
    MinimizeToTray,
    MinimizeToTrayNote,
    InterfaceLanguage,
    LanguageSystem,
    LanguageChinese,
    LanguageEnglish,
    AudioSettingsHint,
    AudioSystemDefault,
    AudioMicDefault,
    RecordingSettingsHint,
    Encoder,
    EncoderAuto,
    EncoderHardware,
    EncoderSoftware,
    CaptureCursor,
    PreviewDefault,
    AboutVersion,
    AboutBuild,
    AboutMadeFor,
    AboutAuthor,
    AboutDiagnostics,
    AboutLogs,
    AboutLicense,
    SettingsSaved,
    BrowseEllipsis,
    Apply,

    // Tray menu / tooltip / toast
    TrayOpen,
    TrayStop,
    TrayPause,
    TrayResume,
    TraySettings,
    TrayHelp,
    TrayExit,
    TrayRecordingElapsed,
    TrayPausedElapsed,
    TrayReady,
    TrayError,
    TrayRecordingTooltip,
    TrayPausedTooltip,
    TrayErrorTooltip,
    ToastRecordingTitle,
    ToastSavedTitle,
    ToastPausedTitle,
    ToastInfoClose,

    // Dialogs and prompts
    ConfirmCloseRecordingTitle,
    ConfirmCloseRecordingBody,
    ConfirmExitRecordingTitle,
    ConfirmExitRecordingBody,
    ConfirmExitPreparingBody,
    NoTargetPrompt,
    NoTargetPromptBody,
    CannotStartTitle,
    DeviceHintTitle,
    DeviceHintBody,
    PathInvalidTitle,
    PathInvalidBody,
    SavingCloseBlocked,

    // Misc
    BrowseForFolder,
    NotAvailable,
    Loading,
    Ellipsis,
    Count
};

// Language selection. SetLanguage also refreshes localized format strings.
void SetLanguage(Language language);
Language CurrentLanguage();
Language DetectSystemLanguage();

// "system" / "zh-CN" / "en-US" <-> Language (unknown values mean system).
Language LanguageFromSetting(const std::string& value);
const char* LanguageToSetting(Language language);
const wchar_t* LanguageName(Language language);

// String lookup. Format() replaces the first %s in order with each argument.
const wchar_t* Text(Str id);
std::wstring Format(Str id, const std::vector<std::wstring>& args);

// Localized error presentation for stable ErrorCode values.
const wchar_t* ErrorTitle(ErrorCode code);
const wchar_t* ErrorMessage(ErrorCode code);
bool ErrorStoppedRecording(ErrorCode code);

// Helpers used by the UI.
std::wstring FormatDuration(std::int64_t milliseconds);
std::wstring FormatBytes(std::uint64_t bytes);

} // namespace vsr::app::i18n

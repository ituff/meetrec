#include "Localization.h"

#include <windows.h>

#include <array>
#include <cstdint>
#include <cwchar>

namespace vsr::app::i18n {

namespace {

struct Entry {
    Str id;
    const wchar_t* text;
};

// Chinese (Simplified) is the primary product language; English keeps the same
// information structure rather than mirroring Chinese word order.
constexpr Entry kZh[] = {
    {Str::AppName, L"MeetRec"},
    {Str::AppTagline, L"会议与演示的窗口录制器"},

    {Str::Settings, L"设置"},
    {Str::More, L"更多"},
    {Str::Minimize, L"最小化"},
    {Str::Maximize, L"最大化"},
    {Str::Restore, L"还原"},
    {Str::Close, L"关闭"},
    {Str::About, L"关于"},
    {Str::Exit, L"退出"},

    {Str::SectionTarget, L"选择要录制的窗口"},
    {Str::SectionTargetActive, L"录制的窗口"},
    {Str::ChangeWindow, L"更换..."},
    {Str::SectionAudio, L"音频设置"},
    {Str::AudioHelp, L"如何选择?"},
    {Str::SectionOther, L"其他设置"},
    {Str::RecordSystemAudio, L"录制系统声音（对方的声音）"},
    {Str::RecordMicrophone, L"录制麦克风（我的声音）"},
    {Str::Quality, L"画质"},
    {Str::SaveLocation, L"保存位置"},
    {Str::Preview, L"预览"},
    {Str::ShowPreview, L"显示预览"},
    {Str::PreviewHint, L"这是所选窗口的预览画面，录制时不会包含其他窗口。"},
    {Str::MinimizeHint, L"提示：请不要最小化目标窗口，最小化会导致录制画面冻结。"},
    {Str::AudioDeviceHint, L"请选择会议软件正在使用的麦克风；建议戴耳机，外放会产生回声。"},
    {Str::AudioNoDevices, L"没有检测到声音设备，可以继续录制视频。"},

    {Str::NoTarget, L"未选择窗口"},
    {Str::ChooseWindow, L"选择窗口"},
    {Str::NoWindows, L"还没有可录制的窗口。先打开会议或演示窗口，再加入会议。"},
    {Str::Rescan, L"重新扫描"},
    {Str::TargetUnavailable, L"这个窗口已不可用，请重新选择。"},
    {Str::TargetPickerTitle, L"选择要录制的窗口"},
    {Str::TargetPickerHint, L"选择会议或演示所在的窗口；录制画面不会包含其他窗口。"},
    {Str::TargetPickerSearch, L"搜索窗口或应用..."},
    {Str::TargetPickerEmpty, L"没有找到可录制的窗口。先打开会议或演示窗口，再重新扫描。"},
    {Str::TargetPickerNoMatch, L"没有匹配的窗口"},
    {Str::TargetPickerSelect, L"选择此窗口"},
    {Str::TargetPickerCancel, L"取消"},
    {Str::TargetPickerFilter, L"只看会议应用"},
    {Str::TargetPickerNoMeetingApps,
     L"没有找到会议应用窗口。先加入会议，或关闭筛选查看所有窗口。"},

    {Str::QualityLow, L"流畅 (720p, 30 FPS)"},
    {Str::QualityStandard, L"标准 (1080p, 30 FPS)"},
    {Str::QualityHigh, L"高清 (1080p, 60 FPS)"},

    {Str::Ready, L"已就绪"},
    {Str::ReadyHint, L"选择窗口并检查音频设置后，点击开始录制。"},
    {Str::ReadyNoTargetHint, L"请选择一个要录制的窗口，然后开始录制。"},
    {Str::Recording, L"正在录制"},
    {Str::Paused, L"已暂停"},
    {Str::PausedHint, L"暂停期间不会录制，点击“继续”恢复。"},
    {Str::Preparing, L"准备中..."},
    {Str::PreparingHint, L"正在准备窗口和编码器，请稍候。"},
    {Str::Saving, L"正在保存..."},
    {Str::SavingHint, L"正在完成文件写入，请稍候。"},
    {Str::StartRecording, L"开始录制"},
    {Str::Pause, L"暂停"},
    {Str::Resume, L"继续录制"},
    {Str::StopAndSave, L"结束并保存"},
    {Str::PreviewOff, L"预览已关闭"},
    {Str::PreviewEmpty, L"选择窗口后显示预览"},
    {Str::PreviewLoading, L"正在准备预览..."},
    {Str::PreviewRecordingEmpty, L"录制开始后显示预览"},
    {Str::PreviewUnavailable, L"无法预览此窗口，录制不会受影响。"},
    {Str::DefaultDevicePrefix, L"默认"},
    {Str::DefaultDeviceLabel, L"默认 - %s"},
    {Str::SampleRateLabel, L"%s kHz，%s 声道"},

    {Str::RecordingComplete, L"录制已完成!"},
    {Str::ResultSavedHint, L"文件已保存到指定位置。"},
    {Str::FileName, L"文件名"},
    {Str::Duration, L"时长"},
    {Str::FileSize, L"大小"},
    {Str::SavedIn, L"保存位置"},
    {Str::OpenFile, L"打开文件"},
    {Str::OpenFolder, L"打开文件夹"},
    {Str::RecordAgain, L"再录制一个"},
    {Str::StoppedTitle, L"录制已停止"},
    {Str::StoppedHint, L"已录内容已保存。"},
    {Str::StoppedErrorTitle, L"录制已停止，文件已保存"},
    {Str::StoppedErrorHint, L"发生了错误，已录内容已保存。"},
    {Str::NotSavedHint, L"没有生成可用的录制文件。"},

    {Str::RecordingFailed, L"录制失败"},
    {Str::StartFailedHint, L"无法启动录制，请检查窗口是否有效，以及音频设备是否可用。"},
    {Str::ErrorCodeLabel, L"错误码"},
    {Str::ErrorDetailLabel, L"详细信息"},
    {Str::ViewHelp, L"查看帮助"},
    {Str::CreateDiagnostics, L"创建诊断包..."},
    {Str::Retry, L"重试"},
    {Str::DiagnosticsCreated, L"诊断包已创建"},
    {Str::DiagnosticsCreatedBody, L"诊断包已保存到：\n%s"},
    {Str::DiagnosticsFailed, L"无法创建诊断包"},
    {Str::DiagnosticsFailedBody, L"日志位于：%s"},
    {Str::HelpTitle, L"录制帮助"},
    {Str::HelpBody,
     L"· 录制期间不要最小化目标窗口，否则画面可能冻结。\n"
     L"· 请选择会议软件“设置 → 设备”里正在使用的麦克风。\n"
     L"· 建议戴耳机；外放会让双方声音产生回声。\n"
     L"· 录制可以随时暂停，暂停期间不会录制。\n"
     L"· 结束录制后，文件会保存到主界面显示的保存位置。"},
    {Str::ErrorSavedTitle, L"录制已停止，文件已保存"},
    {Str::ErrorSavedHint, L"发生了错误，已录内容已保存。"},
    {Str::EmptyRecordingHint, L"没有录到画面，文件可能无法播放。请换一个窗口重试。"},

    {Str::ErrInitOsUnsupportedTitle, L"系统版本不受支持"},
    {Str::ErrInitOsUnsupportedBody, L"MeetRec 需要 Windows 10 2004 (x64) 或更高版本。"},
    {Str::ErrInitDriverMissingTitle, L"无法连接录制显示器"},
    {Str::ErrInitDriverMissingBody, L"窗口录制不需要虚拟显示器。请重新选择窗口后重试。"},
    {Str::ErrInitD3dTitle, L"无法初始化图形设备"},
    {Str::ErrInitD3dBody, L"请更新显卡驱动，然后重试。"},
    {Str::ErrDisplayNotFoundTitle, L"找不到录制显示器"},
    {Str::ErrDisplayNotFoundBody, L"请重新选择窗口后重试。"},
    {Str::ErrDisplayDisconnectedTitle, L"录制显示器已断开"},
    {Str::ErrDisplayDisconnectedBody, L"录制已停止，已录内容已保存。"},
    {Str::ErrTargetInvalidTitle, L"窗口不可用"},
    {Str::ErrTargetInvalidBody, L"所选窗口已关闭或不可见，请重新选择窗口。"},
    {Str::ErrTargetLostTitle, L"窗口已关闭"},
    {Str::ErrTargetLostBody, L"录制的窗口被关闭，录制已停止，已录内容已保存。"},
    {Str::ErrCaptureInitTitle, L"无法开始画面捕获"},
    {Str::ErrCaptureInitBody, L"请换一个窗口重试，或重新启动 MeetRec。"},
    {Str::ErrDeviceLostTitle, L"图形设备已重置"},
    {Str::ErrDeviceLostBody, L"录制已停止，已录内容已保存。如问题反复出现，请更新显卡驱动。"},
    {Str::ErrEncoderUnavailableTitle, L"没有可用的视频编码器"},
    {Str::ErrEncoderUnavailableBody, L"请更新显卡驱动，或在设置中改用软件编码，然后重试。"},
    {Str::ErrWriteFailedTitle, L"编码器停止响应"},
    {Str::ErrWriteFailedBody, L"录制已安全停止，已录内容已保留。"},
    {Str::ErrOutputNotWritableTitle, L"保存位置不可写"},
    {Str::ErrOutputNotWritableBody, L"请选择一个你拥有写入权限的文件夹。"},
    {Str::ErrDiskLowTitle, L"磁盘空间不足"},
    {Str::ErrDiskLowBody, L"请清理磁盘空间，或更换保存位置后重试。"},
    {Str::ErrFinalizeTitle, L"文件保存失败"},
    {Str::ErrFinalizeBody, L"录制文件未能完成收尾，临时文件已保留。"},
    {Str::ErrInternalTitle, L"操作暂时不可用"},
    {Str::ErrInternalBody, L"请稍后重试；如问题持续，请创建诊断包。"},

    {Str::SettingsTitle, L"设置"},
    {Str::NavGeneral, L"常规"},
    {Str::NavAudio, L"音频"},
    {Str::NavRecording, L"录制"},
    {Str::NavAbout, L"关于"},
    {Str::SaveLocationLabel, L"保存位置"},
    {Str::FileNameFormat, L"文件名格式"},
    {Str::FileNameFormatNote, L"文件名根据录制开始时间自动生成。"},
    {Str::AfterRecording, L"录制完成后"},
    {Str::ShowCompletionNotification, L"显示完成通知"},
    {Str::AutoOpenOutputFolder, L"自动打开文件所在文件夹"},
    {Str::MinimizeToTray, L"最小化到系统托盘"},
    {Str::MinimizeToTrayNote, L"关闭录制窗口时继续在后台录制。"},
    {Str::InterfaceLanguage, L"界面语言"},
    {Str::LanguageSystem, L"跟随系统"},
    {Str::LanguageChinese, L"中文（简体）"},
    {Str::LanguageEnglish, L"English"},
    {Str::AudioSettingsHint, L"选择新录制默认使用的声音来源。会议软件可能有自己的设备选择。"},
    {Str::AudioSystemDefault, L"默认录制系统声音"},
    {Str::AudioMicDefault, L"默认录制麦克风"},
    {Str::RecordingSettingsHint, L"选择视频质量和编码方式；通常保持“自动”即可。"},
    {Str::Encoder, L"编码方式"},
    {Str::EncoderAuto, L"自动（推荐）"},
    {Str::EncoderHardware, L"硬件编码"},
    {Str::EncoderSoftware, L"软件编码（兼容性最好）"},
    {Str::CaptureCursor, L"录制鼠标指针"},
    {Str::PreviewDefault, L"默认显示预览"},
    {Str::AboutVersion, L"版本"},
    {Str::AboutBuild, L"构建"},
    {Str::AboutMadeFor, L"会议与演示的窗口录制器。所有内容都保存在本机。"},
    {Str::AboutAuthor, L"作者"},
    {Str::AboutDiagnostics, L"创建诊断包..."},
    {Str::AboutLogs, L"打开日志文件夹"},
    {Str::AboutLicense, L"完全本地录制，不上传任何内容。"},
    {Str::SettingsSaved, L"设置已保存"},
    {Str::BrowseEllipsis, L"浏览..."},
    {Str::Apply, L"应用"},

    {Str::TrayOpen, L"显示主窗口"},
    {Str::TrayStop, L"停止录制"},
    {Str::TrayPause, L"暂停录制"},
    {Str::TrayResume, L"继续录制"},
    {Str::TraySettings, L"设置..."},
    {Str::TrayHelp, L"帮助..."},
    {Str::TrayExit, L"退出"},
    {Str::TrayRecordingElapsed, L"录制中 %s"},
    {Str::TrayPausedElapsed, L"已暂停 %s"},
    {Str::TrayReady, L"MeetRec - 就绪"},
    {Str::TrayError, L"MeetRec - 出错"},
    {Str::TrayRecordingTooltip, L"MeetRec - 正在录制 %s"},
    {Str::TrayPausedTooltip, L"MeetRec - 已暂停 %s"},
    {Str::TrayErrorTooltip, L"MeetRec - 录制出错"},
    {Str::ToastRecordingTitle, L"正在录制"},
    {Str::ToastSavedTitle, L"录制已保存"},
    {Str::ToastPausedTitle, L"已暂停"},
    {Str::ToastInfoClose, L"关闭通知"},

    {Str::ConfirmCloseRecordingTitle, L"录制正在进行"},
    {Str::ConfirmCloseRecordingBody, L"结束录制并关闭窗口吗？已录内容会先保存。"},
    {Str::ConfirmExitRecordingTitle, L"退出 MeetRec"},
    {Str::ConfirmExitRecordingBody,
     L"录制正在进行。结束录制、保存文件并退出吗？退出后无法继续录制。"},
    {Str::ConfirmExitPreparingBody, L"正在准备录制。退出会取消本次录制，确定吗？"},
    {Str::NoTargetPrompt, L"请选择一个窗口"},
    {Str::NoTargetPromptBody, L"请先选择要录制的窗口。"},
    {Str::CannotStartTitle, L"无法开始录制"},
    {Str::DeviceHintTitle, L"选择会议正在使用的声音设备"},
    {Str::DeviceHintBody,
     L"会议软件（Teams、Zoom、腾讯会议等）可能有自己的设备选择，\n"
     L"它不一定和 Windows 默认设备相同。\n\n"
     L"请打开会议软件的“设置 → 设备”，确认它正在使用的麦克风，\n"
     L"然后在主界面选择同一个设备。建议戴耳机，避免回声。"},
    {Str::PathInvalidTitle, L"保存位置不可用"},
    {Str::PathInvalidBody, L"请选择一个存在且可写的文件夹。"},
    {Str::SavingCloseBlocked, L"正在保存录制，请稍候。"},

    {Str::BrowseForFolder, L"选择保存位置"},
    {Str::NotAvailable, L"-"},
    {Str::Loading, L"加载中..."},
    {Str::Ellipsis, L"..."},
};

constexpr Entry kEn[] = {
    {Str::AppName, L"MeetRec"},
    {Str::AppTagline, L"Window recorder for meetings and demos"},

    {Str::Settings, L"Settings"},
    {Str::More, L"More"},
    {Str::Minimize, L"Minimize"},
    {Str::Maximize, L"Maximize"},
    {Str::Restore, L"Restore"},
    {Str::Close, L"Close"},
    {Str::About, L"About"},
    {Str::Exit, L"Exit"},

    {Str::SectionTarget, L"Choose a window to record"},
    {Str::SectionTargetActive, L"Recording this window"},
    {Str::ChangeWindow, L"Change..."},
    {Str::SectionAudio, L"Audio settings"},
    {Str::AudioHelp, L"How to choose?"},
    {Str::SectionOther, L"Other settings"},
    {Str::RecordSystemAudio, L"System audio (other participants)"},
    {Str::RecordMicrophone, L"Microphone (my voice)"},
    {Str::Quality, L"Quality"},
    {Str::SaveLocation, L"Save to"},
    {Str::Preview, L"Preview"},
    {Str::ShowPreview, L"Show preview"},
    {Str::PreviewHint,
     L"This is a preview of the selected window. Other windows are never recorded."},
    {Str::MinimizeHint,
     L"Tip: don't minimize the target window - Windows may stop redrawing it and the "
     L"recording freezes."},
    {Str::AudioDeviceHint,
     L"Pick the microphone your meeting app is using. Headphones are recommended; "
     L"speakers can cause echo."},
    {Str::AudioNoDevices, L"No audio devices detected. You can still record video."},

    {Str::NoTarget, L"No window selected"},
    {Str::ChooseWindow, L"Choose window"},
    {Str::NoWindows, L"No recordable windows yet. Open your meeting or demo window first."},
    {Str::Rescan, L"Scan again"},
    {Str::TargetUnavailable, L"This window is no longer available. Choose another one."},
    {Str::TargetPickerTitle, L"Choose a window to record"},
    {Str::TargetPickerHint,
     L"Pick the window where your meeting or demo is running. Other windows are never "
     L"included."},
    {Str::TargetPickerSearch, L"Search windows or apps..."},
    {Str::TargetPickerEmpty,
     L"No recordable windows found. Open a meeting or demo window first."},
    {Str::TargetPickerNoMatch, L"No matching windows"},
    {Str::TargetPickerSelect, L"Select this window"},
    {Str::TargetPickerCancel, L"Cancel"},
    {Str::TargetPickerFilter, L"Meeting apps only"},
    {Str::TargetPickerNoMeetingApps,
     L"No meeting-app windows found. Join a meeting, or turn off the filter to see all "
     L"windows."},

    {Str::QualityLow, L"Smooth (720p, 30 FPS)"},
    {Str::QualityStandard, L"Standard (1080p, 30 FPS)"},
    {Str::QualityHigh, L"High (1080p, 60 FPS)"},

    {Str::Ready, L"Ready"},
    {Str::ReadyHint, L"Check the window and audio settings, then start recording."},
    {Str::ReadyNoTargetHint, L"Choose a window to record, then start."},
    {Str::Recording, L"Recording"},
    {Str::Paused, L"Paused"},
    {Str::PausedHint, L"Nothing is recorded while paused. Click Resume to continue."},
    {Str::Preparing, L"Getting ready..."},
    {Str::PreparingHint, L"Preparing the window and encoder. This takes a few seconds."},
    {Str::Saving, L"Saving..."},
    {Str::SavingHint, L"Finishing the file. This can take a few seconds."},
    {Str::StartRecording, L"Start recording"},
    {Str::Pause, L"Pause"},
    {Str::Resume, L"Resume recording"},
    {Str::StopAndSave, L"Stop & save"},
    {Str::PreviewOff, L"Preview is off"},
    {Str::PreviewEmpty, L"Select a window to see the preview"},
    {Str::PreviewLoading, L"Preparing the preview..."},
    {Str::PreviewRecordingEmpty, L"The preview appears after recording starts"},
    {Str::PreviewUnavailable, L"This window can't be previewed. Recording is not affected."},
    {Str::DefaultDevicePrefix, L"Default"},
    {Str::DefaultDeviceLabel, L"Default - %s"},
    {Str::SampleRateLabel, L"%s kHz, %s ch"},

    {Str::RecordingComplete, L"Recording complete!"},
    {Str::ResultSavedHint, L"Your recording has been saved."},
    {Str::FileName, L"File name"},
    {Str::Duration, L"Duration"},
    {Str::FileSize, L"Size"},
    {Str::SavedIn, L"Saved to"},
    {Str::OpenFile, L"Open file"},
    {Str::OpenFolder, L"Open folder"},
    {Str::RecordAgain, L"Record again"},
    {Str::StoppedTitle, L"Recording stopped"},
    {Str::StoppedHint, L"Everything recorded so far was saved."},
    {Str::StoppedErrorTitle, L"Recording stopped - file saved"},
    {Str::StoppedErrorHint, L"An error occurred. What was recorded has been saved."},
    {Str::NotSavedHint, L"No recording file was created."},

    {Str::RecordingFailed, L"Recording failed"},
    {Str::StartFailedHint,
     L"Recording couldn't start. Check that the window is still open and the audio "
     L"devices are available."},
    {Str::ErrorCodeLabel, L"Error code"},
    {Str::ErrorDetailLabel, L"Details"},
    {Str::ViewHelp, L"View help"},
    {Str::CreateDiagnostics, L"Create diagnostics..."},
    {Str::Retry, L"Try again"},
    {Str::DiagnosticsCreated, L"Diagnostics package created"},
    {Str::DiagnosticsCreatedBody, L"The package was saved to:\n%s"},
    {Str::DiagnosticsFailed, L"Couldn't create a diagnostics package"},
    {Str::DiagnosticsFailedBody, L"Logs are available at: %s"},
    {Str::HelpTitle, L"Recording help"},
    {Str::HelpBody,
     L"- Don't minimize the target window; its picture may freeze.\n"
     L"- Pick the microphone your meeting app uses in Settings > Devices.\n"
     L"- Headphones are recommended; speakers cause echo.\n"
     L"- You can pause at any time; nothing is recorded while paused.\n"
     L"- When you stop, the file is saved to the folder shown on the main window."},
    {Str::ErrorSavedTitle, L"Recording stopped - file saved"},
    {Str::ErrorSavedHint, L"An error occurred. What was recorded has been saved."},
    {Str::EmptyRecordingHint,
     L"No video frames were recorded; the file may not play. Try another window."},

    {Str::ErrInitOsUnsupportedTitle, L"Windows version not supported"},
    {Str::ErrInitOsUnsupportedBody,
     L"MeetRec requires Windows 10 version 2004 (x64) or later."},
    {Str::ErrInitDriverMissingTitle, L"Recording display unavailable"},
    {Str::ErrInitDriverMissingBody,
     L"Window recording doesn't need a virtual display. Choose the window again and retry."},
    {Str::ErrInitD3dTitle, L"Graphics unavailable"},
    {Str::ErrInitD3dBody, L"Update your graphics driver, then try again."},
    {Str::ErrDisplayNotFoundTitle, L"Recording display not found"},
    {Str::ErrDisplayNotFoundBody, L"Choose the window again and retry."},
    {Str::ErrDisplayDisconnectedTitle, L"Recording display disconnected"},
    {Str::ErrDisplayDisconnectedBody, L"Recording stopped and everything recorded was saved."},
    {Str::ErrTargetInvalidTitle, L"Window unavailable"},
    {Str::ErrTargetInvalidBody,
     L"The selected window was closed or hidden. Choose another window."},
    {Str::ErrTargetLostTitle, L"Window closed"},
    {Str::ErrTargetLostBody,
     L"The window you were recording was closed. Recording stopped and the file was saved."},
    {Str::ErrCaptureInitTitle, L"Screen capture failed"},
    {Str::ErrCaptureInitBody, L"Try another window, or restart MeetRec."},
    {Str::ErrDeviceLostTitle, L"Graphics device reset"},
    {Str::ErrDeviceLostBody,
     L"Recording stopped and the file was saved. If this keeps happening, update your "
     L"graphics driver."},
    {Str::ErrEncoderUnavailableTitle, L"No video encoder available"},
    {Str::ErrEncoderUnavailableBody,
     L"Update your graphics driver, or switch the encoder to software in Settings, then "
     L"try again."},
    {Str::ErrWriteFailedTitle, L"Encoder stopped responding"},
    {Str::ErrWriteFailedBody, L"Recording stopped safely and the file was kept."},
    {Str::ErrOutputNotWritableTitle, L"Save folder not writable"},
    {Str::ErrOutputNotWritableBody, L"Choose a folder where your account can write files."},
    {Str::ErrDiskLowTitle, L"Not enough disk space"},
    {Str::ErrDiskLowBody, L"Free up space or choose another save location, then try again."},
    {Str::ErrFinalizeTitle, L"Couldn't finalize the file"},
    {Str::ErrFinalizeBody, L"The recording couldn't be finished; the partial file was kept."},
    {Str::ErrInternalTitle, L"Command unavailable"},
    {Str::ErrInternalBody,
     L"Try again in a moment. If it persists, create a diagnostics package."},

    {Str::SettingsTitle, L"Settings"},
    {Str::NavGeneral, L"General"},
    {Str::NavAudio, L"Audio"},
    {Str::NavRecording, L"Recording"},
    {Str::NavAbout, L"About"},
    {Str::SaveLocationLabel, L"Save location"},
    {Str::FileNameFormat, L"File name format"},
    {Str::FileNameFormatNote, L"Generated automatically from the recording start time."},
    {Str::AfterRecording, L"After recording"},
    {Str::ShowCompletionNotification, L"Show a completion notification"},
    {Str::AutoOpenOutputFolder, L"Open the folder when finished"},
    {Str::MinimizeToTray, L"Minimize to the system tray"},
    {Str::MinimizeToTrayNote, L"Closing the recorder window keeps recording in the background."},
    {Str::InterfaceLanguage, L"Language"},
    {Str::LanguageSystem, L"Use system language"},
    {Str::LanguageChinese, L"中文（简体）"},
    {Str::LanguageEnglish, L"English"},
    {Str::AudioSettingsHint,
     L"Choose the audio sources new recordings start with. Meeting apps may use their "
     L"own device selection."},
    {Str::AudioSystemDefault, L"Record system audio by default"},
    {Str::AudioMicDefault, L"Record the microphone by default"},
    {Str::RecordingSettingsHint,
     L"Pick the video quality and encoder. Automatic works for most people."},
    {Str::Encoder, L"Encoder"},
    {Str::EncoderAuto, L"Automatic (recommended)"},
    {Str::EncoderHardware, L"Hardware only"},
    {Str::EncoderSoftware, L"Software (most compatible)"},
    {Str::CaptureCursor, L"Capture the mouse cursor"},
    {Str::PreviewDefault, L"Show the preview by default"},
    {Str::AboutVersion, L"Version"},
    {Str::AboutBuild, L"Build"},
    {Str::AboutMadeFor,
     L"Window recorder for meetings and demos. Everything stays on this PC."},
    {Str::AboutAuthor, L"Author"},
    {Str::AboutDiagnostics, L"Create diagnostics..."},
    {Str::AboutLogs, L"Open logs folder"},
    {Str::AboutLicense, L"Records locally. Nothing is uploaded."},
    {Str::SettingsSaved, L"Settings saved"},
    {Str::BrowseEllipsis, L"Browse..."},
    {Str::Apply, L"Apply"},

    {Str::TrayOpen, L"Show main window"},
    {Str::TrayStop, L"Stop recording"},
    {Str::TrayPause, L"Pause recording"},
    {Str::TrayResume, L"Resume recording"},
    {Str::TraySettings, L"Settings..."},
    {Str::TrayHelp, L"Help..."},
    {Str::TrayExit, L"Exit"},
    {Str::TrayRecordingElapsed, L"Recording %s"},
    {Str::TrayPausedElapsed, L"Paused %s"},
    {Str::TrayReady, L"MeetRec - Ready"},
    {Str::TrayError, L"MeetRec - Error"},
    {Str::TrayRecordingTooltip, L"MeetRec - Recording %s"},
    {Str::TrayPausedTooltip, L"MeetRec - Paused %s"},
    {Str::TrayErrorTooltip, L"MeetRec - Recording error"},
    {Str::ToastRecordingTitle, L"Recording"},
    {Str::ToastSavedTitle, L"Recording saved"},
    {Str::ToastPausedTitle, L"Paused"},
    {Str::ToastInfoClose, L"Dismiss notification"},

    {Str::ConfirmCloseRecordingTitle, L"Recording in progress"},
    {Str::ConfirmCloseRecordingBody,
     L"Stop recording and close the window? The file will be saved first."},
    {Str::ConfirmExitRecordingTitle, L"Exit MeetRec"},
    {Str::ConfirmExitRecordingBody,
     L"Recording is in progress. Stop recording, save the file, and exit? The recording "
     L"cannot continue after you exit."},
    {Str::ConfirmExitPreparingBody,
     L"Recording is getting ready. Exiting cancels this recording. Continue?"},
    {Str::NoTargetPrompt, L"Choose a window"},
    {Str::NoTargetPromptBody, L"Select the window you want to record first."},
    {Str::CannotStartTitle, L"Can't start recording"},
    {Str::DeviceHintTitle, L"Choose the audio devices your meeting uses"},
    {Str::DeviceHintBody,
     L"Meeting apps (Teams, Zoom, Tencent Meeting, and others) may pick their own\n"
     L"devices, which can differ from the Windows default.\n\n"
     L"Open the meeting app's Settings > Devices page to see which microphone it uses,\n"
     L"then select the same device in MeetRec. Headphones avoid echo."},
    {Str::PathInvalidTitle, L"Save location unavailable"},
    {Str::PathInvalidBody, L"Choose a folder that exists and that you can write to."},
    {Str::SavingCloseBlocked, L"Saving the recording. Please wait."},

    {Str::BrowseForFolder, L"Choose a folder"},
    {Str::NotAvailable, L"-"},
    {Str::Loading, L"Loading..."},
    {Str::Ellipsis, L"..."},
};

constexpr size_t kStrCount = static_cast<size_t>(Str::Count);
std::array<const wchar_t*, kStrCount> g_zh{};
std::array<const wchar_t*, kStrCount> g_en{};
Language g_language = Language::ZhCn;

void LoadTables() {
    static bool loaded = false;
    if (loaded) return;
    for (const Entry& entry : kZh) {
        g_zh[static_cast<size_t>(entry.id)] = entry.text;
    }
    for (const Entry& entry : kEn) {
        g_en[static_cast<size_t>(entry.id)] = entry.text;
    }
    loaded = true;
}

} // namespace

void SetLanguage(Language language) {
    LoadTables();
    g_language = language;
}

Language CurrentLanguage() {
    return g_language;
}

Language DetectSystemLanguage() {
    const LANGID lang = GetUserDefaultUILanguage();
    return PRIMARYLANGID(lang) == LANG_CHINESE ? Language::ZhCn : Language::EnUs;
}

Language LanguageFromSetting(const std::string& value) {
    if (value == "zh-CN") return Language::ZhCn;
    if (value == "en-US") return Language::EnUs;
    return DetectSystemLanguage();
}

const char* LanguageToSetting(Language language) {
    return language == Language::ZhCn ? "zh-CN" : "en-US";
}

const wchar_t* LanguageName(Language language) {
    return language == Language::ZhCn ? Text(Str::LanguageChinese) : Text(Str::LanguageEnglish);
}

const wchar_t* Text(Str id) {
    LoadTables();
    const size_t index = static_cast<size_t>(id);
    const std::array<const wchar_t*, kStrCount>& table =
        g_language == Language::ZhCn ? g_zh : g_en;
    if (index < kStrCount && table[index] != nullptr) return table[index];
    if (index < kStrCount && g_en[index] != nullptr) return g_en[index];
    return L"";
}

std::wstring Format(Str id, const std::vector<std::wstring>& args) {
    const std::wstring pattern = Text(id);
    std::wstring result;
    result.reserve(pattern.size() + 32);
    size_t argIndex = 0;
    for (size_t i = 0; i < pattern.size(); ++i) {
        if (pattern[i] == L'%' && i + 1 < pattern.size() && pattern[i + 1] == L's') {
            if (argIndex < args.size()) result += args[argIndex++];
            ++i;
        } else {
            result.push_back(pattern[i]);
        }
    }
    return result;
}

const wchar_t* ErrorTitle(ErrorCode code) {
    switch (code) {
        case ErrorCode::InitOsUnsupported: return Text(Str::ErrInitOsUnsupportedTitle);
        case ErrorCode::InitDriverMissing: return Text(Str::ErrInitDriverMissingTitle);
        case ErrorCode::InitD3dFailed: return Text(Str::ErrInitD3dTitle);
        case ErrorCode::DisplayNotFound: return Text(Str::ErrDisplayNotFoundTitle);
        case ErrorCode::DisplayDisconnected: return Text(Str::ErrDisplayDisconnectedTitle);
        case ErrorCode::TargetInvalid: return Text(Str::ErrTargetInvalidTitle);
        case ErrorCode::TargetLost: return Text(Str::ErrTargetLostTitle);
        case ErrorCode::CaptureInitFailed: return Text(Str::ErrCaptureInitTitle);
        case ErrorCode::CaptureDeviceLost: return Text(Str::ErrDeviceLostTitle);
        case ErrorCode::EncoderUnavailable: return Text(Str::ErrEncoderUnavailableTitle);
        case ErrorCode::EncoderWriteFailed: return Text(Str::ErrWriteFailedTitle);
        case ErrorCode::OutputNotWritable: return Text(Str::ErrOutputNotWritableTitle);
        case ErrorCode::OutputDiskLow: return Text(Str::ErrDiskLowTitle);
        case ErrorCode::FinalizeFailed: return Text(Str::ErrFinalizeTitle);
        case ErrorCode::InternalState: return Text(Str::ErrInternalTitle);
        case ErrorCode::None: return Text(Str::RecordingFailed);
    }
    return Text(Str::RecordingFailed);
}

const wchar_t* ErrorMessage(ErrorCode code) {
    switch (code) {
        case ErrorCode::InitOsUnsupported: return Text(Str::ErrInitOsUnsupportedBody);
        case ErrorCode::InitDriverMissing: return Text(Str::ErrInitDriverMissingBody);
        case ErrorCode::InitD3dFailed: return Text(Str::ErrInitD3dBody);
        case ErrorCode::DisplayNotFound: return Text(Str::ErrDisplayNotFoundBody);
        case ErrorCode::DisplayDisconnected: return Text(Str::ErrDisplayDisconnectedBody);
        case ErrorCode::TargetInvalid: return Text(Str::ErrTargetInvalidBody);
        case ErrorCode::TargetLost: return Text(Str::ErrTargetLostBody);
        case ErrorCode::CaptureInitFailed: return Text(Str::ErrCaptureInitBody);
        case ErrorCode::CaptureDeviceLost: return Text(Str::ErrDeviceLostBody);
        case ErrorCode::EncoderUnavailable: return Text(Str::ErrEncoderUnavailableBody);
        case ErrorCode::EncoderWriteFailed: return Text(Str::ErrWriteFailedBody);
        case ErrorCode::OutputNotWritable: return Text(Str::ErrOutputNotWritableBody);
        case ErrorCode::OutputDiskLow: return Text(Str::ErrDiskLowBody);
        case ErrorCode::FinalizeFailed: return Text(Str::ErrFinalizeBody);
        case ErrorCode::InternalState: return Text(Str::ErrInternalBody);
        case ErrorCode::None: return Text(Str::StartFailedHint);
    }
    return Text(Str::StartFailedHint);
}

bool ErrorStoppedRecording(ErrorCode code) {
    switch (code) {
        case ErrorCode::DisplayDisconnected:
        case ErrorCode::TargetLost:
        case ErrorCode::CaptureDeviceLost:
        case ErrorCode::EncoderWriteFailed:
        case ErrorCode::OutputNotWritable:
        case ErrorCode::OutputDiskLow:
        case ErrorCode::FinalizeFailed:
            return true;
        default:
            return false;
    }
}

std::wstring FormatDuration(std::int64_t milliseconds) {
    if (milliseconds < 0) milliseconds = 0;
    const std::int64_t totalSeconds = milliseconds / 1000;
    wchar_t buffer[32] = {};
    swprintf_s(buffer, L"%02lld:%02lld:%02lld", totalSeconds / 3600,
               (totalSeconds / 60) % 60, totalSeconds % 60);
    return buffer;
}

std::wstring FormatBytes(std::uint64_t bytes) {
    wchar_t buffer[64] = {};
    if (bytes < 1024) {
        swprintf_s(buffer, L"%llu B", static_cast<unsigned long long>(bytes));
    } else if (bytes < 1024ull * 1024ull) {
        swprintf_s(buffer, L"%.1f KB", static_cast<double>(bytes) / 1024.0);
    } else if (bytes < 1024ull * 1024ull * 1024ull) {
        const double mb = static_cast<double>(bytes) / (1024.0 * 1024.0);
        if (mb >= 100.0) swprintf_s(buffer, L"%.0f MB", mb);
        else swprintf_s(buffer, L"%.1f MB", mb);
    } else {
        const double gb = static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0);
        swprintf_s(buffer, L"%.2f GB", gb);
    }
    return buffer;
}

} // namespace vsr::app::i18n

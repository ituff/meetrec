# MeetRec 1.3.0

Windows 10 2004+ x64 / Windows 11 x64 · 便携版 · 内置 ffmpeg + ffprobe · 无需管理员权限

MeetRec 1.3.0 增加**录制中静音麦克风**：会议进行中不方便切回主界面时，可以用底部麦克风按钮、托盘菜单或全局快捷键 `Ctrl+Alt+M` 静音录像中的自己；在 Windows 声音设置里静音麦克风也会被自动跟随。这个版本同时修复了录制启动阶段 ffmpeg 管道连接的可靠性问题。

## 1.3.0 变更

- **录制中静音麦克风**
  - 录制/暂停时，底部“暂停”左侧新增麦克风按钮；托盘菜单新增“静音录制麦克风 / 取消录制麦克风静音”
  - 全局快捷键 `Ctrl+Alt+M`，窗口隐藏到托盘、会议软件在前台时仍然有效
  - **只影响录像**：不会静音会议软件，对方仍然能听到你；再次按键或点击按钮恢复
  - 每次开始录制默认恢复为未静音；暂停/继续期间保持当前静音状态
  - 状态栏会显示“手动静音”或“Windows 已静音麦克风”的提示；操作后显示非打断式通知
- **跟随 Windows 端点静音**
  - 在 Windows 声音设置/音量合成器中静音所选麦克风，或耳机硬件静音被驱动映射为端点 mute 时，录像中自动剔除麦克风
  - 电平条同步归零；日志记录 `AUDIO_MIC_SYSTEM_MUTED` / `AUDIO_MIC_SYSTEM_UNMUTED`
  - 只读取端点静音状态，**从不调用 `SetMute`**，不会改变系统或会议软件的麦克风状态
- **会议软件静音按钮仍无法跟随**：Zoom/Teams/腾讯会议的静音是应用层状态，不会改变 Windows 采集端点，也没有公开 API 能读取。需要静音录像时请使用 MeetRec 的按钮或 `Ctrl+Alt+M`。
- **录制启动可靠性**
  - 编码器先于画面捕获启动，避免繁忙窗口下 PrintWindow/D3D 捕获与 ffmpeg 子进程启动竞争导致的 8 秒管道连接超时
  - ffmpeg 子进程在启动阶段退出时立即报错，不再等满超时；启动失败或会话回滚会清理中间文件和未完成的 `.partial.mp4`
  - `FFMPEG_WRITER_CREATED` 日志新增 `ffmpegPath` 和 `startupMs`
- **首页标题栏新增「压缩视频」入口**（更多菜单和托盘菜单入口保留）
- 版本号 1.3.0.0

## 验证

- 103 项单元测试全部通过（新增麦克风静音状态组合测试）
- Debug / Release 构建通过；`MeetRec.exe --self-check` 核心能力通过（本机未加载虚拟显示器驱动，与便携版录制路径一致）
- 音频端点行为需在真机确认：
  - 录制中按 `Ctrl+Alt+M` / 托盘 / 按钮静音后，录像中自己的声音消失、系统声音保留；再按恢复
  - 在 Windows 声音设置中静音所选麦克风，日志出现 `AUDIO_MIC_SYSTEM_MUTED`，录像与电平条同步静音
  - 仅麦克风录制时静音期间文件时长连续，播放为静音而不是中断
- 已知平台限制：会议软件自己的静音按钮不会被第三方程序读取，因此不会同步到录像

## 下载与安装

资产：`MeetRec-1.3.0-win64-with-ffmpeg.zip`

1. 解压整个文件夹（不要只移动 exe）。
2. 双击 `MeetRec.exe`。
3. 无需安装、无需管理员、无需虚拟显示器驱动。

`ffmpeg.exe`、`ffprobe.exe` 及其 DLL 必须与 `MeetRec.exe` 同目录；程序会自动发现。设置与日志位于 `%LOCALAPPDATA%\VirtualScreenRecorder`。

## 系统要求

- Windows 10 2004 (build 19041) 或更高版本，x64；或 Windows 11 x64
- Direct3D 11 显卡
- **没有 32 位版本**；ARM64 未官方支持

## 已知限制

- 发行包未进行代码签名，SmartScreen 可能提示“未知发布者”
- 自动更新不会绕过 SmartScreen/杀毒软件；企业环境可通过设置或组策略关闭自动检查
- 会议软件自身的静音按钮不会自动同步到录像；手动静音只影响录像，不影响会议
- 静音检测跟随 MeetRec 中选择的麦克风端点；如果会议软件使用另一个设备，请两边选择同一个设备
- 压缩为重新编码（有损），已高度压缩的源文件收益有限
- 预览对部分受保护/特殊合成窗口不可用，但录制仍可继续
- 目标窗口最小化可能导致画面冻结
- 无 HDR / H.265 / AV1 / 4K；仅 SDR H.264 MP4

## FFmpeg

内置 FFmpeg（GPLv3 构建，含 libx264）及 `ffprobe`。许可证与对应源码信息见包内 `licenses/`。MeetRec 以独立子进程调用 ffmpeg/ffprobe，不链接 FFmpeg 库。

---

# English summary

MeetRec 1.3.0 adds **muting the recorded microphone during a meeting**: use the microphone button left of *Pause*, the tray item *Mute recording mic*, or the global `Ctrl+Alt+M` shortcut to remove your voice from the recording without touching the meeting app. Muting the microphone in Windows Sound settings is followed automatically. The release also makes recording startup more reliable: the encoder is created before capture starts, ffmpeg child failures are reported immediately, and unfinished intermediate/`.partial.mp4` files are removed on a failed start. A meeting app's own Mute button remains invisible to third-party programs and is not synchronized; use MeetRec's control when the recording should be silent. 103 unit tests pass; Debug/Release builds and `--self-check` pass (the environment had no virtual display driver loaded).

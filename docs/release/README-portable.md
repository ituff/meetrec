# MeetRec 1.5.2 — Windows 10/11 x64 便携版

MeetRec 是会议与演示的窗口录制器：选择一个应用窗口，录成干净的 MP4，可同时混入系统声音（对方）和麦克风（我的声音）。

## 快速开始

1. 将整个压缩包解压到一个可写目录（例如 `D:\Apps\MeetRec`）。
2. 双击 `MeetRec.exe`。
3. 选择要录制的会议/演示窗口。
4. 确认系统声音和麦克风电平条有跳动，点击“开始录制”。
5. 结束后点击“结束并保存”，在结果卡中打开文件或文件夹。
6. 旧录屏可在标题栏「压缩视频」按钮或托盘菜单中重新压缩；压缩窗口右侧可选「硬件加速」（自动检测 Intel QSV / NVIDIA NVENC / AMD AMF，默认关闭），程序会自动检查 GitHub 更新，可在「更多」菜单中下载并安装。
7. 录制中需要静音自己的声音时，点击「暂停」左侧的麦克风按钮、托盘菜单「静音录制麦克风」或按 `Ctrl+Alt+M`；这只影响录像，不会静音会议软件。在 Windows 声音设置里静音麦克风会被自动跟随。
8. 录制时 CPU 占用高时，可在「设置 → 录制 → 编码方式」选择「硬件编码（GPU）」：程序会自动检测并使用 Intel QSV / NVIDIA NVENC / AMD AMF，显著降低 CPU 占用，但文件通常更大；显卡驱动不支持时开始录制前会提示切回自动或软件编码。

> 请保持文件夹完整：`ffmpeg.exe`、`ffprobe.exe` 及其 DLL 必须和 `MeetRec.exe` 放在同一目录。只复制 exe 会导致无法使用内置的 ffmpeg 录制后端和视频压缩功能。

## 系统要求

- Windows 10 2004（build 19041）或更高版本，x64
- 或 Windows 11 x64
- Intel/AMD x64 处理器
- Direct3D 11 显卡
- **不需要管理员权限，不需要虚拟显示器驱动，不需要安装**
- 已静态链接 C++ 运行库，不需要安装 VC++ Redistributable

**没有 32 位版本。** Windows 10 的 32 位版本和 ARM64 均不在支持范围；本发行版只有 x64。Windows 10 已于 2025-10-14 结束常规支持，建议升级 Windows 11。

## 数据位置

- 设置、日志、诊断包：`%LOCALAPPDATA%\VirtualScreenRecorder`
- 默认录制目录：用户的“视频”目录（实际路径以主界面/设置页显示为准）
- 设置页可修改保存位置、画质、编码方式、界面语言、录制完成后的行为

## 首次运行提示

- 本程序尚未进行代码签名。Windows SmartScreen 可能提示“未知发布者”，请选择“更多信息 → 仍要运行”。
- 首次选择窗口后，程序会以共享模式打开音频端点来显示电平条；Windows 可能显示麦克风正在使用，这是正常的电平检测，开始录制后会切换为录制管线。
- 录制期间不要最小化目标窗口；Windows 可能停止重绘，导致画面冻结。
- 建议戴耳机：外放会让麦克风录到对方声音，产生回声。
- 请选择会议软件“设置 → 设备”中正在使用的麦克风/扬声器。

## 已知限制

- 某些受保护/特殊合成的窗口无法显示预览；不影响录制本身。
- 无 HDR / H.265 / AV1 / 4K 录制；仅 SDR、H.264 MP4。
- 不绕过 DRM、锁屏、UAC 安全桌面。
- 会议软件自己的静音按钮无法被第三方程序读取，因此不会自动同步到录像；需要静音录像时请用 MeetRec 的麦克风按钮或 `Ctrl+Alt+M`。
- 当前发行版未签名；如用于企业分发，请先进行代码签名与内部测试。

## 内置 ffmpeg

本包内置 FFmpeg 以直接使用高质量、小体积的 x264 录制后端，无需用户另行安装。

- 版本与构建信息：见 `licenses/FFmpeg-SOURCE.txt`
- GPLv3 build（`--enable-gpl --enable-version3 --enable-libx264`）
- 许可证：`licenses/FFmpeg-GPLv3.txt`
- 源码 / 构建提供方：<https://github.com/BtbN/FFmpeg-Builds>
- MeetRec 以独立子进程 + 管道方式调用 ffmpeg，不链接 FFmpeg 库。

---

# MeetRec 1.5.2 — Windows 10/11 x64 (portable)

Unzip the whole folder and run `MeetRec.exe`. No installer, administrator rights, or virtual-display driver is required. `ffmpeg.exe`, `ffprobe.exe`, and their DLLs are included and must stay next to the application binary so the ffmpeg recording backend and the video compressor can be discovered. The C++ runtime is linked statically, so no VC++ Redistributable is needed.

Compress older recordings from the title-bar **Compress videos** button or the tray menu. The compress window also offers an optional **Hardware acceleration** switch (auto-detects Intel QSV / NVIDIA NVENC / AMD AMF; off by default). The app checks GitHub Releases for updates automatically and offers download/install from the *More* menu.

To mute your own voice in the recording, use the microphone button left of *Pause*, the tray item *Mute recording mic*, or `Ctrl+Alt+M`. This only affects the recording; the meeting app still hears you. Muting the microphone in Windows Sound settings is followed automatically. A meeting app's own Mute button is internal to that app and cannot be detected, so use MeetRec's control when the recording should be silent.

If recording uses too much CPU, set **Settings → Recording → Encoder → Hardware (GPU)**: MeetRec detects and uses Intel QSV / NVIDIA NVENC / AMD AMF, which lowers CPU usage significantly but usually produces larger files. If the graphics driver has no usable encoder, recording stops before capture and asks you to switch back to Automatic or Software.

Requirements: Windows 10 2004+ x64 or Windows 11 x64; Direct3D 11 GPU. There is no 32-bit build; 32-bit Windows 10 and ARM64 are not supported. Windows 10 reached end of regular support on 2025-10-14.

Settings/logs: `%LOCALAPPDATA%\VirtualScreenRecorder`. Default recordings go to the user's Videos folder (the exact path is shown in the app). The build is not code-signed; SmartScreen may warn about an unknown publisher. See `licenses/` for the bundled FFmpeg GPLv3 license and exact source revision.

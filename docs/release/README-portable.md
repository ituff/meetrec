# MeetRec 1.0.0 — Windows 11 x64 便携版

MeetRec 是会议与演示的窗口录制器：选择一个应用窗口，录成干净的 MP4，可同时混入系统声音（对方）和麦克风（我的声音）。

## 快速开始

1. 将整个压缩包解压到一个可写目录（例如 `D:\Apps\MeetRec`）。
2. 双击 `VirtualScreenRecorder.exe`。
3. 选择要录制的会议/演示窗口。
4. 确认系统声音和麦克风电平条有跳动，点击“开始录制”。
5. 结束后点击“结束并保存”，在结果卡中打开文件或文件夹。

> 请保持文件夹完整：`ffmpeg.exe` 及其 DLL 必须和 `VirtualScreenRecorder.exe` 放在同一目录。只复制 exe 会导致无法使用内置的 ffmpeg 录制后端。

## 系统要求

- Windows 11 x64（build 22000 或更高）
- Intel/AMD x64 处理器
- Direct3D 11 显卡
- **不需要管理员权限，不需要虚拟显示器驱动，不需要安装**

**没有 32 位版本。** Windows 11 不再提供 32 位版本；本发行版只有 x64。ARM64 设备可以尝试 x64 模拟，但不在官方支持范围内。

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
- 当前发行版未签名；如用于企业分发，请先进行代码签名与内部测试。

## 内置 ffmpeg

本包内置 FFmpeg 以直接使用高质量、小体积的 x264 录制后端，无需用户另行安装。

- FFmpeg version N-121437-gf4a87d8ca4-20251015
- GPLv3 build（`--enable-gpl --enable-version3 --enable-libx264`）
- 许可证：`licenses/FFmpeg-GPLv3.txt`
- 对应源码：`licenses/FFmpeg-SOURCE.txt` / <https://github.com/FFmpeg/FFmpeg/tree/f4a87d8ca4>
- MeetRec 以独立子进程 + 管道方式调用 ffmpeg，不链接 FFmpeg 库。

---

# MeetRec 1.0.0 — Windows 11 x64 (portable)

Unzip the whole folder and run `VirtualScreenRecorder.exe`. No installer, administrator rights, or virtual-display driver is required. `ffmpeg.exe` and its DLLs are included and must stay next to the application binary so the ffmpeg recording backend can be discovered.

Requirements: Windows 11 x64 (build 22000+), Direct3D 11 GPU. There is no 32-bit build; Windows 11 is x64-only. ARM64 is not officially supported.

Settings/logs: `%LOCALAPPDATA%\VirtualScreenRecorder`. Default recordings go to the user's Videos folder (the exact path is shown in the app). The build is not code-signed; SmartScreen may warn about an unknown publisher. See `licenses/` for the bundled FFmpeg GPLv3 license and exact source revision.

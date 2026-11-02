# MeetRec 1.1.0

Windows 10 2004+ x64 / Windows 11 x64 · 便携版 · 内置 ffmpeg · 无需管理员权限

MeetRec 1.1.0 将产品正式命名为 **MeetRec**（可执行文件 `MeetRec.exe`），并把支持范围扩展到 **Windows 10 2004 (x64) 及以上**，同时保留 Windows 11 x64 支持。发布包内置 ffmpeg 与静态 C++ 运行库，解压即用，不需要单独安装运行库或 ffmpeg。

## 1.1.0 变更

- **可执行文件更名为 `MeetRec.exe`**（设置/日志目录仍为 `%LOCALAPPDATA%\VirtualScreenRecorder`，升级不丢配置）
- **支持 Windows 10 2004+ x64**；Windows 11 x64 继续支持
- **静态链接 C++ 运行库**：目标机器无需安装 VC++ Redistributable
- 内置 ffmpeg（x64，GPLv3）与许可证/源码说明
- 版本号 1.1.0.0

> Windows 10 已于 2025-10-14 结束常规支持；仍可运行，但建议升级 Windows 11 以持续获得安全更新。Windows 10 32 位不在支持范围。

## 亮点（自 1.0.0）

### 全新的 MeetRec 界面
- 按设计稿实现自定义 Win32/GDI+ 界面：自定义标题栏、目标窗口卡片、音频设置、画质/保存位置、预览区和底部状态栏
- 覆盖完整状态：就绪、准备中、录制中、已暂停、正在保存、完成结果卡、错误卡
- 窗口选择器支持搜索、应用图标/标题，以及 **“只看会议应用”筛选**（Teams、Zoom、腾讯会议、VooV、钉钉、飞书、企业微信、Webex、Skype、GoToMeeting、BlueJeans、华为云会议、浏览器 Google Meet）
- 设置窗口包含常规 / 音频 / 录制 / 关于，关于页含作者与项目链接
- 系统托盘菜单与录制通知卡片

### 声音更容易确认
- 选中窗口后立即显示系统声音和麦克风电平（共享模式检测），无需先开始录制
- 系统声音与麦克风相互独立：任一声源都可单独创建 AAC 音轨
- 录制中电平条持续更新，控件禁用也不影响观察

### 预览与分栏
- 预览优先使用 DWM 实时缩略图，PrintWindow 作为回退；受保护窗口会显示“无法预览”，录制不受影响
- 预览双缓冲，10 FPS 刷新不再闪烁
- 设置区与预览区可左右拖动（最小左侧 380、最小右侧 260 逻辑像素），双击手柄恢复默认 40/60；比例会持久化

### 录制与结果
- 三档画质（720p30 / 1080p30 / 1080p60），会议向 x264 参数（capped CRF、长 GOP、屏幕内容优化、单声道 64 kbps AAC）
- ffmpeg 后端内置在发行包中，自动优先使用；不可用时回退 Media Foundation
- 停止后结果卡显示文件名、时长、大小、路径，可打开文件、打开文件夹、再录制一个
- 录制失败/目标关闭/磁盘不足等错误有可操作的错误卡和诊断包入口
- 空录制检测：没有编码帧时提示文件可能无法播放

### 中英双语
- 默认跟随 Windows 显示语言，可在 设置 → 常规 → 界面语言 中切换“跟随系统 / 中文 / English”
- 所有 UI 文案、托盘、通知和错误提示均本地化；`VSR-*` 错误码保持稳定

## 下载与安装

资产：`MeetRec-1.1.0-win64-with-ffmpeg.zip`

1. 解压整个文件夹（不要只移动 exe）。
2. 双击 `MeetRec.exe`。
3. 无需安装、无需管理员、无需虚拟显示器驱动。

`ffmpeg.exe` 及其 DLL 必须与 `MeetRec.exe` 同目录；程序会自动发现。设置与日志位于 `%LOCALAPPDATA%\VirtualScreenRecorder`。

## 系统要求

- Windows 10 2004 (build 19041) 或更高版本，x64
- 或 Windows 11 x64
- Direct3D 11 显卡
- **没有 32 位版本**：Windows 10 32 位和 Windows 11（无 32 位版）都不在支持范围
- ARM64 未官方支持

## 已知限制

- 发行包未进行代码签名，SmartScreen 可能提示“未知发布者”
- 预览对部分受保护/特殊合成窗口不可用，但录制仍可继续
- 目标窗口最小化可能导致画面冻结
- 无 HDR / H.265 / AV1 / 4K；不绕过 DRM、锁屏、UAC
- 仅 SDR H.264 MP4
- Windows 10 已结束常规支持；本项目在 Win10 2004+ 上按 API 兼容性提供支持，建议在 Windows 11 上使用

## FFmpeg

内置 FFmpeg N-121437-gf4a87d8ca4-20251015，GPLv3 构建（含 libx264）。许可证与对应源码信息见包内 `licenses/`。MeetRec 以独立子进程调用 ffmpeg，不链接 FFmpeg 库。

---

# English summary

MeetRec 1.1.0 renames the executable to `MeetRec.exe`, adds **Windows 10 2004+ x64** support alongside Windows 11 x64, and links the C++ runtime statically so no VC++ Redistributable install is required. The portable package bundles FFmpeg (GPLv3) and its DLLs. Unzip and run `MeetRec.exe`; no installer or administrator rights required. No 32-bit build; ARM64 is not officially supported. Windows 10 reached end of regular support on 2025-10-14. The build is unsigned; SmartScreen may warn. See `README.txt` and `licenses/` in the package.

# MeetRec 1.5.2

Windows 10 2004+ x64 / Windows 11 x64 · 便携版 · 内置 ffmpeg + ffprobe · 无需管理员权限

MeetRec 1.5.2 合并了主窗口右上角的两个菜单入口：「⚙ 设置」按钮与「⋯ 更多」按钮合并为单一的「⋯」菜单，并去掉菜单里与其他入口重复的项。录制、压缩、更新等功能行为不变。

## 1.5.2 变更

### 标题栏入口合并

- 右上角现在只有 **「压缩视频」按钮 + 「⋯」菜单** 两个入口；独立的「⚙ 设置」按钮移除，设置改从「⋯ → 设置...」打开（「⋯」菜单第一项）。
- 「⋯」菜单去掉与其他入口重复的三项，功能入口不变：
  - 「压缩视频...」→ 标题栏「压缩视频」按钮（窗口隐藏到托盘时仍可用托盘菜单）；
  - 「关于」→ 设置 → 关于；
  - 「创建诊断包...」→ 设置 → 关于 → 诊断。
  - 保留：设置... / 检查更新... / 帮助... / 退出。
- 菜单弹出位置改为「⋯」按钮正下方（右对齐），不再悬挂在按钮左侧。
- 托盘菜单、键盘操作（Tab / Menu 键打开菜单）与各快捷键不变。

### 其他

- 用户手册（中/英）、README 与便携版说明同步了新的入口描述。
- 版本号 1.5.2.0。

## 验证

- Release 构建通过（Warning Level 4 + TreatWarningAsError），单元测试全部通过。
- 实际启动应用验证：标题栏布局、菜单项与弹出位置均符合预期（含 150% DPI）。
- 设置窗口、压缩窗口、托盘菜单不涉及本次改动。

## 下载与安装

资产：`MeetRec-1.5.2-win64-with-ffmpeg.zip`

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
- 硬件编码降低 CPU，但同画质下文件通常比 x264 大
- 硬件编码依赖显卡与驱动；检测不到或启动失败时录制前报错，需切回自动/软件编码
- 压缩为重新编码（有损），已高度压缩的源文件收益有限
- 会议软件自身的静音按钮不会自动同步到录像；手动静音只影响录像，不影响会议
- 预览对部分受保护/特殊合成窗口不可用，但录制仍可继续
- 目标窗口最小化可能导致画面冻结
- 无 HDR / H.265 / AV1 / 4K；仅 SDR H.264 MP4

## FFmpeg

内置 FFmpeg（GPLv3 构建，含 libx264）及 `ffprobe`。许可证与对应源码信息见包内 `licenses/`。MeetRec 以独立子进程调用 ffmpeg/ffprobe，不链接 FFmpeg 库。

---

# English summary

MeetRec 1.5.2 merges the two menu entries in the main window's title bar: the standalone *Settings* button is gone and the *⋯ More* menu is now the single entry, with *Settings...* as its first item. Three menu items that duplicated other entries were removed — *Compress videos...* (use the title-bar button or the tray menu), *About* (Settings → About) and *Create diagnostics...* (Settings → About → Diagnostics). The remaining items are Settings... / Check for updates... / Help... / Exit, and the menu now opens right-aligned under the ⋯ button itself. Tray menu, keyboard access and all shortcuts are unchanged. Release build passes with warnings-as-errors, unit tests pass, and the layout/menu were verified by running the app at 150% DPI.

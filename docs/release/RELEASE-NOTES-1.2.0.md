# MeetRec 1.2.0

Windows 10 2004+ x64 / Windows 11 x64 · 便携版 · 内置 ffmpeg + ffprobe · 无需管理员权限

MeetRec 1.2.0 增加**压缩旧录屏**和**自动更新**：除了录制会议，现在可以把其他软件以前录下的 MP4/MKV 用同一套屏幕内容编码配方重新压小；程序也会自动检查 GitHub Release，并在发现新版本时提醒、下载、校验和安装。

## 1.2.0 变更

- **压缩旧录屏**：菜单 → **更多 → 压缩视频...**（托盘菜单同样入口），打开独立窗口
  - 支持添加/拖放多个文件，识别时长、分辨率、大小与编码
  - 三档画质 + 可选 720p/1080p 降分辨率；输出 `<原名>-min.mp4`，绝不覆盖源文件
  - 队列串行执行，显示进度/速度/预计剩余时间，可取消当前任务
  - 预估输出体积；源码率已经较低时会提示“压缩收益可能有限”
  - **不与录制抢 CPU**：录制进行中自动暂停转码，录制结束后自动继续；转码进程以低于正常优先级运行
  - 新增 `VSR-TRANSCODE-FAILED` 错误码；转码失败不影响录制功能
- **自动更新**
  - 启动后自动检查 GitHub Release（每 24 小时最多一次），发现新版本时在托盘显示提醒
  - 菜单 → **更多 → 检查更新...** 可手动检查
  - 支持一键下载更新包，使用发布页的 `SHA256SUMS.txt` 校验后再安装
  - 安装时退出 MeetRec、解压替换便携版文件并自动重新启动；安装目录不可写时会改为打开发布页
  - 设置 → 常规 → **自动检查更新** 可关闭自动检查
- **发布包新增 `ffprobe.exe`**（压缩功能探测源文件用），请保持与 `MeetRec.exe` 同目录
- 版本号 1.2.0.0

## 验证

- 97 项单元测试全部通过（新增 14 项转码/子进程/版本比较测试）
- 端到端压缩实测：12.9 MB 1280×720 → 2.6 MB（标准）/ 1.7 MB（流畅）；91.3 MB 1080p 60 秒 → 15.7 MB
- 录制/转码竞争测试：录制开始后转码被挂起并回到队列；与无转码的基线录制对比，转码**未增加丢帧**（本机 GDI 捕获路径本身存在少量基线丢帧）

## 下载与安装

资产：`MeetRec-1.2.0-win64-with-ffmpeg.zip`

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
- 压缩为重新编码（有损），已高度压缩的源文件收益有限
- 预览对部分受保护/特殊合成窗口不可用，但录制仍可继续
- 目标窗口最小化可能导致画面冻结
- 无 HDR / H.265 / AV1 / 4K；仅 SDR H.264 MP4

## FFmpeg

内置 FFmpeg（GPLv3 构建，含 libx264）及 `ffprobe`。许可证与对应源码信息见包内 `licenses/`。MeetRec 以独立子进程调用 ffmpeg/ffprobe，不链接 FFmpeg 库。

---

# English summary

MeetRec 1.2.0 adds **compression of older recordings** and **automatic updates**. The compressor (More → Compress videos...) re-encodes existing MP4/MKV screen recordings with the same screen-content recipe, writes `<name>-min.mp4`, never modifies the source, runs at below-normal priority, and is paused while MeetRec is recording. The app now checks GitHub Releases automatically (at most once per 24 hours), notifies on new versions, downloads the release package, verifies it against the published `SHA256SUMS.txt`, and can install it by replacing the portable files and restarting. The package now also bundles `ffprobe.exe`, which must stay next to `MeetRec.exe`. 102 unit tests pass, including new transcoder, updater and version-comparison tests. A recording/transcode competition run suspended the job and requeued it at recording start, with no additional dropped frames versus a baseline recording (the local GDI capture path has a small baseline loss of its own).

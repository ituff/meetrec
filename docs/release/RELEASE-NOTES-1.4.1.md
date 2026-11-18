# MeetRec 1.4.1

Windows 10 2004+ x64 / Windows 11 x64 · 便携版 · 内置 ffmpeg + ffprobe · 无需管理员权限

MeetRec 1.4.1 重点修复录制启动的稳定性，以及长录像保存时可能丢失音频的问题。

## 1.4.1 变更

### 录制启动稳定性

- **修复新安装后首次录制可能启动失败**：内置 ffmpeg 约 185MB（共享 DLL），Windows Defender 等安全软件首次运行时会扫描这些文件。旧版只给 ffmpeg 8 秒打开输入管道；扫描期间会超时并中止录制。应用启动后现在会在后台预热 ffmpeg，让扫描提前完成。
- **管道连接超时 8 秒 → 30 秒**：即使预热尚未结束（例如安装后立即录制），也会等待扫描完成；ffmpeg 子进程提前退出时仍会立即失败并记录退出码和 stderr。
- **Preparing 阶段可随时取消**：等待 ffmpeg 启动时每 100ms 检查停止请求，点击停止会立即中止，不会显示为故障。
- 保留 1.3.0 的启动顺序：先创建编码器，再启动 1080p GDI/PrintWindow 捕获，避免捕获与 ffmpeg 启动争抢资源。

### 长录像音频保存

- **混流超时 20 秒 → 120 秒**：40 分钟以上的会议录像在磁盘/安全软件繁忙时，旧版 20 秒混流超时会把音频轨丢弃，只保留纯视频。现在有足够时间完成混流。
- **音频子进程未正常结束时不再盲目混流**：记录 `AUDIO_FINALIZE_FAILED` 并直接保留有效视频，避免在未完成的 M4A 上等待超时。
- 正常保存路径仍为 `ENCODER_FINALIZED`，输出 H.264 + AAC 的单个 MP4。

### 诊断

- `FFMPEG_WRITER_CREATED` 日志新增 `ffmpegPath`、`startupMs`。
- 启动日志新增 `FFMPEG_PREWARM_STARTED`。
- ffmpeg 启动超时错误现在包含等待秒数、子进程是否仍在运行以及 stderr。

## 验证

- 105 项单元测试全部通过；Release 构建通过（Warning Level 4 + TreatWarningAsError）。
- 模拟 Defender 首次扫描（修改内置 ffmpeg DLL 哈希，使其重新进入扫描）：首次启动到打开管道 15.5 秒，第二次 73ms；新的 30 秒超时下完整编码成功（总耗时 12.3 秒），旧的 8 秒会失败。
- 用一个永不连接管道的假进程验证：30 秒后正确报超时，子进程被清理。
- 5 秒 1080p 带音频录制：H.264 + AAC，音视频时长 5.97s / 5.97s。
- 在本机部署 1.4.1 测试构建后，启动日志确认 `FFMPEG_PREWARM_STARTED`，后台 ffmpeg 进程正常启动并退出。

## 下载与安装

资产：`MeetRec-1.4.1-win64-with-ffmpeg.zip`

1. 解压整个文件夹（不要只移动 exe）。
2. 双击 `MeetRec.exe`。
3. 无需安装、无需管理员、无需虚拟显示器驱动。

`ffmpeg.exe`、`ffprobe.exe` 及其 DLL 必须与 `MeetRec.exe` 同目录；程序会自动发现。设置与日志位于 `%LOCALAPPDATA%\VirtualScreenRecorder`。

## 系统要求

- Windows 10 2004 (build 19041) 或更高版本，x64；或 Windows 11 x64
- Direct3D 11 显卡
- **没有 32 位版本**；ARM64 未官方支持

## 已知限制

- 首次启动会在后台加载内置 ffmpeg（约 185MB），可能需要几秒钟；这是为了首次录制能立即开始
- 发行包未进行代码签名，SmartScreen 可能提示“未知发布者”
- 自动更新不会绕过 SmartScreen/杀毒软件；企业环境可通过设置或组策略关闭自动检查
- 硬件编码依赖显卡与驱动；检测不到时开关置灰，硬件编码失败会自动回退到 CPU
- 硬件编码以速度优先，同画质下输出文件通常比 x264 略大
- 压缩为重新编码（有损），已高度压缩的源文件收益有限
- 会议软件自身的静音按钮不会自动同步到录像；手动静音只影响录像，不影响会议
- 预览对部分受保护/特殊合成窗口不可用，但录制仍可继续
- 目标窗口最小化可能导致画面冻结
- 无 HDR / H.265 / AV1 / 4K；仅 SDR H.264 MP4

## FFmpeg

内置 FFmpeg（GPLv3 构建，含 libx264）及 `ffprobe`。许可证与对应源码信息见包内 `licenses/`。MeetRec 以独立子进程调用 ffmpeg/ffprobe，不链接 FFmpeg 库。

---

# English summary

MeetRec 1.4.1 fixes recording-start reliability on fresh installs and audio loss when saving long recordings. The bundled ffmpeg is about 185 MB of shared DLLs; real-time antivirus scans it on first use, and the previous 8-second pipe deadline aborted the recording while the scan was still running (measured 15.5 s for a first launch, 73 ms after the scan). The app now prewarms ffmpeg in the background at startup, allows 30 seconds for the input pipes while still failing fast when the child exits, and polls a cancel flag so Stop works during Preparing. The audio/video remux deadline is raised from 20 s to 120 s so long meeting recordings keep their audio track; the mux is skipped if the audio child did not finalize cleanly. `FFMPEG_WRITER_CREATED` now logs `ffmpegPath` and `startupMs`. 105 unit tests pass; Release build passes. A cold-scan simulation (modified ffmpeg DLL hash) succeeded in 12.3 s end-to-end under the new deadline, a non-connecting child is terminated after 30 s, and a 5-second 1080p recording produced matching H.264/AAC streams (5.97 s each). The deployed test build logged `FFMPEG_PREWARM_STARTED` and ran the prewarm child at startup.

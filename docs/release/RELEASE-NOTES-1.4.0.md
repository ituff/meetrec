# MeetRec 1.4.0

Windows 10 2004+ x64 / Windows 11 x64 · 便携版 · 内置 ffmpeg + ffprobe · 无需管理员权限

MeetRec 1.4.0 重点改进**压缩视频**功能：修复了点击「添加视频...」后窗口失去响应的问题，添加文件后不再自动开始转码，完成行新增「打开文件夹」按钮和原文件 / 压缩后体积对比；同时新增可选的**硬件加速**，自动检测并使用 Intel QSV / NVIDIA NVENC / AMD AMF 编码器。

## 1.4.0 变更

### 压缩窗口修复

- **修复点击「添加视频...」后应用失去响应**：压缩窗口所在 UI 线程使用 MTA（Media Foundation 需要），而 Shell 文件对话框要求 STA。添加视频和「更改...」输出文件夹现在在专用 STA 线程打开，结果通过窗口消息回传，不再卡死。
- **添加后不再自动转码**：上一批任务结束后队列回到空闲状态；之后添加的文件保持「等待中」，必须手动点击「开始压缩」。一批任务正在处理时追加的文件仍会接着当前批处理。
- 文件选择对话框同一时间只允许打开一个，避免重复点击弹出多个选择器。

### 压缩结果与队列

- 完成行新增「打开文件夹」按钮，可直接打开输出文件所在目录；「打开文件」保留。
- 完成行副标题显示体积对比，例如 `12.3 MB → 2.6 MB · 会议-min.mp4`，可直观看到压缩效果。
- 「清空列表」「打开文件」「打开文件夹」等操作保持不变，源文件永远不会被修改。

### 可选硬件加速（GPU）

- 画质 / 分辨率一行右侧新增「硬件加速」开关，**默认关闭**，仍使用 x264 以保证最小体积；开关状态保存在 `settings.json`。
- 打开压缩窗口后自动检测可用编码器，顺序为 `h264_qsv`（Intel）→ `h264_nvenc`（NVIDIA）→ `h264_amf`（AMD），使用一帧测试编码确认硬件真正可用；检测不到时开关置灰。
- 质量映射：QSV 使用 ICQ（`global_quality = CRF + 5`）；NVENC 使用 VBR + `cq` + 码率上限；AMF 使用 CQP。
- 开启时窗口下方提示「硬件加速已开启：速度更快，输出文件可能更大。」
- 硬件编码失败（驱动限制、不支持的输入等）会自动回退到 x264 重试并完成任务，日志记录 `HW_FALLBACK`；本次会话中关闭该开关。
- 版本号 1.4.0.0

## 验证

- 105 项单元测试全部通过，新增硬件编码参数测试和「批次结束后添加文件等待手动开始」回归测试。
- Debug / Release 构建通过（Warning Level 4 + TreatWarningAsError）。
- 压缩窗口 UI 验证：
  - 点击「添加视频...」后对话框正常弹出，窗口保持响应；取消后可再次打开；「更改...」文件夹选择器同样正常。
  - 点击完成行「打开文件夹」能打开输出目录；体积对比和两个操作按钮显示正常。
- 硬件加速本机验证（Intel Iris Xe）：
  - 自动检测到 `h264_qsv`；12 秒 1280×720 样片约 1.2 秒完成，输出 2.70 MB（x264 标准档 2.60 MB，数秒完成）。
  - 使用 QSV 无法处理的 8K 输入验证：硬件编码失败后自动回退 x264 并成功完成。
  - 任务管理器中的「GPU 3D」可能仍不高，硬件编码主要占用显卡的 Video Encode 引擎。

## 下载与安装

资产：`MeetRec-1.4.0-win64-with-ffmpeg.zip`

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

MeetRec 1.4.0 improves **Compress videos**. Clicking *Add videos...* no longer freezes the app: the Shell file dialogs now run on a dedicated STA thread because the UI thread uses the MTA required by Media Foundation. Files added after a finished batch stay queued until *Start compressing* is pressed, finished rows gain an *Open folder* button and a source → compressed size comparison, and the window offers an opt-in **Hardware acceleration** switch that auto-detects Intel QSV, NVIDIA NVENC or AMD AMF via a one-frame test encode. Hardware encoding is off by default (x264 still produces the smallest files), falls back to x264 if a job fails, and the choice is stored in `settings.json`. 105 unit tests pass; Debug/Release builds pass. On an Intel Iris Xe machine QSV encoded a 12 s 720p sample in about 1.2 s (2.70 MB vs. 2.60 MB for x264), and an 8K input that QSV rejected completed through the x264 fallback.

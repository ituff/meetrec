# MeetRec 1.5.0

Windows 10 2004+ x64 / Windows 11 x64 · 便携版 · 内置 ffmpeg + ffprobe · 无需管理员权限

MeetRec 1.5.0 让**录制**也能使用硬件编码：在设置里选择「硬件编码（GPU）」后，程序自动检测并使用 Intel QSV / NVIDIA NVENC / AMD AMF，显著降低会议录制时的 CPU 占用。同时改进了超长录像结束保存时的等待与诊断。

## 1.5.0 变更

### 录制硬件编码（GPU）

- **设置 → 录制 → 编码方式 → 硬件编码（GPU）**：录制前自动检测 `h264_qsv`（Intel）、`h264_nvenc`（NVIDIA）、`h264_amf`（AMD），使用一帧 3 帧测试编码确认硬件真正可用。
- **自动 / 软件编码仍使用 CPU x264**：默认保持「自动」，兼容性最好、同画质文件最小；「软件编码」强制 x264。
- 硬件编码使用稳定的实时码率控制：QSV 使用 ABR，NVENC 使用 VBR + `p5`/`hq`，AMF 使用 `vbr_peak`，均受预设码率上限约束。
- 检测不到可用硬件时，录制**在开始前**停止并显示 `VSR-ENCODER-UNAVAILABLE`，提示切回自动或软件编码；不会录制到一半才失败。
- 本机 Intel Iris Xe 实测（1080p30 测试窗口）：
  - x264：ffmpeg 进程约 1.2 个 CPU 核心；
  - QSV：ffmpeg 进程约 0.17 个 CPU 核心，整体编码 CPU 约降低 70–75%；
  - 硬件编码文件通常更大：测试窗口内容约 1.0 Mbps，x264 约 0.34 Mbps；会议静态画面/QSV 的实际码率取决于内容。
- 稳定性验证：连续 5 分钟和 7 分 33 秒 QSV 录制均完整结束，分别为 9,031 / 13,595 帧、30 fps、无编码中断；带音频的 15 秒录制输出 H.264 + AAC。
- 硬件编码依赖显卡驱动；超长或重要会议建议先用短录制确认本机驱动稳定，异常时切回「自动」或「软件编码」。

### 超长录像结束保存

- 用户点击结束/关闭目标窗口时，如果录制线程已经进入 `Stopping`（正在写 MP4 索引或混流），停止等待从固定 10 秒延长到最多 180 秒，避免 1 小时以上录像在保存阶段被误判为卡死；程序关闭和看门狗路径仍保持原有的 10 秒边界。
- 新增 `ENCODER_FINALIZE_TIMING` 日志（video/audio/mux/total 毫秒）和 `FINALIZE_SLOW` 警告，便于定位慢磁盘或超长录像的保存耗时。
- 1.4.1 的启动预热、30 秒管道等待、120 秒混流超时和音频保留策略保持不变。

### 其他

- 设置页「编码方式」标签更新为「硬件编码（GPU）」，并补充自动/硬件/软件三种模式的说明。
- 版本号 1.5.0.0。

## 验证

- 106 项单元测试全部通过（新增录制硬件编码参数测试），Debug / Release 构建通过（Warning Level 4 + TreatWarningAsError）。
- x264 回归：`自动` 和 `软件编码` 仍选择 `libx264`；`硬件编码` 在 Intel 机器上选择 `h264_qsv`。
- 连续 QSV 录制：5 分钟（301 秒、9,031 帧）与 7 分 33 秒（453 秒、13,595 帧、30 fps、0 丢帧），结束时 finalize 分别 0.28 秒 / 0.20 秒。
- 带音频的 QSV 录制 15 秒：视频 H.264 + 音频 AAC（单声道），保存正常。
- 超长录像停止等待路径在本机一次 1 小时 49 分挂起会话中触发 `STOP_FINALIZE_GRACE`，finalize 正常完成（0.28 秒）。

## 下载与安装

资产：`MeetRec-1.5.0-win64-with-ffmpeg.zip`

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
- 硬件编码降低 CPU，但同画质下文件通常比 x264 大；静态画面录制也可能按预设码率运行
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

MeetRec 1.5.0 lets **recordings** use GPU encoding. Selecting **Settings → Recording → Encoder → Hardware (GPU)** makes the bundled ffmpeg backend detect and use Intel QSV, NVIDIA NVENC or AMD AMF, cutting recording CPU usage substantially: on an Intel Iris Xe machine at 1080p30 the ffmpeg encoder process dropped from about 1.2 CPU cores (x264) to about 0.17 (QSV). Automatic and Software modes still use x264, and a machine with no working GPU encoder fails before capture starts with `VSR-ENCODER-UNAVAILABLE` instead of losing a partially recorded file. Hardware files are usually larger and depend on the driver, so a short test recording is recommended before relying on it for long meetings. Continuous QSV runs of 5 and 7.5 minutes completed with no dropped frames, and a 15-second QSV recording with audio produced H.264 + AAC. This release also extends the user-stop wait to 180 seconds once the recorder is in its bounded finalize/remux phase, preventing long recordings from being misreported as stalled while saving, and adds finalize timing logs. 106 unit tests pass; Debug/Release builds pass.

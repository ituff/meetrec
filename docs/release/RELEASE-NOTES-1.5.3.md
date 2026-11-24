# MeetRec 1.5.3

Windows 10 2004+ x64 / Windows 11 x64 · 便携版 · 内置 ffmpeg + ffprobe · 无需管理员权限

MeetRec 1.5.3 修正了 **硬件编码（GPU）录制文件明显大于压缩结果**的问题：GPU 录制现在使用与「压缩视频 → 硬件加速」相同的恒定质量码控，静态会议画面不再按目标码率填满，同样内容的录制文件与压缩结果体积接近。

## 1.5.3 变更

### GPU 录制码率控制与压缩对齐

此前 GPU 录制使用 ABR / 固定目标码率：

| 编码器 | 1.5.2 录制 | 压缩 |
| --- | --- | --- |
| NVENC | `-rc vbr -b:v <码率> -maxrate <码率>` | `-rc vbr -cq <CRF> -b:v 0 -maxrate <码率>` |
| QSV | `-b:v <码率>`（默认 VBR） | `-global_quality <CRF+5>`（ICQ） |
| AMF | `-rc vbr_peak -b:v <码率>` | `-rc cqp -qp_i/p/b <CRF>` |

ABR 会尽量花完目标码率，即使屏幕长时间静止；压缩的恒定质量模式按画面复杂度分配码率，因此同一段会议内容压缩后会小很多。

1.5.3 起录制复用与压缩完全相同的码控片段，同时保留：

- 5 秒关键帧间隔；
- 预设视频码率（NVENC 作为 maxrate 上限）；
- 单声道 64 kbps AAC 等音频参数。

### 行为说明

- 使用同一画质预设时，新 GPU 录制文件与压缩结果处于同一量级；用「压缩视频」再次处理新录制，体积收益会明显变小，这是预期行为。
- NVENC 恒定质量仍受预设码率上限约束；QSV（ICQ）和 AMF（CQP）没有码率上限，动态画面下体积随内容增加，与压缩端一致。
- x264（自动 / 软件编码）路径不变，文件仍最小。

### 其他

- 修复 `--encode-test --encoder hardware` 没有把编码器偏好传入 writer、实际仍使用 x264 的问题；现在可真正验证 GPU 录制管线，日志显示 auto/hardware/software。
- 修复 `scripts/test.ps1` 未设置 `SolutionDir`、构建后运行旧测试程序的问题。
- 用户手册（中/英）、README、已知限制与设置页提示同步更新。
- 版本号 1.5.3.0。

## 验证

- Debug / Release 构建通过（Warning Level 4 + TreatWarningAsError）。
- Debug / Release 单元测试 109 项全部通过；新增「录制与压缩 GPU 码控一致」回归测试。
- Intel QSV 实测：`--encode-test --seconds 5 --encoder hardware --backend ffmpeg` 成功产出 GPU 编码 MP4；NVENC / AMF 参数与压缩路径逐字一致（本机无对应硬件）。
- 打包后便携版 `--version` 与内置 ffmpeg 冒烟测试通过。

## 下载与安装

资产：`MeetRec-1.5.3-win64-with-ffmpeg.zip`

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
- 硬件编码降低 CPU；录制与压缩使用同一套恒定质量码控，体积接近压缩输出。NVENC 受预设码率上限约束，QSV/AMF 无码率上限，动态画面体积随内容增加
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

MeetRec 1.5.3 fixes GPU recordings being much larger than GPU-compressed output. Hardware recording now uses the same capped constant-quality rate control as the compressor — NVENC `-cq`, QSV ICQ (`global_quality`), and AMF CQP — instead of an ABR target that spent the full bitrate even on static slides. New GPU recordings and compressed output are now in the same size range, so re-compressing a fresh recording yields little additional savings. NVENC keeps its preset maxrate ceiling; QSV and AMF are content-driven exactly like the compressor. The x264 (Automatic / Software) path is unchanged. This release also fixes `--encode-test --encoder hardware`, which previously ignored the preference and always used x264, and `scripts/test.ps1`, which could run a stale test binary. Debug/Release builds pass with warnings-as-errors, all 109 unit tests pass (including a new recording-vs-compression rate-control parity test), and a QSV GPU encode was verified end-to-end.

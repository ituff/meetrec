# MeetRec 1.6.0

Windows 10 2004+ x64 / Windows 11 x64 · 便携版 · 内置 ffmpeg + ffprobe · 无需管理员权限

MeetRec 1.6.0 新增**本地会议转录**与**实时字幕**：录制结束后在 MP4 旁生成带说话人标签的
`.srt` / `.txt`，录制中可在目标窗口上方显示句级实时字幕。识别全部在本机完成，音频不会上传。

## 1.6.0 变更

### 本地会议转录

- 系统声音（对方）与麦克风（我）在混音前**分别识别**，天然得到「对方 / 我」两个说话人标签，
  不需要声纹聚类；避免把双方混在一起识别。
- 使用 SenseVoiceSmall int8（sherpa-onnx）：普通话、粤语、英语、日语、韩语自动检测，自带
  标点；CPU 推理，不依赖显卡。
- 结束后在 MP4 同目录生成 `<录制名>.srt`（字幕）与 `<录制名>.txt`（逐句文字稿），UTF-8 BOM，
  带时间戳与说话人前缀；不覆盖已有文件。
- 录制中每个最终句立即追加到 `<录制名>.partial.srt`，异常退出仍保留可读的转录。
- 暂停期间的声音会被排除（时间轴与视频一致）；停止/暂停会冲刷最后一句。
- 默认关闭。开启路径：**设置 → 录制 → 录制后生成会议转录稿**（首次开启会提示下载模型）。
  需要至少一个音频源。

### 实时字幕（P2）

- 录制中在目标窗口下方显示句级字幕浮层：上一句暗色、当前句高亮，左侧色条区分对方（蓝）与
  我（绿）；点击穿透、不抢焦点、不会被录进 MP4。
- 每句在 VAD 端点解码后出现，通常比说话晚 0.5–1.5 秒；连续发言最多 10 秒强制断句。
  不是逐字实时。
- 开关：**设置 → 录制 → 录制时显示实时字幕**、托盘菜单「实时字幕」或 `Ctrl+Alt+S`。
- 暂停、最小化目标窗口或停止录制时自动隐藏。

### 模型包与分发

- 主安装包保持轻量；**首次开启转录时程序会弹窗询问是否下载语音模型**（约 155 MB），
  随后显示下载 / 校验 / 解压 / 安装进度条，可随时取消。下载完成后自动启用转录。
- 模型未安装时,转录选项右侧还会显示「下载」按钮,可随时重新触发。模型安装到程序目录的
  `models\sensevoice\`；程序目录不可写（如安装在 Program Files）时
  安装到 `%LOCALAPPDATA%\VirtualScreenRecorder\models\sensevoice`。也可用
  `VSR_SPEECH_MODEL_DIR` 指定目录。
- 离线/企业环境仍可手动使用单独的模型包：
  `MeetRec-1.6.0-win64-speech-model.zip`（约 153 MB 压缩，解压约 230 MB）。
- sherpa-onnx 运行时 DLL 随主包分发并延迟加载：缺少 DLL 或模型包时录制完全不受影响，
  只是不生成转录/字幕。
- 模型权重遵循 FunASR Model Open Source License v1.1，模型包内附许可证与署名；运行时为
  Apache-2.0（见 `licenses/sherpa-onnx-*`）。

### 其他

- 新增开发/QA 命令：`MeetRec.exe --speech-test <16-bit PCM WAV> [--model DIR] [--out BASE]`；
  `--dev-record-window ... --audio --transcript` 可在无界面录制时显示字幕浮层。
- 单元测试新增重采样、SRT/TXT、崩溃 journal、模型包发现、字幕 feed 等覆盖。
- 版本号 1.6.0.0。

## 验证

- Release / Debug 构建通过（Warning Level 4 + TreatWarningAsError），**121 个单元测试全部通过**。
- 60 秒真实会议样本：9 段字幕，SRT/TXT 正常生成；模型加载约 2.5 秒。
- 20 秒真实窗口录制 + 系统音频播放：MP4 旁生成 SRT/TXT，`droppedChunks=0`；字幕浮层在实际
  屏幕上正确显示（上一句暗色、当前句蓝色、说话人前缀同行），MP4 中**不含**浮层。
- 打包后的程序 + 解压模型包到 `models\sensevoice`：无需环境变量即可转录。
- 删除 `sherpa-onnx-c-api.dll` 后程序仍可启动，转录功能仅报告不可用。

## 下载与安装

资产：

- `MeetRec-1.6.0-win64-with-ffmpeg.zip`（主程序 + ffmpeg + 转录运行时）
- `MeetRec-1.6.0-win64-speech-model.zip`（可选模型包，解压到程序目录）
- `SHA256SUMS.txt`

1. 解压主包到一个可写目录（不要只移动 exe）。
2. 如需转录，再把模型包解压到同一目录，确认 `models\sensevoice\` 存在。
3. 双击 `MeetRec.exe`。无需安装、无需管理员、无需虚拟显示器驱动。
4. 在「设置 → 录制」中开启转录，并启用系统声音或麦克风。

`ffmpeg.exe`、`ffprobe.exe`、`sherpa-onnx-c-api.dll`、`onnxruntime*.dll` 必须与
`MeetRec.exe` 同目录；程序会自动发现。设置与日志位于 `%LOCALAPPDATA%\VirtualScreenRecorder`。

## 系统要求

- Windows 10 2004 (build 19041) 或更高版本，x64；或 Windows 11 x64
- Direct3D 11 显卡
- 转录为 CPU 推理，建议 4 核以上；不需要独立显卡
- **没有 32 位版本**；ARM64 未官方支持

## 已知限制

- 发行包未进行代码签名，SmartScreen 可能提示“未知发布者”
- 转录模型包约 230 MB，需要单独下载；程序不会自动下载模型
- SenseVoice 不支持热词/术语偏置，人名、产品名、缩写需要人工校对
- 字幕为句级，不是逐字实时；短噪声/音乐片段可能被误识别
- 字幕浮层是置顶窗口，共享整个桌面时对方也能看到；它不会进入 MP4
- 预览对部分受保护/特殊合成窗口不可用，但录制仍可继续
- 目标窗口最小化可能导致画面冻结；字幕浮层在最小化时会隐藏
- 无 HDR / H.265 / AV1 / 4K；仅 SDR H.264 MP4

## FFmpeg

本包内置 FFmpeg（GPLv3，`--enable-gpl --enable-version3 --enable-libx264`），以独立子进程 +
管道方式调用，不链接 FFmpeg 库。许可证与源码信息见 `licenses/FFmpeg-*`。

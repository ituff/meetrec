# 附录：现状 UI / 状态 / 错误码 / 设置全表

> 配套主文档：[ui-redesign-brief.md](./ui-redesign-brief.md)
>
> 目的：让设计师和工程在同一个事实上讨论。这里的“现状”指 1.0.0.0 构建（窗口录制模式）。旧规格中的虚拟显示器流程仍存在于代码和部分 UI，但普通窗口录制不使用它。

---

## A. 当前窗口尺寸与布局

| 项 | 值 |
|---|---|
| 默认客户区 | 760 × 640 逻辑像素（125% 缩放下实际约 950 × 800） |
| 窗口样式 | 可调整大小、无最大化按钮 |
| 布局方式 | 固定坐标 `LayoutControls()`；只在创建和 DPI 变化时执行，不在 `WM_SIZE` 重排 |
| 字体 | Segoe UI 9pt（-9 逻辑像素），ClearType |
| 目标列表 | 460 × 200；行高 24；带图标、单行 `exe - 标题`、省略号 |
| 预览面板 | 260 × 150；约 10 FPS；空闲黑色 |
| 主按钮 | Start 150 × 36；Pause/Stop 110 × 36 |
| 状态区 | 多行 STATIC；错误行 460 × 40 |
| 默认尺寸下 | 所有控件刚好放下；拉伸窗口不会重排 |

---

## B. 当前控件清单

| # | 控件 | 类型 | 当前行为 | 启用/状态规则 | 备注 |
|---|---|---|---|---|---|
| 1 | 目标窗口列表 | ListBox（owner-draw） | 列出可见顶层窗口；单击选中；双击刷新 | 始终可用（除不可见） | 显示 `exe - 标题` + 16px 图标；无搜索；刷新会保持同一 HWND 的选中 |
| 2 | Refresh | Button | 重新枚举窗口，同时刷新音频设备（保留当前选择） | 始终可用 | 音频设备也会被刷新 |
| 3 | 保存位置 | Edit | 输出目录文本 | 始终可编辑 | 没有可见标签；不校验直到 Start |
| 4 | Browse... | Button | 打开文件夹选择对话框（STA 线程） | 始终可用；打开时禁用自身 | 选择后回填路径 |
| 5 | Open folder | Button | 用 ShellExecute 打开当前输出目录 | 始终可用 | 不是打开刚录的文件 |
| 6 | 画质 | ComboBox | Low / Standard / High | 始终可用 | 标签即参数：`Standard - 1920x1080 @ 30 FPS` |
| 7 | 编码器 | ComboBox | Auto (hardware first) / Hardware only / Software (slower, most compatible) | 始终可用 | 默认从 settings 读取；普通用户不需要理解 |
| 8 | Capture mouse cursor | Checkbox | 是否捕获鼠标指针（当支持时） | 始终可用 | 对会议录制通常无意义 |
| 9 | Show preview | Checkbox | 显示/隐藏预览面板；关闭时释放预览资源 | 始终可用 | 与预览面板距离远；默认开 |
| 10 | Record system audio (participants) | Checkbox | 录制系统声音（WASAPI loopback）；**当前实现中它也是整个音频管线的总开关** | 始终可用 | 默认关；与下方设备下拉关联；不勾它时只勾麦克风不会创建音轨 |
| 11 | Record microphone (my voice) | Checkbox | 录制麦克风 | 始终可用 | 默认开；与下方设备下拉关联；**依赖“系统声音”开关，当前不能单独启动音轨** |
| 12 | System audio 设备 | ComboBox | 默认 + 所有活动 render endpoint | 始终可用 | 标签：`System audio: Default - <name>` / `System audio: <name>`；下拉宽度 420 |
| 13 | Microphone 设备 | ComboBox | 默认 + 所有活动 capture endpoint | 始终可用 | 标签：`Microphone: Default - <name>` / `Microphone: <name>` |
| 14 | Start Recording | Button（默认按钮） | 校验目标后启动；失败弹 MessageBox | 仅 Ready 可用 | 无目标时弹提示；不检查音频设备 |
| 15 | Pause / Resume | Button | 暂停/继续；文本在 Pause/Resume 间切换 | Recording / Paused 可用 | 录制中文字变 Resume |
| 16 | Stop | Button | 异步停止并 finalize；最长约 10 秒 | Recording / Paused 可用 | 停止中禁用；无进度 |
| 17 | Diagnostics... | Button | 创建诊断包；成功/失败弹 MessageBox | 始终可用 | 没有进度/就地结果 |
| 18 | Connect display / Disconnect display | Button | 连接/断开虚拟显示器（异步） | Ready/Faulted 且无操作进行中 | **窗口录制不需要**；旧流程遗留 |
| 19 | Virtual display 状态 | Static | `active (WxH)` / `inactive` | 每 100ms 刷新 | 窗口录制时通常 `inactive`，容易误导 |
| 20 | State | Static | `State: Ready/Recording/...` | 每 100ms 刷新 | 开发者术语 |
| 21 | Duration | Static | `Duration: HH:MM:SS` | 每 100ms 刷新 | Ready 时可能显示上次会话时长 |
| 22 | FPS | Static | `FPS: N` | 每 100ms 刷新 | Ready 时可能显示上次会话 FPS |
| 23 | Dropped | Static | `Dropped: N` | 每 100ms 刷新 | Ready 时可能显示上次会话丢帧 |
| 24 | Target | Static | `Target: <exe>` | 每 100ms 刷新 | 只显示 exe，不显示标题（隐私） |
| 25 | Output | Static | `Output: <path>` | 每 100ms 刷新 | 长路径截断；无打开/复制 |
| 26 | Error | Static | `Error [VSR-...]: <message>` | 有错误时显示 | 460×40，长文案截断 |
| 27 | 预览面板 | 自绘子窗口 | 显示采集帧，保持宽高比，黑底 | 录制且有帧时 | 空闲黑块；10 FPS |
| 28 | 托盘图标 | Shell_NotifyIcon | 状态色圆点 + tooltip + 菜单 | 始终 | 颜色：蓝=就绪，红=录制，黄=暂停，灰=错误 |
| 29 | 托盘菜单 | PopupMenu | Open / Pause or Resume / Stop / Exit | 按状态显示/禁用 | 右键；左键双击打开 |
| 30 | 关闭按钮 | 系统 | 录制中隐藏到托盘（默认）或确认停止后关闭；空闲时退出 | 按状态 | 行为受隐藏设置 `minimizeToTray` 影响 |

---

## C. 状态机与命令可用性

### C.1 状态

`Initializing → Ready | Faulted`

`Ready → Preparing | ShuttingDown`

`Preparing → Recording | Ready | Faulted`

`Recording → Paused | Stopping | Faulted`

`Paused → Recording | Stopping | Faulted`

`Stopping → Ready | Faulted`

`Faulted → Ready | ShuttingDown`

### C.2 命令表

| 命令 | 允许的状态 | UI 表现 |
|---|---|---|
| Start | Ready | 仅 Ready 且目标/输出有效时启用 |
| Pause | Recording | Pause 按钮 |
| Resume | Paused | 同一按钮变 Resume |
| Stop | Recording / Paused | Stop 按钮；异步，最长约 10 秒 |
| EnableDisplay | Ready / Faulted | Connect display；异步 |
| DisableDisplay | Ready / Faulted | Disconnect display；录制中禁止 |
| Reset | Faulted | **当前 UI 没有入口**；Coordinator 有方法 |
| Exit | 非 ShuttingDown | 托盘 Exit / 关闭窗口 |

### C.3 UI 状态行为

| 状态 | 状态区 | Start | Pause/Resume | Stop | Display | 其他 |
|---|---|---|---|---|---|---|
| Initializing | State: Initializing | 禁用 | 禁用 | 禁用 | 禁用 | 短暂 |
| Ready | State: Ready | 可用（有目标） | 禁用 | 禁用 | 可用 | 上次会话的时长/FPS/丢帧/目标/输出可能仍显示 |
| Preparing | State: Preparing | 禁用 | 禁用 | 禁用 | 禁用 | 无取消；1–5 秒 |
| Recording | State: Recording | 禁用 | Pause 可用 | 可用 | 禁用 | 预览刷新；计时 |
| Paused | State: Paused | 禁用 | Resume 可用 | 可用 | 禁用 | 计时冻结 |
| Stopping | State: Stopping | 禁用 | 禁用 | 禁用 | 禁用 | 最长约 10 秒；无进度 |
| Faulted | State: Faulted | 禁用 | 禁用 | 禁用 | 可用（旧） | **无 Reset UI**；需重启 |
| ShuttingDown | State: ShuttingDown | 禁用 | 禁用 | 禁用 | 禁用 | 退出中 |

---

## D. 错误码与用户文案

> 错误码是稳定契约，不应删除；设计中可以把它们收进“详情/诊断”。下面列出主要错误码、触发场景和当前用户文案（来自源码）。

| 错误码 | 触发场景 | 当前用户文案（示例） | 建议的设计处理 |
|---|---|---|---|
| VSR-INIT-OS-UNSUPPORTED | 启动时不是 Windows 11 | “Virtual Screen Recorder requires Windows 11 (x64).” | 阻塞式启动页：说明系统要求，退出 |
| VSR-INIT-DRIVER-MISSING | 虚拟显示器创建失败（旧路径） | “SwDeviceCreate failed. Is the virtual display driver installed?” | 若移除虚拟显示器则不再出现；如保留，放高级并给安装指引 |
| VSR-INIT-D3D-FAILED | D3D11 设备创建失败 | “Direct3D 11 could not start on this machine. Update the GPU driver and try again.” | 启动/健康检查错误；给“更新显卡驱动/重试/诊断” |
| VSR-DISPLAY-NOT-FOUND | 找不到虚拟显示器 | “The virtual recording display is not active. Start recording from the app so it can create the display, or reinstall the virtual display driver.” | 旧路径；窗口录制不需要 |
| VSR-DISPLAY-DISCONNECTED | 录制中显示器断开 | “The virtual display was disconnected. The recording was stopped and saved.” | 结果卡：已保存到 X；错误横幅；重试 |
| VSR-TARGET-INVALID | 目标窗口无效/是自己的窗口 | “The selected window is no longer available. Pick a window that is still open.” / “The recorder itself cannot be recorded.” | 就地选择器错误；引导重新选择 |
| VSR-TARGET-LOST | 目标窗口关闭 | “Target window closed; stopping and saving the recording.” | 结果卡：录制已停止并保存；打开文件/选择新窗口 |
| VSR-CAPTURE-INIT-FAILED | 捕获启动失败 | “Capture could not start on the selected target.” | 错误横幅 + 重试/换窗口/诊断 |
| VSR-CAPTURE-DEVICE-LOST | GPU 重置/设备丢失 | “The GPU was reset or disconnected. The recording was stopped and saved.” | 结果卡 + 诊断；可能需要重启应用 |
| VSR-ENCODER-UNAVAILABLE | 没有可用 H.264 编码器 | “No compatible H.264 encoder is available on this machine. Install a GPU driver with hardware video encoding, or enable the Microsoft software H.264 encoder, then try again.” | 准备失败页；给“重试/切换编码方式/诊断”；不要只弹 MessageBox |
| VSR-ENCODER-WRITE-FAILED | 编码器停止响应/写入失败 | “The encoder stopped responding. The recording was stopped safely and the partial file was kept.” | 结果卡：文件已保留；诊断；重试 |
| VSR-OUTPUT-NOT-WRITABLE | 目录不可写/不存在/路径无效 | “The output folder is not writable. Choose a folder where your user account has write access.” / “The output folder does not exist. Create it or pick another.” | 就地校验保存位置；高亮字段；给“更改位置” |
| VSR-OUTPUT-DISK-LOW | 磁盘空间不足 | “Not enough free disk space for this recording. Free up space or pick a different drive.” / “Disk space is critically low. The recording was stopped and …” | 开始前预计大小 + 剩余空间；录制中停止时结果卡说明 |
| VSR-FINALIZE-FAILED | 文件重命名/finalize 失败 | “The recording file could not be renamed after finalization. The partial file was kept.” | 结果卡：说明文件位置（可能是 .partial.mp4）；打开文件夹/诊断 |
| VSR-INTERNAL-STATE | 非法命令/已有录制 | “This command is not available in the current state.” / “A recording is already running in this Windows session.” | 通常不应让用户看到；若出现，给“返回就绪/重启” |

### D.1 其他用户可见提示

| 场景 | 当前文案 |
|---|---|
| 未选择目标就点 Start | “Select an application window to record.” + “Note: minimizing the target application may freeze its rendering during recording (Windows may stop redrawing minimized apps).” |
| Start 失败 | MessageBox 标题 “Cannot start recording”，内容为错误的 `userMessage` |
| 显示器操作无法启动 | “Could not start the display operation.” |
| 诊断包创建成功 | “Diagnostics package created: <path>” |
| 诊断包创建失败 | “Diagnostics package could not be created. The log folder is available at %LOCALAPPDATA%\VirtualScreenRecorder\Logs.” |
| 录制中从托盘 Exit | “A recording is active. Stop the recording, finalize the file, and exit? (The recording cannot continue after exit.)” |
| 录制中关闭窗口（未启用托盘隐藏） | “A recording is active. Stop the recording and close the window?” |

---

## E. 当前 UI 字符串清单（英文）

### E.1 主窗口

- 窗口标题：`Virtual Screen Recorder`
- 分组框（代码中存在，但当前布局未显示）：`Target application:`、`Output folder:`、`Quality:`
- 按钮：`Refresh`、`Browse...`、`Open folder`、`Start Recording`、`Pause`、`Resume`、`Stop`、`Diagnostics...`、`Connect display`、`Disconnect display`
- 复选框：`Capture mouse cursor (when supported)`、`Record system audio (participants)`、`Record microphone (my voice)`、`Show preview`
- 画质：`Low - 1280x720 @ 30 FPS`、`Standard - 1920x1080 @ 30 FPS`、`High - 1920x1080 @ 60 FPS`
- 编码器：`Encoder: Auto (hardware first)`、`Encoder: Hardware only`、`Encoder: Software (slower, most compatible)`
- 音频设备：`System audio: Default - <device>`、`System audio: <device>`、`Microphone: Default - <device>`、`Microphone: <device>`
- 状态：`State: <state>`、`Duration: HH:MM:SS`、`FPS: N`、`Dropped: N`、`Virtual display: active (WxH)`、`Virtual display: inactive`、`Target: <exe>`、`Output: <path>`、`Error [<code>]: <message>`
- 状态名：`Initializing`、`Ready`、`Preparing`、`Recording`、`Paused`、`Stopping`、`Faulted`、`ShuttingDown`

### E.2 托盘

- 初始 tooltip：`Virtual Screen Recorder`
- 录制中 tooltip：`Recording HH:MM:SS`
- 暂停 tooltip：`Paused at MM:SS`
- 错误 tooltip：`Virtual Screen Recorder - error`
- 菜单：`Open`、`Pause`、`Resume`、`Stop`、`Exit`

---

## F. 设置项（settings.json）

> 路径：`%LOCALAPPDATA%\VirtualScreenRecorder\settings.json`。未知字段会保留（前向兼容）；非法值回退默认并记录警告。

| 字段 | 类型 | 默认 | UI 是否暴露 | 说明 |
|---|---|---|---|---|
| `schemaVersion` | int | 1 | 否 | 设置格式版本 |
| `outputDirectory` | string | 空 = 默认视频目录 | 是 | 保存位置 |
| `preset` | string | `standard` | 是 | `low` / `standard` / `high` |
| `encoderPreference` | string | `software` | 是（编码器下拉） | `auto` / `hardware` / `software`；默认软件编码以规避厂商 MFT 问题 |
| `encoderBackend` | string | `auto` | 否 | `auto` / `ffmpeg` / `mf`；普通用户不暴露 |
| `showPreview` | bool | true | 是 | 预览开关 |
| `captureCursor` | bool | false | 是 | 鼠标指针 |
| `minimizeToTray` | bool | true | **否** | 关闭窗口时是否隐藏到托盘；用户不可发现 |
| `restoreTargetWindow` | bool | true | 否 | 停止后恢复窗口位置（旧流程） |
| `recordSystemAudio` | bool | false | 是 | 系统声音 |
| `recordMicrophone` | bool | true | 是 | 麦克风 |
| `audioOutputDeviceId` | string | 空 = 默认 | 是 | 渲染端点 ID |
| `audioInputDeviceId` | string | 空 = 默认 | 是 | 采集端点 ID |
| `enableCrashDumps` | bool | false | 否 | 本地崩溃转储，严格 opt-in |

数据位置：

- 设置：`%LOCALAPPDATA%\VirtualScreenRecorder\settings.json`
- 日志：`%LOCALAPPDATA%\VirtualScreenRecorder\Logs\`
- 诊断包：`%LOCALAPPDATA%\VirtualScreenRecorder\Diagnostics\`（由 Diagnostics 按钮创建）

---

## G. 状态快照字段（UI 可用的数据）

| 字段 | 是否显示在当前 UI | 说明 |
|---|---|---|
| `state` | 是 | 状态机状态 |
| `correlationId` | 否 | 日志关联 ID |
| `targetHwnd` / `targetPid` | 否 | 目标窗口/进程 |
| `targetExeName` | 是（Target 行） | 仅 exe 名，隐私考虑 |
| `virtualMonitorActive` / `monitorX/Y/W/H` | 是（Virtual display 行） | 旧虚拟显示器状态 |
| `outputDirectory` | 是（编辑框） | 输出目录 |
| `partialPath` / `finalPath` | 是（Output 行） | 录制中 partial；完成后 final |
| `preset` | 是（画质下拉） | 画质 |
| `captureCursor` | 是（复选框） | 鼠标指针 |
| `activeElapsedMs` | 是（Duration） | 活动录制时长（不含暂停） |
| `approxFps` | 是（FPS） | 近似帧率 |
| `capturedFrames` / `encodedFrames` | 否 | 诊断数据 |
| `droppedFrames` | 是（Dropped） | 丢帧数 |
| `captureWidth` / `captureHeight` | 否 | 采集分辨率 |
| `hasError` / `errorCode` / `errorUserMessage` | 是（Error 行） | 最近错误 |

---

## H. 托盘与关闭行为

### H.1 托盘图标

- 蓝色（RGB 116,185,255）：就绪/空闲
- 红色（RGB 214,48,49）：录制中
- 黄色（RGB 254,211,48）：暂停
- 深灰（RGB 87,87,87）：错误
- 图标是运行时绘制的纯色圆点，没有产品图标/形状差异
- tooltip 更新按“状态 + 秒”节流
- Explorer 重启后自动重建托盘图标

### H.2 托盘菜单

| 项 | 行为 | 状态 |
|---|---|---|
| Open | 显示并激活主窗口 | 始终 |
| Pause / Resume | 录制中显示 Pause；暂停时显示 Resume | 仅 Recording/Paused |
| Stop | 停止并 finalize | Recording/Paused 可用；其他置灰 |
| Exit | 退出；录制中先确认、停止、finalize | 始终 |

### H.3 关闭/退出

- 录制/准备/暂停中关闭窗口：
  - `minimizeToTray = true`（默认）：隐藏到托盘，录制继续。
  - `minimizeToTray = false`：弹确认“A recording is active. Stop the recording and close the window?”；确认后停止并关闭。
- 空闲时关闭窗口：直接退出。
- 托盘 Exit 且录制中：弹确认“A recording is active. Stop the recording, finalize the file, and exit?”；确认后停止、finalize、退出。
- 关闭窗口/退出都不会静默丢弃录像；这是产品要求，设计不要改。

---

## I. 用户可见限制（影响界面文案）

来自 [known-limitations.md](../known-limitations.md)：

**画面**

- 目标窗口必须保持正常渲染；**最小化可能冻结画面**（当前窗口录制的主要限制）。
- DRM/刻意阻止捕获的应用会录成黑屏/冻结；产品不会绕过。
- UAC、锁屏、登录界面不会被捕获。
- 某些系统版本会在捕获内容周围画黄色边框，可能被录进去。
- 虚拟显示器只支持 1280×720@30、1920×1080@30（首选）、1920×1080@60；SDR 8-bit；不支持 HDR/H.265/AV1/4K。
- 虚拟显示器中途断开时，录制会安全停止并 finalize，保留已录内容。

**声音**

- 音频是 opt-in；系统声音默认关。
- 会议软件有自己的设备选择；录制器不能读取它；选错设备会导致该来源静音。
- 录制中插拔/切换设备不会自动跟随；该来源可能静音到录制结束。
- 无回声消除；外放会收录对方声音，建议戴耳机。
- 系统声音会录下所选输出端点的**所有**声音（会议、通知、音乐等）。
- 受保护音频路径录成静音；不会绕过。
- 麦克风电平固定，无自动增益；`VSR_MIC_GAIN` 环境变量可调（不是 UI）。
- 暂停期间不缓冲画面；暂停期间发生的事情不会录进去。

**文件/稳定性**

- 磁盘低于阈值会自动停止并 finalize。
- 崩溃/断电可能留下 `.partial.mp4`，可能不可播放；正常停止总是 finalize。
- 没有硬件编码器时回退软件编码；都没有则开始前失败。
- 某些 GPU 驱动会拒绝 GPU 样本，录制器会自动切换 CPU 路径；第一帧可能是极短黑帧。
- 仅支持 Windows 11 x64 交互式本地会话；RDP/Sandbox/无 GPU 服务器不在支持范围。

---

## J. 术语表（给设计和文案）

| 术语 | 人话解释 | 主界面是否出现 |
|---|---|---|
| 目标窗口 / target | 你要录的那个应用窗口 | 是（“录制目标”） |
| 虚拟显示器 / virtual display | 旧方案里创建的一块假屏幕，把窗口移过去再录 | 否（建议移除/高级） |
| 系统声音 / system audio / loopback | 电脑正在播放的声音，会议里通常是“对方”的声音 | 是（“系统声音（对方）”） |
| 麦克风 / microphone | 你自己的声音 | 是（“麦克风（我的声音）”） |
| 编码器 / encoder | 把画面压成视频文件的组件；普通用户不需要理解 | 高级/详情 |
| 画质 / preset | 分辨率 + 帧率 + 码率的组合 | 是（用 720p/1080p/1080p60） |
| FPS | 每秒帧数，画面流畅度 | 详情/诊断 |
| 丢帧 / dropped frames | 因为处理不过来而丢掉的画面 | 详情/诊断；异常时提示 |
| finalize | 把视频文件收尾、写完整，确保能播放 | 用户语言：“正在保存” |
| partial 文件 | 还没收尾的临时文件 `.partial.mp4` | 错误/详情 |
| 托盘 / notification area | 任务栏右下角的图标区域 | 是（“最小化到托盘”） |
| 单实例 | 同时只能开一个录制器；再次打开会激活已有窗口 | 否 |

---

## K. 参考代码位置（工程/设计对齐用）

| 内容 | 文件 |
|---|---|
| 主窗口控件、布局、文案、命令 | `src/App/MainWindow.cpp` / `.h` |
| 状态机与命令规则 | `src/Application/AppState.cpp` / `.h` |
| 状态快照字段 | `src/Application/StatusSnapshot.h` |
| 协调器/启动/停止/错误发布 | `src/Application/Coordinator.cpp` / `.h` |
| 错误码定义 | `src/Infrastructure/Error.h` / `.cpp` |
| 设置字段 | `src/Infrastructure/Settings.h` / `.cpp` |
| 托盘图标/菜单 | `src/App/TrayIcon.cpp` / `.h` |
| 预览面板 | `src/App/PreviewPanel.cpp` / `.h` |
| 输出校验/命名 | `src/Application/OutputFile.cpp` |
| 已知限制 | `docs/known-limitations.md` |

---

## L. 设计评审时需要工程确认的问题

1. 主窗口是否允许改变尺寸/自适应？当前固定坐标，不在 `WM_SIZE` 重排。
2. 是否新增“设置/偏好”窗口？还是用主窗口内的折叠面板？
3. 是否保留虚拟显示器 UI？如果保留，放在哪里、什么文案？
4. Faulted 的“重试/返回就绪”需要工程接 `Coordinator::Reset()`；确认交互。
5. `Preparing` / `Stopping` 是否允许取消？当前状态机不允许；如设计需要，需改状态机。
6. 麦克风电平测试需要新增音频电平 API/UI 更新；确认成本。
7. 麦克风能否独立于“系统声音”启动音轨？当前不能（`captureAudio` 由系统声音开关决定）。如果设计采用“只录我的声音”，需要工程修改音频管线。
8. 目标窗口缩略图/点击选择需要额外捕获/窗口交互；确认成本。
9. 托盘 toast / 悬浮 HUD / 全局快捷键需要额外窗口/热键；确认成本。
10. 暗色主题/高对比度需要自绘控件适配；确认范围。
11. 关闭时最小化到托盘的设置是否暴露；当前是隐藏设置。

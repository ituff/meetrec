# MeetRec 1.5.1

Windows 10 2004+ x64 / Windows 11 x64 · 便携版 · 内置 ffmpeg + ffprobe · 无需管理员权限

MeetRec 1.5.1 让**应用内更新下载**走 GitHub 反向代理 `https://jscdn.msone.top/`，改善从 GitHub 下载升级包的速度与可达性。

## 1.5.1 变更

### 更新下载加速

- 下载大的升级包（`MeetRec-*-win64-with-ffmpeg.zip`）时，优先使用
  `https://jscdn.msone.top/https://github.com/...` 反向代理；代理不可用、超时或返回的文件校验失败时，自动回退到 GitHub 原始地址。
- 新增 `UPDATE_PROXY_FALLBACK` 日志，记录代理失败原因与回退行为。
- **校验方式不变**：新版本的发布说明正文由 CI 写入 `SHA256: <hash>`，该数据从 `api.github.com` 直接获取（不走代理），因此即使代理返回了错误的文件，也会因 SHA-256 不匹配而被拒绝。
- 对于正文中没有 SHA256 的旧版本，仍读取 `SHA256SUMS.txt`：先尝试代理，失败再直连 GitHub。
- 更新版本检查（`api.github.com/repos/.../releases/latest`）仍直接访问 GitHub，不经过代理。

### 其他

- 1.5.0 的录制硬件编码（QSV/NVENC/AMF）、超长录像保存等待和压缩视频功能保持不变。
- 版本号 1.5.1.0。

## 验证

- 108 项单元测试全部通过（新增代理 URL 重写、发布正文 SHA-256 解析测试），Debug / Release 构建通过（Warning Level 4 + TreatWarningAsError）。
- 用与应用相同的 WinHTTP 客户端完整下载 `MeetRec-1.5.0-win64-with-ffmpeg.zip`（76,483,894 字节）经代理成功，SHA-256 与发布校验值一致。
- 本机测试网络直连 GitHub Release 资源偶发 `WinHttpSendRequest` / `WinHttpReceiveResponse` 失败，代理下载稳定；代理失败时的直连回退路径已保留。
- 代理 URL 形式已确认：`https://jscdn.msone.top/` + 原始 `https://github.com/...` 地址。

> 注意：1.5.0 及更早版本还不认识这个代理，首次升级到 1.5.1 仍需通过旧方式下载；从 1.5.1 起的后续更新会自动使用代理。

## 下载与安装

资产：`MeetRec-1.5.1-win64-with-ffmpeg.zip`

1. 解压整个文件夹（不要只移动 exe）。
2. 双击 `MeetRec.exe`。
3. 无需安装、无需管理员、无需虚拟显示器驱动。

`ffmpeg.exe`、`ffprobe.exe` 及其 DLL 必须与 `MeetRec.exe` 同目录；程序会自动发现。设置与日志位于 `%LOCALAPPDATA%\VirtualScreenRecorder`。

## 系统要求

- Windows 10 2004 (build 19041) 或更高版本，x64；或 Windows 11 x64
- Direct3D 11 显卡
- **没有 32 位版本**；ARM64 未官方支持

## 已知限制

- 更新检查仍直接访问 `api.github.com`；若该接口不可达，应用不会发现新版本，可手动打开 Releases 页面下载
- 第三方反向代理的可用性和速度不受本项目管理；失败时会自动回退 GitHub
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

MeetRec 1.5.1 routes in-app **update downloads** through the GitHub reverse proxy `https://jscdn.msone.top/` to improve download speed and reachability. The large release zip is fetched as `https://jscdn.msone.top/https://github.com/...`, with an automatic fallback to the original GitHub URL if the proxy fails, times out, or serves a file with the wrong hash (`UPDATE_PROXY_FALLBACK` is logged). Verification is unchanged: newer releases carry a `SHA256: <hash>` line in the release body fetched directly from `api.github.com`, so a proxy cannot substitute another package without failing the hash check; older releases still use `SHA256SUMS.txt` (proxy first, then direct). The release-check API stays direct. 108 unit tests pass; Debug/Release builds pass. The same WinHTTP client used by the app downloaded the 76,483,894-byte 1.5.0 package through the proxy and verified its SHA-256. Users on 1.5.0 or earlier need one manual/old-path update to 1.5.1; subsequent updates use the proxy automatically.

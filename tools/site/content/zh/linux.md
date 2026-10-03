---
status: translated
title: Linux
nav: Linux
description: 在 Linux 上通过 Proton 运行 GeForce 版，需要 NVIDIA 显卡。可以处理图片和视频，没有实时相机。
---
Linux 软件包在 Proton 下运行 Windows 版程序。Proton 是大家在 Steam 中熟悉的兼容层。启动脚本会自动完成所有配置。没有原生的 Linux 版本，因为 DLSS 5 运行库只有 Windows 版。

## 需要准备什么 {#requirements}

- 带桌面会话（X11 或 Wayland）的 64 位 Linux。
- 一块 NVIDIA GeForce RTX 显卡（20、30、40 或 50 系列），以及 5xx 或更新的 NVIDIA 驱动。Linux 上不支持 Radeon 显卡。
- `python3`（3.10 或更新），以及 `curl` 或 `wget`。
- 首次启动时，Proton 需要大约 3 GB 的磁盘空间。不需要 Steam。

## 安装并启动 {#install}

1. 从[下载区](index.html#download)下载 `VRChatDLSS5Cam-linux-x86_64.tar.gz`。
2. 解压，然后运行启动脚本：
   ```bash
   tar xzf VRChatDLSS5Cam-linux-x86_64.tar.gz
   cd VRChatDLSS5Cam
   ./vrchat-dlss5-cam
   ```
   => 首次启动会下载 Proton，需要几分钟。之后窗口打开。
3. 可选：`./install-linux.sh` 会把程序加入应用程序菜单，并添加 `vrchat-dlss5-cam` 命令。`./install-linux.sh --remove` 会把两者都移除。

设置、预设、日志和素材库保存在 `~/.local/share/VRChatDLSS5Cam/data`。更新方式与 Windows 相同。

## 支持情况 {#support}

| | 在 Proton 下 |
|---|---|
| 图片：DLSS 5、深度、超分辨率、预设、撤销 | 可用。 |
| 视频和动图作为输入 | 可用。 |
| 视频输出 | WebP、GIF、APNG 或 PNG 序列。没有 MP4，也没有声音：Proton 没有 H.264 或 HEVC 编码器。原本会保存为 MP4 的视频，会改存为 WebP。 |
| 实时相机（Spout） | 不可用。Spout 只存在于 Windows。 |
| 运动矢量 | FSR 光流；Proton 下没有 NVIDIA 光流。 |
| MCP、更新、镜像站、界面语言 | 可用。 |
| Radeon 版 | 不支持。 |

## 命令行 {#command-line}

启动脚本接受程序的选项，并把 Linux 路径转换成程序需要的形式：

```bash
./vrchat-dlss5-cam --headless --add ~/Pictures/shot.png --process ~/Pictures/out
./vrchat-dlss5-cam --open ./clip.mp4 --lang zh
```

`--linux-info` 显示正在使用的文件夹和版本，`--linux-reset` 重建 Proton 环境（设置和素材库会保留）。[全部选项](command-line.html)

## 出现问题时 {#troubleshooting}

- **首次启动要好几分钟。** 那是一次性下载 Proton 及其运行环境，大约 2.5 GB。
- **窗口打开了，但没有反应。** 锁屏或屏幕锁定程序占住了显示。解锁会话即可。
- **画面区域只显示灰色棋盘格。** 程序在使用 Proton 自带的着色器编译器。请使用 Linux 软件包，而不是 Windows 压缩包；Linux 软件包带有正确的编译器。
- **DLSS 5 一直显示“未运行”。** 缺少 NVIDIA 驱动中供 Windows 程序使用的桥接组件。`./vrchat-dlss5-cam --linux-info` 会显示是否找到了 `nvngx.dll`。请安装 5xx 或更新的 NVIDIA 驱动。

完整的 Linux 指南见 GitHub 上的 [LINUX.md](https://github.com/AlanBacker/VRChat-DLSS5-Cam/blob/main/docs/LINUX.md)（英文）。

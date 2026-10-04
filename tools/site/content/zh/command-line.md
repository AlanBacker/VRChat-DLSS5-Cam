---
status: translated
title: 命令行
nav: 命令行
description: 打开文件、不开窗口处理文件，以及用脚本进行测试运行。写给喜欢终端的人。
---
你完全不需要命令行：双击就能启动普通的窗口。命令行是为脚本、定时运行和测试准备的。

```
VRChatDLSS5Cam.exe [file] [options]
```

只给一个文件名，就会打开那张图片或那个视频，这也正是资源管理器中的**打开方式**所做的事。

## 示例 {#examples}

不开窗口，把两个文件处理到 `D:\out`，视频保存为 HEVC：

```
VRChatDLSS5Cam.exe --headless --add "D:\shots\a.png" --add "D:\shots\clip.mp4" --set videoMatchSource=0 --set videoOutput=1 --process "D:\out"
```

把视频中 1:00 到 1:10 的十秒处理到保存文件夹：

```
VRChatDLSS5Cam.exe --headless --open "D:\clip.mp4" --in 1:00 --out 1:10 --process
```

以日语界面和浅色主题打开一个视频，跳到第 30 秒，给窗口截一张图，然后退出：

```
VRChatDLSS5Cam.exe --window 1600x900 --lang ja --set theme=2 --open "D:\clip.mp4" --seek 30 --screenshot 6 "D:\ui.png" --exit-after 8
```

## 选项 {#options}

| 选项 | 作用 |
|---|---|
| `--open <file>` {#open} | 打开一张图片或一个视频，并加入素材库。GIF、APNG 或 WebP 动图算作视频。 |
| `--add <file>` {#add} | 把文件加入素材库，但不打开它。可以重复使用。 |
| `--seek <time>` {#seek} | 视频：显示这个时间点的帧。可以用 `m:ss`、`h:mm:ss` 或秒数。 |
| `--play` {#play} | 视频：开始播放。 |
| `--in <time>` · `--out <time>` {#in-out} | 视频：要处理的片段，声音也随之截取。 |
| `--process [folder]` {#process} | 用当前设置处理素材库（或已打开的文件），然后退出。输出到 `folder`；不指定时输出到保存文件夹。 |
| `--set <key>=<value>` {#set} | 只在本次运行中修改一项设置，使用它在 `settings.ini` 中的名称。见下文。 |
| `--lang <en\|zh\|ja\|ko\|auto>` {#lang} | 本次运行的界面语言。 |
| `--window <W>x<H>` {#window} | 以这个窗口尺寸启动。 |
| `--screenshot <seconds> <file.png>` {#screenshot} | 启动后过这么多秒，保存一张窗口截图。可以重复使用。 |
| `--exit-after <seconds>` {#exit-after} | 这么多秒后退出，会先等截图和要保存的文件写完。 |
| `--headless` {#headless} | 不显示窗口。保存和截图照常工作，工作完成后程序自动退出。 |
| `--data-dir <folder>` {#data-dir} | 把设置、预设、日志和 AI 问答面板的存储数据放在这个文件夹，而不是 `%LOCALAPPDATA%\VRChatDLSS5Cam`。 |
| `--mcp` {#mcp} | 供 MCP 客户端使用的桥接。[MCP](mcp.html) |
| `--mcp-port <port>` {#mcp-port} | 在本次会话中，让 MCP 服务器使用这个端口。 |
| `--update` {#update} | 在所选的通道上查找更新的版本并安装。 |
| `--edition <geforce\|amd>` {#edition} | 像更新一样，切换到另一个版本。 |

## 单次运行的设置 {#set-keys}

`--set` 使用 `settings.ini` 中的名称。在命令行中给出的值不会被保存。下面是几个常用的：

| 键 | 取值 |
|---|---|
| `nrIntensity` | `0` 到 `2`，例如 `--set nrIntensity=1.5`。 |
| `videoMatchSource` | `1`（默认）跟随源文件；`0` 使用下面三个键。 |
| `videoOutput` | `0` MP4 H.264，`1` MP4 HEVC，`2` PNG 序列，`3` GIF，`4` APNG，`5` WebP。 |
| `videoBitrate` · `webpQuality` | MP4 码率；WebP 质量，`50` 到 `100`。 |
| `keepAudio` | `0` 去掉声音。 |
| `outputName` · `captureName` | 文件名模板。[可以使用的词](saving.html#names) |
| `customResolution` · `customWidth` · `upscaleMode` | 放大：`--set customResolution=1 --set customWidth=3840 --set upscaleMode=0`（`0` 为 DLSS 超分辨率，`1` 为重采样）。 |
| `theme` | `0` 跟随 Windows，`1` 深色，`2` 浅色。 |
| `updateCheck` · `updateChannel` | 启动时检查（`1` 为开）；通道（`0` 稳定版，`1` 预览版）。 |
| `driverCheck` | 启动时的驱动提示（`1` 为开）。 |
| `reopenLast` | 启动时重新打开上次的文件（`0` 为关）。 |

## 退出码 {#exit-codes}

| 代码 | 含义 |
|---|---|
| `0` | 一切正常。 |
| `1` | 有文件处理失败。 |
| `2` | 程序崩溃（会写入 `crash.txt`），或者图形设备丢失。 |

日志 `log.txt` 会记录每一个命令行操作，运行失败后可以回头查看。在 Linux 上，`vrchat-dlss5-cam` 启动脚本接受相同的选项。[Linux](linux.html)

包括测试选项在内的完整列表，见 GitHub 上的 [COMMAND_LINE.md](https://github.com/AlanBacker/VRChat-DLSS5-Cam/blob/main/docs/COMMAND_LINE.md)（英文）。

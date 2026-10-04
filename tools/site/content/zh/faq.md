---
status: translated
title: 常见问题
nav: 常见问题
description: 大家问得最多的问题，简短回答。
---
## 免费吗？ {#free}

免费。本程序免费且开源，采用 MIT 许可。

## VRChat 会因此封禁我吗？ {#ban}

本程序完全不碰 VRChat。它不需要 Mod，也从不打开 VRChat 的进程。它只接收 VRChat 自己的相机通过 Spout 发出的画面，而 Spout 是 VRChat 为串流提供的功能。

## 我的图片会被上传吗？ {#upload}

不会。一切都在你自己的显卡上运行，你的图片和视频都留在你的电脑上。

本程序会在检查更新时联网；Radeon 版还会在你要求时联网获取 DLSS-NR-on-AMD。[AI 问答](ai-qa.html)在你打开它之后也会联网：它会把你的提问发送给 Mintlify，但绝不会发送你的文件。[隐私与许可](privacy.html)

## 没有 VRChat 也能用吗？ {#without-vrchat}

能。处理磁盘上的图片和视频不需要 VRChat，只有实时相机需要。

## 哪些显卡可以用？ {#cards}

GeForce 版支持 NVIDIA GeForce RTX 20、30、40 和 50。[Radeon 版](radeon.html)支持 AMD Radeon RX 7000 和 9000。在其他显卡上，本程序可以作为查看器和录制工具运行，但没有 DLSS 5。

## 需要自己下载 DLSS 5 运行库吗？ {#runtime}

不需要。GeForce 版已经附带，分为 RTX 50 一份和 RTX 40、30、20 一份。程序会自动选择合适的那一份。

## 为什么启动时 Windows 会发出警告？ {#windows-warning}

本程序没有使用付费证书签名。点击**更多信息**，再点击**仍要运行**。源代码是公开的，每个版本都是在 GitHub 上根据源代码构建的。

## 为什么 DLSS 5 会改变脸和皮肤？ {#faces}

DLSS 5 会重新渲染光影和材质，而皮肤也是一种材质。调低**强度**；或者打开**高级**，调低**皮肤结构强度**和**局部结构强度**。把喜欢的效果保存为预设，下次用起来更方便。[预设](presets.html)

## 保存的图片在哪里？ {#where}

在 `图片\VRChat DLSS5 Cam` 中，除非你在**拍照** → **保存文件夹**中选了别的文件夹。状态栏会显示最近保存的文件，旁边的按钮可以在资源管理器中显示它。[保存位置](saving.html)

## 为什么处理单张图片比处理一帧视频慢？ {#still-slow}

静态图片没有上一帧，所以程序会让它反复经过 DLSS 5，直到结果稳定下来。[工作原理](how-it-works.html#still)

## 笔记本电脑能用吗？ {#laptop}

能，只要它有 GeForce RTX 显卡。请插上电源：DLSS 5 对显卡来说是很重的工作。

## 能在 Linux 或 Mac 上用吗？ {#linux-mac}

装有 NVIDIA 显卡的 Linux 可以通过 Proton 处理图片和视频。[Linux](linux.html) 没有 Mac 版。

## 怎样让实时画面更清晰？ {#sharper}

在 VRChat 的相机设置中调高串流尺寸，最高可以到 2160p。[来自 VRChat 的实时画面](live.html#resolution)

## 能在程序里提问吗？ {#ask}

能。顶栏中的**问 AI** 会在画面旁边打开 AI 问答。它根据本文档回答，并附上它参考的页面链接。它需要联网，回答也可能有误。[AI 问答](ai-qa.html)

## 在哪里反馈问题或提出功能建议？ {#report}

在 GitHub 上：**关于** → **反馈问题**会打开表单。完成的作品，欢迎带上 **#VRCDLSS5CAM** 标签发布到 X。

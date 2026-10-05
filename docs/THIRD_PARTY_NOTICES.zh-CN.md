# 第三方声明

> 本文为译文，以英文版 `THIRD_PARTY_NOTICES.md` 为准；许可证文本保留英文原文。

VRChat DLSS5 Cam 本身以 MIT 许可证发布（见 `LICENSE`）。
本程序与 VRChat Inc. 及 NVIDIA Corporation 没有关联，也未获得二者的认可或支持。

## NVIDIA DLSS SDK (NGX)

构建会在配置阶段从 <https://github.com/NVIDIA/DLSS> 下载 NVIDIA DLSS SDK（`nvsdk_ngx*.h`、`nvsdk_ngx_s.lib`、`nvngx_dlss.dll`）。
它依据 NVIDIA RTX SDKs License Agreement 使用；协议全文会下载到 `build/ngx_sdk/NVIDIA_LICENSE.txt`，并以 `NVIDIA_LICENSE.txt` 的名称放在可执行文件旁一同发布。

**This software contains source code provided by NVIDIA Corporation.**
（本软件包含由 NVIDIA Corporation 提供的源代码。）

## NVIDIA DLSS 5 神经渲染运行库（`nvngx_dlssnr.dll`）

发布压缩包附带 310.8.0.0 版 DLSS 5 神经渲染运行库，共两份：`runtimes\blackwell\nvngx_dlssnr.dll` 是随游戏发布的原版（GeForce RTX 50），`runtimes\universal\nvngx_dlssnr.dll` 是社区为 GeForce RTX 40 / 30 / 20 适配的同一运行库。
两者都是 NVIDIA 的软件，按 NVIDIA 的条款分发，不在本项目 MIT 许可证的范围内；本项目对它们不主张任何权利。
构建工作流从本仓库的 `runtime-310.8` 发布中获取这两个文件，并在打包前校验它们的 SHA-256 校验和；源码树本身不含任何运行库文件。

## AMD FidelityFX SDK (FidelityFX API, `amd_fidelityfx_dx12.dll`)

FSR 宿主方式通过 FidelityFX API，以原生尺寸运行一个 FSR 3.1 上采样上下文。
FidelityFX API 的头文件（`ffx_api.h`、`ffx_api_types.h`、`ffx_api_loader.h`、`ffx_upscale.h`、`dx12/ffx_api_dx12.h`）和预编译、已签名的 `amd_fidelityfx_dx12.dll` 在配置阶段从 FidelityFX SDK v1.1.4 获取（`cmake/FetchFidelityFX.cmake`，以 SHA-256 锁定），该 DLL 放在可执行文件旁一同发布：Radeon 版（`VRChatDLSS5Cam-win64-amd.zip`）带有它，GeForce 版不带。

- 源码：<https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK>（标签 v1.1.4）
- 许可证：MIT，Copyright (C) 2024 Advanced Micro Devices, Inc. ——以 `licenses/FidelityFX-SDK-LICENSE.txt` 随附

## DLSS-NR-on-AMD（未包含）

DLSS-NR-on-AMD（<https://github.com/danielblnc/DLSS-NR-on-AMD>）通过挂接到使用 FSR 的进程，在 Radeon 显卡上运行 DLSS 5 神经渲染；本程序的 FSR 宿主方式正是为了供它挂接而存在。
它是一个独立程序，遵循其自身条款：本程序既没有捆绑或链接它的任何部分，也没有任何内容源自它。
应用户的请求（Radeon 版的“安装 DLSS-NR-on-AMD…”按钮），本程序会从该项目自己的 GitHub 发布页把它的安装器下载到程序目录并启动；安装器会把可执行文件旁的 `nvngx_dlssnr.dll`（Radeon 版正是为此在那里附带了 RTX 50 版的这个文件）转换为它自己的权重文件，并安装它的 DLL。

## Spout2 (SpoutDX)

`third_party/spout` 是由 Lynn Jarvis 与 Leading Edge 制作的 Spout2（<https://github.com/leadedge/Spout2>）的一部分，采用 BSD-2-Clause 许可证。见 `third_party/spout/LICENSE`。

## Dear ImGui

`third_party/imgui` 是由 Omar Cornut 及贡献者制作的 Dear ImGui（<https://github.com/ocornut/imgui>），采用 MIT 许可证。见 `third_party/imgui/LICENSE.txt`。

## NVIDIA Optical Flow SDK（接口头文件）

`third_party/nvof/nvOpticalFlowCommon.h` 和 `third_party/nvof/nvOpticalFlowD3D11.h` 是 NVIDIA Optical Flow SDK 5.0 的接口头文件，
Copyright (c) 2018-2024 NVIDIA CORPORATION & AFFILIATES，MIT 许可证（许可证文本位于每个文件的开头）。
本项目只包含这些头文件；其运行库 `nvofapi64.dll` 属于 NVIDIA 驱动，不随本程序分发。

## dlss5-bridge

`src/gfx/NvOpticalFlow.cpp` 所依据的 D3D11/D3D12 互操作经验（哪些资源格式可以在两种 API 之间共享、能朝哪个方向共享，以及光流接口需要原生 D3D11 设备）学自 dlss5-bridge（<https://github.com/jpneagle/dlss5-webcam-demo> 一脉，MIT 许可证）。
没有复制任何代码。

## ONNX Runtime

ONNX Runtime（DirectML 版本），Copyright (c) Microsoft Corporation，MIT 许可证。
<https://github.com/microsoft/onnxruntime>
在配置阶段从 NuGet（`Microsoft.ML.OnnxRuntime.DirectML`）下载，以 `onnxruntime.dll` 和 `onnxruntime_providers_shared.dll` 再分发。
许可证全文与第三方声明随附于 `licenses/ONNXRuntime-LICENSE.txt` 和 `licenses/ONNXRuntime-ThirdPartyNotices.txt`。

## DirectML

Microsoft DirectML 可再分发组件（`DirectML.dll`），Copyright (c) Microsoft Corporation。
<https://www.nuget.org/packages/Microsoft.AI.DirectML>
在配置阶段从 NuGet（`Microsoft.AI.DirectML`）下载，并按其许可证的条款再分发；该许可证允许将 DirectML 二进制文件随应用程序一同再分发。
许可证与第三方声明随附于 `licenses/DirectML-LICENSE.txt` 和 `licenses/DirectML-ThirdPartyNotices.txt`。

## Direct3D 着色器编译器（`d3dcompiler_47.dll`，仅限 Linux 软件包）

Microsoft Direct3D HLSL 编译器，Copyright (c) Microsoft Corporation，是 Windows SDK 的可再分发组件（`Redist\D3D\x64\d3dcompiler_47.dll`），依据 Microsoft Software License Terms for the Windows SDK 随 Linux 软件包发布，该条款允许将它随应用程序分发。
程序启动时用这个 DLL 编译自己的计算着色器；Windows 已内置该 DLL，而 Proton 自带的版本无法正确编译这些着色器。

## libwebp

libwebp（WebP 编解码库），Copyright (c) 2010, Google Inc. All rights reserved.
BSD 3-Clause 许可证。<https://chromium.googlesource.com/webm/libwebp>
在配置阶段从该项目的 GitHub 镜像下载（标签 v1.6.0，经 SHA-256 校验），并静态链接进程序：它负责解码和写入 WebP 动图，并在没有 Windows WebP 编解码器的系统上解码 WebP 静态图片。
许可证文本随附于 `licenses/libwebp-COPYING.txt`。

## Microsoft Edge WebView2 SDK

Microsoft Edge WebView2 SDK，Copyright (C) Microsoft Corporation. All rights reserved.
BSD 3-Clause 许可证。<https://www.nuget.org/packages/Microsoft.Web.WebView2>
在配置阶段从 NuGet 下载（版本 1.0.4258.31，经 SHA-256 校验）。它的头文件和静态加载器库（`WebView2LoaderStatic.lib`）编译进程序，程序用它们在“问 AI”面板中显示文档的 AI 问答。
WebView2 Runtime 本身是 Windows 的一部分，不随程序发布。许可证文本随附于 `licenses/WebView2-LICENSE.txt`。

该面板中的 AI 问答是 Mintlify 的 AI 问答组件。本程序不包含它：面板只在第一次打开时，才从 Mintlify 的服务器（`widget.mintlify.com`）加载它。

## Depth Anything V2 Small

Depth Anything V2 Small，Copyright (c) the Depth Anything V2 authors，Apache License 2.0。
<https://github.com/DepthAnything/Depth-Anything-V2>
ONNX FP16 导出版本由 onnx-community 组织发布在 Hugging Face 上：
<https://huggingface.co/onnx-community/depth-anything-v2-small>
在配置阶段下载，以 `models/depth_anything_v2_small_fp16.onnx` 再分发。
许可证文本随附于 `licenses/DepthAnythingV2-LICENSE-Apache-2.0.txt`。

## Lucide 图标

Lucide（<https://lucide.dev>），ISC 许可证；其中源自 Feather 的图标同时适用 MIT 许可证。
界面的线条图标取自 Lucide 图标字体的一个子集，该子集编译进程序（`src/ui/IconFont.inc`，由 `tools/make_icon_font.py` 生成）。许可证如下：

```
ISC License

Copyright (c) 2026 Lucide Icons and Contributors

Permission to use, copy, modify, and/or distribute this software for any
purpose with or without fee is hereby granted, provided that the above
copyright notice and this permission notice appear in all copies.

THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.

---

The following Lucide icons are derived from the Feather project:

airplay, alert-circle, alert-octagon, alert-triangle, aperture, arrow-down-circle, arrow-down-left, arrow-down-right, arrow-down, arrow-left-circle, arrow-left, arrow-right-circle, arrow-right, arrow-up-circle, arrow-up-left, arrow-up-right, arrow-up, at-sign, calendar, cast, check, chevron-down, chevron-left, chevron-right, chevron-up, chevrons-down, chevrons-left, chevrons-right, chevrons-up, circle, clipboard, clock, code, columns, command, compass, corner-down-left, corner-down-right, corner-left-down, corner-left-up, corner-right-down, corner-right-up, corner-up-left, corner-up-right, crosshair, database, divide-circle, divide-square, dollar-sign, download, external-link, feather, frown, hash, headphones, help-circle, info, italic, key, layout, life-buoy, link-2, link, loader, lock, log-in, log-out, maximize, meh, minimize, minimize-2, minus-circle, minus-square, minus, monitor, moon, more-horizontal, more-vertical, move, music, navigation-2, navigation, octagon, pause-circle, percent, plus-circle, plus-square, plus, power, radio, rss, search, server, share, shopping-bag, sidebar, smartphone, smile, square, table-2, tablet, target, terminal, trash-2, trash, triangle, tv, type, upload, x-circle, x-octagon, x-square, x, zoom-in, zoom-out

The MIT License (MIT) (for the icons listed above)

Copyright (c) 2013-present Cole Bemis

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

## 字体

程序使用用户的 Windows 系统中已安装的字体显示文字（Segoe UI、Microsoft YaHei、Yu Gothic、Malgun Gothic、Consolas、Segoe UI Symbol）。
除上述 Lucide 图标子集外，本程序不分发任何字体文件。

# Third-party notices

VRChat DLSS5 Cam itself is released under the MIT License (see `LICENSE`).
It is not affiliated with, endorsed by or supported by VRChat Inc. or NVIDIA Corporation.

## NVIDIA DLSS SDK (NGX)

The build fetches the NVIDIA DLSS SDK (`nvsdk_ngx*.h`, `nvsdk_ngx_s.lib`, `nvngx_dlss.dll`)
from https://github.com/NVIDIA/DLSS at configure time. It is used under the
NVIDIA RTX SDKs License Agreement; the full text is downloaded to
`build/ngx_sdk/NVIDIA_LICENSE.txt` and shipped as `NVIDIA_LICENSE.txt` next to the executable.

**This software contains source code provided by NVIDIA Corporation.**

## NVIDIA DLSS 5 neural rendering runtime (`nvngx_dlssnr.dll`)

The release archives carry the DLSS 5 neural rendering runtime, version 310.8.0.0, in two builds:
`runtimes\blackwell\nvngx_dlssnr.dll`, the build as shipped with games (GeForce RTX 50), and
`runtimes\universal\nvngx_dlssnr.dll`, a community build of the same runtime adapted for
GeForce RTX 40 / 30 / 20. Both are NVIDIA's software, distributed under NVIDIA's terms, and are
not covered by this project's MIT License; this project claims no rights in them. The build
workflow fetches them from the `runtime-310.8` release of this repository and verifies their
SHA-256 checksums before packaging; the source tree itself contains no runtime file.

## AMD FidelityFX SDK (FidelityFX API, `amd_fidelityfx_dx12.dll`)

The FSR host route runs an FSR 3.1 upscaling context at native size through the FidelityFX API. Its headers
(`ffx_api.h`, `ffx_api_types.h`, `ffx_api_loader.h`, `ffx_upscale.h`, `dx12/ffx_api_dx12.h`) and the prebuilt,
signed `amd_fidelityfx_dx12.dll` are fetched at configure time from the FidelityFX SDK v1.1.4 (`cmake/FetchFidelityFX.cmake`,
SHA-256 pinned) and the DLL is shipped next to the executable; the Radeon edition (`VRChatDLSS5Cam-win64-amd.zip`)
carries it, the GeForce edition does not.

- Source: https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK (tag v1.1.4)
- License: MIT, Copyright (C) 2024 Advanced Micro Devices, Inc. — shipped as `licenses/FidelityFX-SDK-LICENSE.txt`

## DLSS-NR-on-AMD (not included)

DLSS-NR-on-AMD (https://github.com/danielblnc/DLSS-NR-on-AMD) runs the DLSS 5 neural rendering pass on Radeon cards
by attaching to a process that uses FSR; the FSR host route of this application exists for it to attach to. It is a
separate program under its own terms: nothing of it is bundled, linked or derived from here. On the user's request
(the "Install DLSS-NR-on-AMD…" button of the Radeon edition) the application downloads that project's installer from
its own GitHub release page into the program folder and starts it; the installer converts the `nvngx_dlssnr.dll`
next to the executable (the Radeon edition carries the RTX 50 build there for this purpose) into its weights file and
installs its DLL.

## Spout2 (SpoutDX)

`third_party/spout` is a subset of Spout2 by Lynn Jarvis and Leading Edge
(https://github.com/leadedge/Spout2), BSD-2-Clause. See `third_party/spout/LICENSE`.

## Dear ImGui

`third_party/imgui` is Dear ImGui by Omar Cornut and contributors
(https://github.com/ocornut/imgui), MIT License. See `third_party/imgui/LICENSE.txt`.

## NVIDIA Optical Flow SDK (interface headers)

`third_party/nvof/nvOpticalFlowCommon.h` and `third_party/nvof/nvOpticalFlowD3D11.h`
are the NVIDIA Optical Flow SDK 5.0 interface headers,
Copyright (c) 2018-2024 NVIDIA CORPORATION & AFFILIATES, MIT License
(the license text is at the top of each file). Only the headers are included;
the runtime `nvofapi64.dll` is part of the NVIDIA driver and is not redistributed.

## dlss5-bridge

The D3D11/D3D12 interop findings behind `src/gfx/NvOpticalFlow.cpp` (which resource
formats can be shared between the two APIs and in which direction, and that the
optical flow interface needs a native D3D11 device) were learned from
dlss5-bridge (https://github.com/jpneagle/dlss5-webcam-demo lineage), MIT License.
No code is copied.

## ONNX Runtime

ONNX Runtime (DirectML build), Copyright (c) Microsoft Corporation, MIT License.
https://github.com/microsoft/onnxruntime
Downloaded at configure time from NuGet (`Microsoft.ML.OnnxRuntime.DirectML`) and
redistributed as `onnxruntime.dll` and `onnxruntime_providers_shared.dll`. The full
license text and third-party notices are shipped in `licenses/ONNXRuntime-LICENSE.txt`
and `licenses/ONNXRuntime-ThirdPartyNotices.txt`.

## DirectML

Microsoft DirectML redistributable (`DirectML.dll`), Copyright (c) Microsoft Corporation.
https://www.nuget.org/packages/Microsoft.AI.DirectML
Downloaded at configure time from NuGet (`Microsoft.AI.DirectML`) and redistributed under
the terms of its license, which permits redistribution of the DirectML binaries with
applications. The license and third-party notices are shipped in
`licenses/DirectML-LICENSE.txt` and `licenses/DirectML-ThirdPartyNotices.txt`.

## Direct3D shader compiler (`d3dcompiler_47.dll`, Linux package only)

Microsoft Direct3D HLSL compiler, Copyright (c) Microsoft Corporation, a redistributable
component of the Windows SDK (`Redist\D3D\x64\d3dcompiler_47.dll`), shipped in the Linux package
under the Microsoft Software License Terms for the Windows SDK, which permit its distribution with
applications. The program compiles its compute shaders at start with this DLL; Windows has it
built in, and Proton's own version compiles them incorrectly.

## libwebp

libwebp (the WebP codec library), Copyright (c) 2010, Google Inc. All rights reserved.
BSD 3-Clause License. https://chromium.googlesource.com/webm/libwebp
Downloaded at configure time from the project's GitHub mirror (tag v1.6.0, verified by its
SHA-256) and linked statically into the application: it decodes and writes animated WebP
files, and decodes still WebP pictures on systems without the Windows WebP codec. The
license text is shipped in `licenses/libwebp-COPYING.txt`.

## Depth Anything V2 Small

Depth Anything V2 Small, Copyright (c) the Depth Anything V2 authors, Apache License 2.0.
https://github.com/DepthAnything/Depth-Anything-V2
ONNX FP16 export published by the onnx-community organization on Hugging Face:
https://huggingface.co/onnx-community/depth-anything-v2-small
Downloaded at configure time and redistributed as `models/depth_anything_v2_small_fp16.onnx`.
The license text is shipped in `licenses/DepthAnythingV2-LICENSE-Apache-2.0.txt`.

## Lucide icons

Lucide (https://lucide.dev), ISC License; the icons derived from Feather are also under the
MIT License. The interface draws its line icons from a subset of the Lucide icon font, compiled
into the application (`src/ui/IconFont.inc`, made by `tools/make_icon_font.py`). The license:

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

## Fonts

The application renders text with fonts already installed on the user's Windows
system (Segoe UI, Microsoft YaHei, Yu Gothic, Malgun Gothic, Consolas, Segoe UI Symbol).
Apart from the Lucide icon subset above, no font files are redistributed.

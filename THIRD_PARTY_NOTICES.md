# Third-party notices

VRChat DLSS5 Cam itself is released under the MIT License (see `LICENSE`).
It is not affiliated with, endorsed by or supported by VRChat Inc. or NVIDIA Corporation.

## NVIDIA DLSS SDK (NGX)

The build fetches the NVIDIA DLSS SDK (`nvsdk_ngx*.h`, `nvsdk_ngx_s.lib`, `nvngx_dlss.dll`)
from https://github.com/NVIDIA/DLSS at configure time. It is used under the
NVIDIA RTX SDKs License Agreement; the full text is downloaded to
`build/ngx_sdk/NVIDIA_LICENSE.txt` and shipped as `NVIDIA_LICENSE.txt` next to the executable.

**This software contains source code provided by NVIDIA Corporation.**

The DLSS 5 neural rendering runtime (`nvngx_dlssnr.dll`) is **not** part of this project,
is never downloaded by the build and is never redistributed here. Users must supply their
own copy; the application only loads the file the user points it to.

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

## Depth Anything V2 Small

Depth Anything V2 Small, Copyright (c) the Depth Anything V2 authors, Apache License 2.0.
https://github.com/DepthAnything/Depth-Anything-V2
ONNX FP16 export published by the onnx-community organization on Hugging Face:
https://huggingface.co/onnx-community/depth-anything-v2-small
Downloaded at configure time and redistributed as `models/depth_anything_v2_small_fp16.onnx`.
The license text is shipped in `licenses/DepthAnythingV2-LICENSE-Apache-2.0.txt`.

## Fonts

The application renders text with fonts already installed on the user's Windows
system (Segoe UI, Microsoft YaHei, Yu Gothic, Malgun Gothic, Consolas, Segoe UI Symbol).
No font files are redistributed.

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

## dlss5-bridge

The raw NVIDIA Optical Flow SDK ABI declarations in `src/gfx/NvOpticalFlow.cpp`
(function-table layout and parameter structures) are adapted from
dlss5-bridge (https://github.com/jpneagle/dlss5-webcam-demo lineage), MIT License.
No NVIDIA Optical Flow SDK headers are included; the runtime `nvofapi64.dll`
is part of the NVIDIA driver.

## Fonts

The application renders text with fonts already installed on the user's Windows
system (Segoe UI, Microsoft YaHei, Yu Gothic, Malgun Gothic, Consolas, Segoe UI Symbol).
No font files are redistributed.

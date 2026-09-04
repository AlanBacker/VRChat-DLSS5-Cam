<p align="center">
  <img src="resources/app-256.png" width="96" alt="VRChat DLSS5 Cam icon">
</p>

# VRChat DLSS5 Cam

**English** · [简体中文](docs/README.zh-CN.md) · [日本語](docs/README.ja.md) · [한국어](docs/README.ko.md)

VRChat DLSS5 Cam grabs the picture of VRChat's built-in camera (Stream Camera with *Spout Stream* enabled),
runs it through **DLSS 5 Neural Rendering (DLSSNR)** in real time, shows the result live and saves
lossless PNG photos. It is a stand-alone Windows application: nothing is injected into VRChat and no mod is required.

[![Build](https://github.com/AlanBacker/VRChat-DLSS5-Cam/actions/workflows/build.yml/badge.svg)](https://github.com/AlanBacker/VRChat-DLSS5-Cam/actions/workflows/build.yml)

> **Status: early preview.** The binaries are produced by GitHub Actions and the pipeline has not yet been
> exercised on a wide range of GPUs and DLSS 5 runtime builds. Please open an issue with `log.txt` if something
> does not work for you.

## Features

- **Live preview** of the VRChat camera after DLSS 5 processing, with side-by-side wipe, original, motion-vector and depth views.
- **DLSS 5 Neural Rendering** hosted directly from `nvngx_dlssnr.dll`, with the same parameters exposed by RenoDX's
  DLSS 5 ReShade add-on: preset, style, intensity, global tone, local tone, local structure, skin structure,
  auto mask and UI correction. Parameters are applied instantly and can be tuned on a frozen frame.
- **Frame guidance.** DLSSNR is a temporal model that expects motion vectors and depth.
  The app feeds it **NVIDIA Optical Flow** motion vectors with a forward/backward consistency check and a depth map
  estimated on the GPU by **Depth Anything V2** (ONNX Runtime + DirectML). GPU block matching and placeholder depth
  remain available as fallbacks, and a **DLAA** pass (DLSS super resolution at native size) can be added before
  neural rendering.
- **Adaptive resolution.** The sender resolution is detected automatically; a custom output resolution can be set,
  optionally letting DLSS 5 upscale to it. Optional scene-cut detection resets the temporal history.
- **Lossless capture.** PNG photos of the processed frame (and optionally of the original), with a global hotkey
  (`Ctrl+Alt+P` by default) that works while VRChat is in the foreground, and an optional time-lapse mode.
- **Four languages** (English, 简体中文, 日本語, 한국어), automatic selection from the Windows UI language.
- Per-monitor DPI aware, dark themed UI, GPU timers and a built-in log.

## Requirements

| | |
|---|---|
| OS | Windows 10 21H2 / Windows 11, 64-bit |
| GPU | NVIDIA GeForce RTX (DLSS 5 neural rendering runs on RTX hardware only), recent Game Ready driver |
| VRChat | Any build with the Stream Camera *Spout Stream* option (desktop or VR) |
| DLSS 5 runtime | Your own copy of `nvngx_dlssnr.dll`. **It is not included and never downloaded by this project.** |

## Setup

1. Download `VRChatDLSS5Cam-win64.zip` from the [Releases](https://github.com/AlanBacker/VRChat-DLSS5-Cam/releases) page and extract it anywhere.
2. Copy your `nvngx_dlssnr.dll` into the extracted folder (next to `VRChatDLSS5Cam.exe`). You can also point the app to the file from *DLSS 5 Neural Rendering → Runtime path*.
3. Start VRChat, open the **Camera**, switch the camera mode to **Stream**, and enable **Spout Stream** in the stream camera settings. VRChat then publishes the camera picture as a Spout sender (`VRCSender1`).
4. Start `VRChatDLSS5Cam.exe`. The sender is picked up automatically and the processed picture appears in the preview.
5. Frame your shot in VRChat and press **Ctrl+Alt+P** (or the *Capture* button). PNG files are written to `Pictures\VRChat DLSS5 Cam` by default.

Tips
- The Spout stream resolution is controlled by VRChat. Raise `camera_spout_res_width` / `camera_spout_res_height` in VRChat's `config.json` for higher-resolution input; the app adapts automatically.
- If you prefer a different output size, enable *Custom resolution* in the *Source* section. With *DLSS 5 upscale* on, DLSSNR renders the larger image itself.
- Use the *Wipe* compare mode and drag the handle in the preview to judge the effect of each parameter.

## Parameters

| Section | Setting | Meaning |
|---|---|---|
| Source | Paper white / Highlight compression | Shown only for floating-point (linear HDR) Spout textures: exposure reference and soft highlight roll-off applied before the SDR neural pass. |
| DLSS 5 | Preset | Hint render preset passed to DLSSNR (0–3). |
| DLSS 5 | Style | `DLSSNR.Style`: default / natural / cinematic. |
| DLSS 5 | Intensity | Overall strength of the neural pass. |
| DLSS 5 | Global tone / Local tone | Global and local tone mapping strength. |
| DLSS 5 | Local structure / Skin structure | Detail enhancement; skin structure may be left at the runtime default. |
| DLSS 5 | Auto mask / UI correction | Automatic subject mask, UI-safe processing. |
| DLSS 5 | Route | *Signed snippet*: host `nvngx_dlssnr.dll` directly. *NGX core*: create the feature through the NGX runtime. |
| Frame guidance | Motion vectors | NVIDIA Optical Flow (driver `nvofapi64.dll`, with a bidirectional consistency check), GPU block matching, or none (zero). |
| Frame guidance | Depth | AI estimated (Depth Anything V2 Small on DirectML; update interval and network resolution are adjustable), flat, gradient, or zero. |
| Frame guidance | Auto reset | Clears the temporal history on sharp matching-cost jumps (scene cuts). Off by default. |
| DLAA | Enable / Preset | Optional DLSS anti-aliasing pass at native resolution before neural rendering. |
| Capture | Keep alpha / Save original / Hotkey / Time-lapse | Capture options. |
| Display | Compare / Fit / VSync / Overlay | Preview options. |

Settings are stored in `%LOCALAPPDATA%\VRChatDLSS5Cam\settings.ini`; the log is `log.txt` in the same folder.

## Troubleshooting

- **"Waiting for Spout sender"** – enable *Spout Stream* on VRChat's Stream camera; the camera must be open. Other Spout senders are listed in the *Sender* combo.
- **"nvngx_dlssnr.dll not found"** – copy the runtime next to the executable or select its path.
- **NGX not initialized / DLAA unsupported** – the NGX runtime needs an NVIDIA GPU and a current driver. DLSSNR still works through the *Signed snippet* route.
- **The app does not start / closes immediately** – open `%LOCALAPPDATA%\VRChatDLSS5Cam\` and check `log.txt` (its last line is the step that failed) and `crash.txt` (written whenever the process crashes). Attach both files to an issue.
- **Neural rendering failed** – some runtime builds need a newer driver; check `log.txt` for the NGX result code. Try *Preset* 0 and the *NGX core* route.
- **Depth estimator unavailable** – `onnxruntime.dll`, `onnxruntime_providers_shared.dll`, `DirectML.dll` and `models\depth_anything_v2_small_fp16.onnx` must sit next to the executable (all are part of the release package). Until the estimator is ready the app falls back to zero depth; its state is shown under *Frame guidance*.
- **Optical flow unavailable** – NVIDIA Optical Flow runs on a private native D3D11 device (the driver rejects the D3D11On12 layer, which was the cause of the "UNSUPPORTED_DEVICE" error in 0.2.0). If `log.txt` says "NVOF unavailable, falling back to block matching", update the GeForce driver; block matching is used automatically until then. The status dot under *Frame guidance* shows which source is active.
- **Low frame rate** – disable DLAA, raise the depth update interval or lower the depth network resolution, lower the search radius, or choose NVIDIA Optical Flow for motion vectors.

## Building from source

Requirements: Visual Studio 2022 (MSVC v143, Windows 10 SDK) and CMake 3.21+.

```powershell
git clone https://github.com/AlanBacker/VRChat-DLSS5-Cam.git
cd VRChat-DLSS5-Cam
cmake -S . -B build -A x64
cmake --build build --config Release --parallel
```

The configure step downloads the NVIDIA DLSS SDK (headers, `nvsdk_ngx_s.lib`, `nvngx_dlss.dll`) from NVIDIA's public
GitHub repository, ONNX Runtime (DirectML build) and DirectML from NuGet, and the Depth Anything V2 Small FP16 model
from Hugging Face (`-DVDC_FETCH_DEPTH_MODEL=OFF` skips the model). All downloads are hash-checked. Shaders are compiled
at run time, so no shader toolchain is needed.

## How it works

```
VRChat Stream Camera ──Spout──▶ D3D11on12 receive ──▶ convert (sRGB / resize)
      ▶ NVIDIA Optical Flow (forward + backward) / block matching ──▶ motion vectors + confidence
      ▶ Depth Anything V2 (ONNX Runtime DirectML, every N frames) ──▶ normalized depth, reprojected in between
      ▶ [DLAA] ──▶ DLSSNR (nvngx_dlssnr.dll) ──▶ composite / compare ──▶ preview + PNG capture
```

The application hosts the DLSS 5 neural-rendering snippet outside the NGX runtime: the DLL is
loaded directly, its module-name check is satisfied, and the `DLSSNR.*` NGX parameter contract is used to create and
evaluate the feature on a D3D12 compute queue. See `src/ngx/DlssnrFeature.cpp`.

The guidance scheme is built for video input: same-resolution SDR input, hardware optical flow whose
confidence is lowered where forward and backward vectors disagree, monocular depth from Depth Anything V2 normalized
(2nd/98th percentile) to inverted relative depth and carried along the motion vectors between inferences, and no
per-frame history resets. Everything here is an independent MIT
implementation (`src/gfx/Pipeline.cpp`, `src/gfx/DepthEstimator.cpp`, `src/gfx/Shaders.cpp`). The optical flow engine
runs on a private native D3D11 device; frames and vectors cross to D3D12 through NT-handle shared textures ordered by a
shared fence (`src/gfx/NvOpticalFlow.cpp`).

## License

MIT (see `LICENSE`). Third-party components and the NVIDIA notice are listed in `THIRD_PARTY_NOTICES.md`.
This project is not affiliated with VRChat Inc. or NVIDIA Corporation. The DLSS 5 runtime is unreleased software;
use it at your own risk and never redistribute it with this application.

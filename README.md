<p align="center">
  <img src="resources/app-256.png" width="96" alt="VRChat DLSS5 Cam icon">
</p>

# VRChat DLSS5 Cam

**English** · [简体中文](docs/README.zh-CN.md) · [日本語](docs/README.ja.md) · [한국어](docs/README.ko.md)

VRChat DLSS5 Cam grabs the picture of VRChat's built-in camera (Stream Camera with *Spout Stream* enabled),
runs it through **DLSS 5 Neural Rendering (DLSSNR)** in real time, shows the result live and saves
lossless PNG photos. It can also open a picture from disk and run it through DLSS 5. It is a stand-alone Windows
application: nothing is injected into VRChat and no mod is required.

[![Build](https://github.com/AlanBacker/VRChat-DLSS5-Cam/actions/workflows/build.yml/badge.svg)](https://github.com/AlanBacker/VRChat-DLSS5-Cam/actions/workflows/build.yml)

> **Status: early preview.** The binaries are produced by GitHub Actions and the pipeline has not yet been
> exercised on a wide range of GPUs and DLSS 5 runtime builds. Please open an issue with `log.txt` if something
> does not work for you.

## Features

- **Live preview** of the VRChat camera after DLSS 5 processing, with side-by-side wipe, original, motion-vector and depth views.
- **DLSS 5 Neural Rendering** hosted directly from `nvngx_dlssnr.dll`, with the same parameters exposed by RenoDX's
  DLSS 5 ReShade add-on: preset, style, intensity, global tone, local tone, local structure, skin structure,
  auto mask and UI correction, plus an output blend (input exposure, tone transfer, colour strength, shadow and highlight strengths) and a reduced neural pass resolution for a lighter GPU load. Parameters are applied instantly and can be tuned on a frozen frame.
- **Still images.** Open a photo or screenshot (PNG, JPEG, BMP, TIFF, GIF, WebP, HEIC…) or drop it onto the window;
  DLSS 5 refines it over several passes and *Process & save PNG* writes the result next to your captures.
- **Frame guidance.** DLSSNR is a temporal model that expects motion vectors and depth. The app feeds it
  **NVIDIA Optical Flow** motion vectors with a forward/backward consistency check and a depth map estimated on the
  GPU by **Depth Anything V2** (ONNX Runtime + DirectML). GPU block matching and placeholder depth remain available
  as fallbacks, and a **DLAA** pass (DLSS super resolution at native size) can be added before neural rendering.
- **Adaptive resolution.** The sender resolution is detected automatically; a custom output resolution can be set,
  optionally letting DLSS 5 upscale to it. Optional scene-cut detection resets the temporal history.
- **Lossless capture.** PNG photos of the processed frame (and optionally of the original), with a global hotkey
  (`Ctrl+Alt+P` by default) that works while VRChat is in the foreground, and an optional time-lapse mode.
- **Four languages** (English, 简体中文, 日本語, 한국어), automatic selection from the Windows UI language.
- **Responsive interface.** The window runs on its own thread and GPU queue; the preview and controls stay smooth even
  when a 4K neural pass takes tens of milliseconds.
- Per-monitor DPI aware, dark themed UI, GPU timers and a built-in log.

## Requirements

| | |
|---|---|
| OS | Windows 10 21H2 / Windows 11, 64-bit |
| GPU | NVIDIA GeForce RTX. The DLSS 5 runtime build decides which generation can run the neural pass: the 310.8 build only contains code for RTX 50 (Blackwell); on RTX 40/30/20 the app reports the failure and keeps working with DLAA and the original picture. Nothing in the app itself is generation-specific. |
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
- To process a picture instead of the live camera, switch *Input* in the *Source* section to *Image file* and open it
  (or simply drop the file onto the window). Adjust the sliders, then press *Process & save PNG* (or the hotkey); the
  result is written as `<name>_DLSS5_<w>x<h>.png` into the capture folder. Pictures larger than 8192 px on the long side
  are processed at a reduced size.

## Parameters

| Section | Setting | Meaning |
|---|---|---|
| Source | Input | *VRChat camera (Spout)* or *Image file*. |
| Source | Paper white / Highlight compression | Shown only for floating-point (linear HDR) Spout textures: exposure reference and soft highlight roll-off applied before the SDR neural pass. |
| DLSS 5 | Preset | Hint render preset passed to DLSSNR (0–3). |
| DLSS 5 | Style | `DLSSNR.Style`: default / natural / cinematic. |
| DLSS 5 | Intensity | Overall strength of the neural pass, 0–2. Up to 1 it is the runtime's own strength; above 1 the app amplifies the difference between the neural result and the original picture (this can exaggerate artifacts too). |
| DLSS 5 | Global tone / Local tone | Global and local tone strength, 0–2. Up to 1 goes to the runtime; above 1 the app amplifies the difference between the neural result and the original, the same way as the intensity (the highest strength above 1 sets the gain). |
| DLSS 5 | Local structure / Skin structure | Detail enhancement, 0–2, with the same rule above 1. Skin structure may be left at the runtime default. |
| DLSS 5 | Neural pass only for captures | For GPUs too slow for live use: the preview bypasses the neural pass, and a capture (button, hotkey, timelapse) first runs it for 16 fresh frames, then saves. Still images are not affected. |
| DLSS 5 | Auto mask / UI correction | Automatic subject mask, UI-safe processing. |
| DLSS 5 | Input exposure / Tone transfer / Colour strength | Output blend. Input exposure (0.25–4×) scales the picture the network sees, like a paper-white scale, and is undone afterwards. Tone transfer and colour strength (0–2) set how much of the neural pass's brightness and colour changes reach the output, in linear light; 1 / 1 reproduces the neural result exactly, 0 keeps the original, above 1 exaggerates. |
| DLSS 5 | Shadow strength / Highlight & glow strength | Output blend, 0–2: how much of the neural pass's darkening (shadows, contour structure) and of its brightening (highlights, reflections, glow) reaches the output, applied before the tone transfer. 1 / 1 = as rendered. |
| DLSS 5 | Neural pass resolution | Runs the neural pass on a smaller picture (25–100 % of the input) and adds its change, upsampled with a bicubic filter, to the full-resolution picture. Lower values cut the GPU load at the cost of the pass's finest detail. Not used while neural upscaling is on. |
| DLSS 5 | Route | *Signed snippet*: host `nvngx_dlssnr.dll` directly. *NGX core*: create the feature through the NGX runtime. |
| Frame guidance | Motion vectors | NVIDIA Optical Flow (driver `nvofapi64.dll`, with a bidirectional consistency check), GPU block matching, or none (zero). |
| Frame guidance | Depth | AI estimated (Depth Anything V2 Small on DirectML; update interval and network resolution are adjustable), flat, gradient, or zero. |
| Frame guidance | Auto reset | Clears the temporal history on sharp matching-cost jumps (scene cuts). Off by default. |
| DLAA | Enable / Preset | Optional DLSS anti-aliasing pass at native resolution before neural rendering. |
| Capture | Keep alpha / Save original / Hotkey / Time-lapse | Capture options. |
| Display | Compare / Fit / Zoom / VSync / Overlay | Preview options. The mouse wheel over the preview zooms around the cursor, dragging pans, a double-click returns to the fitted view. |
| Display | Processing rate cap | Live source: processes at most this many source frames per second at a steady cadence and drops the frames in between, which caps the GPU load; the preview keeps the last processed frame meanwhile. 0 = every source frame. |

Settings are stored in `%LOCALAPPDATA%\VRChatDLSS5Cam\settings.ini`; the log is `log.txt` in the same folder.

## Troubleshooting

- **"Waiting for Spout sender"** – enable *Spout Stream* on VRChat's Stream camera; the camera must be open. Other Spout senders are listed in the *Sender* combo.
- **"nvngx_dlssnr.dll not found"** – copy the runtime next to the executable or select its path.
- **NGX not initialized / DLAA unsupported** – the NGX runtime needs an NVIDIA GPU and a current driver. DLSSNR still works through the *Signed snippet* route.
- **The app does not start / closes immediately** – open `%LOCALAPPDATA%\VRChatDLSS5Cam\` and check `log.txt` (its last line is the step that failed) and `crash.txt` (written whenever the process crashes). Attach both files to an issue.
- **Neural rendering failed** – on RTX 40/30/20 with the 310.8 runtime this is expected: that build only contains RTX 50 code (the app says so under the error). Otherwise some runtime builds need a newer driver; check `log.txt` for the NGX result code. Try *Preset* 0 and the *NGX core* route.
- **Neural pass active, but the picture is black or unchanged** – the app compares every neural frame with its input; the Neural section shows *Output change* and warns when the runtime reports success but delivers a black or unchanged picture (`log.txt`: "DLSSNR output check"). Switch DLSS 5 off and on: off releases the feature and unloads the runtime, on loads it from the file again, the same fresh start as launching the app. If it persists, that runtime build does not produce a picture on this GPU.
- **Depth estimator unavailable** – `onnxruntime.dll`, `onnxruntime_providers_shared.dll`, `DirectML.dll` and `models\depth_anything_v2_small_fp16.onnx` must sit next to the executable (all are part of the release package). Until the estimator is ready the app falls back to zero depth; its state is shown under *Frame guidance*.
- **Optical flow unavailable** – NVIDIA Optical Flow runs on a private native D3D11 device (the driver rejects the D3D11On12 layer, which was the cause of the "UNSUPPORTED_DEVICE" error in 0.2.0). If `log.txt` says "NVOF unavailable, falling back to block matching", update the GeForce driver; block matching is used automatically until then. The status dot under *Frame guidance* shows which source is active.
- **Low frame rate** – disable DLAA, raise the depth update interval or lower the depth network resolution, lower the search radius, or choose NVIDIA Optical Flow for motion vectors. With NVIDIA Optical Flow keep the flow grid at 4 px (the fastest setting; 2 px and 1 px cost far more at 4K). The log prints a `Perf:` line every 15 s with the processing rate, the CPU cost per frame (receive / wait / record / submit), the GPU time of each stage and the depth network cost; only frames that were actually processed count. The interface has its own thread, so a low processing rate no longer slows down the window.

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

The application hosts the DLSS 5 neural-rendering snippet outside the NGX runtime: the DLL is loaded directly, its
module-name check is satisfied, and the `DLSSNR.*` NGX parameter contract is used to create and evaluate the feature on
a D3D12 queue. See `src/ngx/DlssnrFeature.cpp`.

The guidance scheme is built for video input: same-resolution SDR input, hardware optical flow whose confidence is
lowered where forward and backward vectors disagree, monocular depth from Depth Anything V2 normalized (2nd/98th
percentile) to inverted relative depth and carried along the motion vectors between inferences, and no per-frame
history resets. Everything is an independent MIT implementation (`src/gfx/Pipeline.cpp`, `src/gfx/DepthEstimator.cpp`,
`src/gfx/Shaders.cpp`). The optical flow engine runs on a private native D3D11 device; frames and vectors cross to
D3D12 through NT-handle shared textures ordered by a shared fence (`src/gfx/NvOpticalFlow.cpp`).

Two threads share the GPU: the processing thread owns the Spout receiver (or the still image), the pipeline and a
D3D12 queue of its own; the interface thread owns the window, ImGui and a high-priority present queue. Finished
pictures are handed over through four display buffers with cross-queue fence waits, so the preview always shows the
newest completed frame and the window never waits for the neural pass (`src/core/App.cpp`, `src/gfx/Device.cpp`).
A still image is decoded with WIC (EXIF orientation applied), uploaded once and run through the same pipeline with zero
motion for a number of passes until the temporal network settles.

## License

MIT (see `LICENSE`). Third-party components and the NVIDIA notice are listed in `THIRD_PARTY_NOTICES.md`.
This project is not affiliated with VRChat Inc. or NVIDIA Corporation. The DLSS 5 runtime is unreleased software;
use it at your own risk and never redistribute it with this application.

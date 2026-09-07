<p align="center">
  <img src="resources/app-256.png" width="96" alt="VRChat DLSS5 Cam icon">
</p>

# VRChat DLSS5 Cam

**English** · [简体中文](docs/README.zh-CN.md) · [日本語](docs/README.ja.md) · [한국어](docs/README.ko.md)

VRChat DLSS5 Cam takes the picture of VRChat's camera, runs it through **DLSS 5 Neural Rendering** on your
GeForce RTX card and saves the result as a lossless PNG photo. It does the same for pictures and videos from your
disk, one at a time or as a batch. It is a normal Windows program: nothing is injected into VRChat, no mod is needed.

<p align="center">
  <img src="docs/images/main.png" width="900" alt="The main window: a VRChat photo with the wipe compare (original on the left, DLSS 5 on the right), the media library below and the settings on the right">
</p>

[![Build](https://github.com/AlanBacker/VRChat-DLSS5-Cam/actions/workflows/build.yml/badge.svg)](https://github.com/AlanBacker/VRChat-DLSS5-Cam/actions/workflows/build.yml)
[![Release](https://img.shields.io/github/v/release/AlanBacker/VRChat-DLSS5-Cam?label=download)](https://github.com/AlanBacker/VRChat-DLSS5-Cam/releases/latest)

## What it does

- **Live camera.** Shows VRChat's Stream Camera after DLSS 5 in real time and saves PNG photos with a hotkey
  (`Ctrl+Alt+P`) that also works while VRChat is in the foreground.
- **Pictures and videos from disk.** Drop a screenshot or a recording onto the window, tune the sliders on it and
  save the result: PNG for pictures, MP4 (H.264 / HEVC, audio kept) or a PNG sequence for videos.
- **Many files at once.** Every file you open lands in a library under the preview. Select some or all and process
  them in one go, each with the shared settings or with its own.
- **Real DLSS 5 guidance.** The neural network receives motion vectors from NVIDIA Optical Flow and a depth map
  from Depth Anything V2, so videos and the live camera are processed the way a game would be. An optional DLAA
  pass cleans up edges first.
- **Easy to compare.** Wipe, side by side or original view, zoom with the mouse wheel, drag to pan.
- **Made for everyday use.** Dark and light look (follows Windows by default), undo and redo for every setting,
  four languages (English, 简体中文, 日本語, 한국어), a smooth interface on its own thread, and a command line for
  scripted runs.

## What you need

| | |
|---|---|
| Windows | Windows 10 21H2 or Windows 11, 64-bit |
| Graphics card | NVIDIA GeForce RTX. The **RTX 50** series runs the neural pass with the regular runtime. **RTX 40 / 30 / 20** need the modified runtime build (see the next row); with the regular build the app reports the failure and keeps working without the neural pass. |
| DLSS 5 runtime | Your own copy of `nvngx_dlssnr.dll`. **It is not included and never downloaded by this project.** The file is shared on the [RenoDX Discord server](https://discord.com/invite/renodx), where a modified build for cards other than the RTX 50 series is available as well. That server belongs to the RenoDX project and is **not** this project's Discord; this project has no Discord server of its own. |
| VRChat | Any build with the Stream Camera *Spout Stream* option (desktop or VR). Only needed for the live camera. |
| Video files | Windows Media Foundation (part of Windows). The N / KN editions need the *Media Feature Pack*; HEVC files may need the *HEVC Video Extensions* from the Microsoft Store. |

## Quick start

1. Download `VRChatDLSS5Cam-win64.zip` from the [latest release](https://github.com/AlanBacker/VRChat-DLSS5-Cam/releases/latest) and extract it anywhere.
2. Copy your `nvngx_dlssnr.dll` into that folder, next to `VRChatDLSS5Cam.exe`. (You can also point the app to the file later under *DLSS 5 Neural Rendering → Runtime path*.)
3. In VRChat open the **Camera**, switch it to **Stream** mode and enable **Spout Stream** in its settings.
4. Start `VRChatDLSS5Cam.exe`. The camera picture appears in the preview with DLSS 5 applied; the badge next to the *Enable DLSS 5* switch says *Active*.
5. Frame your shot in VRChat and press **Ctrl+Alt+P** (or the *Capture photo* button). The PNG lands in `Pictures\VRChat DLSS5 Cam`.

Tips for the live camera

- VRChat decides the stream resolution. Raise `camera_spout_res_width` / `camera_spout_res_height` in VRChat's `config.json` for a sharper input; the app adapts by itself.
- If the neural pass is too slow for a live preview on your card, switch on *Neural pass only for captures* in the DLSS 5 section: the preview then shows the plain picture and each capture runs the neural pass just for the photo.
- *Processing rate cap* in the *Display* section limits how many camera frames per second are processed, which keeps the GPU free for VRChat.

## Pictures and videos from disk

Drop a picture or a video onto the window, or use *Open image…* / *Open video…* in the *Source* section. The
picture appears in the preview and every slider works on it right away. Press **Process & save PNG** (pictures) or
**Process & save video** (videos), or the hotkey, and the result is written next to your photos as
`<name>_DLSS5_<w>x<h>.png`, `.mp4` or a folder of PNG frames.

<p align="center">
  <img src="docs/images/video.png" width="900" alt="A video open in the preview with the wipe compare and the play, step and range controls under the picture">
</p>

For videos the controls under the preview play and pause the file through the whole pipeline, step one frame at a
time, and show a small picture of the frame under the cursor on the seek bar. *Start here* / *End here* limit the
processing (and the audio) to a range; *Whole video* clears it. By default the output matches the source: same
codec, frame rate and bitrate. Switch *Match the source* off to pick H.264, HEVC or a PNG sequence and a bitrate
yourself. The *Capture* section shows roughly how long the file will take with the current settings.

## Many files at once

Every file you open or drop lands in the **library** under the preview (*Add files…* and *Add folder…* add more).
Click a thumbnail to preview it and tune the sliders on it. Select files by dragging across the thumbnails, with the
box on each one, with Ctrl+click and Shift+click, or with **Ctrl+A** for all of them. *Process selected* or
*Process all* then runs the files one after another; a bar on each thumbnail shows the progress and the state stays
visible afterwards.

The right mouse button opens a menu on a thumbnail: show the file in Explorer, take it out of the library, or give it
**its own DLSS 5 parameters** in a separate window. With several files selected the same menu offers *Own parameters
for N files…*, which sets the values for all of them at once; each file keeps its own copy afterwards.

## Finding your way around

- **Three sections do the everyday work:** *Source* (what comes in), *DLSS 5 Neural Rendering* (how it looks) and
  *Capture* (where it goes). The *Advanced* switch in the DLSS 5 section shows or hides the tone and structure
  sliders, the output blend, frame guidance, DLAA and the timers.
- **Compare** with the wipe (drag the handle in the preview), side by side or the original. The mouse wheel over the
  preview zooms around the cursor, dragging pans, a double-click goes back to the fitted view.
- **Undo and redo** every change to the settings with **Ctrl+Z** / **Ctrl+Y** or the two arrows in the top bar.
- **Dark or light.** *Theme* in the *Display* section: *System* follows the Windows app colour setting, or pick
  *Dark* or *Light*.
- **The sidebar** slides away behind the handle at its edge. While a file is being processed it is locked and offers
  *Cancel*.

<p align="center">
  <img src="docs/images/light.png" width="900" alt="The same window in the light theme">
</p>

### Keyboard and mouse

| Keys | What happens |
|---|---|
| `Ctrl+Alt+P` | Capture a photo of the live camera, or process the open picture or video (global hotkey, changeable in the *Capture* section) |
| `Ctrl+Z` · `Ctrl+Y` / `Ctrl+Shift+Z` | Undo · redo a settings change |
| `Ctrl+A` | Select every file in the library (mouse over the library) |
| `Space` | Play / pause the open video |
| `←` `→` (`Shift`: 10 frames) · `Home` `End` | Step through the video · jump to the ends |
| `I` · `O` | Set the start · the end of the range to process |
| Mouse wheel over the preview · drag · double-click | Zoom · pan · back to the fitted view |
| Drag across thumbnails · `Ctrl`+click · `Shift`+click · right button | Select files · add one · extend · open the menu |

## Settings explained

<details>
<summary>All settings, section by section</summary>

| Section | Setting | Meaning |
|---|---|---|
| Source | Input | *VRChat camera (Spout)*, *Image file* or *Video file*. |
| Source | Sender | Which Spout sender to receive; VRChat's camera is `VRCSender1`. |
| Source | Custom processing resolution | Process at a size of your choice instead of the source size, optionally letting DLSS 5 upscale to it. |
| Source | Match the source | Videos only, on by default: the output uses the codec (H.264 or HEVC), the frame rate and the average bitrate of the source file. |
| Source | Save as / Bitrate / Keep audio | With *Match the source* off: *MP4 (H.264)*, *MP4 (HEVC)* or *PNG sequence*, the encoder bitrate (5–200 Mbit/s), and whether the audio track is copied. |
| Source | Hardware decoding | Decode the video on the GPU. Switch it off if a file shows wrong colours or fails to open. |
| Source | Paper white / Highlight compression | Shown only for floating-point (HDR) Spout textures: exposure reference and soft highlight roll-off before the neural pass. |
| DLSS 5 | Enable DLSS 5 (DLSSNR) | Switches the neural pass on or off. Off releases the runtime; on loads it again from the file. |
| DLSS 5 | Runtime path / Reload | Where `nvngx_dlssnr.dll` is. *Reload* loads the file again. |
| DLSS 5 | Host route | *Signed snippet*: host `nvngx_dlssnr.dll` directly. *NGX core*: create the feature through the NGX runtime. |
| DLSS 5 | Preset / Style | Render preset (0–3) and style (default / natural / cinematic) passed to the runtime. |
| DLSS 5 | Intensity | Overall strength of the neural pass, 0–2. Up to 1 it is the runtime's own strength; above 1 the app amplifies the difference between the neural result and the original (which can exaggerate artifacts). At 0 the picture is left untouched. |
| DLSS 5 | Global tone / Local tone | Global and local tone strength, 0–2, with the same rule above 1 (the highest strength above 1 sets the gain). |
| DLSS 5 | Local structure / Skin structure | Detail enhancement, 0–2, same rule above 1. Skin structure may be left at the runtime default. |
| DLSS 5 | Auto mask / UI correction | Automatic subject mask, UI-safe processing. |
| DLSS 5 | Neural pass only for captures | For cards too slow for live use: the preview bypasses the neural pass, and a capture first runs it for 16 fresh frames, then saves. Still images are not affected. |
| DLSS 5 | Input exposure / Tone transfer / Colour strength | Output blend. Input exposure (0.25–4×) scales the picture the network sees and is undone afterwards. Tone transfer and colour strength (0–2) set how much of the neural pass's brightness and colour changes reach the output; 1 / 1 reproduces the neural result exactly, 0 keeps the original. |
| DLSS 5 | Shadow strength / Highlight & glow strength | Output blend, 0–2: how much of the neural pass's darkening and of its brightening reaches the output. 1 / 1 = as rendered. |
| DLSS 5 | Neural pass resolution | Runs the neural pass on a smaller picture (25–100 % of the input) and adds its change, upsampled, to the full-resolution picture. Lower values cut the GPU load at the cost of the finest detail. Not used while neural upscaling is on. |
| Frame guidance | Motion vectors | NVIDIA Optical Flow (with a forward/backward consistency check), GPU block matching, or none. |
| Frame guidance | Depth | AI estimated (Depth Anything V2 Small on DirectML; update interval and network resolution adjustable), flat, gradient, or zero. |
| Frame guidance | Auto reset | Clears the temporal history on scene cuts. Off by default. |
| DLAA pre-pass | Enable / Preset | Optional DLSS anti-aliasing pass at native resolution before neural rendering. |
| Capture | Folder / Keep alpha / Also save the original / Hotkey / Time-lapse | Where and how photos and videos are saved. |
| Capture | Estimated time | Rough processing time of the open picture or video with the current settings, refined by every run. |
| Display | Theme | *System* (follows Windows), *Dark* or *Light*. |
| Display | Compare / Fit / Zoom / VSync / Overlay / Show the library | Preview options. |
| Display | Processing rate cap | Live camera: process at most this many frames per second and skip the rest. 0 = every frame. |
| About | Open log file / Open settings folder / Project page / Third-party notices / Reset all settings | Version, GPU and driver, and the maintenance buttons. |

Settings are stored in `%LOCALAPPDATA%\VRChatDLSS5Cam\settings.ini`; the log is `log.txt` in the same folder.

</details>

Command-line options (open files, process unattended, screenshots, headless runs) are listed in
[docs/COMMAND_LINE.md](docs/COMMAND_LINE.md).

## Troubleshooting

- **"Waiting for VRChat Spout stream…"** – enable *Spout Stream* on VRChat's Stream camera; the camera must be open. Other Spout senders are listed in the *Sender* box.
- **"nvngx_dlssnr.dll not found"** – copy the runtime next to `VRChatDLSS5Cam.exe` or select its path in the DLSS 5 section.
- **Neural rendering failed** – on RTX 40 / 30 / 20 with the regular runtime this is expected: that build only contains RTX 50 code, and the app says so under the error. Get the modified build from the RenoDX Discord server (see *What you need*; not affiliated with this project). Otherwise some runtime builds need a newer driver; check `log.txt` for the NGX result code, and try *Preset* 0 and the *NGX core* route.
- **Neural pass active, but the picture is black or unchanged** – the app compares every neural frame with its input and warns when the runtime reports success but delivers a black or unchanged picture (`log.txt`: "DLSSNR output check"). Switch DLSS 5 off and on again for a fresh start. If it persists, that runtime build does not produce a picture on this GPU. (With *Intensity* at 0 an unchanged picture is normal and no warning is shown.)
- **The app does not start / closes immediately** – open `%LOCALAPPDATA%\VRChatDLSS5Cam\` and check `log.txt` (its last line is the step that failed) and `crash.txt`. Attach both files to an issue.
- **NGX not initialized / DLAA unsupported** – the NGX runtime needs an NVIDIA GPU and a current driver. DLSS 5 still works through the *Signed snippet* route.
- **Depth estimator unavailable** – `onnxruntime.dll`, `onnxruntime_providers_shared.dll`, `DirectML.dll` and `models\depth_anything_v2_small_fp16.onnx` must sit next to the executable (all are in the release package). Until the estimator is ready the app uses zero depth; its state is shown under *Frame guidance*.
- **Optical flow unavailable** – if `log.txt` says "NVOF unavailable, falling back to block matching", update the GeForce driver; block matching is used until then. The status dot under *Frame guidance* shows which source is active.
- **The video preview is black** – many films start with a fade from black; the preview skips those frames and says so under the picture when the frame on show is still dark. Seek forward with the bar or the arrow keys.
- **Video file does not open / no encoder available** – the formats depend on the codecs installed in Windows. Install the *HEVC Video Extensions* (Microsoft Store) for HEVC files, or the *Media Feature Pack* on Windows N / KN. If the H.264 encoder is missing, choose *PNG sequence* as the output. Switching *Hardware decoding* off helps with files the GPU decoder rejects.
- **Low frame rate** – switch DLAA off, raise the depth update interval or lower the depth network resolution, lower the neural pass resolution, or set a processing rate cap. Keep the optical-flow grid at 4 px (2 px and 1 px cost far more at 4K). The log prints a `Perf:` line every 15 s with the cost of each stage.

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
at run time, so no shader toolchain is needed. The DLSS 5 runtime is never part of the build or the package.

## How it works

<details>
<summary>Pipeline and design notes</summary>

```
VRChat Stream Camera ──Spout──▶ D3D11on12 receive ──▶ convert (sRGB / resize)
      ▶ NVIDIA Optical Flow (forward + backward) / block matching ──▶ motion vectors + confidence
      ▶ Depth Anything V2 (ONNX Runtime DirectML, every N frames) ──▶ normalized depth, reprojected in between
      ▶ [DLAA] ──▶ DLSSNR (nvngx_dlssnr.dll) ──▶ composite / compare ──▶ preview + PNG capture
Video file ──Media Foundation──▶ decode (GPU) ──▶ same pipeline, one frame at a time ──▶ MP4 (H.264 / HEVC + AAC) or PNG sequence
```

The application hosts the DLSS 5 neural-rendering snippet outside the NGX runtime: the DLL is loaded directly, its
module-name check is satisfied, and the `DLSSNR.*` NGX parameter contract is used to create and evaluate the feature on
a D3D12 queue (`src/ngx/DlssnrFeature.cpp`). The parameters are the same set the RenoDX DLSS 5 add-on exposes.

The guidance scheme is built for video input: same-resolution SDR input, hardware optical flow whose confidence is
lowered where forward and backward vectors disagree, monocular depth from Depth Anything V2 normalized (2nd/98th
percentile) to inverted relative depth and carried along the motion vectors between inferences, and no per-frame
history resets. Everything is an independent MIT implementation (`src/gfx/Pipeline.cpp`, `src/gfx/DepthEstimator.cpp`,
`src/gfx/Shaders.cpp`). The optical flow engine runs on a private native D3D11 device; frames and vectors cross to
D3D12 through NT-handle shared textures ordered by a shared fence (`src/gfx/NvOpticalFlow.cpp`).

Two threads share the GPU: the processing thread owns the Spout receiver (or the file), the pipeline and a D3D12
queue of its own; the interface thread owns the window, ImGui and a high-priority present queue. Finished pictures
are handed over through display buffers with cross-queue fence waits, so the preview always shows the newest
completed frame and the window never waits for the neural pass (`src/core/App.cpp`, `src/gfx/Device.cpp`). A still
image is decoded with WIC (EXIF orientation applied), uploaded once and run through the same pipeline with zero
motion for a number of passes until the temporal network settles.

A video file is decoded by a Media Foundation source reader on its own thread (hardware decoder through a DXGI device
manager, software fallback) into a short frame queue. The processing thread hands the pipeline one frame at a time and
asks for a readback of that frame's result; readbacks are collected in frame order and passed to the writer, so no frame
is skipped or duplicated even when the neural pass takes longer than the frame interval. The writer converts each frame
to NV12 on a thread of its own and feeds a Media Foundation sink writer (hardware encoder where available) together with
the decoded audio samples (`src/core/VideoSource.cpp`, `src/core/VideoWriter.cpp`).

</details>

## License

MIT (see `LICENSE`). Third-party components and the NVIDIA notice are listed in `THIRD_PARTY_NOTICES.md`.
This project is not affiliated with VRChat Inc. or NVIDIA Corporation. The DLSS 5 runtime is unreleased software;
use it at your own risk and never redistribute it with this application.

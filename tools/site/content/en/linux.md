---
title: Linux
nav: Linux
description: Run the GeForce edition on Linux with an NVIDIA card, through Proton. Pictures and videos, no live camera.
---
The Linux package runs the Windows app under Proton, the compatibility layer known from Steam. A launcher sets everything up by itself. There is no native Linux build, because the DLSS 5 runtime exists only for Windows.

## What you need {#requirements}

- 64-bit Linux with a desktop session (X11 or Wayland).
- An NVIDIA GeForce RTX card (20, 30, 40 or 50 series) with the NVIDIA driver 5xx or newer. Radeon cards are not supported on Linux.
- `python3` (3.10 or newer) and `curl` or `wget`.
- About 3 GB of disk space for Proton on the first start. Steam is not needed.

## Install and start {#install}

1. Download `VRChatDLSS5Cam-linux-x86_64.tar.gz` from the [download section](index.html#download).
2. Unpack it and start the launcher:
   ```bash
   tar xzf VRChatDLSS5Cam-linux-x86_64.tar.gz
   cd VRChatDLSS5Cam
   ./vrchat-dlss5-cam
   ```
   => The first start downloads Proton, which takes a few minutes. Then the window opens.
3. Optional: `./install-linux.sh` adds the app to your application menu and a `vrchat-dlss5-cam` command. `./install-linux.sh --remove` takes both out again.

Settings, presets, the log and the library live in `~/.local/share/VRChatDLSS5Cam/data`. Updates work as on Windows.

## What works {#support}

| | Under Proton |
|---|---|
| Pictures: DLSS 5, depth, super resolution, presets, undo | Works. |
| Videos and animated images as input | Works. |
| Video output | WebP, GIF, APNG or PNG sequence. No MP4, and no sound: Proton has no H.264 or HEVC encoder. A video that would come back as MP4 is saved as WebP. |
| Live camera (Spout) | Not available. Spout exists only on Windows. |
| Motion vectors | The FSR optical flow; NVIDIA Optical Flow is not available under Proton. |
| MCP, updates, mirror sites, languages | Work. |
| AI Q&A | **Ask AI** opens it in your browser instead of a panel in the app. [AI Q&A](ai-qa.html#browser) |
| Radeon edition | Not supported. |

## Command line {#command-line}

The launcher takes the app's options and turns Linux paths into the form the app expects:

```bash
./vrchat-dlss5-cam --headless --add ~/Pictures/shot.png --process ~/Pictures/out
./vrchat-dlss5-cam --open ./clip.mp4 --lang zh
```

`--linux-info` prints the folders and versions in use, `--linux-reset` rebuilds the Proton setup (settings and library are kept). [All options](command-line.html)

## If something goes wrong {#troubleshooting}

- **The first start takes minutes.** That is the one-time download of Proton and its runtime, about 2.5 GB.
- **The window opens but does not react.** A locked screen or a screen locker holds the display. Unlock the session.
- **The picture area shows only a grey checkerboard.** The app runs with Proton's own shader compiler. Use the Linux package rather than the Windows zip; it brings the right one.
- **DLSS 5 stays Inactive.** The NVIDIA driver's bridge for Windows programs is missing. `./vrchat-dlss5-cam --linux-info` shows whether `nvngx.dll` was found. Install the NVIDIA driver 5xx or newer.

The full Linux guide is [LINUX.md](https://github.com/AlanBacker/VRChat-DLSS5-Cam/blob/main/docs/LINUX.md) on GitHub.

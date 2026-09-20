# VRChat DLSS5 Cam on Linux

The Linux package runs the Windows build of the program under [Proton](https://github.com/ValveSoftware/Proton) through
[umu-launcher](https://github.com/Open-Wine-Components/umu-launcher). There is no native Linux build: the DLSS 5 runtime
(`nvngx_dlssnr.dll`) exists only for Windows, and the NVIDIA Linux driver exposes it to Windows programs running under
Wine. The neural pass, DLSS super resolution, the depth estimate and the whole interface work this way on an NVIDIA card;
what does not is listed under [What works and what does not](#what-works-and-what-does-not).

## Requirements

| | |
|---|---|
| System | x86_64 Linux with a desktop session (X11 or Wayland). Tested on Ubuntu 26.04 with Cinnamon. |
| Graphics card | NVIDIA GeForce RTX 20 / 30 / 40 / 50 with the NVIDIA driver **5xx or newer** (the driver ships the Wine bridge `nvngx.dll`, which the neural pass needs). Radeon cards are not supported on Linux: the Radeon edition depends on a Windows installer. |
| Tools | `python3` (3.10 or newer) and `curl` or `wget`. Nothing else is installed on the system: umu-launcher, GE-Proton and the Steam Linux Runtime are downloaded into your home folder at the first start. |
| Disk | About 3 GB for Proton and the runtime on the first start, plus the program itself. |
| Steam | Not required. If Steam is installed, a GE-Proton already in `~/.local/share/Steam/compatibilitytools.d` is reused. |

The launcher runs the program with **GE-Proton** (umu-launcher fetches the latest, GE-Proton 11 at the time of writing);
that is the tested configuration. UMU-Proton 10 (umu's own default) ran the picture path in a short test as well:
`VDC_PROTONPATH=UMU-Proton ./vrchat-dlss5-cam` selects it.

## Install and start

```bash
tar xzf VRChatDLSS5Cam-linux-x86_64.tar.gz
cd VRChatDLSS5Cam
./vrchat-dlss5-cam            # first start: downloads Proton (a few minutes), then opens the window
./install-linux.sh            # optional: adds the program to the application menu
```

`install-linux.sh` writes a `.desktop` entry and puts a `vrchat-dlss5-cam` command in `~/.local/bin`; run it again after
moving the folder, `./install-linux.sh --remove` takes both out.

The program's own files (settings, presets, log, media library) live in `~/.local/share/VRChatDLSS5Cam/data`; the Proton
prefix next to it in `prefix`. Set `VDC_HOME` to keep them elsewhere.

Updates work as on Windows: the program downloads the new version and restarts. The launcher scripts and the shader
compiler (`d3dcompiler_47.dll`, see below) are not touched.

The package is the Windows program plus three things: the launcher script, `install-linux.sh` and Microsoft's Direct3D
shader compiler `d3dcompiler_47.dll`. The compiler is needed because Proton's own version compiles the program's shaders
incorrectly. Compiling takes a few seconds at the first start; the result is cached in the data folder (`data/shaders`)
and later starts skip it.

## Command line

Everything after the launcher's own options goes to the program; [COMMAND_LINE.md](COMMAND_LINE.md) lists the options.
Unix paths are converted to the `Z:\` form the program expects, so this works as written:

```bash
./vrchat-dlss5-cam --headless --add ~/Pictures/shot.png --process ~/Pictures/out
./vrchat-dlss5-cam --open ./clip.mp4 --lang zh
./vrchat-dlss5-cam --headless --mcp-port 51570                  # the MCP server without a window (see MCP.md)
```

Launcher options (they come first):

| Option | Effect |
|---|---|
| `--linux-info` | Prints the folders and versions in use. |
| `--linux-reset` | Removes the Proton prefix (settings and library are kept); the next start recreates it. |
| `--linux-help` | The launcher's help. |

Environment variables:

| Variable | Effect |
|---|---|
| `VDC_HOME` | Where umu-launcher, the prefix and the data live (default `~/.local/share/VRChatDLSS5Cam`). |
| `VDC_PROTONPATH` | The Proton to use: `GE-Proton` (default, the latest GE-Proton, fetched when missing) or the folder of an unpacked one. |
| `VDC_GITHUB_MIRROR` | A site that relays github.com (`https://host`) for the umu-launcher download, for places where GitHub is slow. |
| `VDC_PROTON_LOG=1` | Writes Proton/Wine logs to `$VDC_HOME/logs`. |

The program's `--data-dir` option still works; the launcher only adds its default when the option is absent.

## What works and what does not

| | Linux (Proton) |
|---|---|
| Pictures: neural pass, depth, DLSS super resolution, all controls, presets, undo | Works. |
| Videos and animated images (MP4, MOV, MKV, WebM, GIF, APNG, WebP) as input | Works, with the GPU decoder. |
| Video output | **WebP, GIF, APNG or a PNG sequence.** Proton has no H.264/HEVC encoder, so MP4 output is not offered; a video that would have come back as MP4 (including "Match the source") is saved as WebP at the WebP quality set in the sidebar. Audio is not kept. |
| Live mode (VRChat's Spout camera) | **Not available.** Spout senders exist only on Windows; VRChat running under Proton in its own prefix cannot be reached either. |
| Motion estimation | NVIDIA optical flow is not exposed to Proton; the program falls back to its FSR optical flow by itself. |
| MCP server and remote access | Works. The port opens on the Linux side as it would on Windows; the reach (this computer or the local network) and the keys are set in the sidebar's MCP section as usual ([MCP.md](MCP.md)). |
| Auto-update, GitHub mirror sites, setup guide, languages | Work. |
| Radeon edition (DLSS-NR-on-AMD) | Not supported on Linux. |

Performance is close to Windows on the same card for the GPU work; the WebP encoder is CPU-bound, so long videos take
their time (a 2560x1440 clip encodes at about 3 frames per second on eight Zen 2 cores).

## Steam Deck

The program was developed and tested on a Steam Deck running Ubuntu with an external RTX 4070 Ti SUPER. The Deck's own
AMD GPU cannot run the neural pass (see the Radeon row above); with an NVIDIA eGPU the package works like on any other
Linux machine. It has not been tried on SteamOS.

## Troubleshooting

- **The first start takes minutes and prints download progress.** That is umu-launcher fetching GE-Proton and the Steam
  Linux Runtime (about 2.5 GB); it happens once.
- **The window opens but stays still and does not react.** Another program holds an input grab on the display, most often
  the screen locker of a locked session (for example a remote desktop session that locked itself). Wine waits for the grab
  to go away. Unlock the session, or run the program from an unlocked one.
- **The picture area shows only a grey checkerboard, while the saved files are fine.** The program is running with
  Proton's own shader compiler, which gets the picture's transparency wrong. The Linux package ships Microsoft's
  `d3dcompiler_47.dll` next to `VRChatDLSS5Cam.exe` for this reason; if you started the Windows zip under Proton by
  hand, use the Linux package instead (the log says `Shader compiler: Proton's built-in HLSL compiler is in use`).
- **"Shader compilation failed" or "Initialization failed" at start.** The Proton in use is too old, or the shader
  compiler DLL is missing (see above). Use the launcher's default: `VDC_PROTONPATH=GE-Proton ./vrchat-dlss5-cam`.
- **The neural pass is "Inactive" and the log says the runtime could not be loaded.** The NVIDIA driver's Wine bridge is
  missing: `./vrchat-dlss5-cam --linux-info` shows whether `nvngx.dll` was found. Install the NVIDIA driver 5xx or newer
  from your distribution.
- **Something else.** `VDC_PROTON_LOG=1 ./vrchat-dlss5-cam` writes Proton's log to `~/.local/share/VRChatDLSS5Cam/logs`,
  the program's own log is `~/.local/share/VRChatDLSS5Cam/data/log.txt`; open an issue with both.

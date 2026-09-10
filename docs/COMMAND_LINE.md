# Command line

`VRChatDLSS5Cam.exe` takes a few options for opening files, for unattended processing and for testing without a
display. All of them are optional; a plain double-click starts the normal interface.

```
VRChatDLSS5Cam.exe [file] [options]
```

A bare `file` argument (what Windows passes for *Open with*) opens that picture or video.

| Option | Meaning |
|---|---|
| `--open <file>` | Open a picture or a video in the preview and add it to the library. |
| `--add <file>` | Add a picture or a video to the library without opening it (repeatable). |
| `--seek <seconds>` | Video: show the frame at this time. `m:ss`, `h:mm:ss` and plain seconds are accepted. |
| `--play` | Video: start playback in the preview. |
| `--in <seconds>` / `--out <seconds>` | Video: set the range that processing (and the audio) covers. |
| `--process [folder]` | Process the library (or the opened file) with the current settings and exit. The files are written into `folder`, or into the capture folder when none is given. |
| `--set <key>=<value>` | Override one setting for this run, using the key names of `settings.ini` (for example `--set nrIntensity=1.5`, `--set videoMatchSource=0 --set videoOutput=2`, `--set keepAudio=0`, `--set theme=2`, `--set customResolution=1 --set customWidth=3840 --set upscaleMode=0` for an upscale: 0 = DLSS super resolution, 1 = resampling). The value is not saved. `videoMatchSource` (1 by default) makes the output follow the codec, frame rate and bitrate of the source; set it to 0 for `videoOutput` (0 = MP4 H.264, 1 = MP4 HEVC, 2 = PNG sequence) and `videoBitrate` to apply. `theme` selects the look: 0 = follow Windows, 1 = dark, 2 = light. `updateCheck` (1 by default) is the check for a new version at every start and `updateChannel` picks its channel (0 = Stable, 1 = Pre-release); `sidebarWidth` and `libraryHeight` are the layout sizes in units of the font size (0 = the default size); `reopenLast` (0 by default) opens the file of the previous session again at start. |
| `--lang <en\|zh\|ja\|ko\|auto>` | Interface language for this run. |
| `--window <W>x<H>` | Start with this client size instead of the saved one (320×240 up to 16384×16384). |
| `--screenshot <seconds> <file.png>` | Save a picture of the whole window `seconds` after the start (repeatable). |
| `--exit-after <seconds>` | Quit after this many seconds, once pending screenshots and captures are written. |
| `--headless` | No window: everything is drawn into an off-screen buffer. Screenshots and processing work as usual, and the program exits by itself when its work is done. Meant for scripted tests and for sessions without a desktop (services, SSH). |
| `--data-dir <folder>` | Keep `settings.ini`, `presets.txt`, `log.txt` and `crash.txt` in this folder instead of `%LOCALAPPDATA%\VRChatDLSS5Cam`. |
| `--update` | Look for a newer version on the chosen channel and, if there is one, download and install it: the app closes, replaces its files and starts again. Without a newer version it simply carries on. The check that normally runs at every start is skipped in `--process` runs, so this switch is the way to ask for it there; a `--headless` run never updates. |
| `--edition <geforce\|amd>` | Fetch that edition of this program from the release page (this version or newer, pre-releases included) and swap to it the way an update does. The GeForce edition on a Radeon card offers the same as a button (**Get the Radeon edition…**), the Radeon edition on a GeForce card the reverse. Nothing happens when this is that edition already. |

Exit codes: `0` when everything succeeded, `1` when a file failed to process (or the process ended with an unhandled
C++ exception), `2` after a crash (`crash.txt` is written) or after the graphics device was lost. A run without
`--process`, `--headless` or `--exit-after` stays open like a normal session; when such a session loses the graphics
device (a driver reset) it starts itself again with `--after-device-loss` added to its arguments and keeps the log of
the lost session as `log-device-loss.txt`, while a scripted run exits with code 2.

## Examples

Process every file of a folder into `D:\out`, using HEVC for the videos, without opening a window:

```
VRChatDLSS5Cam.exe --headless --add "D:\shots\a.png" --add "D:\shots\clip.mp4" --set videoMatchSource=0 --set videoOutput=1 --process "D:\out"
```

Process ten seconds of a video, from 1:00 to 1:10:

```
VRChatDLSS5Cam.exe --headless --open "D:\clip.mp4" --in 1:00 --out 1:10 --process
```

Take a picture of the interface with a video open at 30 s, in Japanese and in the light theme, then quit:

```
VRChatDLSS5Cam.exe --window 1600x900 --lang ja --set theme=2 --open "D:\clip.mp4" --seek 30 --screenshot 6 "D:\ui.png" --exit-after 8
```

The log (`log.txt`) records every command-line action, so a failed unattended run can be read back afterwards.

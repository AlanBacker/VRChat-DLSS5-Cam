---
title: Command line
nav: Command line
description: Open files, process them without a window, and script test runs. For people who like a terminal.
---
You never need the command line: a double-click starts the normal window. It is there for scripts, scheduled runs and tests.

```
VRChatDLSS5Cam.exe [file] [options]
```

A file name on its own opens that picture or video, which is what **Open with** in Explorer does.

## Examples {#examples}

Process two files into `D:\out` without a window, saving the videos as HEVC:

```
VRChatDLSS5Cam.exe --headless --add "D:\shots\a.png" --add "D:\shots\clip.mp4" --set videoMatchSource=0 --set videoOutput=1 --process "D:\out"
```

Process ten seconds of a video, from 1:00 to 1:10, into the capture folder:

```
VRChatDLSS5Cam.exe --headless --open "D:\clip.mp4" --in 1:00 --out 1:10 --process
```

Take a picture of the window with a video at 30 seconds, in Japanese and the light theme, then quit:

```
VRChatDLSS5Cam.exe --window 1600x900 --lang ja --set theme=2 --open "D:\clip.mp4" --seek 30 --screenshot 6 "D:\ui.png" --exit-after 8
```

## Options {#options}

| Option | What it does |
|---|---|
| `--open <file>` {#open} | Open a picture or a video and add it to the library. An animated GIF, APNG or WebP counts as a video. |
| `--add <file>` {#add} | Add a file to the library without opening it. Can be repeated. |
| `--seek <time>` {#seek} | Video: show the frame at this time. `m:ss`, `h:mm:ss` and seconds work. |
| `--play` {#play} | Video: start playing. |
| `--in <time>` · `--out <time>` {#in-out} | Video: the part to process, sound included. |
| `--process [folder]` {#process} | Process the library (or the open file) with the current settings, then exit. Into `folder`, or the capture folder. |
| `--set <key>=<value>` {#set} | Change one setting for this run only, by its name in `settings.ini`. See below. |
| `--lang <en\|zh\|ja\|ko\|auto>` {#lang} | The interface language for this run. |
| `--window <W>x<H>` {#window} | Start with this window size. |
| `--screenshot <seconds> <file.png>` {#screenshot} | Save a picture of the window this many seconds after the start. Can be repeated. |
| `--exit-after <seconds>` {#exit-after} | Quit after this many seconds, once screenshots and saves are written. |
| `--headless` {#headless} | No window. Saving and screenshots work as usual, and the app exits when its work is done. |
| `--data-dir <folder>` {#data-dir} | Keep the settings, presets and logs in this folder instead of `%LOCALAPPDATA%\VRChatDLSS5Cam`. |
| `--mcp` {#mcp} | The bridge for an MCP client. [MCP](mcp.html) |
| `--mcp-port <port>` {#mcp-port} | Run the MCP server on this port for this session. |
| `--update` {#update} | Look for a newer version on the chosen channel and install it. |
| `--edition <geforce\|amd>` {#edition} | Swap to the other edition, the way an update does. |

## Settings for one run {#set-keys}

`--set` takes the names used in `settings.ini`. Values given on the command line are not saved. A few useful ones:

| Key | Values |
|---|---|
| `nrIntensity` | `0` to `2`, for example `--set nrIntensity=1.5`. |
| `videoMatchSource` | `1` (default) follows the source; `0` uses the three keys below. |
| `videoOutput` | `0` MP4 H.264, `1` MP4 HEVC, `2` PNG sequence, `3` GIF, `4` APNG, `5` WebP. |
| `videoBitrate` · `webpQuality` | The MP4 bitrate; the WebP quality, `50` to `100`. |
| `keepAudio` | `0` drops the sound. |
| `outputName` · `captureName` | The file-name templates. [Words you can use](saving.html#names) |
| `customResolution` · `customWidth` · `upscaleMode` | An upscale: `--set customResolution=1 --set customWidth=3840 --set upscaleMode=0` (`0` DLSS super resolution, `1` resampling). |
| `theme` | `0` follows Windows, `1` dark, `2` light. |
| `updateCheck` · `updateChannel` | The check at start (`1` on); the channel (`0` Stable, `1` Pre-release). |
| `driverCheck` | The driver notice at start (`1` on). |
| `reopenLast` | Reopen the last file at start (`0` off). |

## Exit codes {#exit-codes}

| Code | Meaning |
|---|---|
| `0` | Everything worked. |
| `1` | A file failed to process. |
| `2` | The app crashed (`crash.txt` is written), or the graphics device was lost. |

The log, `log.txt`, records every command-line action, so a failed run can be read back afterwards. On Linux the `vrchat-dlss5-cam` launcher takes the same options. [Linux](linux.html)

The full list, with the testing options, is in [COMMAND_LINE.md](https://github.com/AlanBacker/VRChat-DLSS5-Cam/blob/main/docs/COMMAND_LINE.md) on GitHub.

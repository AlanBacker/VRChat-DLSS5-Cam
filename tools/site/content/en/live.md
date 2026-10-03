---
title: Live from VRChat
nav: Live camera
description: Send VRChat's camera to the app, watch DLSS 5 live and save shots with a hotkey.
---
VRChat can send what its camera sees to other programs, through a system called Spout. The app receives that picture, applies DLSS 5 and shows it. One key press saves a lossless PNG.

{{diagram:live}}

## Turn on the stream in VRChat {#vrchat}

1. In VRChat, open the **Camera**.
2. Switch the camera to **Stream** mode.
3. In the camera's settings, turn on **Spout Stream**.

## Receive it in the app {#receive}

1. Click **Live** in the top bar, or **Live from VRChat** on the Get started page.
   => The camera picture appears, with DLSS 5 applied.

If no picture comes:

- **No Spout sender found**: Spout Stream is off, or the camera is closed. Check the three steps above.
- **Waiting for VRChat Spout stream…**: a sender is there, but it has not sent a picture yet.

The section **Source** shows the sender in use. **Spout sender** is set to **Auto (VRChat)** at first, which finds VRChat's camera. Other programs that send over Spout are listed there too.

## Take a photo {#capture}

1. Frame the shot in VRChat.
2. Press ((Ctrl+Alt+P)). VRChat can stay in front: the key works anyway.
   => A PNG is saved to `Pictures\VRChat DLSS5 Cam`, named like `VRChat_DLSS5_2026-10-03_21-15-08.412_1920x1080.png`.

The **Capture photo** button at the top right does the same.

> [!TIP]
> Another program already uses ((Ctrl+Alt+P))? Pick other keys under **Capture** → **Global hotkey**.

## A photo every few seconds {#timelapse}

**Capture** → **Auto capture every (seconds)** saves a photo at that interval while the camera sends. **0 = off**. Use it for a time-lapse, or to catch a moment without pressing keys.

## Turn or mirror the camera {#turn}

The small toolbar over the picture turns the camera picture by a quarter or mirrors it. The change applies to the preview, the photos and the time-lapse alike, and the app keeps it for the next start.

## A sharper picture {#resolution}

VRChat decides the size of the stream. Set it in the camera's settings (720p up to 2160p), or with `camera_spout_res_width` and `camera_spout_res_height` in VRChat's `config.json`. The app follows any size by itself.

## If the live picture is slow {#slow}

DLSS 5 is heavy, and VRChat needs the same graphics card. Two controls help. They show while the **Advanced** switch is on.

- **DLSS 5 Neural Rendering** → **Neural pass only for captures**: the preview shows the plain picture and the card rests. Every photo still gets DLSS 5: the app runs it on 16 fresh frames first, then saves.
- **Display** → **Processing rate cap**: process at most this many frames per second, and skip the rest.

A smaller **Neural pass resolution**, for example **Long edge up to 1440 px**, also helps with a large stream. [More in Troubleshooting](troubleshooting.html#slow)

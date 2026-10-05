---
title: Troubleshooting
nav: Troubleshooting
description: What a message means and what to do, from a missing VRChat picture to a slow card.
---
## Live camera {#live}

### "No Spout sender found" {#no-sender}

No program sends a picture. In VRChat, open the **Camera**, switch it to **Stream** mode and turn on **Spout Stream**. The camera must stay open.

### "Waiting for VRChat Spout stream…" {#waiting}

A sender runs, but the chosen one has not sent a picture yet. Check that the VRChat camera is open, or pick another sender under **Source** → **Spout sender**.

## DLSS 5 {#dlss5}

### "A newer graphics driver is needed" {#driver}

DLSS 5 needs GeForce driver 616.56 or newer. Click **Download driver** in the notice, install the newest driver and start the app again. [Graphics driver](install.html#driver)

### "The runtime files are missing" {#runtime-missing}

The zip was not unpacked completely. Unzip it again, into a new folder, with **Extract All**. The files `runtimes\blackwell\nvngx_dlssnr.dll` and `runtimes\universal\nvngx_dlssnr.dll` belong next to `VRChatDLSS5Cam.exe`.

### DLSS 5 failed to start {#failed}

The app tries the other included build by itself and says so. If neither starts:

1. Install the newest graphics driver.
2. Start the app again.
3. Still failing? **About** → **Report a problem**, and attach `log.txt`.

### Active, but the picture is black or unchanged {#unchanged}

The app compares every DLSS 5 frame with its input and warns when the runtime returns a black or unchanged picture. Turn **Enable DLSS 5 (DLSSNR)** off and on again. At **Intensity** 0 an unchanged picture is normal, and no warning appears.

## Speed {#slow}

### The live picture is slow {#live-slow}

Try these, one at a time:

1. Turn off the **DLAA pre-pass** and **DLSS super resolution**, if you turned them on.
2. Choose a smaller **Neural pass resolution**, for example **Long edge up to 1440 px**.
3. Set a **Processing rate cap**.
4. Turn on **Neural pass only for captures**: the preview rests, and photos still get DLSS 5.

These controls show while **Advanced** is on. Leave **Flow grid** at 4 px: 2 px and 1 px cost far more at 4K.

### "The graphics device was lost" {#device-lost}

The graphics driver reset the card. Windows gives the card about two seconds per job, and a card that is fully loaded for a long time can miss that, for example while VRChat and DLSS 5 run on the same card at a large size. The app starts again by itself and says so.

If it happens again, lower the load: a processing rate cap, a smaller neural pass resolution, a smaller output size, or a frame-rate limit in VRChat. A newer graphics driver is worth a try.

## Videos {#videos}

### The video preview is black {#video-black}

Many videos start with a fade from black. The preview skips those frames and says so under the picture. Move forward with the timeline or the arrow keys.

### A video does not open, or cannot be saved as MP4 {#video-codecs}

Windows decides which formats work. HEVC files need the **HEVC Video Extensions** from the Microsoft Store, and the N editions of Windows need the **Media Feature Pack**. Turning off **Source** → **Hardware decoding** helps with files the graphics card rejects. A **PNG sequence** always works as the output.

## Updates {#updates}

### The update cannot be installed {#update-folder}

The app's folder must be writable, and a folder under `Program Files` usually is not. Move the app to a folder of your own, or download the new zip from the release page and replace the files by hand. Your settings and library stay.

### The update check fails {#update-check}

Where GitHub is slow or blocked, choose **About** → **GitHub access** → **Mirror sites, the fastest one**. [Updates](updates.html#mirror)

## AI Q&A {#ask-ai}

### "AI Q&A could not be loaded" {#ask-failed}

AI Q&A needs an internet connection. Check the connection, then click **Try again** in the panel. **Open documentation** beside it opens these pages in your browser instead.

### "AI Q&A is unavailable right now" {#ask-off}

The project's monthly allowance for AI Q&A may be used up, or Mintlify's service is down. The allowance comes back the next month. Until then, these pages answer the same questions: **Open documentation** in the notice opens them.

The other notices in the panel, and what to do about each: [When a question gets no answer](ai-qa.html#notices)

### Ask AI opens the browser {#ask-browser}

The panel inside the app needs the Microsoft Edge WebView2 Runtime. Without it, and in the Linux package, **Ask AI** opens AI Q&A in your browser instead. To have the panel in the app on Windows, install the runtime and start the app again. [AI Q&A](ai-qa.html#browser)

## The app closed or crashed {#crash}

At the next start the app notices that the last session did not end properly. A window offers **Report on GitHub**, which opens an issue form already filled in, and **Open log folder**.

The log folder is `%LOCALAPPDATA%\VRChatDLSS5Cam`. Attach `log-crash.txt` and `crash.txt` to the report. **About** → **Report a problem** works at any time, too.

> [!TIP]
> Before you report, read the report form once. It is filled in with the app version and your graphics card, and nothing is sent until you submit it.

---
title: Install
nav: Install
description: Pick the download for your graphics card, unzip it and start the app.
---
## What you need {#requirements}

- Windows 10 (21H2 or newer) or Windows 11, 64-bit.
- An NVIDIA GeForce RTX card: RTX 20, 30, 40 or 50 series.
  An AMD Radeon RX 7000 or 9000 works with the [Radeon edition](radeon.html).
- For the live camera: VRChat, on desktop or in VR.

Nothing else. The DLSS 5 runtime is in the download, in one build for RTX 50 and one for RTX 40, 30 and 20. The app picks the right one by itself.

## Download and unzip {#package}

1. Open the [download section](index.html#download) and pick the file for your card.
   For a GeForce card it is `VRChatDLSS5Cam-win64.zip`.
2. Right-click the zip and choose **Extract All**. Pick a folder you own, for example **Documents**.
   => A new folder with `VRChatDLSS5Cam.exe` inside.
3. Double-click `VRChatDLSS5Cam.exe`.
   => A small card with the app's icon appears, then the window.

> [!WARNING]
> Do not start the app from inside the zip, and do not put it under `Program Files`. The app updates itself by replacing its own files, and it cannot do that in a folder that needs administrator rights.

> [!NOTE]
> The app is not signed with a paid certificate, so Windows may show a blue box saying it protected your PC. Click **More info**, then **Run anyway**. The source code is public on GitHub.

## The setup guide {#setup-guide}

The first start opens a short setup guide with five pages:

1. **Welcome**: the language, and the look (**System**, **Dark** or **Light**).
2. **GitHub access**: how the app reaches GitHub for updates. Keep **GitHub directly**. Where GitHub is slow or blocked, choose **Mirror sites, the fastest one**.
3. **How the program works**
4. **What the settings do**
5. **Where to find things**

**Next** goes on, **Skip** closes it. To see it again later: **About** → **Setup guide**.

![](setup-guide "The first page of the setup guide.")

## Graphics driver {#driver}

DLSS 5 needs GeForce driver **616.56** or newer. The app checks the driver at every start and tells you when yours is older:

![](driver-notice)

1. Click **Download driver**. NVIDIA's download page opens in your browser.
2. Install the newest driver for your card.
3. Start the app again.
   => The notice no longer appears, and **DLSS 5 Neural Rendering** says **Active**.

With an older driver everything else still works: the viewer, captures and videos. Only DLSS 5 does not start.

> [!TIP]
> **Don't show again** in the notice turns the check off. **About** → **Check the graphics driver at start** turns it back on.

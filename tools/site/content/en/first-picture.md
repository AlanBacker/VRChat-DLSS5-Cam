---
title: Your first picture
nav: First picture
description: Open a photo, compare it with the DLSS 5 result and save it. VRChat does not need to run.
---
The easiest start is a photo you already have, for example one from VRChat's own `Pictures\VRChat` folder.

## Open a picture {#open}

1. Start the app.
   => The **Get started** page fills the picture area.
2. Click **Open a picture** and choose a file. PNG, JPEG, BMP, TIFF, WebP and HEIC work.
   You can also drag the file onto the window.
   => The photo appears. After a moment the section **DLSS 5 Neural Rendering** says **Active**, and the top bar shows a **DLSS 5** badge.

![](start-page "The Get started page. Its three cards open the live camera, a picture or a video.")

A still picture runs through DLSS 5 several times until the result settles. That takes a second or two.

## Compare {#compare}

A white line with a round handle splits the picture: the original on the left, the DLSS 5 output on the right. Drag the handle to move the line. You can try it here:

{{wipe}}

![](picture-dlss5 "The photo with the comparison line. The library is below, the settings are on the right.")

- **Zoom**: turn the mouse wheel over the picture. It zooms around the pointer.
- **Move around**: drag the picture.
- **Whole picture again**: double-click.
- **Fullscreen**: ((F11)). ((Esc)) or ((F11)) leaves.

To see only the result, set **Display** → **Compare** to **DLSS 5 output**.

![](picture-output)

## Adjust the look {#adjust}

The section **DLSS 5 Neural Rendering** holds the look. Every change shows at once.

- **Intensity**: how strongly the picture is rendered again. 1 is the runtime's own look, 0 leaves the picture as it is, and above 1 the change is exaggerated.
- **Style**: **Default**, **Natural** or **Cinematic**.

Changed too much? ((Ctrl+Z)) undoes the last change. **Reset to defaults** at the end of the section brings back the starting values.

> [!TIP]
> The **Advanced** switch at the top of the sidebar is on at first, so every control shows. Turn it off to keep only what a first picture needs. [All settings](settings.html)

## Save {#save}

1. Click **Process & save PNG** at the top right.
   => The status bar at the bottom names the saved file. The button next to the name shows it in Explorer.

The file goes to `Pictures\VRChat DLSS5 Cam` and is named after the original, for example `photo_DLSS5_1920x1080.png`. A PNG is lossless: nothing is lost to compression. [Change the folder or the name](saving.html)

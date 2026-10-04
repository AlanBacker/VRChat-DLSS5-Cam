---
title: Questions and answers
nav: FAQ
description: Short answers to the questions people ask most.
---
## Is it free? {#free}

Yes. The app is free and open source, under the MIT licence.

## Can VRChat ban me for it? {#ban}

The app does not touch VRChat at all. It needs no mod, and VRChat's process is never opened. It only receives the picture that VRChat's own camera sends through Spout, a feature VRChat offers for streaming.

## Do my pictures get uploaded? {#upload}

No. Everything runs on your own graphics card, and your pictures and videos stay on your computer.

The app goes online to look for updates and, in the Radeon edition, to fetch DLSS-NR-on-AMD when you ask. [AI Q&A](ai-qa.html) goes online too, once you open it: it sends your questions to Mintlify, never your files. [Privacy and licences](privacy.html)

## Does it work without VRChat? {#without-vrchat}

Yes. Pictures and videos from your disk need no VRChat. Only the live camera does.

## Which graphics cards work? {#cards}

NVIDIA GeForce RTX 20, 30, 40 and 50 with the GeForce edition. AMD Radeon RX 7000 and 9000 with the [Radeon edition](radeon.html). On other cards the app runs as a viewer and recorder, without DLSS 5.

## Do I need to download the DLSS 5 runtime? {#runtime}

No. The GeForce edition includes it, in one build for RTX 50 and one for RTX 40, 30 and 20. The app picks the right one.

## Why does Windows warn me when I start it? {#windows-warning}

The app is not signed with a paid certificate. Click **More info**, then **Run anyway**. The source code is public, and every release is built on GitHub from it.

## Why does DLSS 5 change faces and skin? {#faces}

DLSS 5 renders the light and materials again, and skin is a material too. Lower the **Intensity**, or with **Advanced** on lower **Skin structure strength** and **Local structure strength**. Saving a look you like as a preset helps next time. [Presets](presets.html)

## Where are my saved pictures? {#where}

In `Pictures\VRChat DLSS5 Cam`, unless you chose another folder under **Capture** → **Folder**. The status bar names the last saved file, with a button that shows it in Explorer. [Saving](saving.html)

## Why is a single picture slower than a video frame? {#still-slow}

A still picture has no earlier frame, so the app runs it through DLSS 5 several times until the result settles. [How it works](how-it-works.html#still)

## Can I use it on a laptop? {#laptop}

Yes, if it has a GeForce RTX card. Plug in the charger: DLSS 5 is heavy work for the card.

## Does it work on Linux or a Mac? {#linux-mac}

Linux with an NVIDIA card works through Proton, for pictures and videos. [Linux](linux.html) There is no Mac version.

## How do I get a sharper live picture? {#sharper}

Raise the stream size in VRChat's camera settings, up to 2160p. [Live from VRChat](live.html#resolution)

## Can I ask a question inside the app? {#ask}

Yes. **Ask AI** in the top bar opens AI Q&A beside the picture. It answers from this documentation and links to the pages it used. It needs an internet connection, and its answers can be wrong. [AI Q&A](ai-qa.html)

## Where do I report a problem or ask for a feature? {#report}

On GitHub: **About** → **Report a problem** opens the form. Finished pictures are welcome on X under **#VRCDLSS5CAM**.

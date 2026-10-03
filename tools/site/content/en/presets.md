---
title: Presets and undo
nav: Presets
description: Keep the looks you like under a name, and step back through every change.
---
## Save a look as a preset {#save}

A preset keeps the DLSS 5 look under a name: the **Style**, the **Intensity**, the tone and structure strengths, **Auto mask** and **UI correction**, the output blend and the **Neural pass resolution**.

1. Set the look you like.
2. In **DLSS 5 Neural Rendering**, click **+** next to **Preset**.
3. Type a name and click **Save**.
   => The name shows in the **Preset** box.

Saving under a name that already exists overwrites that preset.

![](sidebar-dlss5 "The DLSS 5 section. Preset is the row with the + button.")

## Use, change, delete {#manage}

- Open the **Preset** box and click a name to apply it.
- Each row has three buttons: overwrite it with the current values, rename it, delete it.
- When you change a value after applying a preset, the box says **Custom**.

![](presets "The Preset list. Each row has overwrite, rename and delete.")

Presets are kept in `presets.txt` in the settings folder. [Where that is](saving.html#folders)

> [!TIP]
> A single library file can have values of its own, apart from any preset. [Own values for one file](library.html#own)

## Undo and redo {#undo}

Every change to a setting, and every file added to or taken out of the library, can be undone.

- ((Ctrl+Z)) undoes. ((Ctrl+Y)) or ((Ctrl+Shift+Z)) redoes.
- The two arrows in the top bar do the same.
- The clock button next to them opens the **History**: every change as a list, such as *Intensity: 1.2* or *Added photo.png*. Click an entry to go back to it. The later steps stay until you make a new change.

![](history "The History panel. The step you are at is marked; the later steps stay until a new change.")

The app keeps up to 100 steps.

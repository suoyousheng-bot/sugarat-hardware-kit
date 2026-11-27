---
description: "Run the synchronized Sugarat Reactor lighting effects on a round badge."
kind: "reference"
---

# Sugarat Reactor side display

English | [中文](README.zh.md)

Install this application on each side badge and open [Sugarat Reactor](../sugarat-reactor/README.md) on the controller. Both side badges find the controller automatically on channel 6. Swipe left or right on the controller to switch between Core ignition, Segmented ring and Twin arcs. Swipe left or right on a side badge to change its own style, even while the controller is offline. The next style change received from the controller applies to both displays. Both displays fade up as the mask closes and fade to black as it opens. The controller plays the sound.

The displays accept two successive status broadcasts from the same controller and follow it while its signal is fresh. After about 1.5 seconds without status, they fade to black and resume discovery. Use a distinct `SUGARAT_LIGHTING_GROUP` for each nearby installation with its own controller. The controller and its two displays must use the same group. The default group is 1.

From the workspace root with ESP-IDF 5.5.4 activated:

```sh
idf.py -C firmware-plays/stage1/sugarat-reactor-side build
```

Install `build/sugarat_reactor_side.bin` in a separate application slot on a badge with the launcher. The top edge opens the system control center. Select **Exit** to return to the launcher. The generated bootloader and partition table are for standalone builds.

The screen holds one 360 × 360 RGB565 render buffer in PSRAM. [The asset tool](tools/prepare_art.py) converts the approved three-stage Core, Ring and Arcs preview strips into the endpoint art and per-pixel lighting phases. The device interpolates brightness between snapshots and renders intermediate frames locally. Changing styles fades through black.

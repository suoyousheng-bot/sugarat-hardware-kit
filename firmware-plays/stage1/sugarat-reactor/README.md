---
description: "Build and run the Sugarat Reactor mask remote on the ESP32-S3 round badge."
kind: "reference"
---

# Sugarat Reactor

English | [中文](README.zh.md)

Swipe up on the reactor to open the mask and swipe down across its center to close it. Ignition lights the core blue and sends sparks upward. Discharge darkens the core and sends embers downward. The transition starts with its audio cue and reaches its final appearance when the receiver reports that the mask has stopped at the requested position. When the badge cannot reach the receiver, it searches and shows the connection status. A swipe made while searching does not execute later.

Swipe down from the top edge for the system control center. In multi-application mode, its **Exit** button returns to the launcher.

Swipe left or right on the controller to cycle the side displays through Core ignition, Segmented ring and Twin arcs. The default is Segmented ring. Both [side displays](../sugarat-reactor-side/README.md) discover the controller automatically and follow the brightness synchronized to the cue.

## Build

From the repository root with ESP-IDF 5.5.4 activated:

```sh
idf.py -C firmware-plays/stage1/sugarat-reactor build
```

The project uses the existing Sugarat board and launcher support in `../sugarat-lunch` and [mask remote controller](../../../esp32c3supermini/sugarat/main/manual_control.c). The app image is `build/sugarat_reactor.bin`. Install this image into a managed application slot on an existing launcher device. The generated standalone bootloader and tables are only for standalone builds.

Set `SERVO_PEER_MAC` at CMake configuration if the receiver uses a different Wi-Fi STA MAC. The C3 receiver (not included in this branch) must also be configured for the badge's STA MAC. The app uses ESP-NOW on channel 6, and the existing receiver searches for the paired badge. The A and B positions, travel speed and light behavior remain defined by the receiver.

The screen uses crops of the selected reference artwork in the round panel's 360 × 360 RGB565 format. The two selected preview WAVs are resampled to 16 kHz mono PCM for the badge speaker.

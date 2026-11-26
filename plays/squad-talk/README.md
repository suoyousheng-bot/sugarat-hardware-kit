---
description: "Build and use Squad Talk, an ESP-NOW group push-to-talk application for Sugarat round badges."
---

# Sugarat Squad Talk

English | [中文](README.zh.md)

## Summary

Squad Talk connects nearby Sugarat ESP32-S3 round badges directly over ESP-NOW. Choose the same channel, wait for `GROUP ONLINE`, hold `HOLD TO TALK`, and release to listen. The English cream/lavender interface uses a large fingerprint hold target, rounded bold text and state-specific fills. The application needs no router, phone, account or server. [validation.json](validation.json) separates firmware build, host tests and device acceptance.

## Contents

- [Use](#use)
- [Build](#build)
- [Host checks](#host-checks)
- [Communication and failures](#communication-and-failures)
- [Hardware and sources](#hardware-and-sources)
- [Dev Note](#dev-note)

## Use

Install Squad Talk 0.2.0 on every badge in the group. Earlier versions use a different radio protocol and cannot join. Swipe left on the screen background for the next logical channel and right for the previous one; channels 01–08 wrap around and each boot starts on 01. Start outside the fingerprint button and below the top control-center strip. Each swipe changes one channel on release; taps, short drags and predominantly vertical movement do not change channels. `GROUP ONLINE` means at least one participating badge has been heard on your channel.

Hold the fingerprint button to request a turn. `CALLING...` waits for a reply from any listener, then `RELEASE TO LISTEN` indicates transmission. Other badges on the channel display `LISTENING...` and play the speech, including badges that join during the turn. One person speaks at a time. When requests arrive together, the lower device address has priority. Press again after a lost request, timeout or pause. A turn lasts up to 30 seconds. If the release packet is lost, listeners free the channel after 600 ms without audio. A badge drops out of discovery after 3 seconds without a valid packet.

A drag that starts on the fingerprint button never changes channels; moving outside its ellipse cancels speech until a fresh press. A lost touch, opening the control center, screen suspension, or application exit releases transmission. Closing the control center or waking never resumes an old held press. Top-edge swipe retains the platform's volume, brightness and Exit/Power controls. Fatal audio or radio errors disable speech and display `RESTART APP`; tapping the button restarts the firmware. The fingerprint is a touch illustration; it does not scan or identify fingerprints.

## Build

Activate an existing ESP-IDF 5.5.4 environment with the ESP32-S3 toolchain, then run from the resource-kit root:

```sh
idf.py -C plays/squad-talk -B /tmp/squad-talk-build build
```

Deliver only `/tmp/squad-talk-build/squad_talk.bin`. The project name `squad_talk` is the stable launcher identity; application version is 0.2.0. Preserve the platform partition table, bootloader configuration, power latch and launcher Exit entry. Device inspection must establish the actual writable application address before flashing. The application never changes partitions, otadata or another application's data.

## Host checks

With CMake, Ninja, a C compiler and Pillow available, run:

```sh
python3 plays/squad-talk/tests/check_host.py --output /tmp/squad-talk
```

The command runs the real protocol, independent ADPCM blocks, LVGL view and touch controller under AddressSanitizer and UndefinedBehaviorSanitizer. Four simulated badges exercise 200 alternating turns, simultaneous requests, recovery after loss of radio contact, joining mid-turn, channel isolation, duplicate and late packets, pause and timeouts. A separate test rotates through 24 simulated speakers and confirms each broadcast voice block reaches the other 23. The maximum reliable group size on hardware remains unmeasured. Real LVGL pointer events check hold, release, drag-out, swipe directions, channel wraparound, rejected short/diagonal/top-edge drags, speech/swipe isolation and overlay/sleep cancellation. The owner-local [English output snapshot](tests/expected/ui-text.txt) covers eight states. The exported `preview-en.png` renders the actual view; it is not a device screenshot or evidence of wireless range, audio quality or end-to-end latency.

## Communication and failures

All logical groups share radio channel 6. Firmware initializes the Wi-Fi radio in station mode without joining an access point; it does not start a hotspot or IP server. Discovery, turn control and audio all use broadcasts. Each voice block is sent once regardless of how many badges are listening. A turn grant identifies its requester by device address, boot identity and turn number. Logical channels filter packets without encrypting voice. Broadcast frames can be lost, and devices outside one another’s radio range can briefly transmit at the same time.

Speech uses independently decodable 16 ms, 16 kHz mono IMA ADPCM blocks with a 24-byte versioned header. Packet lengths, version, group, codec state, speaker address, boot identity and turn number are validated before playback. Duplicate or late sequence numbers are dropped; a lost block does not corrupt subsequent decoder state. The radio worker serializes sends; microphone and Wi-Fi callbacks only copy bounded data into queues. Full audio queues drop audio, while release/pause input uses separately synchronized state. A missing send completion stops the radio instead of associating a late completion with a new packet.

The application-local audio component buffers 64 ms before playback, waits at most 80 ms for a short burst, and buffers 48 ms after an underrun. One playback writer awaits audio-task acknowledgement when discarding a previous conversation. Startup warms the microphone once; it only transmits captured audio while a listener has granted the held turn. Speech peaks are limited to the documented badge speaker range. Audio playback (priority 8), microphone capture (9) and radio delivery (7) run on CPU 1, above the CPU 0 UI (6). Level changes redraw only the activity bars; the fingerprint target stays static. During a turn, `FLOW` logs report transmitted blocks, cumulative receive/loss/drop counts, queued samples and maximum worker-loop gap. These settings require physical acoustic and range tests with multiple badges. Wi-Fi credentials are neither requested nor stored; channel selection is volatile and no audio is saved.

## Hardware and sources

The project is initialized from the Sugarat board foundation 0.2.1: ESP-IDF 5.5.4, LVGL 8.3.11, 360×360 ST77916, CST816 touch, 32 MiB Flash and 8 MiB Octal PSRAM. The microphone and speaker retain their BSP I2S wiring. This application adapts the bundled Game controls with a cream/lavender/lime palette, a 220×350 px elliptical fingerprint target whose lower half extends beyond the screen and Nunito Black English glyphs. Text fits the physical circular display; pointer tests distinguish the ellipse from its rectangular corners. [Nunito's OFL license](assets/fonts/OFL.txt) covers the bundled source and generated subsets; regenerate them with `python3 plays/squad-talk/tools/make_ui_fonts.py`. [upstream.json](upstream.json) pins the inspected atomic14 walkie-talkie reference; [its MIT notice](licenses/atomic14-MIT.txt) and the foundation notices remain distributed. The group session protocol, independent codec blocks and hardware adaptation are maintained in this application.

Follow the kit's [deployment instructions](https://github.com/suoyousheng-bot/sugarat-hardware-kit/blob/v0.3.10/references/deployment.md). A full private backup is skipped only with the user's explicit instruction. Launcher installation, standalone flashing, written-byte verification, startup, touch, microphone, speaker, half-duplex conversation, range and power behavior are separate acceptance results. A successful firmware build or simulated conversation does not establish device behavior.

## Dev Note

None.

# Wolf Companion

A little wolf on your round badge: it follows your finger and the tilt of the badge, thinks when tapped, gets dizzy when shaken, jumps onto its sofa and dozes off when left alone.

## About the play

Tap the wolf and it turns its head to think, with a bobbing question mark. Double tap or shake the badge and it gets dizzy, with stars circling its head. Hold your finger on the screen for about half a second and it jumps onto its sofa. Drag a finger or tilt the badge and its head and eyes follow. Left alone for 30 seconds it dozes off with floating Zs; tap or pick the badge up to wake it. The animation runs at a steady 30 fps from 569 frames. This play runs offline.

## Requirements

Sugarat ESP32-S3 round badge with touch and a motion sensor. The application image is about 5.5 MB.

## Make your own (做同款)

1. Download the attachment `sugarat-wolf.tar.gz` of release `v0.3.7`: https://github.com/suoyousheng-bot/sugarat-hardware-kit/releases/download/v0.3.7/sugarat-wolf.tar.gz. It is this folder plus every shared component it uses, including LVGL; nothing else needs to be downloaded. From a repository checkout, `python3 scripts/kit.py export-play --id sugarat-wolf --directory <new-folder>` writes the same project.
2. Make the requested changes in that standalone project. Keep the components, partition table and license files; do not start from the basic template instead.
3. Raise the version in `version.txt` (and `PROJECT_VER` when CMakeLists.txt sets it).
4. Build with ESP-IDF 5.5.4 for `esp32s3`. The application image is `build/sugarat_wolf.bin`; it is the only file to install.
5. Install the application image through the Sugarat desktop app or a launcher slot. Never change the device partition table or reuse a historical flash address.

Report compilation, desktop preview, written-byte verification, startup and physical behavior separately.

# Hamster Wheel

Spin your Sugarat badge to send a pixel hamster running, hopping, and tumbling in its wooden wheel.

Community play: Sugarat 玩法社区 #44 (`sugarat_hamster`), published version 1.0.0.
Source version in this directory: 1.0.0.

## About the play

Turn the badge gently to help your hamster run in its wooden wheel. Change direction and it hops to turn around. Keep spinning too fast and it tumbles, then asks you to slow down. Soft wooden footsteps and short sounds follow the action. This play runs offline.

## Requirements

Sugarat ESP32-S3 round badge with a motion sensor. Sound uses the built-in speaker and follows the system volume.

## Make your own (做同款)

1. Download the attachment `sugarat-hamster.tar.gz` of release `v0.3.6`: https://github.com/suoyousheng-bot/sugarat-hardware-kit/releases/download/v0.3.6/sugarat-hamster.tar.gz. It is this folder plus every shared component it uses, including LVGL; nothing else needs to be downloaded. From a repository checkout, `python3 scripts/kit.py export-play --id sugarat-hamster --directory <new-folder>` writes the same project.
2. Make the requested changes in that standalone project. Keep the components, partition table and license files; do not start from the basic template instead.
3. Raise the version in `version.txt` (and `PROJECT_VER` when CMakeLists.txt sets it).
4. Build with ESP-IDF 5.5.4 for `esp32s3`. The application image is `build/sugarat_hamster.bin`; it is the only file to install.
5. Install the application image through the Sugarat desktop app or a launcher slot. Never change the device partition table or reuse a historical flash address.

Report compilation, desktop preview, written-byte verification, startup and physical behavior separately.

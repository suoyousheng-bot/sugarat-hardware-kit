# Armor Fly

Fly a pixel-art powered suit using your Sugarat badge as a motion controller.

Community play: Sugarat 玩法社区 #49 (`sugarat_armor`), published version 0.1.8.
Source version in this directory: 0.1.8.

## About the play

Install Sugarat Armor on your badge from this play, then select **Open game** on your computer. Select **Connect Sugarat** and choose **Sugarat Armor**. Hold the badge screen up, with the edge marked **TO SCREEN** facing the computer. Lift that edge to climb, lower it to descend, or lower either side to turn. Swipe up on the badge for **Center**, **Boost**, and **View**. **View** cycles overhead, chase, and helmet cameras. The game also supports keyboard play.

## Requirements

Motion control requires a Sugarat ESP32-S3 badge with a 360 × 360 round display and 32 MiB of flash, and a desktop browser with Web Bluetooth, such as Chrome. You can also fly with the keyboard.

## Make your own (做同款)

1. Download the attachment `sugarat-armor.tar.gz` of release `v0.3.8`: https://github.com/suoyousheng-bot/sugarat-hardware-kit/releases/download/v0.3.8/sugarat-armor.tar.gz. It is this folder plus every shared component it uses, including LVGL; nothing else needs to be downloaded. From a repository checkout, `python3 scripts/kit.py export-play --id sugarat-armor --directory <new-folder>` writes the same project.
2. Make the requested changes in that standalone project. Keep the components, partition table and license files; do not start from the basic template instead.
3. Raise the version in `version.txt` (and `PROJECT_VER` when CMakeLists.txt sets it).
4. Build with ESP-IDF 5.5.4 for `esp32s3`. The application image is `build/sugarat_armor.bin`; it is the only file to install.
5. Install the application image through the Sugarat desktop app or a launcher slot. Never change the device partition table or reuse a historical flash address.

Report compilation, desktop preview, written-byte verification, startup and physical behavior separately.

# Zodiac Ring

A daily horoscope dial: a spinning ring of text, your constellation drawn star by star and today's score in the center. Offline, in Chinese or English.

Community play: Sugarat 玩法社区 #51 (`sugarat_zodiac`), published version 0.1.1.
Source version in this directory: 0.1.1.

## About the play

Zodiac Ring turns the round badge into a daily horoscope dial. A ring of bold text turns slowly around the edge, colored by your sign's element: fire, earth, air or water.

**Reveal today's reading.** Tap the screen or pick the badge up. Your sign's constellation draws itself star by star, today's score counts up in the center, and the rim switches to today's line. After 20 seconds without activity the dial returns to its idle ring.

**Change your sign.** Long-press for one second until the ring fills. Drag the ring of 12 signs left or right, or tap a sign, and the center shows its constellation, name and dates. Use the 中 / EN pill to switch between Chinese and English. Tap the center to save, or wait 5 seconds and it saves itself. On first launch the sign picker opens directly.

**Works offline.** The reading is computed on the badge from your sign and the local date, so it needs no network and gives the same result all day. The date comes from the launcher's synced clock and time zone.

Runs as a launcher app: swipe down from the top edge for the control center, and use Exit to return to the launcher.

## Requirements

Sugarat badge with the Sugarat launcher (multi-app mode). No network needed; the date comes from the launcher's synced clock.

## Make your own (做同款)

1. Download the attachment `sugarat-zodiac.tar.gz` of release `v0.3.11`: https://github.com/suoyousheng-bot/sugarat-hardware-kit/releases/download/v0.3.11/sugarat-zodiac.tar.gz. It is this folder plus every shared component it uses, including LVGL; nothing else needs to be downloaded. From a repository checkout, `python3 scripts/kit.py export-play --id sugarat-zodiac --directory <new-folder>` writes the same project.
2. Make the requested changes in that standalone project. Keep the components, partition table and license files; do not start from the basic template instead.
3. Raise the version in `version.txt` (and `PROJECT_VER` when CMakeLists.txt sets it).
4. Build with ESP-IDF 5.5.4 for `esp32s3`. The application image is `build/sugarat_zodiac.bin`; it is the only file to install.
5. Install the application image through the Sugarat desktop app or a launcher slot. Never change the device partition table or reuse a historical flash address.

Report compilation, desktop preview, written-byte verification, startup and physical behavior separately.

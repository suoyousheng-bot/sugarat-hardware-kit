# Squad Talk

Same channel. Hold to talk. Talk with a group of nearby Sugarat round badges directly over ESP-NOW, even offline.

Community play: Sugarat 玩法社区 #29 (`squad_talk`), published version 0.2.0.
Source version in this directory: 0.2.0.

## About the play

# Squad Talk

Same channel. Hold to talk.

Start a nearby push-to-talk group with Sugarat round badges. Choose the same channel, wait for GROUP ONLINE, hold the large fingerprint button to speak, then release to listen. ESP-NOW carries voice directly between the badges without a router, phone, account or server.

## How to play

1. Install Squad Talk 0.2.0 on every badge in the group and turn them on.
2. Swipe left on the screen background for the next channel, or right for the previous channel. CH 01 to CH 08 wrap around. Start outside the fingerprint button and below the top control strip. Each boot starts on CH 01.
3. Wait for READY TO ROLL and GROUP ONLINE.
4. Hold HOLD TO TALK. YOU ARE ON AIR confirms transmission; release when finished.
5. Other badges on the same channel show SOMEONE TALKING and LISTENING while playing your voice. Take turns to reply.

The cream, lavender and lime interface has a bottom half-ellipse fingerprint touch target, clear speaking/listening states and audio activity bars. A swipe changes one channel on release. Taps, short drags and predominantly vertical drags do not change channels. A drag starting on the speech button cancels speech when it leaves the button and does not change channels. The fingerprint is a touch illustration, not a biometric reader. Swipe down from the top for the platform controls.

## Conversation behavior

One person speaks at a time. Each turn is limited to 30 seconds. Releasing or moving off the button, opening platform controls, or screen suspension cancels transmission; resuming requires a new press. Badges can join a channel while someone is speaking. Channels share the same 2.4 GHz radio channel and do not encrypt voice. Range has not been measured.

## Installation and verification

Version 0.2.0 uses the group voice protocol. Update every badge in the group to 0.2.0. Physical group size, audio quality and range are pending device tests.

## Credits

Application code: Apache-2.0. Includes the upstream platform foundation, Nunito glyph subsets under SIL OFL 1.1, and the retained MIT notice for the atomic14 ESP32 walkie-talkie reference. No audio recordings or Wi-Fi credentials are stored by this application.

## Requirements

Two or more Sugarat ESP32-S3 round badges with 360 x 360 touch displays, built-in microphones and speakers, and Squad Talk 0.2.0. Select the same channel on every device. No router, phone or internet connection is required for voice.

## Make your own (做同款)

1. Download the attachment `squad-talk.tar.gz` of release `v0.3.10`: https://github.com/suoyousheng-bot/sugarat-hardware-kit/releases/download/v0.3.10/squad-talk.tar.gz. It is this folder plus every shared component it uses, including LVGL; nothing else needs to be downloaded. From a repository checkout, `python3 scripts/kit.py export-play --id squad-talk --directory <new-folder>` writes the same project.
2. Make the requested changes in that standalone project. Keep the components, partition table and license files; do not start from the basic template instead.
3. Raise the version in `version.txt` (and `PROJECT_VER` when CMakeLists.txt sets it).
4. Build with ESP-IDF 5.5.4 for `esp32s3`. The application image is `build/squad_talk.bin`; it is the only file to install.
5. Install the application image through the Sugarat desktop app or a launcher slot. Never change the device partition table or reuse a historical flash address.

Report compilation, desktop preview, written-byte verification, startup and physical behavior separately.

# Sugarat Wolf Companion

A little wolf for the Sugarat ESP32-S3 round badge, shown at the native 360×360 resolution from 569 pre-rendered frames.

## Play

Tap: turn and think (?). Double tap or shake: dizzy with stars. Hold about 0.5 s: jump onto the sofa. Drag or tilt: the head follows. 30 s without input: doze (Z); tap or pick up to wake. Now and then, looking straight ahead, it grins.

## Rendering

- `artwork/frames/f000.jpg`–`f286.jpg` are the 287 original 360×360 baseline JPEG frames (4:2:0): clips 0–255 (grin, think, doze, wake, dizzy, sofa) and the gaze grid 256–286 (rows of 9, 13 and 9 poses).
- `tools/interpolate.py` (needs `opencv-python-headless` and `numpy`) inserts one optical-flow in-between frame after each original frame inside every continuous clip and gaze row, giving 569 frames in `artwork/frames_x2/`. Originals are copied byte for byte; in-betweens are JPEG quality 82. It also writes `main/wolf_frames.h` (original frame → pack index).
- `tools/build_pack.py` (needs `jpegtran`/`djpeg` from libjpeg-turbo and Pillow) repacks those frames losslessly (crop to content, optimized Huffman tables, a restart marker per 16-row MCU row) into `main/wolf_frames.pack` (4.49 MiB) and draws the overlay sprites (`main/wolf_sprites.bin`, `main/wolf_sprites.h`). It verifies with `djpeg -nosmooth` that every band decodes like the same rows of the whole frame.
- On the badge, both cores decode the two 16-row JPEG bands of each 32-row strip with `esp_new_jpeg` into internal RAM, and `sugarat_frame` streams only the changed rectangle to the panel on a fixed 30 fps grid.
- Clips and horizontal gaze moves step one frame per slot (15 original frames per second); gaze row changes and clip starts and ends crossfade.
- `main/apps/wolf_app.c` holds the interaction scripts, gaze, overlays and presentation; `main/wolf_pack.c` reads the pack and composes bands.

## Build and test

```sh
python3 tools/interpolate.py artwork/frames artwork/frames_x2 main/wolf_frames.h
python3 tools/build_pack.py artwork/frames_x2 main/wolf_frames.pack main/wolf_sprites.bin main/wolf_sprites.h
mkdir -p build-pack
cc -std=c11 -fsanitize=address,undefined -DWOLF_PACK_HOST -include string.h -Imain -I"$(brew --prefix jpeg-turbo)/include" \
   tests/host_compose.c main/wolf_pack.c -L"$(brew --prefix jpeg-turbo)/lib" -ljpeg -lz -o build-pack/host_compose
python3 tests/check_compose.py build-pack/host_compose
idf.py build
```

The host test composes views band by band and compares them with a reference decode of the frames, and checks that corrupt packs are rejected. The application image is `build/sugarat_wolf.bin` (about 5.5 MB).

## Serial console

`~` runs a 6 s gaze sweep benchmark, `j` taps, `k` holds, `m` shakes, `t` dozes and `n` wakes. Every 10 s the app logs `wolf: fps=… avg=… max=… late=… missed_slots=…`; `late` counts frames that took longer than the 33.3 ms slot. `python3 tools/verify_device.py <port>` resets the badge, sends every command and prints those lines.

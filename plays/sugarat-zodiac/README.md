# Zodiac Ring

A launcher app for the Sugarat round badge. A ring of bold text turns around the edge of the dial, and today's horoscope score sits in the center. Install it into a launcher slot and leave it through Exit in the control center.

## Interaction

- **Idle:** the rim turns "COME CLOSER · TODAY'S STARS" around the sign's glyph.
- **Reveal:** tap the screen or pick the badge up (gyro above 40 dps on two samples in a row). The constellation draws itself, the score counts up from 0, and the rim switches to today's line. After 20 s without activity the dial returns to idle.
- **Change sign:** long-press for 1 s until the progress ring fills. Drag the ring with inertia, or tap a sign. The 中 / EN pill switches language. Tap the center to confirm, or stop touching for 5 s to save automatically.
- First launch opens the picker. The top 50 px belong to the system control-center pull-down.

## Data

The reading is computed on the badge: the same sign and local date always give the same numbers, using the hash and formula of the design preview. The date comes from the system clock with the time zone the launcher stores in NVS `launcher_clock/utc_off`. Without a set clock the app falls back to the last date it saw, then to the build date. Sign and language are stored in NVS namespace `sg_zodiac` (keys `sign`, `lang`, `day`).

## Rendering

`zd_ui` (state machine) and `zd_render` (vector and glyph rasterizer) do not depend on ESP-IDF. Each frame becomes a draw list that `sugarat_frame` streams straight to the panel in 12-row strips; each strip is split into 4 column ranges rendered on both cores while the previous strip transfers. Animation runs on a 60 Hz grid and drops to an even 30 fps when a frame overruns; the idle dial runs at 30 fps. While the control center or another LVGL layer is visible, frames go through a PSRAM image and LVGL. The serial log prints `fps=… avg=… max=… gap=…` every 10 s.

`tools/gen_assets.py` rasterizes the OFL fonts in `assets/fonts` into A8 glyphs in `main/zodiac/zd_assets.{h,c}` with Pillow. Run it after changing on-screen text; the font files are not distributed with the source, so fetch them first with `tools/fetch_fonts.sh`.

## Build and test

```sh
idf.py build    # ESP-IDF 5.5.4, target esp32s3
# Replay a scripted session on a computer and write PPM frames of each state
cc -std=c11 -O2 -fsanitize=address,undefined -Imain/zodiac tests/host_render.c main/zodiac/zd_ui.c main/zodiac/zd_render.c main/zodiac/zd_fortune.c main/zodiac/zd_assets.c -lm -o zd_host && ./zd_host OUTDIR
```

Serial test keys: `z` open picker, `[` / `]` step one sign, `g` toggle language, `k` confirm, `v` simulate pick-up, `h` real long press, `w` swipe left, `p` screenshot.

Install only the application image into a launcher slot; never flash this project's partition table or bootloader over a badge that runs the launcher.

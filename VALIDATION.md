# Validation — 2026-11-27

Both projects were built from this standalone snapshot in a separate directory with ESP-IDF v5.5.4 and the ESP32-S3 toolchain. The original workspace was not modified. No device was connected, flashed or functionally tested during this upload.

| Check | Result |
| --- | --- |
| `idf.py -C firmware-plays/stage1/sugarat-reactor build` | PASS |
| `idf.py -C firmware-plays/stage1/sugarat-reactor-side build` | PASS |
| Existing host lighting tests, compiled with `cc -std=c11 -Wall -Wextra -Werror` | PASS |
| esptool v4.12.0 `image_info` for both delivered images | Checksum and validation hash valid |
| Delivered BIN comparison with original workspace BIN | SHA-256 identical |
| Credential and developer home-path pattern scan of shipped text | No matching files |

The host tests exercise controller binding and expiry, pixel phase rendering, and local style selection while following controller snapshots. Reproduce from the branch root:

```sh
cc -std=c11 -Wall -Wextra -Werror \
  -Ifirmware-plays/stage1/sugarat-reactor/shared \
  -Ifirmware-plays/stage1/sugarat-reactor-side/main \
  firmware-plays/stage1/sugarat-reactor/tests/lighting.c \
  firmware-plays/stage1/sugarat-reactor/shared/lighting_sync.c \
  firmware-plays/stage1/sugarat-reactor-side/main/light_pixels.c \
  firmware-plays/stage1/sugarat-reactor-side/main/lighting_selection.c \
  -o /tmp/reactor-lighting-test
/tmp/reactor-lighting-test
```

The delivered files in `firmware/` are the original prebuilt application images: main 0.2.0, 1,793,008 bytes; side 0.1.2, 2,936,912 bytes. Fresh compilation produced the same application versions at 1,793,024 and 2,936,928 bytes respectively. The fresh builds are not byte-identical to the original files and do not replace them. Successful compilation establishes buildability of the included snapshot, not reproducible binary identity or verified device behavior.

The first restricted build attempts were blocked by macOS process-enumeration access inside the ESP-IDF component manager. Both commands passed when rerun with host access. No firmware tests were bypassed.

The complete initial-import `git diff --cached --check` reports existing whitespace in copied LVGL, JPEG, board-support and partition-table files. These dependency files are retained byte-for-byte. The new root documentation and metadata pass the scoped whitespace check.

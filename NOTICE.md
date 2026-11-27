# Source credits and licenses

This branch contains Sugarat Reactor application and synchronization code by Deotaland, copied from the ThinkReady workspace on 2026-11-27. Existing file notices are retained. Deotaland application files carrying an SPDX identifier use Apache-2.0; this notice does not assign a new license to files or media without one.

The board support and launcher application interfaces derive from SDGOODS/SDGOODS-ESP32S3. Their original LICENSE, NOTICE, LICENSING.md and TRADEMARK.md are retained in `firmware-plays/stage1/sugarat-lunch/`. This branch vendors LVGL 8.3.11 locally under its MIT license (`components/lvgl/LICENCE.txt`), rather than relying on the registry download described in the original platform NOTICE. The JPEG encoder derives from bitbank2/JPEGENC under Apache-2.0; its source notices are retained. Font licensing is retained in `components/sdgoods_board/fonts/OFL.txt`. Component paths in this paragraph are relative to `firmware-plays/stage1/sugarat-lunch/`.

Embedded RGB565, phase-map and WAV assets are copied from the existing application projects without alteration. Code license notices do not establish separate media or trademark rights. ESP-IDF and its toolchain are external build prerequisites and are not included.

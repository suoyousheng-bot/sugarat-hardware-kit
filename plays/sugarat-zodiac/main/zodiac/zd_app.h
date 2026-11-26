/* Zodiac Ring application: LVGL screen, touch and motion input, persistence, and direct panel
 * streaming of the zd_ui scenes. Runs on the LVGL task. */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/** Creates the screen on first use and shows it. */
void zd_app_show(void);
/** Platform poll hook; animation runs from the screen's own LVGL timer. */
void zd_app_poll(void);

#ifdef __cplusplus
}
#endif

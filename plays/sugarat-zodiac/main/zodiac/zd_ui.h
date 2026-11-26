/* Zodiac Ring screen logic: the dial (idle / revealed), long-press zodiac picker, language switch and
 * auto-save. Pure C with no ESP-IDF or LVGL dependency; the caller feeds touch, motion and time, and
 * receives one or two scenes per frame. Times are seconds on a monotonic clock. */
#pragma once
#include <stdbool.h>
#include "zd_fortune.h"
#include "zd_render.h"

#ifdef __cplusplus
extern "C" {
#endif

enum { ZD_LANG_ZH = 0, ZD_LANG_EN = 1 };
typedef enum { ZD_TOUCH_DOWN, ZD_TOUCH_MOVE, ZD_TOUCH_UP } zd_touch_t;

/** Called when the user confirms a sign (and the language chosen with it). */
typedef void (*zd_save_fn)(int sign, int lang, void *arg);

typedef struct {
    int sign, lang;
    zd_date_t date;
    zd_reading_t reading;
    zd_save_fn save;
    void *save_arg;

    /* dial */
    float face_q;                 /**< reveal progress, eased into the dial animation */
    bool face_near;
    double last_activity;
    bool face_press;
    double press_t0;
    float press_k;                /**< long-press ring, 0..1 */

    /* picker */
    float m;                      /**< dial → picker cross-fade, 0..1 */
    bool picker;
    float rot, rot_target;        /**< ring position in sign steps */
    int sel_idx;
    float const_k, lang_k;
    bool dragging, down, moved;
    float down_x, down_y, down_rot, last_x, vel;
    double last_t, last_touch, saving_t;

    zd_rim_t rim_idle, rim_near;
    int rim_sign, rim_lang, rim_line;
    zd_const_layer_t face_const, pick_const;
} zd_ui_t;

/** Prepares the screen. first_run opens the picker immediately. False when the constellation layers
 *  could not be allocated. */
bool zd_ui_init(zd_ui_t *u, int sign, int lang, bool first_run, zd_date_t date, zd_save_fn save, void *arg, double now);
/** Switches the reading to another local date (midnight rollover). */
void zd_ui_set_date(zd_ui_t *u, zd_date_t date);
void zd_ui_touch(zd_ui_t *u, zd_touch_t kind, float x, float y, double now);
/** Drops the current press without acting on it (the system took the screen). */
void zd_ui_touch_cancel(zd_ui_t *u);
/** The badge moved enough to count as someone looking at it: reveal the dial. */
void zd_ui_motion(zd_ui_t *u, double now);
/** Test hooks matching the serial keys: open the picker, step the ring, toggle language, confirm. */
void zd_ui_open_picker(zd_ui_t *u, double now);
void zd_ui_step(zd_ui_t *u, int delta, double now);
void zd_ui_toggle_lang(zd_ui_t *u, double now);
void zd_ui_confirm(zd_ui_t *u, double now);

void zd_ui_update(zd_ui_t *u, double now, float dt);
/** True while something follows a finger or a transition runs; the caller then paces at 60 Hz. */
bool zd_ui_fast(const zd_ui_t *u);
/** Builds this frame. *mix is the picker's share 0..256: 0 needs only face, 256 only picker. */
void zd_ui_build(zd_ui_t *u, double now, zd_scene_t *face, zd_scene_t *picker, int *mix);

#ifdef __cplusplus
}
#endif

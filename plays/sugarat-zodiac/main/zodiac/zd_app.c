#include "zd_app.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "lvgl.h"
#include "nvs.h"
#include "sdgoods_board.h"
#include "sdgoods_nvs.h"
#include "sdgoods_app_shell.h"
#include "sdgoods_cc.h"
#include "sdgoods_screenshot.h"
#include "sugarat_frame.h"
#include "zd_ui.h"

static const char *TAG = "zodiac";

#define NVS_NS "sg_zodiac"
#define STRIP_ROWS 12
#define PARTS 4                     /* column slices per strip shared by both cores */
#define SLOW_SLOT_US 33333          /* idle dial: rim text turns at 30 fps */
#define MOTION_PERIOD_US 50000
#define MOTION_DPS 40.f             /* gyro magnitude that counts as the badge being picked up */
#define DATE_PERIOD_US (30 * 1000000LL)
#define VALID_EPOCH 1704067200      /* clocks before 2024 were never set */
#define SHELL_ZONE_Y 50             /* top band belongs to the control-center pull-down */

static zd_ui_t *s_ui;
static zd_scene_t *s_face, *s_pick;
static int s_mix;
static uint16_t *s_fb;              /* LVGL fallback image, PSRAM */
static uint16_t *s_over;            /* cross-fade scratch strip */
static lv_img_dsc_t s_dsc;
static lv_obj_t *s_screen, *s_img, *s_touch;
static lv_timer_t *s_timer;
static sugarat_frame_stream_t *s_stream;
static sugarat_frame_pacer_t s_pacer;
static sugarat_frame_stats_t s_stats;
static int64_t s_send_us;
static bool s_paused, s_failed, s_touching;
static uint32_t s_screen_children;
static double s_last_s;
static int64_t s_next_motion_us, s_next_date_us;
static int s_motion_hits;

static double now_s(void) { return esp_timer_get_time() / 1e6; }

/* ---- persistence and date ---- */

static void save_choice(int sign, int lang, void *arg)
{
    (void)arg;
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) {
        ESP_LOGW(TAG, "NVS unavailable; sign %d lang %d kept until power off", sign, lang);
        return;
    }
    nvs_set_u8(h, "sign", (uint8_t)sign);
    nvs_set_u8(h, "lang", (uint8_t)lang);
    const esp_err_t err = nvs_commit(h);
    nvs_close(h);
    ESP_LOGI(TAG, "saved sign=%d lang=%d (%s)", sign, lang, esp_err_to_name(err));
}

static void apply_launcher_timezone(void)
{
    /* The launcher stores its IP-derived UTC offset here and syncs the clock over SNTP; the system
     * time survives the software reset into this app. */
    nvs_handle_t h;
    int32_t off = 0;
    if (nvs_open("launcher_clock", NVS_READONLY, &h) != ESP_OK) return;
    const bool found = nvs_get_i32(h, "utc_off", &off) == ESP_OK;
    nvs_close(h);
    if (!found) return;
    const int west = -off, a = abs(west);
    char tz[24];
    snprintf(tz, sizeof(tz), "UTC%c%d:%02d", west < 0 ? '-' : '+', a / 3600, (a % 3600) / 60);
    setenv("TZ", tz, 1);
    tzset();
}

static zd_date_t build_date(void)
{
    static const char months[] = "JanFebMarAprMayJunJulAugSepOctNovDec";
    const char *d = __DATE__;   /* "Mmm dd yyyy" */
    zd_date_t out = {atoi(d + 7), 1, atoi(d + 4)};
    for (int i = 0; i < 12; i++) {
        if (!strncmp(d, months + i * 3, 3)) out.month = i + 1;
    }
    return out;
}

/* Today's local date; without a set clock, the last date this app saw, else the build date. */
static zd_date_t current_date(bool *from_clock)
{
    const time_t now = time(NULL);
    nvs_handle_t h;
    zd_date_t date;
    if (now >= VALID_EPOCH) {
        struct tm tm;
        localtime_r(&now, &tm);
        date = (zd_date_t){tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday};
        *from_clock = true;
        if (nvs_open(NVS_NS, NVS_READWRITE, &h) == ESP_OK) {
            uint32_t stored = 0;
            const uint32_t key = (uint32_t)(date.year * 10000 + date.month * 100 + date.day);
            if (nvs_get_u32(h, "day", &stored) != ESP_OK || stored != key) {
                nvs_set_u32(h, "day", key);
                nvs_commit(h);
            }
            nvs_close(h);
        }
        return date;
    }
    *from_clock = false;
    uint32_t stored = 0;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) == ESP_OK) {
        nvs_get_u32(h, "day", &stored);
        nvs_close(h);
    }
    if (stored) return (zd_date_t){(int)(stored / 10000), (int)(stored / 100 % 100), (int)(stored % 100)};
    return build_date();
}

/* ---- rendering ---- */

typedef struct { uint16_t *pixels; int x1, y, x2, rows; } strip_t;
static strip_t s_strip;

static void render_region(uint16_t *out, int stride, int ox, int y, int rows, int c0, int c1)
{
    if (s_mix < 256) zd_render_scene(s_face, out, stride, ox, y, rows, c0, c1);
    if (s_mix >= 256) {
        zd_render_scene(s_pick, out, stride, ox, y, rows, c0, c1);
    } else if (s_mix > 0) {
        zd_render_scene(s_pick, s_over, stride, ox, y, rows, c0, c1);
        zd_blend_region(out, s_over, stride, ox, rows, c0, c1, s_mix);
    }
}

static void render_part(int index, void *arg)
{
    (void)arg;
    const int w = s_strip.x2 - s_strip.x1;
    render_region(s_strip.pixels, w, s_strip.x1, s_strip.y, s_strip.rows, w * index / PARTS, w * (index + 1) / PARTS);
}

static void fill_strip(uint16_t *pixels, int x1, int y, int x2, int rows, void *arg)
{
    (void)arg;
    s_strip = (strip_t){pixels, x1, y, x2, rows};
    sugarat_frame_parallel(PARTS, render_part, NULL);
}

static void render_to_image(void)
{
    for (int y = 0; y < ZD_SCREEN; y += STRIP_ROWS) {
        const int rows = y + STRIP_ROWS > ZD_SCREEN ? ZD_SCREEN - y : STRIP_ROWS;
        fill_strip(s_fb + y * ZD_SCREEN, 0, y, ZD_SCREEN, rows, NULL);
    }
    lv_img_cache_invalidate_src(&s_dsc);
    lv_obj_invalidate(s_img);
}

static void build(double now)
{
    zd_ui_build(s_ui, now, s_face, s_pick, &s_mix);
    if (s_face->overflow || s_pick->overflow) ESP_LOGW(TAG, "scene full; some drawing was dropped");
}

static void poll_motion(int64_t now_us)
{
    if (now_us < s_next_motion_us) return;
    s_next_motion_us = now_us + MOTION_PERIOD_US;
    int x, y, z;
    if (!sdgoods_hw_gyro(&x, &y, &z)) return;
    const float mag = sqrtf((float)(x * x + y * y + z * z));
    s_motion_hits = mag > MOTION_DPS ? s_motion_hits + 1 : 0;
    if (s_motion_hits == 2) zd_ui_motion(s_ui, now_us / 1e6);
}

static void poll_date(int64_t now_us)
{
    if (now_us < s_next_date_us) return;
    s_next_date_us = now_us + DATE_PERIOD_US;
    bool from_clock;
    const zd_date_t d = current_date(&from_clock);
    if (d.year != s_ui->date.year || d.month != s_ui->date.month || d.day != s_ui->date.day) {
        ESP_LOGI(TAG, "date %04d-%02d-%02d (%s)", d.year, d.month, d.day, from_clock ? "clock" : "fallback");
        zd_ui_set_date(s_ui, d);
    }
}

static void frame_tick(lv_timer_t *timer)
{
    (void)timer;
    if (!s_screen || lv_scr_act() != s_screen || s_paused || s_failed) return;
    const int64_t start = esp_timer_get_time();
    poll_motion(start);
    poll_date(start);
    if (!sugarat_frame_pacer_due(&s_pacer, start)) return;
    const double now = start / 1e6;
    float dt = (float)(now - s_last_s);
    if (dt > .1f) dt = .1f;
    s_last_s = now;
    zd_ui_update(s_ui, now, dt);
    build(now);
    bool direct = false;
    /* A direct strip overwrites whatever LVGL showed there, so stream only while nothing LVGL draws
     * covers the dial. */
    const bool can_stream = s_stream && lv_obj_get_child_cnt(s_screen) == s_screen_children &&
                            sugarat_frame_lvgl_layers_clear() && !sdgoods_cc_is_open();
    if (can_stream) {
        const esp_err_t err = sugarat_frame_stream_send(s_stream, 0, 0, ZD_SCREEN, ZD_SCREEN, fill_strip, NULL);
        direct = err == ESP_OK;
        if (err == ESP_ERR_TIMEOUT || err == ESP_ERR_INVALID_STATE) {
            ESP_LOGE(TAG, "panel stream stopped (%s); drawing through LVGL from now on", esp_err_to_name(err));
            s_stream = NULL;
        }
        s_send_us += s_stream ? sugarat_frame_stream_take_send_us(s_stream) : 0;
    }
    if (!direct) render_to_image();
    const int64_t done = esp_timer_get_time();
    s_pacer.slot_us = zd_ui_fast(s_ui) ? SUGARAT_FRAME_SLOT_60HZ_US : SLOW_SLOT_US;
    sugarat_frame_pacer_finish(&s_pacer, done, true);
    sugarat_frame_stats_record(&s_stats, start, done, true, direct);
    sugarat_frame_summary_t sum;
    if (sugarat_frame_stats_window(&s_stats, done, 10 * 1000000LL, &sum)) {
        ESP_LOGI(TAG, "fps=%.1f frames=%lu direct=%lu avg=%lld us max=%lld us gap=%lld us panel=%lld us/frame",
                 sum.fps, (unsigned long)sum.frames, (unsigned long)sum.direct_frames, (long long)sum.avg_us,
                 (long long)sum.max_us, (long long)sum.gap_max_us, (long long)(sum.frames ? s_send_us / sum.frames : 0));
        s_send_us = 0;
    }
}

/* ---- input and lifecycle ---- */

static void on_touch(lv_event_t *e)
{
    const lv_event_code_t code = lv_event_get_code(e);
    lv_indev_t *indev = lv_indev_get_act();
    if (!indev) return;
    lv_point_t p;
    lv_indev_get_point(indev, &p);
    const double now = now_s();
    if (code == LV_EVENT_PRESSED) {
        s_touching = !s_paused && p.y >= SHELL_ZONE_Y;
        if (!s_touching) return;
    }
    if (!s_touching) return;
    if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) s_touching = false;
    if (code == LV_EVENT_PRESSED) zd_ui_touch(s_ui, ZD_TOUCH_DOWN, p.x, p.y, now);
    else if (code == LV_EVENT_PRESSING) zd_ui_touch(s_ui, ZD_TOUCH_MOVE, p.x, p.y, now);
    else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) zd_ui_touch(s_ui, ZD_TOUCH_UP, p.x, p.y, now);
}

static void on_pause(void)
{
    /* The control center draws over this screen through LVGL: leave it the latest frame. */
    s_paused = true;
    if (s_touching) {
        s_touching = false;
        zd_ui_touch_cancel(s_ui);
    }
    build(now_s());
    render_to_image();
    sdgoods_lvgl_fast_loop(false);
}

static void on_resume(void)
{
    s_paused = false;
    s_last_s = now_s();
    sugarat_frame_pacer_reset(&s_pacer);
    sdgoods_lvgl_fast_loop(true);
}

static void on_app_exit(void)
{
    sdgoods_lvgl_fast_loop(false);
}

static bool create(void)
{
    s_ui = heap_caps_calloc(1, sizeof(*s_ui), MALLOC_CAP_SPIRAM);
    s_face = heap_caps_malloc(sizeof(*s_face), MALLOC_CAP_SPIRAM);
    s_pick = heap_caps_malloc(sizeof(*s_pick), MALLOC_CAP_SPIRAM);
    s_fb = heap_caps_malloc(ZD_SCREEN * ZD_SCREEN * 2, MALLOC_CAP_SPIRAM);
    s_over = heap_caps_malloc(ZD_SCREEN * STRIP_ROWS * 2, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!s_over) s_over = heap_caps_malloc(ZD_SCREEN * STRIP_ROWS * 2, MALLOC_CAP_SPIRAM);
    if (!s_ui || !s_face || !s_pick || !s_fb || !s_over) {
        ESP_LOGE(TAG, "out of memory for the screen buffers");
        return false;
    }

    sdgoods_nvs_ensure();
    apply_launcher_timezone();
    nvs_handle_t h;
    uint8_t sign = 0, lang = ZD_LANG_EN;
    bool first_run = true;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) == ESP_OK) {
        first_run = nvs_get_u8(h, "sign", &sign) != ESP_OK || sign >= ZD_SIGNS;
        if (nvs_get_u8(h, "lang", &lang) != ESP_OK || lang > ZD_LANG_EN) lang = ZD_LANG_EN;
        nvs_close(h);
    }
    if (first_run) sign = 0;
    bool from_clock;
    const zd_date_t date = current_date(&from_clock);
    ESP_LOGI(TAG, "sign=%d lang=%d first_run=%d date %04d-%02d-%02d (%s)", sign, lang, first_run, date.year, date.month,
             date.day, from_clock ? "clock" : "fallback");
    const double now = now_s();
    if (!zd_ui_init(s_ui, sign, lang, first_run, date, save_choice, NULL, now)) {
        ESP_LOGE(TAG, "out of memory for the constellation layers");
        return false;
    }
    s_last_s = now;
    s_next_date_us = esp_timer_get_time() + DATE_PERIOD_US;

    sugarat_frame_workers_config_t workers = sugarat_frame_workers_default_config();
    esp_err_t err = sugarat_frame_workers_start(&workers);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) ESP_LOGW(TAG, "render helper unavailable (%s)", esp_err_to_name(err));
    sugarat_frame_stream_config_t config = sugarat_frame_stream_default_config();
    config.rows = STRIP_ROWS;
    err = sugarat_frame_stream_create(&config, &s_stream);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "panel stream unavailable (%s); drawing through LVGL", esp_err_to_name(err));
        s_stream = NULL;
    } else {
        ESP_LOGI(TAG, "panel stream ready with %d strip buffer(s)", sugarat_frame_stream_buffers(s_stream));
    }
    sugarat_frame_pacer_init(&s_pacer, SUGARAT_FRAME_SLOT_60HZ_US);

    s_screen = lv_obj_create(NULL);
    lv_obj_remove_style_all(s_screen);
    lv_obj_set_style_bg_color(s_screen, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);
    s_dsc = (lv_img_dsc_t){.header = {.cf = LV_IMG_CF_TRUE_COLOR, .w = ZD_SCREEN, .h = ZD_SCREEN},
                           .data_size = ZD_SCREEN * ZD_SCREEN * 2, .data = (const uint8_t *)s_fb};
    s_img = lv_img_create(s_screen);
    lv_obj_clear_flag(s_img, LV_OBJ_FLAG_CLICKABLE);
    lv_img_set_src(s_img, &s_dsc);
    s_touch = lv_obj_create(s_screen);
    lv_obj_remove_style_all(s_touch);
    lv_obj_set_size(s_touch, ZD_SCREEN, ZD_SCREEN);
    lv_obj_clear_flag(s_touch, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_touch, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_PRESS_LOCK);
    lv_obj_add_event_cb(s_touch, on_touch, LV_EVENT_ALL, NULL);
    s_screen_children = lv_obj_get_child_cnt(s_screen);

    build(now);
    render_to_image();
    s_timer = lv_timer_create(frame_tick, 1, NULL);
    return true;
}

void zd_app_show(void)
{
    const bool created = s_screen != NULL;
    if (!created && !create()) {
        s_failed = true;
        lv_obj_t *scr = lv_obj_create(NULL);
        lv_obj_t *label = lv_label_create(scr);
        lv_label_set_text_static(label, "Zodiac Ring needs more memory.\nRestart the badge.");
        lv_obj_center(label);
        lv_scr_load(scr);
        sdgoods_app_shell_bind(scr);
        return;
    }
    if (lv_scr_act() != s_screen) lv_scr_load(s_screen);
    sdgoods_app_shell_bind(s_screen);
    sdgoods_app_shell_set_pause_cb(on_pause);
    sdgoods_app_shell_set_resume_cb(on_resume);
    sdgoods_app_shell_set_exit_cb(on_app_exit);
    on_resume();
}

void zd_app_poll(void) {}

/* ---- serial test keys (USB console), run on the LVGL task ---- */

static void on_key(void *arg)
{
    if (!s_ui) return;
    const char c = (char)(intptr_t)arg;
    const double now = now_s();
    switch (c) {
    case 'z': zd_ui_open_picker(s_ui, now); break;
    case ']': zd_ui_step(s_ui, 1, now); break;
    case '[': zd_ui_step(s_ui, -1, now); break;
    case 'g': zd_ui_toggle_lang(s_ui, now); break;
    case 'k': zd_ui_confirm(s_ui, now); break;
    case 'v': zd_ui_motion(s_ui, now); break;
    case 'h': sdgoods_tap_synth_hold(ZD_SCREEN / 2, 200, 1300); break;   /* real long press through touch */
    case 'w': sdgoods_tap_synth(250, 200, -120, 0, 12); break;           /* swipe left on the picker */
    case '1': sdgoods_tap_synth(ZD_SCREEN / 2, ZD_SCREEN / 2, 0, 0, 1); break;
    case 'p':
        build(now);
        render_to_image();
        sdgoods_screenshot_capture();
        break;
    default: break;
    }
}

void sdgoods_console_ext_cmd(char c)
{
    sdgoods_lvgl_post(on_key, (void *)(intptr_t)c);
}

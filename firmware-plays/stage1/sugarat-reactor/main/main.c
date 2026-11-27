/* Copyright (c) 2026 Deotaland. SPDX-License-Identifier: Apache-2.0 */
#include <stdlib.h>

#include "driver/gpio.h"
#include "esp_log.h"
#include "lighting_tx.h"
#include "manual_control.h"
#include "reactor_transition.h"
#include "sdgoods_board.h"
#include "sdgoods_cc.h"
#include "servo_radio.h"
#include "sugarat_audio.h"

static const char *TAG = "sugarat_reactor";

extern const uint8_t open_art[] asm("_binary_reactor_open_rgb565_start");
extern const uint8_t close_art[] asm("_binary_reactor_close_rgb565_start");
extern const uint8_t ignition[] asm("_binary_reactor_open_wav_start");
extern const uint8_t ignition_end[] asm("_binary_reactor_open_wav_end");
extern const uint8_t discharge[] asm("_binary_reactor_close_wav_start");
extern const uint8_t discharge_end[] asm("_binary_reactor_close_wav_end");

_Static_assert(LCD_WIDTH == 360 && LCD_HEIGHT == 360, "Reactor art uses the 360-pixel round panel");
_Static_assert(LV_COLOR_DEPTH == 16 && LV_COLOR_16_SWAP, "Reactor art uses big-endian RGB565");

static const lv_img_dsc_t OPEN_ART = {
    .header = {.cf = LV_IMG_CF_TRUE_COLOR, .w = LCD_WIDTH, .h = LCD_HEIGHT},
    .data_size = LCD_WIDTH * LCD_HEIGHT * 2,
    .data = open_art,
};
static const lv_img_dsc_t CLOSE_ART = {
    .header = {.cf = LV_IMG_CF_TRUE_COLOR, .w = LCD_WIDTH, .h = LCD_HEIGHT},
    .data_size = LCD_WIDTH * LCD_HEIGHT * 2,
    .data = close_art,
};

enum { FRAME_MS = 32, SWIPE_MIN_PX = 52, FAILURE_NOTICE_MS = 2000, STYLE_NOTICE_MS = 1100 };
enum { MASK_CLOSED, MASK_OPEN };

static const lv_point_t SPARK_ORIGINS[] = {{163, 164}, {193, 171}, {172, 177}, {204, 159}, {154, 184}};

static struct {
    manual_control_view_t receiver;
    screen_action_t swipe;
    lv_obj_t *lit_art;
    lv_obj_t *notice;
    lv_obj_t *sparks[sizeof(SPARK_ORIGINS) / sizeof(*SPARK_ORIGINS)];
    const char *shown_notice;
    lv_point_t touch_start;
    bool tracking_touch;
    bool audio_ready;
    uint8_t light;
    lighting_style_t style;
    bool style_notice;
    uint32_t style_changed_at;
    uint32_t last_frame;
    uint32_t missed_at;
    bool missed_command;
    struct {
        bool active;
        uint8_t target;
        uint8_t initial_light;
        uint32_t started_at;
    } effect;
} reactor = {.style = LIGHTING_DEFAULT_STYLE};

static const char *style_label(lighting_style_t style)
{
    switch (style) {
    case LIGHTING_CORE: return SDG_T("核心点火", "Core ignition");
    case LIGHTING_RING: return SDG_T("分段光环", "Segmented ring");
    case LIGHTING_ARCS: return SDG_T("双弧流光", "Twin arcs");
    default: abort();
    }
}

static void swipe_event(lv_event_t *event)
{
    const lv_event_code_t code = lv_event_get_code(event);
    if (code == LV_EVENT_PRESS_LOST) {
        reactor.tracking_touch = false;
        return;
    }
    if (code != LV_EVENT_PRESSED && code != LV_EVENT_RELEASED) return;
    lv_indev_t *input = lv_indev_get_act();
    if (!input || lv_indev_get_type(input) != LV_INDEV_TYPE_POINTER) return;

    lv_point_t point;
    lv_indev_get_point(input, &point);
    if (code == LV_EVENT_PRESSED) {
        reactor.touch_start = point;
        reactor.tracking_touch = true;
        return;
    }
    if (!reactor.tracking_touch) return;
    reactor.tracking_touch = false;

    const int dx = point.x - reactor.touch_start.x;
    const int dy = point.y - reactor.touch_start.y;
    if (abs(dx) >= SWIPE_MIN_PX && abs(dx) > abs(dy)) {
        const unsigned advance = dx < 0 ? 1 : LIGHTING_STYLE_COUNT - 1;
        reactor.style = (reactor.style + advance) % LIGHTING_STYLE_COUNT;
        reactor.style_changed_at = lv_tick_get();
        reactor.style_notice = true;
    } else if (abs(dy) >= SWIPE_MIN_PX && abs(dy) > abs(dx)) {
        reactor.swipe = (screen_action_t){.kind = SCREEN_SET_TARGET, .value = dy < 0 ? MASK_OPEN : MASK_CLOSED};
    }
}

static void begin_effect(uint8_t target, uint32_t now)
{
    reactor.effect.active = true;
    reactor.effect.target = target;
    reactor.effect.initial_light = reactor.light;
    reactor.effect.started_at = now;
    reactor.missed_command = false;
    if (reactor.audio_ready) {
        const esp_err_t err = target == MASK_OPEN
            ? sugarat_audio_play_wav(ignition, (size_t)(ignition_end - ignition))
            : sugarat_audio_play_wav(discharge, (size_t)(discharge_end - discharge));
        if (err != ESP_OK) ESP_LOGW(TAG, "Could not play mask cue: %s", esp_err_to_name(err));
    }
}

static uint32_t cue_duration(void)
{
    return reactor.effect.target == MASK_OPEN ? REACTOR_IGNITION_MS : REACTOR_DISCHARGE_MS;
}

static void render_sparks(uint32_t now)
{
    const bool opening = reactor.effect.target == MASK_OPEN;
    const uint32_t elapsed = now - reactor.effect.started_at;
    for (unsigned i = 0; i < sizeof(SPARK_ORIGINS) / sizeof(*SPARK_ORIGINS); ++i) {
        lv_obj_t *spark = reactor.sparks[i];
        const unsigned start = (opening ? 62 : 365) + i * 16;
        enum { RISE_MS = 60, SPARK_MS = 240 };
        if (!reactor.effect.active || elapsed < start || elapsed >= start + SPARK_MS) {
            if (!lv_obj_has_flag(spark, LV_OBJ_FLAG_HIDDEN)) lv_obj_add_flag(spark, LV_OBJ_FLAG_HIDDEN);
            continue;
        }

        const unsigned age = elapsed - start;
        const int travel = (int)(age * (38 + i * 6) / SPARK_MS);
        const lv_opa_t alpha = age < RISE_MS ? 180 * age / RISE_MS
                                           : 180 * (SPARK_MS - age) / (SPARK_MS - RISE_MS);
        lv_obj_set_pos(spark, SPARK_ORIGINS[i].x, SPARK_ORIGINS[i].y + (opening ? -travel : travel));
        lv_obj_set_style_bg_color(spark, lv_color_hex(opening ? 0xc0f6ff : 0xffb267), 0);
        lv_obj_set_style_bg_opa(spark, alpha, 0);
        lv_obj_clear_flag(spark, LV_OBJ_FLAG_HIDDEN);
    }
}

static void render_frame(uint32_t now)
{
    const manual_control_view_t *receiver = &reactor.receiver;
    uint8_t wanted = 0;
    if (receiver->online && receiver->position == MASK_OPEN) {
        wanted = receiver->moving ? 210 : UINT8_MAX;
    } else if (receiver->online && receiver->moving) {
        wanted = 45;
    }

    if (reactor.effect.active) {
        const bool at_target = receiver->online && receiver->position == reactor.effect.target;
        const bool settled = at_target && !receiver->pending && !receiver->moving;
        const uint32_t elapsed = now - reactor.effect.started_at;
        if (!receiver->pending && !at_target) {
            reactor.effect.active = false;
            reactor.missed_command = true;
            reactor.missed_at = now;
        } else {
            wanted = reactor_light_at(reactor.effect.initial_light, reactor.effect.target == MASK_OPEN, elapsed, settled);
            if (settled && elapsed >= cue_duration() && reactor.light == wanted) reactor.effect.active = false;
        }
    }

    const uint8_t light = reactor_light_step(reactor.light, wanted);
    if (light != reactor.light) {
        reactor.light = light;
        lv_obj_set_style_img_opa(reactor.lit_art, light, 0);
    }

    render_sparks(now);

    const char *notice = NULL;
    if (reactor.style_notice && now - reactor.style_changed_at >= STYLE_NOTICE_MS) reactor.style_notice = false;
    if (reactor.style_notice) {
        notice = style_label(reactor.style);
    } else if (!receiver->online) {
        notice = SDG_T("正在寻找面罩…", "Finding mask...");
    } else if (reactor.missed_command && now - reactor.missed_at < FAILURE_NOTICE_MS) {
        notice = SDG_T("面罩未确认，请重试", "No confirmation. Swipe again.");
    }
    if (notice != reactor.shown_notice) {
        reactor.shown_notice = notice;
        if (notice) {
            lv_label_set_text_static(reactor.notice, notice);
            lv_obj_clear_flag(reactor.notice, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(reactor.notice, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

static void poll_reactor(void)
{
    const uint32_t now = lv_tick_get();
    screen_action_t action = {.kind = SCREEN_NONE};
    if (!reactor.receiver.pending &&
        (!reactor.effect.active || now - reactor.effect.started_at >= cue_duration()) &&
        reactor.swipe.kind == SCREEN_SET_TARGET && reactor.swipe.value != reactor.receiver.position) {
        action = reactor.swipe;
    }
    reactor.swipe.kind = SCREEN_NONE;

    manual_control_update(true, false, action, &reactor.receiver);
    if (action.kind == SCREEN_SET_TARGET && reactor.receiver.pending) begin_effect(action.value, now);

    if (now - reactor.last_frame >= FRAME_MS) {
        reactor.last_frame = now;
        render_frame(now);
    }
    lighting_tx_poll(now, reactor.style, reactor.light, reactor.effect.active || reactor.receiver.moving);
}

static void show_reactor(void)
{
    lv_obj_t *screen = lv_scr_act();
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(screen, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_PRESS_LOCK);
    lv_obj_set_style_bg_color(screen, lv_color_black(), 0);

    lv_obj_t *dark_art = lv_img_create(screen);
    lv_img_set_src(dark_art, &CLOSE_ART);
    reactor.lit_art = lv_img_create(screen);
    lv_img_set_src(reactor.lit_art, &OPEN_ART);
    lv_obj_set_style_img_opa(reactor.lit_art, LV_OPA_TRANSP, 0);

    for (unsigned i = 0; i < sizeof(SPARK_ORIGINS) / sizeof(*SPARK_ORIGINS); ++i) {
        lv_obj_t *spark = lv_obj_create(screen);
        lv_obj_remove_style_all(spark);
        lv_obj_set_size(spark, 2, 5 + i % 3);
        lv_obj_set_style_radius(spark, LV_RADIUS_CIRCLE, 0);
        lv_obj_clear_flag(spark, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_flag(spark, LV_OBJ_FLAG_HIDDEN);
        reactor.sparks[i] = spark;
    }

    reactor.notice = lv_label_create(screen);
    lv_obj_set_style_text_color(reactor.notice, lv_color_hex(0xe3f4ff), 0);
    lv_obj_set_style_bg_color(reactor.notice, lv_color_hex(0x0c161f), 0);
    lv_obj_set_style_bg_opa(reactor.notice, LV_OPA_90, 0);
    lv_obj_set_style_radius(reactor.notice, 12, 0);
    lv_obj_set_style_pad_hor(reactor.notice, 8, 0);
    lv_obj_set_style_pad_ver(reactor.notice, 4, 0);
    lv_obj_align(reactor.notice, LV_ALIGN_TOP_MID, 0, 55);
    render_frame(lv_tick_get());

    lv_obj_add_event_cb(screen, swipe_event, LV_EVENT_ALL, NULL);
    sdgoods_app_shell_bind(screen);
    sdgoods_apps_set_poll(poll_reactor);
}

/** Start the round badge remote with the shared control center and launcher exit. */
void app_main(void)
{
    const gpio_config_t power = {.pin_bit_mask = 1ULL << BOARD_BAT_CONTROL_GPIO, .mode = GPIO_MODE_OUTPUT};
    ESP_ERROR_CHECK(gpio_config(&power));
    ESP_ERROR_CHECK(gpio_set_level(BOARD_BAT_CONTROL_GPIO, BOARD_BAT_CONTROL_LATCH_LEVEL));
    sdgoods_lcd_init_panel();
    sdgoods_lcd_apply_vendor_madctl();
    sdgoods_key_init();
    sdg_i18n_init();
    sdgoods_lvgl_init();
    ESP_ERROR_CHECK(sdgoods_touch_init());

    const esp_err_t audio = sugarat_audio_init(NULL);
    reactor.audio_ready = audio == ESP_OK;
    if (!reactor.audio_ready) ESP_LOGW(TAG, "Speaker unavailable: %s", esp_err_to_name(audio));
    manual_control_init(false);
    show_reactor();
    lv_refr_now(NULL);
    sdgoods_app_shell_init();
    sdgoods_lcd_set_backlight(sdgoods_cc_brightness_get());
    ESP_LOGI(TAG, "Reactor ready");
    sdgoods_lvgl_loop();
}

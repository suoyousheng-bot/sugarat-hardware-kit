/* Copyright (c) 2026 Deotaland. SPDX-License-Identifier: Apache-2.0 */
#include <stdlib.h>
#include <string.h>

#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_now.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "light_pixels.h"
#include "lighting_art.h"
#include "lighting_selection.h"
#include "sdgoods_board.h"
#include "sdgoods_cc.h"

enum { FRAME_MS = 30, FADE_MS = 140, DISCONNECT_FADE_MS = 300, JOIN_FADE_MS = 140 };
enum { SWIPE_MIN_PX = 52, STYLE_NOTICE_MS = 1100, FULL_EXPOSURE_LEVEL = 64 };

typedef struct {
    lighting_frame_t frame;
    uint32_t arrived_ms;
    uint8_t source[LIGHTING_MAC_BYTES];
} received_t;

static const char *TAG = "reactor_side";
static const uint8_t BROADCAST[] = {255, 255, 255, 255, 255, 255};
static QueueHandle_t inbox;

static struct {
    lighting_link_t link;
    lighting_selection_t selection;
    lv_obj_t *notice;
    lv_point_t touch_start;
    bool tracking_touch;
    bool notice_visible;
    uint32_t notice_ms;
    lighting_style_t drawn_style;
    uint8_t gain;
    bool online;
    uint32_t rendered_ms;
    struct {
        uint32_t since_ms;
        uint16_t duration_ms;
        uint8_t from;
        uint8_t to;
    } level;
} side = {
    .selection = {.selected = LIGHTING_DEFAULT_STYLE},
    .drawn_style = LIGHTING_DEFAULT_STYLE,
    .gain = LIGHT_FULL_GAIN,
};

static void receive(const esp_now_recv_info_t *info, const uint8_t *bytes, int length)
{
    received_t packet;
    if (info->rx_ctrl->channel != LIGHTING_CHANNEL || memcmp(info->des_addr, BROADCAST, sizeof BROADCAST) ||
        !lighting_decode(&packet.frame, bytes, length, SUGARAT_LIGHTING_GROUP)) return;
    packet.arrived_ms = (uint32_t)(esp_timer_get_time() / 1000);
    memcpy(packet.source, info->src_addr, sizeof packet.source);
    (void)xQueueSend(inbox, &packet, 0);
}

static esp_err_t start_radio(void)
{
    ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "Network interface");
    ESP_RETURN_ON_ERROR(esp_event_loop_create_default(), TAG, "Network event loop");
    wifi_init_config_t config = WIFI_INIT_CONFIG_DEFAULT();
    config.nvs_enable = 0;
    ESP_RETURN_ON_ERROR(esp_wifi_init(&config), TAG, "Wi-Fi initialization");
    ESP_RETURN_ON_ERROR(esp_wifi_set_storage(WIFI_STORAGE_RAM), TAG, "Wi-Fi RAM storage");
    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "Station mode");
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "Wi-Fi start");
    ESP_RETURN_ON_ERROR(esp_wifi_set_ps(WIFI_PS_NONE), TAG, "Wi-Fi power saving");
    ESP_RETURN_ON_ERROR(esp_wifi_set_channel(LIGHTING_CHANNEL, WIFI_SECOND_CHAN_NONE), TAG, "Lighting channel");
    ESP_RETURN_ON_ERROR(esp_now_init(), TAG, "ESP-NOW");
    const esp_err_t err = esp_now_register_recv_cb(receive);
    if (err != ESP_OK) esp_now_deinit();
    return err;
}

static uint8_t level_at(uint32_t now)
{
    const uint32_t elapsed = now - side.level.since_ms;
    if (elapsed >= side.level.duration_ms) return side.level.to;
    const unsigned weight = elapsed * 256 / side.level.duration_ms;
    return (side.level.from * (256 - weight) + side.level.to * weight + 128) / 256;
}

static void set_level(uint32_t now, uint8_t level, uint16_t duration)
{
    if (level == side.level.to) return;
    side.level.from = level_at(now);
    side.level.to = level;
    side.level.since_ms = now;
    side.level.duration_ms = duration;
}

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
        side.tracking_touch = false;
        return;
    }
    if (code != LV_EVENT_PRESSED && code != LV_EVENT_RELEASED) return;
    lv_indev_t *input = lv_indev_get_act();
    if (!input || lv_indev_get_type(input) != LV_INDEV_TYPE_POINTER) return;

    lv_point_t point;
    lv_indev_get_point(input, &point);
    if (code == LV_EVENT_PRESSED) {
        side.touch_start = point;
        side.tracking_touch = true;
        return;
    }
    if (!side.tracking_touch) return;
    side.tracking_touch = false;

    const int dx = point.x - side.touch_start.x;
    const int dy = point.y - side.touch_start.y;
    if (abs(dx) < SWIPE_MIN_PX || abs(dx) <= abs(dy)) return;
    lighting_selection_cycle(&side.selection, dx < 0 ? 1 : -1);
    lv_label_set_text_static(side.notice, style_label(side.selection.selected));
    lv_obj_clear_flag(side.notice, LV_OBJ_FLAG_HIDDEN);
    side.notice_visible = true;
    side.notice_ms = (uint32_t)(esp_timer_get_time() / 1000);
}

static void bind_swipe(lv_obj_t *screen)
{
    side.notice = lv_label_create(screen);
    lv_obj_set_style_text_color(side.notice, lv_color_hex(0xe3f4ff), 0);
    lv_obj_set_style_bg_color(side.notice, lv_color_hex(0x0c161f), 0);
    lv_obj_set_style_bg_opa(side.notice, LV_OPA_90, 0);
    lv_obj_set_style_radius(side.notice, 12, 0);
    lv_obj_set_style_pad_hor(side.notice, 8, 0);
    lv_obj_set_style_pad_ver(side.notice, 4, 0);
    lv_obj_align(side.notice, LV_ALIGN_TOP_MID, 0, 55);
    lv_obj_add_flag(side.notice, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(screen, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_PRESS_LOCK);
    lv_obj_add_event_cb(screen, swipe_event, LV_EVENT_ALL, NULL);
}

static void poll_display(void)
{
    const uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
    received_t packet;
    while (inbox && xQueueReceive(inbox, &packet, 0) == pdTRUE) {
        if (now - packet.arrived_ms >= LIGHTING_HEARTBEAT_MS ||
            !lighting_receive(&side.link, packet.source, &packet.frame, packet.arrived_ms)) continue;
        const lighting_style_t previous = side.selection.selected;
        lighting_selection_follow(&side.selection, packet.frame.style);
        if (side.notice_visible && previous != side.selection.selected) {
            side.notice_visible = false;
            lv_obj_add_flag(side.notice, LV_OBJ_FLAG_HIDDEN);
        }
        const uint8_t level = UINT8_MAX - packet.frame.level;
        if (!side.online) {
            ESP_LOGI(TAG, "Bound to master " MACSTR, MAC2STR(side.link.source));
            side.online = true;
            set_level(now, level, JOIN_FADE_MS);
        } else {
            set_level(now, level, LIGHTING_ANIMATION_MS);
        }
    }
    if (side.online && !lighting_online(&side.link, now)) {
        side.online = false;
        set_level(now, 0, DISCONNECT_FADE_MS);
        ESP_LOGI(TAG, "Master timed out");
    }

    if (side.notice_visible && now - side.notice_ms >= STYLE_NOTICE_MS) {
        side.notice_visible = false;
        lv_obj_add_flag(side.notice, LV_OBJ_FLAG_HIDDEN);
    }

    const uint32_t elapsed = now - side.rendered_ms;
    if (elapsed < FRAME_MS) return;
    side.rendered_ms = now;
    const unsigned step = elapsed >= FADE_MS ? LIGHT_FULL_GAIN : elapsed * LIGHT_FULL_GAIN / FADE_MS;
    if (side.selection.selected != side.drawn_style) {
        if (side.gain <= step) {
            side.gain = 0;
            side.drawn_style = side.selection.selected;
            ESP_LOGI(TAG, "Effect %u", (unsigned)side.drawn_style);
        } else {
            side.gain -= step;
        }
    } else if (side.gain < LIGHT_FULL_GAIN) {
        side.gain += LV_MIN(step, LIGHT_FULL_GAIN - side.gain);
    }
    const uint8_t level = level_at(now);
    const unsigned exposure = LV_MIN(level, FULL_EXPOSURE_LEVEL);
    lighting_art_draw(side.drawn_style, level, side.gain * exposure / FULL_EXPOSURE_LEVEL);
}

/** Start a display follower with launcher control-center and Exit support. */
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

    lv_obj_t *screen = lv_scr_act();
    lv_obj_set_style_bg_color(screen, lv_color_black(), 0);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    inbox = xQueueCreate(8, sizeof(received_t));
    const bool display_ready = lighting_art_create(screen);
    const esp_err_t radio = display_ready && inbox ? start_radio() : ESP_ERR_NO_MEM;
    if (display_ready) {
        bind_swipe(screen);
        sdgoods_apps_set_poll(poll_display);
    }
    if (radio == ESP_OK) {
        ESP_LOGI(TAG, "Ready on channel %d, group %d", LIGHTING_CHANNEL, SUGARAT_LIGHTING_GROUP);
    } else {
        ESP_LOGE(TAG, "Side display unavailable: %s", esp_err_to_name(radio));
        lv_obj_t *label = lv_label_create(screen);
        lv_label_set_text_static(label, display_ready ? SDG_T("无线不可用", "Radio unavailable")
                                                     : SDG_T("显示不可用", "Display unavailable"));
        lv_obj_set_style_text_color(label, lv_color_white(), 0);
        lv_obj_center(label);
    }
    sdgoods_app_shell_bind(screen);
    lv_refr_now(NULL);
    sdgoods_app_shell_init();
    sdgoods_lcd_set_backlight(sdgoods_cc_brightness_get());
    sdgoods_lvgl_loop();
}

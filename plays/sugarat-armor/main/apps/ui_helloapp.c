/* SPDX-License-Identifier: Apache-2.0 */
#include "ui_helloapp.h"

#include <math.h>
#include <stdint.h>

#include "armor_ble.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdgoods_board.h"
#include "sugarat_imu.h"
#include "sugarat_watch.h"

enum {
    CONTROL_PERIOD_MS = 20,
    SCREEN_PERIOD_MS = 33,
    SAMPLE_TIMEOUT_MS = 220,
    PANEL_TOP = 186,
    PANEL_HIDDEN = 360,
};
static const char *const TAG = "sugarat_armor";

static lv_obj_t *s_screen;
static lv_obj_t *s_wait;
static lv_obj_t *s_shade;
static lv_obj_t *s_sheet;
static lv_obj_t *s_sheet_status;
static bool s_sheet_open;
static lv_obj_t *s_core;
static lv_obj_t *s_glow;
static lv_obj_t *s_segments[3];
static uint16_t s_phase;
static uint8_t s_spin_tick;
static void close_sheet(bool animate);
/* Written by the UI and read by the control task, or the reverse. */
static volatile bool s_paused;
static volatile bool s_pose_level;
static volatile bool s_pose_ready;
static volatile bool s_ready;
static volatile bool s_boost;
static bool s_sensor_started;
static bool s_ble_started;
static bool s_last_subscribed;
static bool s_ui_subscribed;
static volatile uint16_t s_view_count;
static uint32_t s_last_samples;
static uint32_t s_last_sample_tick;
static float s_nose_up;
static float s_bank_right;
static float s_center_nose;
static float s_center_bank;

/* Pose fusion: the gyro carries the angle from sample to sample and gravity pulls it back over
 * FUSE_TAU_S, so moving the hand without turning it does not read as tilt. */
#define FUSE_TAU_S 0.25f
/* Evidence (deg²/s, decaying) needed before the gyro's sign on an axis is taken as reversed. */
#define SIGN_EVIDENCE 4000.0f
/* Flight input: dead zone and full tilt in degrees, and how much of the response is cubic so
 * small tilts give fine control. */
#define DEAD_ZONE_DEG 2.0f
#define FULL_BANK_DEG 30.0f
#define FULL_NOSE_DEG 25.0f
#define EXPO 0.3f
static bool s_fused;
static int64_t s_fuse_us;
static float s_acc_nose;
static float s_acc_bank;
static int8_t s_gyro_sign[2] = {1, 1};
static float s_sign_evidence[2];

static float shape_axis(float angle, float full_scale)
{
    const float m = fminf(1, fmaxf(0, (fabsf(angle) - DEAD_ZONE_DEG) / (full_scale - DEAD_ZONE_DEG)));
    return copysignf((1 - EXPO) * m + EXPO * m * m * m, angle);
}

/* Confirms on hardware that each gyro axis turns the same way as its gravity angle; a reversed
 * axis would fight the gravity correction, so it is flipped once the evidence is clear. */
static void learn_gyro_sign(int axis, float rate_dps, float gravity_step_deg)
{
    if (fabsf(rate_dps) < 30.0f) return;
    s_sign_evidence[axis] = s_sign_evidence[axis] * 0.98f + rate_dps * gravity_step_deg;
    const int8_t sign = s_sign_evidence[axis] > 0 ? 1 : -1;
    if (fabsf(s_sign_evidence[axis]) > SIGN_EVIDENCE && sign != s_gyro_sign[axis]) {
        s_gyro_sign[axis] = sign;
        s_sign_evidence[axis] = 0;
        ESP_LOGW(TAG, "ARMOR gyro axis %d sign set to %d", axis, sign);
    }
}

static float wrap180(float angle)
{
    while (angle > 180) angle -= 360;
    while (angle < -180) angle += 360;
    return angle;
}

/* `trust_gravity` is false while the badge is being flung about, when only the gyro is used. */
static void fuse_pose(const sugarat_imu_state_t *imu, bool trust_gravity)
{
    const int64_t now = esp_timer_get_time();
    const float dt = (now - s_fuse_us) / 1e6f;
    s_fuse_us = now;
    /* The IMU's rotation about X lifts the top edge; rotation about Y lowers the right edge. */
    const float acc_nose = imu->roll_deg;
    const float acc_bank = imu->pitch_deg;
    if (!s_fused || dt <= 0 || dt > 0.2f) {
        s_fused = true;
        s_nose_up = s_acc_nose = acc_nose;
        s_bank_right = s_acc_bank = acc_bank;
        return;
    }
    learn_gyro_sign(0, imu->gyro_dps.x, wrap180(acc_nose - s_acc_nose));
    learn_gyro_sign(1, imu->gyro_dps.y, acc_bank - s_acc_bank);
    s_acc_nose = acc_nose;
    s_acc_bank = acc_bank;
    const float k = trust_gravity ? dt / (FUSE_TAU_S + dt) : 0;
    /* The nose angle from gravity is meaningless with the badge on its side, so it counts for less
     * the further the badge is banked. */
    const sugarat_imu_vec3_t a = imu->accel_g;
    const float upright = hypotf(a.y, a.z) / fmaxf(0.1f, hypotf(hypotf(a.x, a.y), a.z));
    const float k_nose = k * fminf(1, upright / 0.5f);
    s_nose_up = wrap180(s_nose_up + s_gyro_sign[0] * imu->gyro_dps.x * dt);
    s_nose_up = wrap180(s_nose_up + k_nose * wrap180(acc_nose - s_nose_up));
    s_bank_right = wrap180(s_bank_right + s_gyro_sign[1] * imu->gyro_dps.y * dt);
    s_bank_right += k * (acc_bank - s_bank_right);
}

static bool read_pose(void)
{
    sugarat_imu_state_t imu = {0};
    sugarat_imu_get_state(&imu);
    if (imu.samples != s_last_samples) {
        s_last_samples = imu.samples;
        s_last_sample_tick = lv_tick_get();
    }
    const float gravity = hypotf(hypotf(imu.accel_g.x, imu.accel_g.y), imu.accel_g.z);
    const bool fresh = imu.sensor_ok && s_last_samples &&
        lv_tick_elaps(s_last_sample_tick) < SAMPLE_TIMEOUT_MS;
    /* The Center button needs the badge roughly flat; flying works at any angle, saturating past
     * full tilt, and stops only when the sensor stops reporting. */
    s_pose_level = fresh && gravity >= 0.7f && gravity <= 1.3f && imu.accel_g.z >= 0.866f * gravity;
    if (!fresh) {
        s_fused = false;
        return false;
    }
    fuse_pose(&imu, gravity >= 0.6f && gravity <= 1.4f);
    return true;
}

static void update_reactor(bool ready, bool boost)
{
    if (s_paused || !s_last_subscribed) return;
    s_spin_tick = (s_spin_tick + 1) % 2;
    if (s_spin_tick && !boost) return;

    s_phase = (s_phase + (boost ? 6 : ready ? 3 : 2)) % 360;
    const lv_color_t beam = lv_color_hex(boost ? 0xe8ffff : ready ? 0x67eaff : 0x4181af);
    const lv_opa_t glow = (lv_opa_t)(125 + 55 * lv_trigo_sin(s_phase) / LV_TRIGO_SIN_MAX);
    lv_obj_set_style_bg_color(s_core, beam, 0);
    lv_obj_set_style_outline_color(s_core, beam, 0);
    lv_obj_set_style_outline_opa(s_core, glow, 0);
    lv_obj_set_style_border_color(s_glow, beam, 0);
    lv_obj_set_style_border_opa(s_glow, glow, 0);
    for (int segment = 0; segment < 3; ++segment) {
        lv_obj_set_style_arc_color(s_segments[segment], beam, LV_PART_INDICATOR);
        lv_arc_set_rotation(s_segments[segment], (s_phase + segment * 120) % 360);
    }
}

static void center_pose(void)
{
    s_center_bank = s_bank_right;
    s_center_nose = s_nose_up;
}

/* Reads the pose and sends a control packet every CONTROL_PERIOD_MS on its own task, so screen
 * drawing does not delay or thin out the packets. */
static void control_task(void *arg)
{
    TickType_t wake = xTaskGetTickCount();
    uint32_t logged = 0;
    for (;;) {
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(CONTROL_PERIOD_MS));
        const bool subscribed = armor_ble_subscribed();
        if (subscribed != s_last_subscribed) {
            s_last_subscribed = subscribed;
            /* Neutral is the badge lying flat, so a new connection flies straight away. */
            s_center_bank = 0;
            s_center_nose = 0;
            s_boost = false;
        }
        s_pose_ready = s_sensor_started && read_pose();
        const bool ready = subscribed && s_pose_ready && !s_paused;
        const float turn = ready ? shape_axis(wrap180(s_bank_right - s_center_bank), FULL_BANK_DEG) : 0;
        const float climb = ready ? shape_axis(wrap180(s_nose_up - s_center_nose), FULL_NOSE_DEG) : 0;
        armor_ble_send(ready, s_boost, turn, climb, s_view_count);
        s_ready = ready;
        if (subscribed && ++logged % 50 == 0) {
            ESP_LOGI(TAG, "ARMOR pose nose=%.1f bank=%.1f gravity nose=%.1f bank=%.1f turn=%.2f climb=%.2f",
                s_nose_up, s_bank_right, s_acc_nose, s_acc_bank, turn, climb);
        }
    }
}

static void update_screen(lv_timer_t *timer)
{
    const bool subscribed = armor_ble_subscribed();
    if (subscribed != s_ui_subscribed) {
        s_ui_subscribed = subscribed;
        close_sheet(false);
        if (subscribed) lv_obj_add_flag(s_wait, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_clear_flag(s_wait, LV_OBJ_FLAG_HIDDEN);
    }
    update_reactor(s_ready, s_ready && s_boost);

    if (s_sheet_open) {
        const char *status = !s_pose_ready ? "Motion sensor not responding"
            : "Flight controls";
        if (lv_label_get_text(s_sheet_status) != status) lv_label_set_text_static(s_sheet_status, status);
    }
}

static void on_center(lv_event_t *event)
{
    if (!s_pose_level || !armor_ble_subscribed() || s_paused) return;
    center_pose();
}

static void on_view(lv_event_t *event)
{
    if (s_pose_ready && armor_ble_subscribed() && !s_paused) s_view_count++;
}

static void on_boost(lv_event_t *event)
{
    s_boost = lv_event_get_code(event) == LV_EVENT_PRESSING;
}

static void pause_armor(void)
{
    s_paused = true;
    close_sheet(false);
}

static void resume_armor(void)
{
    s_paused = false;
}

static lv_obj_t *surface(lv_obj_t *parent, int width, int height, uint32_t color)
{
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(obj, width, height);
    if (color) {
        lv_obj_set_style_bg_color(obj, lv_color_hex(color), 0);
        lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    }
    return obj;
}

static lv_obj_t *reactor_ring(int diameter, uint32_t fill, uint32_t edge, int width)
{
    lv_obj_t *ring = surface(s_screen, diameter, diameter, fill);
    lv_obj_center(ring);
    lv_obj_set_style_radius(ring, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(ring, width, 0);
    lv_obj_set_style_border_color(ring, lv_color_hex(edge), 0);
    return ring;
}

static lv_obj_t *reactor_arc(int diameter, int start, int length, int width, uint32_t color)
{
    lv_obj_t *arc = lv_arc_create(s_screen);
    lv_obj_remove_style(arc, NULL, LV_PART_KNOB);
    lv_obj_clear_flag(arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(arc, diameter, diameter);
    lv_obj_center(arc);
    lv_obj_set_style_arc_opa(arc, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc, width, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(arc, false, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(arc, lv_color_hex(color), LV_PART_INDICATOR);
    lv_arc_set_bg_angles(arc, 0, 360);
    lv_arc_set_angles(arc, 0, length);
    lv_arc_set_rotation(arc, start);
    return arc;
}

/* A chevron and label on the top edge: this edge points at the computer, and lifting it climbs. */
static void create_direction_marker(lv_obj_t *parent, uint32_t color)
{
    static const lv_point_t chevron[] = {{0, 14}, {14, 0}, {28, 14}};
    lv_obj_t *arrow = lv_line_create(parent);
    lv_line_set_points(arrow, chevron, sizeof(chevron) / sizeof(chevron[0]));
    lv_obj_set_style_line_color(arrow, lv_color_hex(color), 0);
    lv_obj_set_style_line_width(arrow, 4, 0);
    lv_obj_set_style_line_rounded(arrow, true, 0);
    lv_obj_align(arrow, LV_ALIGN_TOP_MID, 0, 10);
    lv_obj_t *label = sw_label(parent, "TO SCREEN", SW_FONT_BODY, SW_TONE_TEAL);
    lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 30);
}

static void create_reactor(void)
{
    reactor_ring(360, 0x111e2b, 0x3a4b57, 5);
    reactor_ring(336, 0x071421, 0x5a342c, 3);
    reactor_ring(304, 0x102b3d, 0x557587, 3);
    reactor_ring(260, 0x071725, 0x4499ac, 2);
    reactor_ring(222, 0x0b2639, 0x235767, 4);
    reactor_ring(178, 0x0a1b2a, 0x6fc2d1, 2);
    s_glow = reactor_ring(146, 0x113a50, 0x39698b, 4);

    for (int sector = 0; sector < 12; ++sector) {
        const int start = sector * 30;
        reactor_arc(326, start + 4, 22, 8, 0x6e4934);
        reactor_arc(290, start + 5, 20, 10, 0x3a9daf);
        reactor_arc(246, start + 10, 10, 3, 0xb0e6ef);
    }
    for (int segment = 0; segment < 3; ++segment) {
        s_segments[segment] = reactor_arc(211, segment * 120, 64, 8, 0x4181af);
        reactor_arc(131, segment * 120 + 20, 80, 7, 0x8edce8);
    }

    reactor_ring(105, 0x082130, 0x39727e, 2);
    s_core = reactor_ring(65, 0x4181af, 0xccfaff, 3);
    lv_obj_set_style_outline_width(s_core, 6, 0);
    lv_obj_set_style_outline_pad(s_core, 8, 0);
    reactor_ring(38, 0xcffbff, 0xe8ffff, 2);
    create_direction_marker(s_screen, 0x67eaff);
}

static void create_wait_page(void)
{
    s_wait = surface(s_screen, 360, 360, 0x081625);
    lv_obj_add_flag(s_wait, LV_OBJ_FLAG_CLICKABLE);
    static const lv_point_t bluetooth[] = {
        {0, 5}, {20, 25}, {11, 34}, {11, 0}, {20, 9}, {0, 29},
    };
    lv_obj_t *icon = lv_line_create(s_wait);
    lv_line_set_points(icon, bluetooth, sizeof(bluetooth) / sizeof(bluetooth[0]));
    lv_obj_set_style_line_color(icon, lv_color_hex(0x67eaff), 0);
    lv_obj_set_style_line_width(icon, 3, 0);
    lv_obj_set_style_line_rounded(icon, true, 0);
    lv_obj_align(icon, LV_ALIGN_CENTER, 0, -94);
    lv_obj_t *title = sw_label(s_wait, "Sugarat Armor", SW_FONT_TITLE, SW_TONE_TEXT);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, -56);
    const char *heading = s_ble_started && s_sensor_started ? "Waiting for Bluetooth" : "Restart the badge";
    lv_obj_t *status = sw_label(s_wait, heading, SW_FONT_HEADLINE, SW_TONE_TEAL);
    lv_obj_align(status, LV_ALIGN_CENTER, 0, -10);
    create_direction_marker(s_wait, 0x67eaff);
    lv_obj_t *hint = sw_label(s_wait,
        "Open Armor Fly on your computer.\nSelect Connect Sugarat.\nScreen up, arrow edge to the\ncomputer. Tilt to fly.",
        SW_FONT_BODY, SW_TONE_TEXT);
    lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(hint, LV_ALIGN_CENTER, 0, 55);
}

static void move_sheet(void *target, int32_t y)
{
    lv_obj_set_y(target, y);
}

static void hide_sheet(lv_anim_t *animation)
{
    lv_obj_add_flag(s_sheet, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_shade, LV_OBJ_FLAG_HIDDEN);
}

static void slide_sheet(int y, bool closing)
{
    lv_anim_t animation;
    lv_anim_init(&animation);
    lv_anim_set_var(&animation, s_sheet);
    lv_anim_set_exec_cb(&animation, move_sheet);
    lv_anim_set_values(&animation, lv_obj_get_y(s_sheet), y);
    lv_anim_set_time(&animation, 200);
    lv_anim_set_path_cb(&animation, lv_anim_path_ease_out);
    if (closing) lv_anim_set_ready_cb(&animation, hide_sheet);
    lv_anim_start(&animation);
}

static void open_sheet(void)
{
    if (!s_last_subscribed || s_paused || s_sheet_open) return;
    s_sheet_open = true;
    lv_obj_clear_flag(s_shade, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_sheet, LV_OBJ_FLAG_HIDDEN);
    slide_sheet(PANEL_TOP, false);
}

static void close_sheet(bool animate)
{
    s_boost = false;
    if (!s_sheet_open && animate) return;
    s_sheet_open = false;
    if (animate) {
        slide_sheet(PANEL_HIDDEN, true);
    } else {
        lv_anim_del(s_sheet, move_sheet);
        move_sheet(s_sheet, PANEL_HIDDEN);
        hide_sheet(NULL);
    }
}

static void dismiss_sheet(lv_event_t *event)
{
    if (lv_event_get_code(event) == LV_EVENT_GESTURE) {
        lv_indev_t *input = lv_indev_get_act();
        if (!input || lv_indev_get_gesture_dir(input) != LV_DIR_BOTTOM) return;
        lv_indev_wait_release(input);
    }
    close_sheet(true);
}

static lv_obj_t *add_button(const char *text, int x, int width, uint32_t color)
{
    lv_obj_t *button = lv_btn_create(s_sheet);
    lv_obj_set_size(button, width, 42);
    lv_obj_set_pos(button, x, 45);
    lv_obj_set_style_bg_color(button, lv_color_hex(color), 0);
    lv_obj_t *label = sw_label(button, text, SW_FONT_BODY, SW_TONE_TEXT);
    lv_obj_center(label);
    return button;
}

static void create_sheet(void)
{
    s_shade = surface(s_screen, 360, 360, 0x01070c);
    lv_obj_set_style_bg_opa(s_shade, LV_OPA_50, 0);
    lv_obj_add_flag(s_shade, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_shade, dismiss_sheet, LV_EVENT_SHORT_CLICKED, NULL);
    s_sheet = surface(s_screen, 300, 174, 0x102332);
    lv_obj_set_pos(s_sheet, 30, PANEL_HIDDEN);
    lv_obj_set_style_radius(s_sheet, 25, 0);
    lv_obj_add_flag(s_sheet, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(s_sheet, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_event_cb(s_sheet, dismiss_sheet, LV_EVENT_GESTURE, NULL);

    lv_obj_t *handle = surface(s_sheet, 40, 4, 0x6aa5b5);
    lv_obj_align(handle, LV_ALIGN_TOP_MID, 0, 7);
    lv_obj_set_style_radius(handle, LV_RADIUS_CIRCLE, 0);
    s_sheet_status = sw_label(s_sheet, "Flight controls", SW_FONT_BODY, SW_TONE_TEXT);
    lv_obj_align(s_sheet_status, LV_ALIGN_TOP_MID, 0, 17);

    lv_obj_t *center = add_button("Center", 44, 64, 0x23425f);
    lv_obj_add_event_cb(center, on_center, LV_EVENT_SHORT_CLICKED, NULL);
    lv_obj_t *boost = add_button("Boost", 116, 68, 0x7c3a28);
    lv_obj_add_event_cb(boost, on_boost, LV_EVENT_PRESSING, NULL);
    lv_obj_add_event_cb(boost, on_boost, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(boost, on_boost, LV_EVENT_PRESS_LOST, NULL);
    lv_obj_t *view = add_button("View", 192, 64, 0x23425f);
    lv_obj_add_event_cb(view, on_view, LV_EVENT_SHORT_CLICKED, NULL);
    hide_sheet(NULL);
}

void ui_helloapp_start(void)
{
    if (s_screen) {
        lv_scr_load(s_screen);
        return;
    }

    sugarat_imu_config_t config = sugarat_imu_default_config();
    config.step_detection = false;
    config.sample_hz = 200;
    esp_err_t imu_error = sugarat_imu_start(&config);
    esp_err_t ble_error = armor_ble_start();
    s_sensor_started = imu_error == ESP_OK;
    s_ble_started = ble_error == ESP_OK;
    ESP_LOG_LEVEL(s_sensor_started && s_ble_started ? ESP_LOG_INFO : ESP_LOG_ERROR, TAG,
        "ARMOR controller IMU=%s BLE=%s", esp_err_to_name(imu_error), esp_err_to_name(ble_error));

    sw_init();
    s_screen = lv_obj_create(NULL);
    lv_obj_clear_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(s_screen, lv_color_hex(0x0a111c), 0);
    lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);

    create_reactor();
    sdgoods_swipe_up_bind(s_screen, open_sheet);
    create_wait_page();
    create_sheet();

    lv_scr_load(s_screen);
    sdgoods_app_shell_bind(s_screen);
    sdgoods_app_shell_set_pause_cb(pause_armor);
    sdgoods_app_shell_set_resume_cb(resume_armor);
    lv_timer_create(update_screen, SCREEN_PERIOD_MS, NULL);
    xTaskCreate(control_task, "armor_ctl", 4096, NULL, 5, NULL);
}

void ui_helloapp_poll(void)
{
}

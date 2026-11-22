/* Modified by Sugarat: see SUGARAT_TEMPLATE.md for source changes. */
/*
 * 谷仓共创计划 · 谷仓 SDGOODS 开放平台基础工程
 * 应用层示例
 * https://github.com/SDGOODS/SDGOODS-ESP32S3
 *
 * Copyright (c) 2026 深圳希德创新网络有限公司 (SDGOODS)
 * SPDX-License-Identifier: Apache-2.0
 *
 * 本文件属于应用层，以 Apache-2.0 发布：可自由商用、可闭源分发，
 * 只需保留本声明并携带 NOTICE 文件。详见 LICENSING.md。
 */

/**
 * The badge is the cage: the habitat is drawn 1:1 and never resampled. The hamster
 * stays at the gravity-bottom of the wheel, so turning the badge makes it run; the
 * axle drags the wheel along with the cage, and a sustained fast spin sweeps the
 * hamster off its feet into a tumbling ball, after which it is dizzy for a while.
 * Angles share one sense: the screen direction dir(a) = (-sin a, cos a), y down.
 */
#include "ui_helloapp.h"

#include <math.h>
#include <inttypes.h>
#include <string.h>
#include <stdatomic.h>
#include <stdlib.h>
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "sdgoods_board.h"
#include "sugarat_imu.h"
#include "sugarat_audio.h"
#include "hamster_assets.h"

enum { LCD = 360, TREAD_CX = 178, TREAD_CY = 177, TREAD_RX = 83, TREAD_RY = 88, BALL_R = 26 };
/* The pet is composed upright in a local buffer whose ANCHOR is the paw contact point. */
enum { LOCAL_W = 176, LOCAL_H = 124, ANCHOR_X = 88, ANCHOR_Y = 118, PET_MID = 63 };
/* The torso plus legs are drawn into a sprite whose bottom row is the paw contact line.
 * Legs use 2-pixel cells, the pixel size of the artwork at 360x360. */
enum { SPRITE_H = 74, PET_FOOT = SPRITE_H - 1, CELL_W = PET_W / 2, CELL_H = SPRITE_H / 2, GROUND = CELL_H - 1 };
enum { TURN_MS = 320 };
enum { FRAME_US = 16000, REFRESH_MS = 16, BUBBLE_Y = 48 };
typedef enum { MODE_RUN, MODE_TUMBLE, MODE_DIZZY } play_mode_t;

/* Physics in radians; see the file comment for the model. */
static const float AXLE = 4.5f;          /* wheel follows the cage with a ~0.22 s lag */
static const float RUN_MAX = 6.0f;       /* hamster top speed relative to the tread, ~340 deg/s */
static const float DIZZY_RUN_MAX = 2.0f;
static const float SLIP_ANGLE = 1.0f;    /* carried this far up the wheel, the hamster loses footing */
static const float GRAVITY = 40.0f;      /* ball pendulum stiffness, ~1 Hz */
static const float GRIP = 5.0f;          /* ball drag toward the tread speed */

static const float radians = 0.01745329252f;
static const float full_turn = 6.28318530718f;
static const float half_turn = 3.14159265359f;
static const char *TAG = "hamster";

extern const uint16_t habitat_flash[] asm("_binary_habitat_bin_start");
/* PSRAM copy of the habitat: octal PSRAM serves the rotated reads far faster than DIO flash. */
static uint16_t *habitat;
extern const uint16_t pet_frames[] asm("_binary_hamster_bin_start");
extern const uint16_t ball[] asm("_binary_ball_bin_start");
extern const uint16_t bubbles[] asm("_binary_bubble_bin_start");
extern const uint8_t wheel_map_flash[] asm("_binary_wheel_map_bin_start");

/** One habitat row of the wheel rotation map; see artwork/build_assets.py. */
typedef struct {
    int16_t x0, x1;
    int32_t offset;
} wheel_row_t;
typedef struct {
    int16_t qx, qy, rho;
} wheel_texel_t;
static const wheel_row_t *wheel_rows;
static const wheel_texel_t *wheel_texels;

static lv_obj_t *screen;
static lv_obj_t *image;
static uint16_t *local;
static uint16_t *sprite;
static uint8_t cells[CELL_H][CELL_W], leg_mask[CELL_H][CELL_W];
static bool imu_started, orientation_ready;
static uint32_t sample_number;
static int64_t previous_poll, last_sample, last_frame, last_report, app_started;
static float orientation, angular_rate;

/* Simulation state. */
static play_mode_t mode;
static float tread;             /* wheel surface rate in the room, rad/s */
static float wheel_angle;       /* wheel rotation relative to the cage */
static float alpha, alpha_rate; /* hamster position from the gravity-bottom */
static float run;               /* hamster speed relative to the tread */
static float spin;              /* ball roll angle */
static float settle_time, dizzy_left, idle_time, jog_left, jog_speed, next_jog, blink_wait;
static float gait, facing = 1.0f, clock_s, tumble_travel;
static float turn_t = -1.0f, reverse_time; /* turn progress 0..1, or negative when not turning */
static int laps;
static bool intro_hidden;
static float intro_motion;

/* State latched for the draw callback. */
typedef struct {
    float sigma, ball_angle, wheel, tilt;
    int mode, frame, bob, sway, paw[4], lift[4], stars, drop, bubble;
    int16_t scale; /* facing * 64 */
} look_t;
static look_t drawn;
static bool drawn_valid;
static lv_area_t pet_box, wheel_box, bubble_box;
static int used_x0, used_y0, used_x1, used_y1;

/* Diagnostics. */
static atomic_int console_command;
static float sim_rate, sim_angle;
static bool sim_hold;
static float turn_freeze = -1.0f; /* console 't': hold the turn pose at this progress */
static int64_t benchmark_start;
static uint32_t benchmark_samples, frame_count, draw_count;
static int64_t draw_total, draw_max, gap_max;
static float peak_rate;

/** Queue one console command from the USB serial dispatcher. */
void sugarat_hamster_console(char command)
{
    atomic_store(&console_command, command);
}

static inline uint16_t swap565(uint8_t r, uint8_t g, uint8_t b)
{
    return __builtin_bswap16((uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)));
}

static float wrap(float angle)
{
    return remainderf(angle, full_turn);
}

static float clampf(float value, float low, float high)
{
    return value < low ? low : value > high ? high : value;
}

static float random_unit(void)
{
    return (esp_random() & 0xffff) / 65535.0f;
}

/* ---------------------------------------------------------------- local pet buffer */

static void plot(int x, int y, uint16_t color)
{
    if ((unsigned)x >= LOCAL_W || (unsigned)y >= LOCAL_H) return;
    local[y * LOCAL_W + x] = color;
    if (x < used_x0) used_x0 = x;
    if (y < used_y0) used_y0 = y;
    if (x >= used_x1) used_x1 = x + 1;
    if (y >= used_y1) used_y1 = y + 1;
}

/** Map a sprite-space x to the local buffer for the current facing. */
static float face_x(float x, float scale, int sway)
{
    return ANCHOR_X + sway + (x - PET_MID) * scale;
}

enum { INK = 1, FAR_FUR, FAR_PINK, NEAR_FUR, NEAR_SHADE, NEAR_PINK, NEAR_TOE, PALETTE };

/**
 * One leg in cell space: a two-cell fur stub from the hip to the ankle and a pink foot,
 * outlined like the artwork. A planted foot lies flat with its toes forward; a lifted
 * foot curls under.
 */
static void leg(int hx, int hy, int dx, int lift, bool near)
{
    memset(leg_mask, 0, sizeof(leg_mask));
    const int ay = GROUND - 1 - lift, ax = hx + dx;
    for (int r = hy; r < ay; ++r) {
        const int x = hx + (int)lroundf((float)(ax - hx) * (r - hy) / LV_MAX(1, ay - hy));
        leg_mask[r][x] = near ? NEAR_SHADE : FAR_FUR;
        leg_mask[r][x + 1] = near ? NEAR_FUR : FAR_FUR;
    }
    const uint8_t pink = near ? NEAR_PINK : FAR_PINK;
    leg_mask[ay][ax] = leg_mask[ay][ax + 1] = pink;
    if (lift == 0) {
        leg_mask[ay + 1][ax] = leg_mask[ay + 1][ax + 1] = pink;
        leg_mask[ay + 1][ax + 2] = near ? NEAR_TOE : FAR_PINK;
    } else {
        leg_mask[ay + 1][ax + 1] = pink;
    }
    const int r0 = LV_MAX(0, hy - 1), c0 = LV_MAX(0, LV_MIN(hx, ax) - 1);
    const int c1 = LV_MIN(CELL_W - 1, LV_MAX(hx, ax) + 3);
    for (int r = r0; r < CELL_H; ++r) {
        for (int c = c0; c <= c1; ++c) {
            if (leg_mask[r][c]) continue;
            bool edge = false;
            for (int k = 0; k < 9 && !edge; ++k) {
                const int rr = r + k / 3 - 1, cc = c + k % 3 - 1;
                edge = rr >= 0 && rr < CELL_H && cc >= 0 && cc < CELL_W && leg_mask[rr][cc];
            }
            if (edge) cells[r][c] = INK;
        }
    }
    for (int r = r0; r < CELL_H; ++r)
        for (int c = c0; c <= c1; ++c)
            if (leg_mask[r][c]) cells[r][c] = leg_mask[r][c];
}

/** Legs behind the torso, then the torso frame, in facing-right sprite space. */
static void compose_sprite(const look_t *look)
{
    /* Hips in cells: hind far, front far, hind near, front near. */
    static const int8_t hips[4][2] = { { 21, 31 }, { 40, 30 }, { 17, 31 }, { 43, 30 } };
    const uint16_t palette[PALETTE] = {
        0, swap565(90, 46, 30), swap565(206, 168, 126), swap565(194, 106, 110),
        swap565(252, 234, 204), swap565(230, 200, 160), swap565(242, 150, 150), swap565(255, 196, 186),
    };
    memset(cells, 0, sizeof(cells));
    for (int i = 0; i < 4; ++i) leg(hips[i][0], hips[i][1], look->paw[i], look->lift[i], i >= 2);
    for (int y = 0; y < SPRITE_H; ++y) {
        const uint8_t *row = cells[y / 2];
        for (int x = 0; x < PET_W; ++x) sprite[y * PET_W + x] = palette[row[x / 2]];
    }
    const uint16_t *torso = pet_frames + look->frame * PET_W * PET_H;
    for (int y = 0; y < PET_H; ++y)
        for (int x = 0; x < PET_W; ++x)
            if (torso[y * PET_W + x]) sprite[y * PET_W + x] = torso[y * PET_W + x];
}

static void star(int cx, int cy)
{
    static const char *const shape[] = { "..o..", ".oxo.", "oxwxo", ".oxo.", "..o.." };
    const uint16_t outline = swap565(120, 64, 20), gold = swap565(255, 214, 64);
    const uint16_t white = swap565(255, 250, 220);
    for (int y = 0; y < 5; ++y) {
        for (int x = 0; x < 5; ++x) {
            const char c = shape[y][x];
            if (c == '.') continue;
            const uint16_t color = c == 'o' ? outline : c == 'w' ? white : gold;
            for (int k = 0; k < 4; ++k) plot(cx - 5 + 2 * x + (k & 1), cy - 5 + 2 * y + (k >> 1), color);
        }
    }
}

static void sweat(int cx, int cy)
{
    static const char *const shape[] = { "..o..", ".oxo.", "oxxxo", "oxwxo", ".ooo." };
    const uint16_t outline = swap565(52, 104, 168), water = swap565(150, 208, 255);
    const uint16_t shine = swap565(240, 250, 255);
    for (int y = 0; y < 5; ++y) {
        for (int x = 0; x < 5; ++x) {
            const char c = shape[y][x];
            if (c == '.') continue;
            const uint16_t color = c == 'o' ? outline : c == 'w' ? shine : water;
            plot(cx + x, cy + 2 * y, color);
            plot(cx + x, cy + 2 * y + 1, color);
        }
    }
}

static void compose_pet(const look_t *look)
{
    memset(local, 0, LOCAL_W * LOCAL_H * sizeof(*local));
    used_x0 = LOCAL_W;
    used_y0 = LOCAL_H;
    used_x1 = used_y1 = 0;
    const float scale = look->scale / 64.0f;
    const int top = ANCHOR_Y - PET_FOOT + look->bob;
    compose_sprite(look);
    const float inverse = 1.0f / scale;
    const float half = PET_MID * fabsf(scale) + 1;
    const int x0 = (int)floorf(ANCHOR_X + look->sway - half);
    const int x1 = (int)ceilf(ANCHOR_X + look->sway + half);
    for (int x = x0; x <= x1; ++x) {
        const int sx = (int)floorf(PET_MID + (x + 0.5f - ANCHOR_X - look->sway) * inverse);
        if ((unsigned)sx >= PET_W) continue;
        for (int y = 0; y < SPRITE_H; ++y) {
            const uint16_t color = sprite[y * PET_W + sx];
            if (color) plot(x, top + y, color);
        }
    }

    if (look->stars >= 0) {
        const float phase = look->stars * (full_turn / 64);
        for (int i = 0; i < 3; ++i) {
            const float a = phase + i * full_turn / 3;
            star((int)lroundf(face_x(96 + 20 * cosf(a), scale, look->sway)),
                 (int)lroundf(top - 2 + 6 * sinf(a)));
        }
    }
    if (look->drop >= 0)
        sweat((int)lroundf(face_x(80, scale, look->sway)) - 2, top - 4 + look->drop);
}

/* --------------------------------------------------------------------- rendering */

typedef struct {
    uint16_t *out;
    int stride;
    const lv_area_t *buf;
    lv_area_t clip;
} target_t;

static inline uint16_t *pixel_row(const target_t *t, int y)
{
    return t->out + (y - t->buf->y1) * t->stride - t->buf->x1;
}

/** Screen bounding box of a source rectangle rotated about (ax, ay) -> (px, py). */
static void rotated_box(float px, float py, float ax, float ay, float angle,
                        int x0, int y0, int x1, int y1, lv_area_t *box)
{
    const float c = cosf(angle), s = sinf(angle);
    float min_x = 1e9f, min_y = 1e9f, max_x = -1e9f, max_y = -1e9f;
    for (int i = 0; i < 4; ++i) {
        const float lx = (i & 1 ? x1 : x0) - ax, ly = (i & 2 ? y1 : y0) - ay;
        const float sx = px + c * lx - s * ly, sy = py + s * lx + c * ly;
        min_x = fminf(min_x, sx);
        max_x = fmaxf(max_x, sx);
        min_y = fminf(min_y, sy);
        max_y = fmaxf(max_y, sy);
    }
    box->x1 = (lv_coord_t)clampf(floorf(min_x) - 1, 0, LCD - 1);
    box->y1 = (lv_coord_t)clampf(floorf(min_y) - 1, 0, LCD - 1);
    box->x2 = (lv_coord_t)clampf(ceilf(max_x) + 1, 0, LCD - 1);
    box->y2 = (lv_coord_t)clampf(ceilf(max_y) + 1, 0, LCD - 1);
}

/** Nearest-neighbour rotated blit at full display resolution; 0 is transparent. */
static void blit_rotated(const target_t *t, const uint16_t *src, int width,
                         int x0, int y0, int x1, int y1,
                         float px, float py, float ax, float ay, float angle)
{
    lv_area_t box, area;
    rotated_box(px, py, ax, ay, angle, x0, y0, x1, y1, &box);
    if (!_lv_area_intersect(&area, &box, &t->clip)) return;
    const float c = cosf(angle), s = sinf(angle);
    const int32_t ic = (int32_t)lroundf(c * 65536), is = (int32_t)lroundf(s * 65536);
    for (int y = area.y1; y <= area.y2; ++y) {
        const float dx = area.x1 + 0.5f - px, dy = y + 0.5f - py;
        int32_t lx = (int32_t)lroundf((ax + c * dx + s * dy) * 65536);
        int32_t ly = (int32_t)lroundf((ay - s * dx + c * dy) * 65536);
        uint16_t *row = pixel_row(t, y);
        for (int x = area.x1; x <= area.x2; ++x) {
            const int sx = lx >> 16, sy = ly >> 16;
            if (sx >= x0 && sx < x1 && sy >= y0 && sy < y1) {
                const uint16_t color = src[sy * width + sx];
                if (color) row[x] = color;
            }
            lx += ic;
            ly -= is;
        }
    }
}

/** Horizontal extent [x0, x1] of the wheel disc on row y; false when the row misses it. */
static bool wheel_span(int y, int *x0, int *x1)
{
    if ((unsigned)(y - WHEEL_Y0) >= WHEEL_ROWS) return false;
    const wheel_row_t *row = &wheel_rows[y - WHEEL_Y0];
    *x0 = row->x0;
    *x1 = row->x1;
    return row->x0 <= row->x1;
}

/** Resample the wheel disc of the habitat through the rotation map. */
static void draw_wheel(const target_t *t, float angle)
{
    const float c = cosf(angle), s = sinf(angle);
    /* Map coordinates are Q12 and coefficients Q10, so products land in Q22. */
    const int32_t kxx = (int32_t)lroundf(WHEEL_RX * c * 1024), kxy = (int32_t)lroundf(WHEEL_RX * s * 1024);
    const int32_t kyx = (int32_t)lroundf(-WHEEL_RY * s * 1024), kyy = (int32_t)lroundf(WHEEL_RY * c * 1024);
    const int32_t kdx = (int32_t)lroundf(WHEEL_DX * 1024), kdy = (int32_t)lroundf(WHEEL_DY * 1024);
    const int32_t hub_x = (int32_t)lroundf(WHEEL_HUB_X * 4194304.0f);
    const int32_t hub_y = (int32_t)lroundf(WHEEL_HUB_Y * 4194304.0f);
    for (int y = LV_MAX(t->clip.y1, WHEEL_Y0); y <= LV_MIN(t->clip.y2, WHEEL_Y0 + WHEEL_ROWS - 1); ++y) {
        int x0, x1;
        if (!wheel_span(y, &x0, &x1)) continue;
        const wheel_texel_t *texel = wheel_texels + wheel_rows[y - WHEEL_Y0].offset;
        x0 = LV_MAX(t->clip.x1, x0);
        texel += x0 - wheel_rows[y - WHEEL_Y0].x0;
        x1 = LV_MIN(t->clip.x2, x1);
        uint16_t *row = pixel_row(t, y);
        for (int x = x0; x <= x1; ++x, ++texel) {
            const int32_t sx = (hub_x + kdx * texel->rho + kxx * texel->qx + kxy * texel->qy) >> 22;
            const int32_t sy = (hub_y + kdy * texel->rho + kyx * texel->qx + kyy * texel->qy) >> 22;
            row[x] = habitat[LV_CLAMP(0, sy, LCD - 1) * LCD + LV_CLAMP(0, sx, LCD - 1)];
        }
    }
}

static void draw_bubble(const target_t *t, int index)
{
    const int w = index ? BUBBLE1_W : BUBBLE0_W, h = index ? BUBBLE1_H : BUBBLE0_H;
    const uint16_t *src = bubbles + (index ? BUBBLE1_OFFSET : BUBBLE0_OFFSET);
    const lv_area_t box = { (lv_coord_t)(LCD / 2 - w / 2), BUBBLE_Y,
                            (lv_coord_t)(LCD / 2 - w / 2 + w - 1), (lv_coord_t)(BUBBLE_Y + h - 1) };
    lv_area_t area;
    if (!_lv_area_intersect(&area, &box, &t->clip)) return;
    for (int y = area.y1; y <= area.y2; ++y) {
        uint16_t *row = pixel_row(t, y);
        const uint16_t *line = src + (y - box.y1) * w - box.x1;
        for (int x = area.x1; x <= area.x2; ++x)
            if (line[x]) row[x] = line[x];
    }
}

static float tread_x(float sigma, int inset)
{
    return TREAD_CX - (TREAD_RX - inset) * sinf(sigma);
}

static float tread_y(float sigma, int inset)
{
    return TREAD_CY + (TREAD_RY - inset) * cosf(sigma);
}

/** Paint the scene directly into LVGL's active RGB565 buffer. */
static void draw_scene(lv_event_t *event)
{
    lv_area_t bounds;
    lv_obj_get_coords(lv_event_get_target(event), &bounds);
    if (lv_event_get_code(event) == LV_EVENT_COVER_CHECK) {
        if (_lv_area_is_in(lv_event_get_cover_area(event), &bounds, 0))
            lv_event_set_cover_res(event, LV_COVER_RES_COVER);
        return;
    }
    const int64_t start = esp_timer_get_time();
    lv_draw_ctx_t *ctx = lv_event_get_draw_ctx(event);
    target_t t = { ctx->buf, lv_area_get_width(ctx->buf_area), ctx->buf_area, { 0 } };
    if (!_lv_area_intersect(&t.clip, &bounds, ctx->clip_area)) return;
    const bool wheel_turned = fabsf(drawn.wheel) > 0.0005f;
    for (int y = t.clip.y1; y <= t.clip.y2; ++y) {
        uint16_t *row = pixel_row(&t, y);
        const uint16_t *line = habitat + y * LCD;
        int x0, x1;
        /* The rotated wheel overwrites its disc, so copy only the habitat around it. */
        if (wheel_turned && wheel_span(y, &x0, &x1) && x0 <= t.clip.x2 && x1 >= t.clip.x1) {
            if (x0 > t.clip.x1) memcpy(row + t.clip.x1, line + t.clip.x1, (x0 - t.clip.x1) * sizeof(*row));
            if (x1 < t.clip.x2) memcpy(row + x1 + 1, line + x1 + 1, (t.clip.x2 - x1) * sizeof(*row));
        } else {
            memcpy(row + t.clip.x1, line + t.clip.x1, lv_area_get_width(&t.clip) * sizeof(*row));
        }
    }
    if (wheel_turned) draw_wheel(&t, drawn.wheel);
    if (drawn.mode == MODE_TUMBLE) {
        blit_rotated(&t, ball, BALL, 0, 0, BALL, BALL,
                     tread_x(drawn.sigma, BALL_R), tread_y(drawn.sigma, BALL_R),
                     BALL / 2.0f, BALL / 2.0f, drawn.ball_angle);
    } else if (used_x1 > used_x0) {
        blit_rotated(&t, local, LOCAL_W, used_x0, used_y0, used_x1, used_y1,
                     tread_x(drawn.sigma, 0), tread_y(drawn.sigma, 0),
                     ANCHOR_X, ANCHOR_Y, drawn.sigma + drawn.tilt);
    }
    if (drawn.bubble >= 0) draw_bubble(&t, drawn.bubble);
    const int64_t spent = esp_timer_get_time() - start;
    draw_total += spent;
    if (spent > draw_max) draw_max = spent;
    ++draw_count;
}

/* --------------------------------------------------------------------- simulation */

static const char *mode_name(play_mode_t value)
{
    switch (value) {
    case MODE_RUN: return "run";
    case MODE_TUMBLE: return "tumble";
    case MODE_DIZZY: return "dizzy";
    }
    return "?";
}

typedef struct {
    const char *name;
    const uint8_t *start;
    const uint8_t *end;
} hamster_sound_t;

#define SOUND_DATA(id) \
    extern const uint8_t sound_##id##_start[] asm("_binary_sfx_" #id "_pcm_start"); \
    extern const uint8_t sound_##id##_end[] asm("_binary_sfx_" #id "_pcm_end"); \
    static const hamster_sound_t sound_##id = { #id, sound_##id##_start, sound_##id##_end }
SOUND_DATA(step);
SOUND_DATA(turn);
SOUND_DATA(slip);
SOUND_DATA(dizzy);

static int64_t sound_busy_until;

/** Submit a sound without waiting for the I2S worker. Action cues take priority over footsteps. */
static void play_sound(const hamster_sound_t *sound, bool action)
{
    const int64_t now = esp_timer_get_time();
    if ((!action && now < sound_busy_until) || sdgoods_audio_get_volume() == 0) return;
    const size_t samples = (sound->end - sound->start) / sizeof(int16_t);
    if (sugarat_audio_play_pcm((const int16_t *)sound->start, samples) != ESP_OK) return;
    sound_busy_until = now + (int64_t)samples * 1000000 / SUGARAT_AUDIO_SAMPLE_RATE + 160000;
    if (action) ESP_LOGI("hamster_audio", "cue=%s", sound->name);
}

static void set_mode(play_mode_t next, float rate_dps)
{
    if (next == mode) return;
    ESP_LOGI(TAG, "play mode=%s rate=%.0f laps=%d", mode_name(next), rate_dps, laps);
    mode = next;
    if (next == MODE_TUMBLE) play_sound(&sound_slip, true);
    if (next == MODE_DIZZY && laps > 0) play_sound(&sound_dizzy, true);
}

/** Advance the cage/wheel/hamster model by dt seconds; w is the cage rate in rad/s. */
static void step(float w, float dt)
{
    const bool turning = fabsf(w) > 0.5f;
    if (turning) jog_left = 0;
    tread += (-w - (jog_left > 0 ? jog_speed : 0) - tread) * fminf(dt * AXLE, 1.0f);
    wheel_angle = wrap(wheel_angle + (tread + w) * dt);

    if (mode == MODE_TUMBLE) {
        alpha_rate += (-GRAVITY * sinf(alpha) + GRIP * (tread - alpha_rate)) * dt;
        alpha += alpha_rate * dt;
        tumble_travel += fabsf(alpha_rate) * dt;
        if (alpha > half_turn || alpha < -half_turn) {
            alpha = wrap(alpha);
            ++laps;
        }
        spin = wrap(spin - (alpha_rate - tread) * ((float)TREAD_RY / BALL_R) * dt);
        run = 0;
        const bool calm = fabsf(alpha) < 0.3f && fabsf(alpha_rate) < 1.2f && fabsf(tread) < 2.5f;
        settle_time = calm ? settle_time + dt : 0;
        if (settle_time >= 0.35f) {
            dizzy_left = 0.9f + 0.7f * (float)LV_MIN(laps, 4) + (tumble_travel > 1.5f ? 0.4f : 0);
            alpha_rate = 0;
            set_mode(MODE_DIZZY, w / radians);
        }
        return;
    }

    const float top = mode == MODE_DIZZY ? DIZZY_RUN_MAX : RUN_MAX;
    run = clampf(-4.0f * alpha - tread, -top, top);
    alpha_rate = tread + run;
    alpha += alpha_rate * dt;
    if (fabsf(alpha) > SLIP_ANGLE) {
        laps = 0;
        tumble_travel = 0;
        settle_time = 0;
        set_mode(MODE_TUMBLE, w / radians);
        return;
    }
    if (mode == MODE_DIZZY) {
        dizzy_left -= dt;
        if (dizzy_left <= 0) set_mode(MODE_RUN, w / radians);
    }
}

/** Per-frame behaviour that does not need sub-stepping: facing, gait, idle life. */
static void animate(float w, float dt)
{
    clock_s += dt;
    /* Running toward +sigma (run > 0) means facing left in sprite space. */
    const float wanted = run > 0.6f ? -1.0f : run < -0.6f ? 1.0f : facing;
    if (turn_freeze >= 0) {
        turn_t = turn_freeze;
    } else if (turn_t >= 0) {
        turn_t += dt * 1000.0f / TURN_MS;
        if (turn_t >= 1.0f) {
            facing = -facing;
            turn_t = -1.0f;
        }
    } else if (wanted != facing) {
        reverse_time += dt;
        if (mode == MODE_TUMBLE) facing = wanted;
        else if (reverse_time >= 0.08f) {
            turn_t = 0;
            play_sound(&sound_turn, true);
        }
    } else {
        reverse_time = 0;
    }
    const float speed = fabsf(run);
    const float prior_gait = gait;
    gait = wrap(gait + fminf(speed * 0.8f, 5.5f) * full_turn * dt);
    if (mode == MODE_RUN && fabsf(w) > 0.5f && speed > 0.7f && turn_t < 0 && gait < prior_gait)
        play_sound(&sound_step, false);

    const bool resting = mode == MODE_RUN && speed < 0.3f && fabsf(w) < 0.3f;
    idle_time = resting ? idle_time + dt : 0;
    if (jog_left > 0) {
        jog_left -= dt;
        if (jog_left <= 0) next_jog = 9.0f + 8.0f * random_unit();
    } else if (resting && idle_time > next_jog) {
        jog_left = 2.0f + 2.0f * random_unit();
        jog_speed = (random_unit() < 0.5f ? -1.0f : 1.0f) * (2.5f + 1.5f * random_unit());
        idle_time = 0;
    }
    blink_wait -= dt;
    if (blink_wait < -0.12f) blink_wait = 2.2f + 2.5f * random_unit();

    if (!intro_hidden) {
        intro_motion = fabsf(w) > 1.0f ? intro_motion + dt : intro_motion;
        if (intro_motion > 0.6f || clock_s > 6.0f) intro_hidden = true;
    }
}

static look_t current_look(float lead)
{
    look_t look = { 0 };
    look.mode = mode;
    look.sigma = wrap(orientation + lead + alpha);
    look.wheel = wheel_angle;
    look.ball_angle = wrap(look.sigma + spin);
    look.stars = -1;
    look.drop = -1;
    look.bubble = intro_hidden ? (mode == MODE_DIZZY && laps > 0 ? 1 : -1) : 0;
    if (mode == MODE_TUMBLE) return look;

    const float speed = fabsf(run);
    float motion = fminf(speed / 2.5f, 1.0f);
    float scale = facing;
    if (turn_t >= 0) {
        /* Hop and rear up; the body narrows to half width and flips at the top of the
         * hop, so it never collapses to a sliver. Legs tuck during the hop. */
        const float e = turn_t * turn_t * (3.0f - 2.0f * turn_t);
        const float arc = sinf(half_turn * turn_t);
        scale = (e < 0.5f ? facing : -facing) * (1.0f - 0.5f * sinf(half_turn * e));
        look.tilt = -0.28f * arc * scale;
        look.bob = -(int)lroundf(9.0f * arc);
        motion = 0;
    }
    look.scale = (int16_t)lroundf(fabsf(scale) < 0.12f ? (scale < 0 ? -8 : 8) : scale * 64);
    /* Bounding gait: hind legs push together, front legs reach together. */
    static const float offsets[4] = { 0.5f, 3.64f, 0.0f, 3.14f };
    static const float centre[4] = { -1, 0, -1, 0 }, reach[4] = { 3, 2.5f, 3, 2.5f };
    for (int i = 0; i < 4; ++i) {
        const float phase = gait + offsets[i];
        look.paw[i] = (int)lroundf(centre[i] + reach[i] * motion * sinf(phase) + (i & 1 ? 0.5f * motion : 0));
        look.lift[i] = turn_t >= 0 ? 1 : (int)lroundf(fmaxf(0, cosf(phase)) * 2.0f * motion);
    }
    if (turn_t < 0 && motion > 0.1f) {
        look.bob = (int)lroundf(-fabsf(sinf(gait)) * 2.5f * motion);
        look.tilt = 0.035f * motion * sinf(gait) * (facing > 0 ? -1.0f : 1.0f);
    } else if (turn_t < 0) {
        look.bob = sinf(clock_s * 2.6f) > 0.6f ? -1 : 0;
    }
    const bool mid_turn = turn_t >= 0.3f && turn_t < 0.7f;
    look.frame = mode == MODE_DIZZY ? 2 : blink_wait < 0 || mid_turn ? 1 : 0;
    if (mode == MODE_DIZZY) {
        look.stars = (int)(clock_s * 64 / 0.9f) & 63;
        look.sway = (int)lroundf(3.0f * sinf(clock_s * 5.0f));
    }
    if (mode == MODE_RUN && speed > 0.82f * RUN_MAX) look.drop = (int)(clock_s * 30) % 8;
    return look;
}

static bool same_pet(const look_t *a, const look_t *b)
{
    return a->mode == b->mode && a->frame == b->frame && a->bob == b->bob && a->sway == b->sway &&
           a->stars == b->stars && a->drop == b->drop && a->scale == b->scale &&
           !memcmp(a->paw, b->paw, sizeof(a->paw)) && !memcmp(a->lift, b->lift, sizeof(a->lift));
}

static void invalidate(const lv_area_t *area)
{
    if (area->x2 >= area->x1 && area->y2 >= area->y1) lv_obj_invalidate_area(image, area);
}

static void present(const look_t *look)
{
    const bool pet_changed = !drawn_valid || !same_pet(look, &drawn);
    const bool moved = !drawn_valid || fabsf(wrap(look->sigma - drawn.sigma)) > 0.004f ||
                       fabsf(look->tilt - drawn.tilt) > 0.004f ||
                       (look->mode == MODE_TUMBLE && fabsf(wrap(look->ball_angle - drawn.ball_angle)) > 0.01f);
    const bool wheel_moved = !drawn_valid || fabsf(wrap(look->wheel - drawn.wheel)) > 0.002f;
    const bool bubble_changed = !drawn_valid || look->bubble != drawn.bubble;
    if (!pet_changed && !moved && !wheel_moved && !bubble_changed) return;

    if (wheel_moved) invalidate(&wheel_box);
    if (pet_changed || moved) invalidate(&pet_box);
    if (bubble_changed) invalidate(&bubble_box);
    if (pet_changed && look->mode != MODE_TUMBLE) compose_pet(look);
    const float wheel = drawn.wheel;
    const bool first = !drawn_valid;
    drawn = *look;
    /* Unredrawn wheel pixels keep the latched angle, so partial redraws stay seamless. */
    if (!wheel_moved && !first) drawn.wheel = wheel;
    drawn_valid = true;
    const float sigma = look->sigma;
    if (look->mode == MODE_TUMBLE) {
        rotated_box(tread_x(sigma, BALL_R), tread_y(sigma, BALL_R), BALL / 2.0f, BALL / 2.0f,
                    look->ball_angle, 0, 0, BALL, BALL, &pet_box);
    } else if (used_x1 > used_x0) {
        rotated_box(tread_x(sigma, 0), tread_y(sigma, 0), ANCHOR_X, ANCHOR_Y, sigma + look->tilt,
                    used_x0, used_y0, used_x1, used_y1, &pet_box);
    }
    if (look->bubble >= 0) {
        const int w = look->bubble ? BUBBLE1_W : BUBBLE0_W, h = look->bubble ? BUBBLE1_H : BUBBLE0_H;
        bubble_box = (lv_area_t){ (lv_coord_t)(LCD / 2 - w / 2), BUBBLE_Y,
                                  (lv_coord_t)(LCD / 2 - w / 2 + w - 1), (lv_coord_t)(BUBBLE_Y + h - 1) };
    } else {
        bubble_box = (lv_area_t){ 1, 1, 0, 0 };
    }
    invalidate(&pet_box);
    if (bubble_changed) invalidate(&bubble_box);
    ++frame_count;
}

static void keep_refresh_period(uint32_t period)
{
    lv_timer_t *timer = _lv_disp_get_refr_timer(lv_disp_get_default());
    if (timer && timer->period != period) lv_timer_set_period(timer, period);
}

static void screen_deleted(lv_event_t *event)
{
    (void)event;
    screen = NULL;
    image = NULL;
    sdgoods_lvgl_fast_loop(false);
    keep_refresh_period(LV_DISP_DEF_REFR_PERIOD);
}

void ui_helloapp_start(void)
{
    if (screen) {
        lv_scr_load(screen);
        return;
    }
    if (!local) {
        local = heap_caps_malloc(LOCAL_W * LOCAL_H * sizeof(*local), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        if (!local) local = heap_caps_malloc(LOCAL_W * LOCAL_H * sizeof(*local), MALLOC_CAP_SPIRAM);
        ESP_ERROR_CHECK(local ? ESP_OK : ESP_ERR_NO_MEM);
        sprite = heap_caps_malloc(PET_W * SPRITE_H * sizeof(*sprite), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        if (!sprite) sprite = heap_caps_malloc(PET_W * SPRITE_H * sizeof(*sprite), MALLOC_CAP_SPIRAM);
        ESP_ERROR_CHECK(sprite ? ESP_OK : ESP_ERR_NO_MEM);
        habitat = heap_caps_malloc(LCD * LCD * sizeof(*habitat), MALLOC_CAP_SPIRAM);
        ESP_ERROR_CHECK(habitat ? ESP_OK : ESP_ERR_NO_MEM);
        memcpy(habitat, habitat_flash, LCD * LCD * sizeof(*habitat));
        const size_t map_bytes = WHEEL_ROWS * sizeof(wheel_row_t) + WHEEL_MAP_PIXELS * sizeof(wheel_texel_t);
        uint8_t *map = heap_caps_malloc(map_bytes, MALLOC_CAP_SPIRAM);
        ESP_ERROR_CHECK(map ? ESP_OK : ESP_ERR_NO_MEM);
        memcpy(map, wheel_map_flash, map_bytes);
        wheel_rows = (const wheel_row_t *)map;
        wheel_texels = (const wheel_texel_t *)(map + WHEEL_ROWS * sizeof(wheel_row_t));
        sugarat_imu_config_t config = sugarat_imu_default_config();
        config.step_detection = false;
        const esp_err_t status = sugarat_imu_start(&config);
        imu_started = status == ESP_OK;
        if (!imu_started) ESP_LOGE(TAG, "IMU unavailable: %s", esp_err_to_name(status));
        sdgoods_screenshot_init();
    }

    screen = lv_obj_create(NULL);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(screen, lv_color_black(), 0);
    lv_obj_add_event_cb(screen, screen_deleted, LV_EVENT_DELETE, NULL);
    image = lv_obj_create(screen);
    lv_obj_remove_style_all(image);
    lv_obj_clear_flag(image, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(image, LCD, LCD);
    lv_obj_center(image);
    lv_obj_add_event_cb(image, draw_scene, LV_EVENT_DRAW_MAIN, NULL);
    lv_obj_add_event_cb(image, draw_scene, LV_EVENT_COVER_CHECK, NULL);
    wheel_box = (lv_area_t){ LCD, WHEEL_Y0, 0, WHEEL_Y0 + WHEEL_ROWS - 1 };
    for (int i = 0; i < WHEEL_ROWS; ++i) {
        if (wheel_rows[i].x0 > wheel_rows[i].x1) continue;
        wheel_box.x1 = LV_MIN(wheel_box.x1, wheel_rows[i].x0);
        wheel_box.x2 = LV_MAX(wheel_box.x2, wheel_rows[i].x1);
    }
    intro_hidden = false;
    intro_motion = clock_s = 0;
    next_jog = 7.0f;
    blink_wait = 2.0f;
    drawn_valid = false;
    pet_box = bubble_box = (lv_area_t){ 1, 1, 0, 0 };
    present((const look_t[]){ current_look(0) });
    lv_obj_invalidate(image);
    lv_scr_load(screen);
    sdgoods_app_shell_bind(screen);
    previous_poll = last_frame = last_report = app_started = esp_timer_get_time();
    ESP_LOGI(TAG, "scene ready: 360x360 native, IMU=%s, initial mode=run", imu_started ? "ready" : "unavailable");
}

static void read_imu(int64_t now)
{
    sugarat_imu_state_t sample;
    if (!imu_started || !sugarat_imu_get_state(&sample) || !sample.sensor_ok ||
        sample.samples == sample_number)
        return;
    sample_number = sample.samples;
    last_sample = now;
    angular_rate = sample.gyro_dps.z;
    peak_rate = fmaxf(peak_rate, fabsf(angular_rate));
    const float ax = sample.accel_g.x, ay = sample.accel_g.y, az = sample.accel_g.z;
    const float plane = ax * ax + ay * ay;
    const float norm = plane + az * az;
    if (plane > 0.16f && norm > 0.77f && norm < 1.25f && fabsf(angular_rate) < 90) {
        const float gravity = atan2f(ax, ay);
        if (!orientation_ready) orientation = gravity;
        else orientation = wrap(orientation + 0.018f * wrap(gravity - orientation));
    }
    orientation_ready = true;
    if (sample.still && fabsf(angular_rate) < 1.2f) angular_rate = 0;
}

static void handle_console(int64_t now)
{
    const char command = (char)atomic_exchange(&console_command, 0);
    switch (command) {
    case '~':
        benchmark_start = now;
        benchmark_samples = sample_number;
        sim_angle = orientation;
        frame_count = draw_count = 0;
        draw_total = draw_max = gap_max = 0;
        ESP_LOGI(TAG, "spin benchmark: run, reverse, fast spin, rest");
        break;
    case 'j': sim_hold = true; sim_rate = 150; sim_angle = orientation; break;
    case 'k': sim_hold = true; sim_rate = -150; sim_angle = orientation; break;
    case 'm': sim_hold = true; sim_rate = 720; sim_angle = orientation; break;
    case 'n': sim_hold = false; turn_freeze = -1.0f; turn_t = -1.0f; break;
    case 't': turn_freeze = turn_freeze < 0 ? 0.3f : turn_freeze + 0.2f; break;
    default: break;
    }
}

void ui_helloapp_poll(void)
{
    if (!screen || lv_scr_act() != screen) return;
    const int64_t now = esp_timer_get_time();
    const float dt = fminf((now - previous_poll) * 0.000001f, 0.05f);
    previous_poll = now;
    read_imu(now);
    handle_console(now);

    float rate = now - last_sample < 45000 ? angular_rate : 0.0f;
    orientation = wrap(orientation + rate * radians * dt);
    if (benchmark_start) {
        const int64_t elapsed = now - benchmark_start;
        static const float test_rates[] = { 150, 150, -150, -150, 720, 720, 720, 0, 0, 0, 0, 0 };
        const int phase = (int)(elapsed / 500000);
        if (phase >= (int)(sizeof(test_rates) / sizeof(test_rates[0]))) {
            ESP_LOGI(TAG, "benchmark done frames=%" PRIu32 " draw avg/max=%lld/%lld us max gap=%lld us"
                     " IMU samples=%" PRIu32 " flush failures=0",
                     frame_count, (long long)(draw_count ? draw_total / draw_count : 0),
                     (long long)draw_max, (long long)gap_max, sample_number - benchmark_samples);
            benchmark_start = 0;
            last_report = now;
        } else {
            sim_rate = test_rates[phase];
            sim_angle = wrap(sim_angle + sim_rate * radians * dt);
            rate = sim_rate;
            orientation = sim_angle;
        }
    } else if (sim_hold) {
        sim_angle = wrap(sim_angle + sim_rate * radians * dt);
        rate = sim_rate;
        orientation = sim_angle;
    }

    const float w = rate * radians;
    for (float left = dt; left > 0; left -= 0.004f) step(w, fminf(left, 0.004f));
    animate(w, dt);

    sdgoods_lvgl_fast_loop(true);
    keep_refresh_period(REFRESH_MS);
    const int64_t gap = now - last_frame;
    if (gap >= FRAME_US) {
        if (gap > gap_max && gap < 500000) gap_max = gap;
        /* Lead the cage angle by the scan-out and display latency. */
        present((const look_t[]){ current_look(w * 0.028f) });
        last_frame = now;
    }

    if (!benchmark_start && now - last_report >= 3000000) {
        ESP_LOGI(TAG, "spin peak=%.0f dps mode=%s frames=%" PRIu32 " draw avg/max=%lld/%lld us"
                 " max gap=%lld us samples=%" PRIu32,
                 peak_rate, mode_name(mode), frame_count,
                 (long long)(draw_count ? draw_total / draw_count : 0), (long long)draw_max,
                 (long long)gap_max, sample_number);
        last_report = now;
        peak_rate = 0;
        frame_count = draw_count = 0;
        draw_total = draw_max = gap_max = 0;
    }
}

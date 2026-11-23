/*
 * Wolf Companion: an offline pet built from the 287 pre-rendered frames of the original Wolf Companion
 * 1.0.0 image, with an optical-flow in-between frame after each one (tools/interpolate.py), packed as
 * 16-row JPEG bands (tools/build_pack.py).
 *
 * Rendering: each screen frame is a view (frame a blended toward frame b, plus overlay sprites). Only
 * the rectangle that changed is sent: the union of the old and new frame crops, or just the overlay
 * boxes when only overlays moved. Two cores decode the two JPEG bands of each 32-row panel strip with
 * esp_new_jpeg straight into internal RAM, so nothing passes through PSRAM or the LVGL draw buffer.
 * Frames are presented on a fixed 30 fps grid; clips and horizontal gaze moves step one frame per slot,
 * and the remaining jumps (gaze row changes, clip starts and ends) crossfade. LVGL draws the same view through draw_view()
 * whenever its layers cover the screen.
 *
 * Play: tap = turn and think (?), double tap or shake = dizzy, hold = jump onto the sofa, drag or tilt
 * = head follows, rest = doze (Z), tap or pick up = wake.
 */
#include "wolf_app.h"

#include <inttypes.h>
#include <math.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "sdgoods_board.h"
#include "sugarat_frame.h"
#include "sugarat_imu.h"
#include "wolf_frames.h"
#include "wolf_pack.h"
#include "wolf_sprites.h"

static const char *TAG = "wolf";

enum { LCD = 360, STRIP_ROWS = 32, REFRESH_MS = 16, REPORT_US = 10000000, SLOT_US = 2 * SUGARAT_FRAME_SLOT_60HZ_US };

/* Frame map in original frame numbers (SRC() gives the pack index); frames 0, 38, 72, 106, 146 and the
 * gaze centre 271 share one pose. */
enum {
    F_GRIN = 0, F_THINK = 38, F_THINK_END = 71,
    F_SLEEP = 72, F_DOZE = 86, F_DOZE_END = 89, F_WAKE = 90, F_WAKE_END = 106,
    F_DIZZY = 106, F_DIZZY_LOOP = 124, F_DIZZY_LOOP_END = 136, F_DIZZY_END = 146,
    F_SOFA = 146, F_SOFA_END = 255,
    F_GAZE_TOP = 256, F_GAZE_MID = 265, F_GAZE_LOW = 278,
};

/* Interaction tuning. */
static const int64_t TAP_US = 350000, DOUBLE_TAP_US = 380000, HOLD_US = 500000;
static const int TAP_SLOP_PX = 18;
static const float SLEEP_AFTER_S = 30.0f, GRIN_MIN_S = 9.0f, GRIN_SPAN_S = 7.0f;
static const float TILT_FULL_G = 0.30f;     /* tilt that turns the head fully */
static const float TILT_BASELINE_S = 3.5f;  /* rest pose adapts this fast, so the head drifts back */
static const int64_t TOUCH_GAZE_HOLD_US = 1200000;
static const int64_t GAZE_ROW_FADE_US = 2 * SLOT_US;
static const int GAZE_FAST_DISTANCE = 10; /* in-between columns; farther targets move two per slot */
static const int64_t CLIP_FADE_US = 5 * SLOT_US, RETURN_FADE_US = 8 * SLOT_US;
static const int DOZE_FPS = 8;

#define SRC(frame) (wolf_src_frame[frame])

typedef enum { OV_NONE, OV_QUESTION, OV_SLEEP, OV_STARS } overlay_kind_t;

/** One clip segment: original frames from..to, played over pack frames at fps (30 or 15 keeps an even
 *  cadence; 30 shows 15 original frames per second), then hold_ms on the last. */
typedef struct {
    int16_t from, to;
    uint8_t fps;
    uint16_t hold_ms;
} step_t;

typedef struct {
    const char *name;
    const step_t *steps;
    int count;
    overlay_kind_t overlay;
} script_t;

#define SCRIPT(id, overlay, ...) \
    static const step_t id##_steps[] = { __VA_ARGS__ }; \
    static const script_t id = { #id, id##_steps, sizeof(id##_steps) / sizeof(step_t), overlay }

SCRIPT(think, OV_QUESTION, { F_THINK, F_THINK_END, 30, 900 }, { F_THINK_END, F_THINK, 30, 0 });
SCRIPT(grin, OV_NONE, { F_GRIN, F_THINK, 30, 0 });
SCRIPT(sofa, OV_NONE, { F_SOFA, F_SOFA_END, 30, 300 });
SCRIPT(dizzy, OV_STARS, { F_DIZZY, F_DIZZY_LOOP, 30, 0 }, { F_DIZZY_LOOP, F_DIZZY_LOOP_END, 30, 0 },
       { F_DIZZY_LOOP_END, F_DIZZY_LOOP, 30, 0 }, { F_DIZZY_LOOP, F_DIZZY_LOOP_END, 30, 300 },
       { F_DIZZY_LOOP_END, F_DIZZY_END, 30, 0 });
SCRIPT(doze, OV_SLEEP, { F_SLEEP, F_DOZE, 30, 0 });
SCRIPT(wake, OV_NONE, { F_WAKE, F_WAKE_END, 30, 0 });

typedef enum { MODE_GAZE, MODE_SCRIPT, MODE_ASLEEP } wolf_mode_t;

/* ------------------------------------------------------------------ rendering resources */

extern const uint8_t sprite_pixels[] asm("_binary_wolf_sprites_bin_start");
extern const uint8_t pack_start[] asm("_binary_wolf_frames_pack_start");
extern const uint8_t pack_end[] asm("_binary_wolf_frames_pack_end");
static const wolf_sprite_set_t sprite_set = { wolf_sprites, WOLF_SPRITE_COUNT, sprite_pixels };
static wolf_pack_t pack;
static bool pack_ok;
static wolf_scratch_t scratch[STRIP_ROWS / 16];
static uint16_t *fallback_band;
static sugarat_frame_stream_t *stream;
static sugarat_frame_pacer_t pacer;
static sugarat_frame_stats_t stats;
static wolf_view_t view, drawn;
static bool drawn_valid, stream_failed, paused;
static wolf_rect_t shown_frames, shown_overlays; /* what the panel holds outside black */
static uint32_t decode_errors, late_frames, missed_slots;
static int64_t last_present;
static int64_t send_total_us, region_px_total;

static lv_obj_t *screen, *image;

/* ------------------------------------------------------------------ behaviour state */

static wolf_mode_t mode;
static const script_t *script;
static int step_index;
static int64_t step_start, script_start, hold_until;
static int clip_frame;

static int fade_from = -1;
static int64_t fade_start, fade_us;

static int gaze_row = 1, gaze_col = 12;
static int64_t next_gaze_step;
static float target_x, target_y; /* -1..1, +y looks up */

static bool imu_started;
static uint32_t imu_samples, shake_seen, pickup_seen;
static sugarat_imu_vec3_t baseline;
static bool baseline_ready;
static float tilt_x, tilt_y;

static bool pressed, dragging, hold_fired;
static lv_point_t press_point, touch_point;
static int64_t press_start, last_tap, last_touch_gaze, last_activity, next_grin;

static atomic_int console_command;
static int64_t bench_until;

/* ------------------------------------------------------------------ helpers */

static float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }
static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
static float random_unit(void) { return (esp_random() & 0xffff) / 65535.0f; }

static wolf_rect_t rect_union(wolf_rect_t a, wolf_rect_t b)
{
    if (!a.w || !a.h) return b;
    if (!b.w || !b.h) return a;
    const int x0 = LV_MIN(a.x0, b.x0), y0 = LV_MIN(a.y0, b.y0);
    const int x1 = LV_MAX(a.x0 + a.w, b.x0 + b.w), y1 = LV_MAX(a.y0 + a.h, b.y0 + b.h);
    return (wolf_rect_t){ x0, y0, x1 - x0, y1 - y0 };
}

/* Gaze columns count in-between poses: the middle row has 25 (13 original poses), the up and down rows
 * 17 (9 poses) spanning middle columns 2..22. */
static int gaze_frame(int row, int col)
{
    return SRC(row == 0 ? F_GAZE_TOP : row == 1 ? F_GAZE_MID : F_GAZE_LOW) + col;
}

static float mid_column(int row, int col) { return row == 1 ? (float)col : col / 0.8f + 2.0f; }
static int column_in(int row, float mid) { return row == 1 ? clampi(lroundf(mid), 0, 24) : clampi(lroundf((mid - 2.0f) * 0.8f), 0, 16); }

static int dominant_frame(void) { return view.mix * 2 >= WOLF_MIX_MAX ? view.b : view.a; }

static void begin_fade(int from, int64_t now, int64_t us)
{
    fade_from = from;
    fade_start = now;
    fade_us = us;
}

static const char *mode_name(void)
{
    return mode == MODE_GAZE ? "gaze" : mode == MODE_ASLEEP ? "asleep" : script->name;
}

/* ------------------------------------------------------------------ behaviour */

static void start_script(const script_t *next, int64_t now)
{
    begin_fade(dominant_frame(), now, CLIP_FADE_US);
    script = next;
    step_index = 0;
    step_start = script_start = now;
    hold_until = 0;
    clip_frame = SRC(next->steps[0].from);
    mode = MODE_SCRIPT;
    ESP_LOGI(TAG, "play %s", next->name);
}

static void return_to_gaze(int64_t now)
{
    begin_fade(dominant_frame(), now, RETURN_FADE_US);
    mode = MODE_GAZE;
    script = NULL;
    next_gaze_step = now + RETURN_FADE_US;
    next_grin = now + (int64_t)((GRIN_MIN_S + GRIN_SPAN_S * random_unit()) * 1e6f);
}

static void advance_script(int64_t now)
{
    const step_t *s = &script->steps[step_index];
    if (hold_until) {
        if (now < hold_until) return;
        hold_until = 0;
    } else {
        const int from = SRC(s->from), span = SRC(s->to) - from;
        /* Rounded to the nearest source frame, so presentation-slot jitter never repeats or skips one. */
        const int travelled = (int)(((now - step_start) * s->fps + 500000) / 1000000);
        if (travelled < abs(span)) {
            clip_frame = from + (span > 0 ? travelled : -travelled);
            return;
        }
        clip_frame = SRC(s->to);
        if (s->hold_ms) {
            hold_until = now + s->hold_ms * 1000LL;
            return;
        }
    }
    if (++step_index < script->count) {
        step_start = now;
        clip_frame = SRC(script->steps[step_index].from);
        return;
    }
    if (script == &doze) {
        mode = MODE_ASLEEP;
        step_start = now;
        ESP_LOGI(TAG, "asleep");
        return;
    }
    return_to_gaze(now);
}

static void step_gaze(int64_t now)
{
    if (now < next_gaze_step) return;
    const int want_row = target_y > 0.45f ? 0 : target_y < -0.45f ? 2 : 1;
    const float want_mid = 12.0f + 12.0f * target_x;
    if (gaze_row != want_row) {
        /* Rows have no in-between poses: crossfade to the neighbouring row. */
        const float mid = mid_column(gaze_row, gaze_col);
        begin_fade(gaze_frame(gaze_row, gaze_col), now, GAZE_ROW_FADE_US);
        gaze_row += want_row > gaze_row ? 1 : -1;
        gaze_col = column_in(gaze_row, mid);
        next_gaze_step = now + GAZE_ROW_FADE_US;
        return;
    }
    /* Along a row neighbouring frames are in-betweens: step one per slot, two when far behind. */
    const int want_col = column_in(gaze_row, want_mid), distance = abs(want_col - gaze_col);
    if (!distance) return;
    const int step = distance > GAZE_FAST_DISTANCE ? 2 : 1;
    gaze_col += want_col > gaze_col ? step : -step;
    next_gaze_step = now + SLOT_US;
}

static void on_tap(int64_t now)
{
    last_activity = now;
    if (mode == MODE_ASLEEP || script == &doze) {
        start_script(&wake, now);
        return;
    }
    if (last_tap && now - last_tap < DOUBLE_TAP_US) {
        last_tap = 0;
        if (script != &dizzy) start_script(&dizzy, now);
        return;
    }
    last_tap = now;
    if (mode != MODE_SCRIPT || script == &grin) start_script(&think, now);
}

static void on_hold(int64_t now)
{
    last_activity = now;
    if (mode == MODE_ASLEEP) start_script(&wake, now);
    else if (script != &sofa) start_script(&sofa, now);
}

static void on_shake(int64_t now)
{
    last_activity = now;
    if (mode == MODE_ASLEEP || script == &doze) start_script(&wake, now);
    else if (script != &dizzy) start_script(&dizzy, now);
}

static void read_imu(int64_t now, float dt)
{
    sugarat_imu_state_t s;
    if (!imu_started || !sugarat_imu_get_state(&s) || !s.sensor_ok || s.samples == imu_samples) return;
    imu_samples = s.samples;
    if (!baseline_ready) {
        baseline = s.accel_g;
        baseline_ready = true;
        shake_seen = s.shake_count;
        pickup_seen = s.pickup_count;
    }
    const float k = clampf(dt / TILT_BASELINE_S, 0.0f, 1.0f);
    baseline.x += (s.accel_g.x - baseline.x) * k;
    baseline.y += (s.accel_g.y - baseline.y) * k;
    /* Look toward the edge that tips down: specific force on that axis turns negative. */
    tilt_x = clampf(-(s.accel_g.x - baseline.x) / TILT_FULL_G, -1.0f, 1.0f);
    tilt_y = clampf(-(s.accel_g.y - baseline.y) / TILT_FULL_G, -1.0f, 1.0f);
    if (fabsf(tilt_x) > 0.35f || fabsf(tilt_y) > 0.35f) last_activity = now;
    if (s.shake_count != shake_seen) {
        shake_seen = s.shake_count;
        on_shake(now);
    }
    if (s.pickup_count != pickup_seen) {
        pickup_seen = s.pickup_count;
        last_activity = now;
        if (mode == MODE_ASLEEP) start_script(&wake, now);
    }
}

static void touch_event(lv_event_t *event)
{
    const lv_event_code_t code = lv_event_get_code(event);
    lv_indev_t *indev = lv_indev_get_act();
    const int64_t now = esp_timer_get_time();
    if (indev) lv_indev_get_point(indev, &touch_point);
    if (code == LV_EVENT_PRESSED) {
        pressed = true;
        dragging = hold_fired = false;
        press_point = touch_point;
        press_start = now;
        last_activity = now;
    } else if (code == LV_EVENT_PRESSING && pressed) {
        if (!dragging && (abs(touch_point.x - press_point.x) > TAP_SLOP_PX || abs(touch_point.y - press_point.y) > TAP_SLOP_PX))
            dragging = true;
        last_activity = now;
    } else if ((code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) && pressed) {
        pressed = false;
        if (code == LV_EVENT_RELEASED && !dragging && !hold_fired && now - press_start < TAP_US) on_tap(now);
        if (dragging) last_touch_gaze = now;
    }
}

static void update_behaviour(int64_t now, float dt)
{
    read_imu(now, dt);
    if (pressed && !dragging && !hold_fired && now - press_start >= HOLD_US) {
        hold_fired = true;
        on_hold(now);
    }

    if (bench_until) {
        const float t = (bench_until - now) * 1e-6f;
        target_x = cosf(t * 2.2f);
        target_y = sinf(t * 1.4f);
        last_activity = now;
    } else if (pressed && dragging) {
        target_x = clampf((touch_point.x - LCD / 2) / 130.0f, -1.0f, 1.0f);
        target_y = clampf((LCD / 2 - touch_point.y) / 130.0f, -1.0f, 1.0f);
        last_touch_gaze = now;
    } else if (now - last_touch_gaze > TOUCH_GAZE_HOLD_US) {
        target_x = tilt_x;
        target_y = tilt_y;
    }

    switch (mode) {
    case MODE_SCRIPT:
        advance_script(now);
        break;
    case MODE_ASLEEP:
        break;
    case MODE_GAZE:
        step_gaze(now);
        if (now - last_activity > (int64_t)(SLEEP_AFTER_S * 1e6f)) {
            start_script(&doze, now);
        } else if (now > next_grin && !pressed && gaze_row == 1 && abs(gaze_col - 12) <= 2) {
            start_script(&grin, now);
        }
        break;
    }
}

/* ------------------------------------------------------------------ view */

static void add_overlay(int sprite, float x, float y, float alpha)
{
    if (view.overlay_count >= WOLF_MAX_OVERLAYS || alpha <= 0.01f) return;
    view.overlays[view.overlay_count++] = (wolf_overlay_t){ sprite, lroundf(x), lroundf(y), lroundf(clampf(alpha, 0, 1) * 255) };
}

static void build_overlays(int64_t now)
{
    view.overlay_count = 0;
    const float t = now * 1e-6f;
    if (mode == MODE_SCRIPT && script->overlay == OV_QUESTION) {
        /* Appear as the head turns up, leave while it turns back. */
        const float in = clampf((clip_frame - SRC(52)) / 16.0f, 0, 1);
        const float alpha = step_index == 0 ? in : in * clampf(1.0f - (now - step_start) * 1e-6f / 0.5f, 0, 1);
        add_overlay(WOLF_SPRITE_QMARK, 258, 34 + 5 * sinf(t * 5.0f), alpha);
    } else if (mode == MODE_SCRIPT && script->overlay == OV_STARS) {
        const float since = (now - script_start) * 1e-6f;
        const float alpha = clampf((clip_frame - SRC(112)) / 12.0f, 0, 1) *
                            (step_index == script->count - 1 ? clampf(1.0f - (now - step_start) * 1e-6f / 0.4f, 0, 1) : 1.0f);
        for (int i = 0; i < 4; i++) {
            const float a = since * 3.2f + i * 1.5708f;
            add_overlay(WOLF_SPRITE_STAR, 166 + 92 * cosf(a), 92 + 24 * sinf(a), alpha * (0.75f + 0.25f * sinf(t * 9 + i)));
        }
    } else if (mode == MODE_ASLEEP || (mode == MODE_SCRIPT && script == &doze)) {
        const float since = (now - (mode == MODE_ASLEEP ? step_start : script_start)) * 1e-6f;
        const float fade_in = mode == MODE_ASLEEP ? 1.0f : clampf((clip_frame - SRC(F_SLEEP)) / 20.0f, 0, 1);
        for (int i = 0; i < 2; i++) {
            const float p = fmodf(since / 2.6f + i * 0.5f, 1.0f);
            add_overlay(i ? WOLF_SPRITE_ZSMALL : WOLF_SPRITE_ZBIG, 236 + p * 46 + 6 * sinf(p * 7), 150 - p * 120,
                        sinf(p * 3.14159f) * fade_in);
        }
    }
}

static void build_view(int64_t now)
{
    int base;
    if (mode == MODE_GAZE) {
        base = gaze_frame(gaze_row, gaze_col);
    } else if (mode == MODE_ASLEEP) {
        /* Slow breathing: ping-pong through the near-identical doze frames. */
        static const int8_t breath[] = { 0, 1, 2, 3, 4, 5, 6, 5, 4, 3, 2, 1 };
        base = SRC(F_DOZE) + breath[((now - step_start) * DOZE_FPS / 1000000) % (int64_t)sizeof(breath)];
    } else {
        base = clip_frame;
    }
    view.a = view.b = base;
    view.mix = 0;
    if (fade_from >= 0) {
        const int64_t elapsed = now - fade_start;
        if (elapsed >= fade_us || fade_from == base) {
            fade_from = -1;
        } else {
            /* Smoothstep crossfade from the previous pose to the current one. */
            float p = (float)elapsed / (float)fade_us;
            p = p * p * (3.0f - 2.0f * p);
            view.a = fade_from;
            view.mix = clampi((int)(p * WOLF_MIX_MAX + 0.5f), 0, WOLF_MIX_MAX);
            if (view.mix == 0) view.b = view.a;
        }
    }
    build_overlays(now);
}

static bool same_frames(const wolf_view_t *x, const wolf_view_t *y)
{
    return x->a == y->a && x->mix == y->mix && (!x->mix || x->b == y->b);
}

static bool same_overlays(const wolf_view_t *x, const wolf_view_t *y)
{
    return x->overlay_count == y->overlay_count &&
           !memcmp(x->overlays, y->overlays, sizeof(x->overlays[0]) * x->overlay_count);
}

static wolf_rect_t frames_rect(const wolf_view_t *v)
{
    wolf_rect_t r = wolf_pack_frame_rect(&pack, v->mix >= WOLF_MIX_MAX ? v->b : v->a);
    if (v->mix > 0 && v->mix < WOLF_MIX_MAX) r = rect_union(r, wolf_pack_frame_rect(&pack, v->b));
    return r;
}

static wolf_rect_t overlays_rect(const wolf_view_t *v)
{
    wolf_rect_t r = { 0 };
    for (int i = 0; i < v->overlay_count; i++) {
        const wolf_sprite_t *s = &wolf_sprites[v->overlays[i].sprite];
        r = rect_union(r, (wolf_rect_t){ v->overlays[i].x, v->overlays[i].y, s->w, s->h });
    }
    return r;
}

/* ------------------------------------------------------------------ output */

typedef struct {
    int first_band, x1, x2;
    uint16_t *pixels;
    bool failed;
} strip_job_t;

static void band_job(int index, void *arg)
{
    strip_job_t *job = arg;
    uint16_t *dst = job->pixels + (size_t)index * pack.band_rows * (job->x2 - job->x1);
    if (!wolf_pack_compose_band(&pack, &drawn, &sprite_set, job->first_band + index, job->x1, job->x2, dst, &scratch[index]))
        job->failed = true;
}

/* Strips start on 16-row band boundaries: the region is band-aligned and STRIP_ROWS is two bands. */
static void fill_strip(uint16_t *pixels, int x1, int y, int x2, int rows, void *arg)
{
    (void)arg;
    strip_job_t job = { y / pack.band_rows, x1, x2, pixels, false };
    sugarat_frame_parallel((rows + pack.band_rows - 1) / pack.band_rows, band_job, &job);
    if (job.failed) ++decode_errors;
}

static void draw_view(lv_event_t *event)
{
    lv_area_t bounds;
    lv_obj_get_coords(lv_event_get_target(event), &bounds);
    if (lv_event_get_code(event) == LV_EVENT_COVER_CHECK) {
        if (_lv_area_is_in(lv_event_get_cover_area(event), &bounds, 0)) lv_event_set_cover_res(event, LV_COVER_RES_COVER);
        return;
    }
    lv_draw_ctx_t *ctx = lv_event_get_draw_ctx(event);
    lv_area_t clip;
    if (!pack_ok || !drawn_valid || !_lv_area_intersect(&clip, &bounds, ctx->clip_area)) return;
    const int stride = lv_area_get_width(ctx->buf_area), w = lv_area_get_width(&clip);
    lv_color_t *buf = ctx->buf;
    for (int band = clip.y1 / pack.band_rows; band <= clip.y2 / pack.band_rows; band++) {
        if (!wolf_pack_compose_band(&pack, &drawn, &sprite_set, band, clip.x1, clip.x2 + 1, fallback_band, &scratch[0]))
            ++decode_errors;
        const int y0 = band * pack.band_rows;
        for (int y = LV_MAX(y0, clip.y1); y <= LV_MIN(y0 + pack.band_rows - 1, clip.y2); y++) {
            memcpy(buf + (size_t)(y - ctx->buf_area->y1) * stride + (clip.x1 - ctx->buf_area->x1),
                   fallback_band + (size_t)(y - y0) * w, (size_t)w * 2);
        }
    }
}

static void present(int64_t now)
{
    /* The pacer admits one call per slot; a longer interval means the main loop stalled. */
    if (last_present && now - last_present > SLOT_US + SLOT_US / 2) missed_slots += (uint32_t)((now - last_present) / SLOT_US - 1);
    last_present = now;
    build_view(now);
    const bool frames_changed = !drawn_valid || !same_frames(&view, &drawn);
    if (!frames_changed && same_overlays(&view, &drawn)) {
        sugarat_frame_pacer_finish(&pacer, esp_timer_get_time(), false);
        return;
    }
    const wolf_rect_t new_frames = frames_rect(&view), new_overlays = overlays_rect(&view);
    wolf_rect_t region = rect_union(shown_overlays, new_overlays);
    if (frames_changed) region = rect_union(region, rect_union(shown_frames, new_frames));
    drawn = view;
    drawn_valid = true;
    shown_frames = new_frames;
    shown_overlays = new_overlays;

    /* Clip to the screen and widen to whole bands. */
    const int x1 = clampi(region.x0, 0, LCD), x2 = clampi(region.x0 + region.w, 0, LCD);
    const int y1 = clampi(region.y0, 0, LCD) / pack.band_rows * pack.band_rows;
    const int y2 = LV_MIN(LCD, (clampi(region.y0 + region.h, 0, LCD) + pack.band_rows - 1) / pack.band_rows * pack.band_rows);
    if (x2 <= x1 || y2 <= y1) {
        sugarat_frame_pacer_finish(&pacer, esp_timer_get_time(), true);
        return;
    }
    const bool direct = stream && !stream_failed && !paused && sugarat_frame_lvgl_layers_clear();
    esp_err_t err = ESP_FAIL;
    if (direct) {
        err = sugarat_frame_stream_send(stream, x1, y1, x2, y2, fill_strip, NULL);
        send_total_us += sugarat_frame_stream_take_send_us(stream);
        region_px_total += (int64_t)(x2 - x1) * (y2 - y1);
        if (err == ESP_ERR_TIMEOUT || err == ESP_ERR_INVALID_STATE) {
            stream_failed = true;
            ESP_LOGE(TAG, "frame stream stopped: %s; drawing through LVGL", esp_err_to_name(err));
        }
    }
    if (err != ESP_OK) {
        lv_area_t area = { (lv_coord_t)x1, (lv_coord_t)y1, (lv_coord_t)(x2 - 1), (lv_coord_t)(y2 - 1) };
        lv_obj_invalidate_area(image, &area);
    }
    const int64_t done = esp_timer_get_time();
    if (done - now > SLOT_US) ++late_frames;
    sugarat_frame_stats_record(&stats, now, done, true, err == ESP_OK);
    sugarat_frame_pacer_finish(&pacer, done, true);
}

static void report(int64_t now)
{
    sugarat_frame_summary_t s;
    if (!sugarat_frame_stats_window(&stats, now, REPORT_US, &s)) return;
    ESP_LOGI(TAG, "fps=%.1f frames=%" PRIu32 " direct=%" PRIu32 " avg=%lldus max=%lldus gap_max=%lldus panel=%lldus "
             "region=%lldpx late=%" PRIu32 " missed_slots=%" PRIu32 " mode=%s decode_errors=%" PRIu32 " internal_free=%u",
             s.fps, s.frames, s.direct_frames, (long long)s.avg_us, (long long)s.max_us, (long long)s.gap_max_us,
             (long long)(s.frames ? send_total_us / s.frames : 0), (long long)(s.direct_frames ? region_px_total / s.direct_frames : 0),
             late_frames, missed_slots, mode_name(), decode_errors, (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    send_total_us = region_px_total = 0;
    late_frames = missed_slots = 0;
}

/* ------------------------------------------------------------------ setup */

static void *alloc_fast(size_t bytes, const char *what)
{
    /* 16-byte alignment is required for esp_new_jpeg output buffers. */
    void *p = heap_caps_aligned_alloc(16, bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!p) {
        ESP_LOGW(TAG, "%s: internal RAM full, using PSRAM", what);
        p = heap_caps_aligned_alloc(16, bytes, MALLOC_CAP_SPIRAM);
    }
    ESP_ERROR_CHECK(p ? ESP_OK : ESP_ERR_NO_MEM);
    return p;
}

static void on_pause(void) { paused = true; }

static void on_resume(void)
{
    paused = false;
    sugarat_frame_pacer_reset(&pacer);
}

static void keep_refresh_period(uint32_t period)
{
    lv_timer_t *timer = _lv_disp_get_refr_timer(lv_disp_get_default());
    if (timer && timer->period != period) lv_timer_set_period(timer, period);
}

static void screen_deleted(lv_event_t *event)
{
    (void)event;
    screen = image = NULL;
    sdgoods_lvgl_fast_loop(false);
    keep_refresh_period(LV_DISP_DEF_REFR_PERIOD);
}

static void show_broken_pack(void)
{
    lv_obj_t *label = lv_label_create(screen);
    lv_label_set_text(label, "Wolf frames damaged.\nReinstall the firmware.");
    lv_obj_set_style_text_color(label, lv_color_white(), 0);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(label);
}

static void init_rendering(void)
{
    pack_ok = wolf_pack_open(&pack, pack_start, (size_t)(pack_end - pack_start)) && pack.width == LCD && pack.height == LCD &&
              pack.frame_count == WOLF_FRAME_COUNT && STRIP_ROWS == 2 * pack.band_rows;
    if (!pack_ok) {
        ESP_LOGE(TAG, "frame pack rejected (corrupt or wrong layout)");
        return;
    }
    ESP_LOGI(TAG, "frame pack: %d frames, %d bands of %d rows, %u bytes", pack.frame_count, pack.band_count,
             pack.band_rows, (unsigned)pack.size);
    const size_t band_bytes = wolf_pack_band_pixels(&pack) * 2;
    for (int i = 0; i < (int)(sizeof(scratch) / sizeof(scratch[0])); i++) {
        ESP_ERROR_CHECK(wolf_pack_decoder_open(&scratch[i].decoder) ? ESP_OK : ESP_ERR_NO_MEM);
        scratch[i].jpeg = alloc_fast(pack.max_band_jpeg, "band jpeg");
        scratch[i].band_a = alloc_fast(band_bytes, "band");
        scratch[i].band_b = alloc_fast(band_bytes, "band");
    }
    fallback_band = heap_caps_malloc((size_t)LCD * pack.band_rows * 2, MALLOC_CAP_SPIRAM);
    ESP_ERROR_CHECK(fallback_band ? ESP_OK : ESP_ERR_NO_MEM);

    /* The helper runs below the IMU task (priority 4, core 1) so decoding never starves sensor reads. */
    sugarat_frame_workers_config_t workers = sugarat_frame_workers_default_config();
    workers.priority = 3;
    ESP_ERROR_CHECK(sugarat_frame_workers_start(&workers));
    sugarat_frame_stream_config_t config = sugarat_frame_stream_default_config();
    config.rows = STRIP_ROWS;
    const esp_err_t err = sugarat_frame_stream_create(&config, &stream);
    if (err != ESP_OK) {
        stream = NULL;
        ESP_LOGE(TAG, "frame stream unavailable (%s); drawing through LVGL", esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "frame stream: %d strip buffer(s) of %d rows", sugarat_frame_stream_buffers(stream), STRIP_ROWS);
    }
    sugarat_frame_pacer_init(&pacer, SLOT_US);
}

void wolf_app_start(void)
{
    if (screen) {
        lv_scr_load(screen);
        return;
    }
    static bool initialised;
    if (!initialised) {
        initialised = true;
        init_rendering();
        sugarat_imu_config_t imu = sugarat_imu_default_config();
        imu.step_detection = false;
        const esp_err_t imu_err = sugarat_imu_start(&imu);
        imu_started = imu_err == ESP_OK;
        if (!imu_started) ESP_LOGE(TAG, "IMU unavailable: %s", esp_err_to_name(imu_err));
        sdgoods_screenshot_init();
        sdgoods_app_shell_set_pause_cb(on_pause);
        sdgoods_app_shell_set_resume_cb(on_resume);
    }

    screen = lv_obj_create(NULL);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(screen, lv_color_black(), 0);
    lv_obj_add_event_cb(screen, screen_deleted, LV_EVENT_DELETE, NULL);
    lv_obj_add_event_cb(screen, touch_event, LV_EVENT_ALL, NULL);
    image = lv_obj_create(screen);
    lv_obj_remove_style_all(image);
    lv_obj_clear_flag(image, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(image, LCD, LCD);
    lv_obj_center(image);
    lv_obj_add_event_cb(image, draw_view, LV_EVENT_DRAW_MAIN, NULL);
    lv_obj_add_event_cb(image, draw_view, LV_EVENT_COVER_CHECK, NULL);
    if (!pack_ok) show_broken_pack();

    const int64_t now = esp_timer_get_time();
    mode = MODE_GAZE;
    last_activity = now;
    next_grin = now + 4000000;
    if (pack_ok) {
        build_view(now);
        drawn = view;
        drawn_valid = true;
        shown_frames = frames_rect(&view);
        shown_overlays = overlays_rect(&view);
    }
    lv_obj_invalidate(image);
    lv_scr_load(screen);
    sdgoods_app_shell_bind(screen);
    ESP_LOGI(TAG, "Wolf Companion ready: IMU=%s pack=%s", imu_started ? "ready" : "unavailable", pack_ok ? "ok" : "damaged");
}

void sugarat_app_console(char command)
{
    atomic_store(&console_command, command);
}

static void handle_console(int64_t now)
{
    switch ((char)atomic_exchange(&console_command, 0)) {
    case 'j': on_tap(now); break;
    case 'k': on_hold(now); break;
    case 'm': on_shake(now); break;
    case 't': if (mode != MODE_ASLEEP) start_script(&doze, now); break;
    case 'n': bench_until = 0; last_activity = now; if (mode == MODE_ASLEEP) start_script(&wake, now); break;
    case '~':
        bench_until = now + 6000000;
        ESP_LOGI(TAG, "gaze sweep benchmark: 6 s");
        break;
    default: break;
    }
}

void wolf_app_poll(void)
{
    static int64_t previous;
    if (!screen || lv_scr_act() != screen || !pack_ok) return;
    const int64_t now = esp_timer_get_time();
    const float dt = previous ? fminf((now - previous) * 1e-6f, 0.1f) : 0.0f;
    previous = now;
    handle_console(now);
    if (bench_until && now >= bench_until) {
        bench_until = 0;
        ESP_LOGI(TAG, "gaze sweep benchmark done");
    }
    update_behaviour(now, dt);
    keep_refresh_period(REFRESH_MS);
    sdgoods_lvgl_fast_loop(true);
    if (sugarat_frame_pacer_due(&pacer, now)) present(now);
    report(now);
}

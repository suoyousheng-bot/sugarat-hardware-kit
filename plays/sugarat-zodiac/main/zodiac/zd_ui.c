#include "zd_ui.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#define TAU 6.28318531f
#define C (ZD_SCREEN / 2.f)

/* Geometry and timing, matching the approved web prototype (zodiac-dials.html, D1 + picker). */
#define INNER_R 136.f
#define RIM_R 158.f
#define RIM_SPEED .12f          /* rad/s */
#define PICK_INNER_R 118.f
#define PICK_RING_R 146.f
#define STEP_PX 46.f            /* finger travel per sign */
#define HOLD_S 1.0
#define HOLD_SHOW_S .15
#define MOVE_SLOP 8.f
#define AUTOSAVE_S 5.0
#define SAVED_S 1.3
#define IDLE_AFTER_S 20.0
#define REVEAL_S 1.3f
#define FADE_S .45f
#define PILL_Y 252.f
#define PILL_W 84.f
#define PILL_H 26.f

typedef struct { uint32_t pop, ink; } palette_t;
static const palette_t PALETTE[4] = {
    {0xff7448, 0x1f0904},   /* fire */
    {0xc9e36a, 0x14200c},   /* earth */
    {0xaaa3ff, 0x150f38},   /* air */
    {0x5fe0cf, 0x04252c},   /* water */
};

static inline float clampf(float v, float a, float b) { return v < a ? a : v > b ? b : v; }
static inline float lerpf(float a, float b, float k) { return a + (b - a) * k; }
static inline float ease(float k) { return k < .5f ? 4 * k * k * k : 1 - powf(-2 * k + 2, 3) / 2; }
static inline float seg(float e, float a, float b) { return ease(clampf((e - a) / (b - a), 0, 1)); }
static inline int mod12(float v) { return (((int)lroundf(v) % 12) + 12) % 12; }

static const zd_face_t *rim_face(int lang) { return lang == ZD_LANG_ZH ? &zd_face_rim_cjk : &zd_face_rim_lat; }

static void relayout(zd_ui_t *u)
{
    if (u->rim_sign == u->sign && u->rim_lang == u->lang && u->rim_line == u->reading.line) return;
    u->rim_sign = u->sign;
    u->rim_lang = u->lang;
    u->rim_line = u->reading.line;
    char text[160];
    const char *line = zd_line[u->reading.line][u->lang];
    if (u->lang == ZD_LANG_ZH) {
        snprintf(text, sizeof(text), "%s · %s · %s · ", zd_sign_name[ZD_LANG_ZH][u->sign], zd_sign_caps[u->sign], line);
    } else {
        char upper[64];
        size_t i = 0;
        for (; line[i] && i < sizeof(upper) - 1; i++) upper[i] = (char)toupper((unsigned char)line[i]);
        upper[i] = 0;
        snprintf(text, sizeof(text), "%s · %s · ", zd_sign_caps[u->sign], upper);
    }
    zd_rim_layout(&u->rim_near, rim_face(u->lang), text, RIM_R);
    zd_rim_layout(&u->rim_idle, rim_face(u->lang), zd_str_rim_idle[u->lang], RIM_R);
}

bool zd_ui_init(zd_ui_t *u, int sign, int lang, bool first_run, zd_date_t date, zd_save_fn save, void *arg, double now)
{
    memset(u, 0, sizeof(*u));
    u->sign = sign;
    u->lang = lang;
    u->lang_k = (float)lang;
    u->date = date;
    u->reading = zd_fortune(sign, date);
    u->save = save;
    u->save_arg = arg;
    u->rim_sign = -1;
    u->last_activity = now;
    u->face_near = true;
    u->sel_idx = -1;
    relayout(u);
    const bool ok = zd_const_layer_init(&u->face_const, 220, 140) && zd_const_layer_init(&u->pick_const, 220, 140);
    if (first_run) {
        zd_ui_open_picker(u, now);
        u->m = 1;
    }
    return ok;
}

void zd_ui_set_date(zd_ui_t *u, zd_date_t date)
{
    if (date.year == u->date.year && date.month == u->date.month && date.day == u->date.day) return;
    u->date = date;
    u->reading = zd_fortune(u->sign, date);
    relayout(u);
}

void zd_ui_open_picker(zd_ui_t *u, double now)
{
    u->picker = true;
    u->rot = u->rot_target = (float)u->sign;
    u->sel_idx = -1;
    u->last_touch = now;
    u->saving_t = 0;
    u->face_press = false;
    u->dragging = false;
}

void zd_ui_confirm(zd_ui_t *u, double now)
{
    if (!u->picker || u->saving_t > 0) return;
    u->rot_target = roundf(u->rot_target);
    u->sign = mod12(u->rot_target);
    u->saving_t = now;
    u->const_k = 0;
    u->reading = zd_fortune(u->sign, u->date);
    relayout(u);
    if (u->save) u->save(u->sign, u->lang, u->save_arg);
}

static void set_lang(zd_ui_t *u, int lang)
{
    u->lang = lang;
    relayout(u);
}

void zd_ui_toggle_lang(zd_ui_t *u, double now)
{
    set_lang(u, u->lang == ZD_LANG_ZH ? ZD_LANG_EN : ZD_LANG_ZH);
    u->last_touch = now;
}

void zd_ui_step(zd_ui_t *u, int delta, double now)
{
    if (!u->picker || u->saving_t > 0) return;
    u->rot_target = roundf(u->rot_target) + delta;
    u->last_touch = now;
}

static void reveal(zd_ui_t *u, double now)
{
    u->face_near = true;
    u->last_activity = now;
}

void zd_ui_motion(zd_ui_t *u, double now)
{
    if (!u->picker) reveal(u, now);
}

static void tap_picker(zd_ui_t *u, float x, float y, double now)
{
    if (fabsf(y - PILL_Y) < PILL_H / 2 + 6 && fabsf(x - C) < PILL_W / 2 + 6) {
        set_lang(u, x < C ? ZD_LANG_ZH : ZD_LANG_EN);
        u->last_touch = now;
        return;
    }
    if (hypotf(x - C, y - C) < PICK_INNER_R) {
        zd_ui_confirm(u, now);
        return;
    }
    float d = (atan2f(y - C, x - C) + TAU / 4) / (TAU / 12);
    d = fmodf(fmodf(d, 12) + 18, 12) - 6;
    u->rot_target = roundf(u->rot + d);
}

void zd_ui_touch(zd_ui_t *u, zd_touch_t kind, float x, float y, double now)
{
    switch (kind) {
    case ZD_TOUCH_DOWN:
        u->down = true;
        u->moved = false;
        u->down_x = x;
        u->down_y = y;
        u->down_rot = u->rot;
        u->last_x = x;
        u->last_t = now;
        u->vel = 0;
        u->last_activity = now;
        if (!u->picker) {
            u->face_press = true;
            u->press_t0 = now;
        } else if (u->saving_t <= 0) {
            u->dragging = true;
            u->last_touch = now;
        }
        break;
    case ZD_TOUCH_MOVE:
        if (!u->down) break;
        if (hypotf(x - u->down_x, y - u->down_y) > MOVE_SLOP) {
            u->moved = true;
            u->face_press = false;
        }
        if (u->dragging) {
            u->rot = u->rot_target = u->down_rot - (x - u->down_x) / STEP_PX;
            const double ms = (now - u->last_t) * 1000;
            u->vel = (float)((x - u->last_x) / (ms < 1 ? 1 : ms));
            u->last_x = x;
            u->last_t = now;
            u->last_touch = now;
        }
        break;
    case ZD_TOUCH_UP:
        if (!u->down) break;
        u->down = false;
        if (u->face_press) {
            /* Released before the long press completed: a tap reveals the reading. */
            u->face_press = false;
            if (!u->moved) reveal(u, now);
        }
        if (u->dragging) {
            if (!u->moved) tap_picker(u, x, y, now);
            else u->rot_target = roundf(u->rot - u->vel * 3.5f);
            u->dragging = false;
            u->last_touch = now;
        }
        break;
    }
}

void zd_ui_touch_cancel(zd_ui_t *u)
{
    u->down = u->face_press = u->dragging = false;
    u->rot_target = roundf(u->rot);
}

void zd_ui_update(zd_ui_t *u, double now, float dt)
{
    if (u->face_press) {
        u->press_k = clampf((float)((now - u->press_t0 - HOLD_SHOW_S) / (HOLD_S - HOLD_SHOW_S)), 0, 1);
        if (u->press_k >= 1) zd_ui_open_picker(u, now);
    } else {
        u->press_k = fmaxf(0, u->press_k - dt * 4);
    }
    u->m = clampf(u->m + (u->picker ? 1 : -1) * dt / FADE_S, 0, 1);
    if (!u->dragging) u->rot += (u->rot_target - u->rot) * (1 - expf(-dt * 10));
    u->const_k = clampf(u->const_k + dt / .9f, 0, 1);
    u->lang_k += ((float)u->lang - u->lang_k) * (1 - expf(-dt * 14));
    u->face_q = clampf(u->face_q + (u->face_near ? 1 : -1) * dt / REVEAL_S, 0, 1);
    if (!u->picker && u->face_near && !u->down && now - u->last_activity > IDLE_AFTER_S) u->face_near = false;
    if (u->picker && u->saving_t <= 0 && !u->dragging && now - u->last_touch > AUTOSAVE_S) zd_ui_confirm(u, now);
    if (u->saving_t > 0 && now - u->saving_t > SAVED_S) {
        u->saving_t = 0;
        u->picker = false;
        u->face_q = 0;
        reveal(u, now);
    }
}

bool zd_ui_fast(const zd_ui_t *u)
{
    const bool face_moving = u->face_q > 0 && u->face_q < 1;
    return u->picker || u->m > 0 || u->down || u->press_k > 0 || face_moving;
}

static void colors(int sign, zd_color_t *pop, zd_color_t *ink)
{
    const palette_t *p = &PALETTE[zd_sign_element[sign]];
    *pop = zd_rgb(p->pop);
    *ink = zd_rgb(p->ink);
}

static void sign_glyph(zd_scene_t *s, const zd_face_t *f, int sign, float cx, float cy, float scale, zd_color_t c, float alpha)
{
    const zd_face_t *owner;
    const zd_glyph_t *g = zd_find(f, zd_sign_glyph[sign], &owner);
    if (g) zd_glyph(s, owner, g, cx, cy, 0, scale, c, alpha);
}

static void build_face(zd_ui_t *u, double now, zd_scene_t *s)
{
    const float t = (float)fmod(now, 3600.0);
    const float e = ease(u->face_q);
    zd_color_t pop, ink;
    colors(u->sign, &pop, &ink);
    const zd_color_t inner = zd_mix(pop, zd_rgb(0xffffff), .16f);
    zd_scene_begin(s, pop);
    zd_disc(s, C, C, INNER_R, inner, 1);
    zd_ring(s, C, C, INNER_R, 3, 0, TAU, ink, 1);
    const float rot = (float)fmod(now * RIM_SPEED, (double)TAU);
    zd_rim_draw(s, &u->rim_idle, RIM_R, rot, ink, 1 - seg(e, 0, .5f));
    zd_rim_draw(s, &u->rim_near, RIM_R, rot, ink, seg(e, .4f, 1));
    sign_glyph(s, &zd_face_sym_big, u->sign, C, C + sinf(t) * 3, 1, ink, 1 - seg(e, 0, .4f));
    const float cp = seg(e, .2f, .9f);
    if (cp > 0) {
        zd_const_layer_update(&u->face_const, u->sign, C, 104, 140, 62, cp, 2.5f, 3.2f);
        zd_const_layer_draw(s, &u->face_const, t, ink, .55f);
    }
    const float nk = seg(e, .3f, 1);
    if (nk > 0) {
        char num[8];
        snprintf(num, sizeof(num), "%d", (int)lroundf(u->reading.score * seg(e, .25f, .95f)));
        zd_text(s, &zd_face_num, num, C, 206, lerpf(.85f, 1, nk), ink, nk);
        zd_text(s, u->lang == ZD_LANG_ZH ? &zd_face_label_cjk : &zd_face_label_lat, zd_str_today[u->lang], C, 268, 1, ink, .7f * nk);
    }
    if (u->press_k > .002f) zd_ring(s, C, C, 175, 8, -TAU / 4, TAU * ease(u->press_k), ink, 1);
}

static void build_picker(zd_ui_t *u, double now, zd_scene_t *s)
{
    const float t = (float)fmod(now, 3600.0);
    const float base = floorf(u->rot), fr = u->rot - base;
    zd_color_t pa, ia, pb, ib;
    colors(mod12(base), &pa, &ia);
    colors(mod12(base + 1), &pb, &ib);
    const zd_color_t pop = zd_mix(pa, pb, fr), ink = zd_mix(ia, ib, fr);
    const zd_color_t inner = zd_mix(pop, zd_rgb(0xffffff), .16f);
    zd_scene_begin(s, pop);
    zd_disc(s, C, C, PICK_INNER_R, inner, 1);
    zd_ring(s, C, C, PICK_INNER_R, 3, 0, TAU, ink, .2f);
    const float left = u->saving_t > 0 ? 0 : u->dragging ? 1 : clampf(1 - (float)((now - u->last_touch) / AUTOSAVE_S), 0, 1);
    if (left > .002f) zd_ring(s, C, C, PICK_INNER_R, 3, -TAU / 4, TAU * left, ink, 1);
    zd_disc(s, C, C - PICK_RING_R, 25, ink, 1);
    for (int i = 0; i < 12; i++) {
        float d = fmodf(fmodf(i - u->rot, 12) + 18, 12) - 6;
        const float ang = -TAU / 4 + d * TAU / 12, near = clampf(1 - fabsf(d), 0, 1);
        const float far_alpha = clampf(1 - fabsf(d) / 7, .4f, .7f);
        sign_glyph(s, &zd_face_sym_ring, i, C + cosf(ang) * PICK_RING_R, C + sinf(ang) * PICK_RING_R,
                   lerpf(22, 30, near) / 30, zd_mix(ink, inner, near), lerpf(far_alpha, 1, near));
    }
    const int idx = mod12(u->rot);
    if (idx != u->sel_idx) {
        u->sel_idx = idx;
        u->const_k = 0;
    }
    zd_const_layer_update(&u->pick_const, idx, C, 128, 132, 60, ease(u->const_k), 2.5f, 3.2f);
    zd_const_layer_draw(s, &u->pick_const, t, ink, .55f);
    const bool zh = u->lang == ZD_LANG_ZH;
    zd_text(s, zh ? &zd_face_name_cjk : &zd_face_name_lat, zd_sign_name[u->lang][idx], C, 192, 1, ink, 1);
    zd_text(s, &zd_face_date, zd_dates[u->lang][idx], C, 218, 1, ink, .7f);
    const float k = clampf(u->lang_k, 0, 1);
    zd_rrect(s, C, PILL_Y, PILL_W, PILL_H, PILL_H / 2, 2, ink, 1);
    zd_rrect(s, C - PILL_W / 4 + k * PILL_W / 2, PILL_Y, PILL_W / 2 - 6, PILL_H - 6, (PILL_H - 6) / 2, 0, ink, 1);
    zd_text(s, &zd_face_pill_cjk, "\xe4\xb8\xad", C - PILL_W / 4, PILL_Y, 1, zd_mix(inner, ink, k), 1);
    zd_text(s, &zd_face_pill_lat, "EN", C + PILL_W / 4, PILL_Y, 1, zd_mix(ink, inner, k), 1);
    const bool saved = u->saving_t > 0;
    zd_text(s, zh ? &zd_face_hint_cjk : &zd_face_hint_lat, saved ? zd_str_saved[u->lang] : zd_str_confirm[u->lang], C, 284, 1, ink,
            saved ? .9f : .55f);
}

void zd_ui_build(zd_ui_t *u, double now, zd_scene_t *face, zd_scene_t *picker, int *mix)
{
    const int k = (int)lroundf(ease(u->m) * 256);
    *mix = k;
    if (k < 256) build_face(u, now, face);
    if (k > 0) build_picker(u, now, picker);
}

#include "zd_render.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>
#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#endif

#define TAU 6.28318531f

static inline float clampf(float v, float a, float b) { return v < a ? a : v > b ? b : v; }
static inline int mini(int a, int b) { return a < b ? a : b; }
static inline int maxi(int a, int b) { return a > b ? a : b; }
static inline float easef(float k) { return k < .5f ? 4 * k * k * k : 1 - powf(-2 * k + 2, 3) / 2; }

static inline uint16_t pack(int r, int g, int b)
{
    const uint16_t v = (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
    return (uint16_t)(v >> 8 | v << 8);
}

static inline void unpack(uint16_t px, int *r, int *g, int *b)
{
    const uint16_t v = (uint16_t)(px >> 8 | px << 8);
    const int r5 = v >> 11, g6 = (v >> 5) & 63, b5 = v & 31;
    *r = r5 << 3 | r5 >> 2;
    *g = g6 << 2 | g6 >> 4;
    *b = b5 << 3 | b5 >> 2;
}

static inline void blend(uint16_t *p, const zd_color_t *c, int a)
{
    if (a <= 0) return;
    if (a >= 256) { *p = c->px; return; }
    int r, g, b;
    unpack(*p, &r, &g, &b);
    r += ((c->r - r) * a) >> 8;
    g += ((c->g - g) * a) >> 8;
    b += ((c->b - b) * a) >> 8;
    *p = pack(r, g, b);
}

zd_color_t zd_rgb(uint32_t hex)
{
    zd_color_t c = {(uint8_t)(hex >> 16), (uint8_t)(hex >> 8), (uint8_t)hex, 0};
    c.px = pack(c.r, c.g, c.b);
    return c;
}

zd_color_t zd_mix(zd_color_t a, zd_color_t b, float k)
{
    zd_color_t c = {(uint8_t)(a.r + (b.r - a.r) * k + .5f), (uint8_t)(a.g + (b.g - a.g) * k + .5f),
                    (uint8_t)(a.b + (b.b - a.b) * k + .5f), 0};
    c.px = pack(c.r, c.g, c.b);
    return c;
}

/* ---- scene building ---- */

void zd_scene_begin(zd_scene_t *s, zd_color_t base)
{
    s->base = base.px;
    s->count = 0;
    s->overflow = false;
}

static zd_op_t *push(zd_scene_t *s, zd_op_kind_t kind, zd_color_t c, float alpha, float x0, float y0, float x1, float y1)
{
    const int a = (int)(clampf(alpha, 0, 1) * 256 + .5f);
    if (a <= 0) return NULL;
    const int ix0 = maxi(0, (int)floorf(x0)), iy0 = maxi(0, (int)floorf(y0));
    const int ix1 = mini(ZD_SCREEN, (int)ceilf(x1)), iy1 = mini(ZD_SCREEN, (int)ceilf(y1));
    if (ix0 >= ix1 || iy0 >= iy1) return NULL;
    if (s->count >= ZD_MAX_OPS) {
        s->overflow = true;
        return NULL;
    }
    zd_op_t *o = &s->ops[s->count++];
    o->kind = kind;
    o->color = c;
    o->alpha = (uint16_t)a;
    o->x0 = (int16_t)ix0; o->y0 = (int16_t)iy0; o->x1 = (int16_t)ix1; o->y1 = (int16_t)iy1;
    return o;
}

void zd_disc(zd_scene_t *s, float cx, float cy, float r, zd_color_t c, float alpha)
{
    if (r <= 0) return;
    zd_op_t *o = push(s, ZD_OP_DISC, c, alpha, cx - r - 1, cy - r - 1, cx + r + 1, cy + r + 1);
    if (o) { o->disc.cx = cx; o->disc.cy = cy; o->disc.r = r; }
}

void zd_ring(zd_scene_t *s, float cx, float cy, float r, float w, float a0, float sweep, zd_color_t c, float alpha)
{
    if (sweep <= 0) return;
    const float ro = r + w / 2 + 1;
    zd_op_t *o = push(s, ZD_OP_RING, c, alpha, cx - ro, cy - ro, cx + ro, cy + ro);
    if (!o) return;
    o->ring.cx = cx; o->ring.cy = cy; o->ring.r = r; o->ring.half_w = w / 2;
    o->ring.full = sweep >= TAU;
    o->ring.a0 = fmodf(a0, TAU);
    if (o->ring.a0 < 0) o->ring.a0 += TAU;
    o->ring.sweep = sweep;
    if (!o->ring.full) {
        zd_disc(s, cx + cosf(a0) * r, cy + sinf(a0) * r, w / 2, c, alpha);
        zd_disc(s, cx + cosf(a0 + sweep) * r, cy + sinf(a0 + sweep) * r, w / 2, c, alpha);
    }
}

void zd_rrect(zd_scene_t *s, float cx, float cy, float w, float h, float rad, float stroke, zd_color_t c, float alpha)
{
    const float m = stroke / 2 + 1;
    zd_op_t *o = push(s, ZD_OP_RRECT, c, alpha, cx - w / 2 - m, cy - h / 2 - m, cx + w / 2 + m, cy + h / 2 + m);
    if (o) { o->rrect.cx = cx; o->rrect.cy = cy; o->rrect.hw = w / 2; o->rrect.hh = h / 2; o->rrect.rad = rad; o->rrect.stroke = stroke; }
}

void zd_mask(zd_scene_t *s, const uint8_t *a8, int x, int y, int w, int h, zd_color_t c, float alpha)
{
    zd_op_t *o = push(s, ZD_OP_MASK, c, alpha, x, y, x + w, y + h);
    if (o) { o->mask.a8 = a8; o->mask.x = (int16_t)x; o->mask.y = (int16_t)y; o->mask.w = (int16_t)w; o->mask.h = (int16_t)h; }
}

void zd_glyph(zd_scene_t *s, const zd_face_t *f, const zd_glyph_t *g, float cx, float cy, float angle, float scale,
              zd_color_t c, float alpha)
{
    if (!g->w || !g->h || scale <= 0.01f) return;
    const uint8_t *a8 = f->bitmap + g->off;
    const float hw = g->w / 2.f, hh = g->h / 2.f;
    if (angle == 0 && scale == 1) {
        const float lx = cx - hw, ty = cy - hh;
        if (fabsf(lx - roundf(lx)) < 1e-3f && fabsf(ty - roundf(ty)) < 1e-3f) {
            zd_mask(s, a8, (int)roundf(lx), (int)roundf(ty), g->w, g->h, c, alpha);
            return;
        }
    }
    const float cs = cosf(angle), sn = sinf(angle);
    const float ex = (fabsf(cs) * hw + fabsf(sn) * hh) * scale + 1, ey = (fabsf(sn) * hw + fabsf(cs) * hh) * scale + 1;
    zd_op_t *o = push(s, ZD_OP_GLYPH, c, alpha, cx - ex, cy - ey, cx + ex, cy + ey);
    if (!o) return;
    o->glyph.a8 = a8; o->glyph.w = g->w; o->glyph.h = g->h;
    o->glyph.cx = cx; o->glyph.cy = cy; o->glyph.cs = cs; o->glyph.sn = sn; o->glyph.inv = 1 / scale;
}

/* ---- text ---- */

uint32_t zd_utf8(const char **p)
{
    const uint8_t *s = (const uint8_t *)*p;
    uint32_t cp;
    int n;
    if (!s[0]) return 0;
    if (s[0] < 0x80) { cp = s[0]; n = 1; }
    else if ((s[0] & 0xE0) == 0xC0) { cp = s[0] & 0x1F; n = 2; }
    else if ((s[0] & 0xF0) == 0xE0) { cp = s[0] & 0x0F; n = 3; }
    else { cp = s[0] & 0x07; n = 4; }
    for (int i = 1; i < n; i++) {
        if ((s[i] & 0xC0) != 0x80) { *p += i; return 0xFFFD; }
        cp = cp << 6 | (s[i] & 0x3F);
    }
    *p += n;
    return cp;
}

const zd_glyph_t *zd_find(const zd_face_t *f, uint32_t cp, const zd_face_t **owner)
{
    for (; f; f = f->fallback) {
        int lo = 0, hi = f->count - 1;
        while (lo <= hi) {
            const int mid = (lo + hi) / 2;
            if (f->glyphs[mid].cp == cp) { *owner = f; return &f->glyphs[mid]; }
            if (f->glyphs[mid].cp < cp) lo = mid + 1; else hi = mid - 1;
        }
    }
    return NULL;
}

float zd_text_width(const zd_face_t *f, const char *text)
{
    float w = 0;
    for (uint32_t cp; (cp = zd_utf8(&text));) {
        const zd_face_t *owner;
        const zd_glyph_t *g = zd_find(f, cp, &owner);
        if (g) w += g->adv16 / 16.f;
    }
    return w;
}

void zd_text(zd_scene_t *s, const zd_face_t *f, const char *text, float cx, float cy, float scale, zd_color_t c, float alpha)
{
    const float width = zd_text_width(f, text);
    const float baseline = -f->mid16 / 16.f;
    float pen = -width / 2;
    if (scale == 1) {
        /* Static text lands on whole pixels so it is drawn unfiltered. */
        cx = roundf(cx + pen) - pen;
        cy = roundf(cy + baseline) - baseline;
    }
    for (uint32_t cp; (cp = zd_utf8(&text));) {
        const zd_face_t *owner;
        const zd_glyph_t *g = zd_find(f, cp, &owner);
        if (!g) continue;
        const float rx = pen + g->x + g->w / 2.f, ry = baseline + g->y + g->h / 2.f;
        zd_glyph(s, owner, g, cx + rx * scale, cy + ry * scale, 0, scale, c, alpha);
        pen += g->adv16 / 16.f;
    }
}

void zd_rim_layout(zd_rim_t *rim, const zd_face_t *f, const char *text, float r)
{
    const zd_face_t *owner[ZD_MAX_RIM];
    const zd_glyph_t *glyph[ZD_MAX_RIM];
    int n = 0;
    float total = 0;
    for (uint32_t cp; (cp = zd_utf8(&text)) && n < ZD_MAX_RIM;) {
        const zd_glyph_t *g = zd_find(f, cp, &owner[n]);
        if (!g) continue;
        glyph[n++] = g;
        total += g->adv16 / 16.f;
    }
    rim->count = 0;
    rim->mid = f->mid16 / 16.f;
    if (!n || total <= 0) return;
    const float circ = TAU * r;
    int repeat = (int)(circ / total);
    if (repeat < 1) repeat = 1;
    while (repeat * n > ZD_MAX_RIM) repeat--;
    const float scale = circ / (total * repeat);
    float acc = 0;
    for (int k = 0; k < repeat; k++) {
        for (int i = 0; i < n; i++) {
            const float adv = glyph[i]->adv16 / 16.f;
            rim->owner[rim->count] = owner[i];
            rim->glyph[rim->count] = glyph[i];
            rim->angle[rim->count] = (acc + adv / 2) * scale / r - TAU / 4;
            rim->count++;
            acc += adv;
        }
    }
}

void zd_rim_draw(zd_scene_t *s, const zd_rim_t *rim, float r, float rotation, zd_color_t c, float alpha)
{
    if (alpha <= 0.004f) return;
    const float centre = ZD_SCREEN / 2.f;
    for (int i = 0; i < rim->count; i++) {
        const zd_glyph_t *g = rim->glyph[i];
        if (!g->w) continue;
        const float a = rim->angle[i] + rotation, theta = a + TAU / 4;
        const float lx = g->x + g->w / 2.f - g->adv16 / 32.f, ly = -rim->mid + g->y + g->h / 2.f;
        const float cs = cosf(theta), sn = sinf(theta);
        const float ax = centre + cosf(a) * r, ay = centre + sinf(a) * r;
        zd_glyph(s, rim->owner[i], g, ax + lx * cs - ly * sn, ay + lx * sn + ly * cs, theta, 1.0001f, c, alpha);
    }
}

/* ---- constellation layer ---- */

bool zd_const_layer_init(zd_const_layer_t *l, int cap_w, int cap_h)
{
    memset(l, 0, sizeof(*l));
    const size_t bytes = (size_t)cap_w * cap_h;
#ifdef ESP_PLATFORM
    l->lines = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM);
    l->stars = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM);
#else
    l->lines = malloc(bytes);
    l->stars = malloc(bytes);
#endif
    l->cap_w = cap_w;
    l->cap_h = cap_h;
    l->sign = -1;
    return l->lines && l->stars;
}

static void put_capsule(uint8_t *m, int mw, int mh, float ax, float ay, float bx, float by, float rad)
{
    const int x0 = maxi(0, (int)floorf(fminf(ax, bx) - rad - 1)), x1 = mini(mw, (int)ceilf(fmaxf(ax, bx) + rad + 1));
    const int y0 = maxi(0, (int)floorf(fminf(ay, by) - rad - 1)), y1 = mini(mh, (int)ceilf(fmaxf(ay, by) + rad + 1));
    const float vx = bx - ax, vy = by - ay, len2 = vx * vx + vy * vy;
    for (int y = y0; y < y1; y++) {
        for (int x = x0; x < x1; x++) {
            const float px = x + .5f - ax, py = y + .5f - ay;
            const float t = len2 > 0 ? clampf((px * vx + py * vy) / len2, 0, 1) : 0;
            const float dx = px - t * vx, dy = py - t * vy;
            const float cov = clampf(rad - sqrtf(dx * dx + dy * dy) + .5f, 0, 1);
            const int v = (int)(cov * 255 + .5f);
            if (v > m[y * mw + x]) m[y * mw + x] = (uint8_t)v;
        }
    }
}

/* Coverage of a disc of radius rad at (cx, cy) for each mask pixel, combined by op. */
static void put_disc(uint8_t *m, int mw, int mh, float cx, float cy, float rad, bool cut)
{
    const int x0 = maxi(0, (int)floorf(cx - rad - 1)), x1 = mini(mw, (int)ceilf(cx + rad + 1));
    const int y0 = maxi(0, (int)floorf(cy - rad - 1)), y1 = mini(mh, (int)ceilf(cy + rad + 1));
    for (int y = y0; y < y1; y++) {
        for (int x = x0; x < x1; x++) {
            const float dx = x + .5f - cx, dy = y + .5f - cy;
            const float cov = clampf(rad - sqrtf(dx * dx + dy * dy) + .5f, 0, 1);
            uint8_t *p = &m[y * mw + x];
            if (cut) *p = (uint8_t)(*p * (1 - cov) + .5f);
            else if (cov * 255 > *p) *p = (uint8_t)(cov * 255 + .5f);
        }
    }
}

void zd_const_layer_update(zd_const_layer_t *l, int sign, float cx, float cy, float bw, float bh, float prog, float lw, float sr)
{
    if (l->sign == sign && l->cx == cx && l->cy == cy && l->bw == bw && l->bh == bh && l->prog == prog && l->lw == lw && l->sr == sr)
        return;
    l->sign = sign; l->cx = cx; l->cy = cy; l->bw = bw; l->bh = bh; l->prog = prog; l->lw = lw; l->sr = sr;
    const zd_const_t *c = &zd_const[sign];
    const float sc = fminf(bw / c->aw, bh / c->ah);
    const float margin = sr * 2.8f + lw + 3;
    l->x = (int)floorf(cx - c->aw * sc / 2 - margin);
    l->y = (int)floorf(cy - c->ah * sc / 2 - margin);
    l->w = mini(l->cap_w, (int)ceilf(c->aw * sc + 2 * margin) + 1);
    l->h = mini(l->cap_h, (int)ceilf(c->ah * sc + 2 * margin) + 1);
    memset(l->lines, 0, (size_t)l->w * l->h);
    memset(l->stars, 0, (size_t)l->w * l->h);
    float px[16], py[16];
    for (int i = 0; i < c->stars && i < 16; i++) {
        px[i] = cx + c->pts[i][0] * sc - l->x;
        py[i] = cy + c->pts[i][1] * sc - l->y;
    }
    const float reach = prog * (c->edges + 1);
    for (int i = 0; i < c->edges; i++) {
        const float f = clampf(reach - i, 0, 1);
        if (f <= 0) break;
        const int a = c->edge[i][0], b = c->edge[i][1];
        put_capsule(l->lines, l->w, l->h, px[a], py[a], px[a] + (px[b] - px[a]) * f, py[a] + (py[b] - py[a]) * f, lw / 2);
    }
    l->spark = false;
    for (int i = 0; i < c->stars && i < 16; i++) {
        int first = c->edges;
        for (int e = 0; e < c->edges; e++) {
            if (c->edge[e][0] == i || c->edge[e][1] == i) { first = e; break; }
        }
        const float a = easef(clampf((reach - first) * 2.5f, 0, 1));
        if (a <= 0) continue;
        if (i == c->bright) {
            const float r = sr * 2.8f * a;
            put_disc(l->lines, l->w, l->h, px[i], py[i], r * .55f + lw, true);
            l->spark = true;
            l->spark_x = px[i] + l->x;
            l->spark_y = py[i] + l->y;
            l->spark_r = r;
        } else {
            put_disc(l->lines, l->w, l->h, px[i], py[i], sr * a + lw * 1.2f, true);
            put_disc(l->stars, l->w, l->h, px[i], py[i], sr * a, false);
        }
    }
}

void zd_const_layer_draw(zd_scene_t *s, zd_const_layer_t *l, float t, zd_color_t ink, float line_alpha)
{
    if (l->sign < 0) return;
    zd_mask(s, l->lines, l->x, l->y, l->w, l->h, ink, line_alpha);
    zd_mask(s, l->stars, l->x, l->y, l->w, l->h, ink, 1);
    if (!l->spark) return;
    /* Four-point star: an astroid, (sqrt|x| + sqrt|y|)^2 <= R, gently pulsing and rocking. */
    const float r = fminf(l->spark_r * (1 + .08f * sinf(t * 2)), 22);
    const float rot = sinf(t * .6f) * .15f, cs = cosf(rot), sn = sinf(rot);
    const int ox = (int)floorf(l->spark_x) - 24, oy = (int)floorf(l->spark_y) - 24;
    memset(l->spark_a8, 0, sizeof(l->spark_a8));
    const int lo = maxi(0, 24 - (int)r - 2), hi = mini(48, 24 + (int)r + 3);
    for (int y = lo; y < hi; y++) {
        for (int x = lo; x < hi; x++) {
            const float dx = ox + x + .5f - l->spark_x, dy = oy + y + .5f - l->spark_y;
            const float ux = fabsf(dx * cs + dy * sn) / r, uy = fabsf(-dx * sn + dy * cs) / r;
            const float q = sqrtf(ux) + sqrtf(uy), f = q * q;
            const float cov = clampf((1 - f) * r * .6f + .5f, 0, 1);
            l->spark_a8[y * 48 + x] = (uint8_t)(cov * 255 + .5f);
        }
    }
    zd_mask(s, l->spark_a8, ox, oy, 48, 48, ink, 1);
}

/* ---- rasterization ---- */

static void r_disc(const zd_op_t *o, uint16_t *row0, int stride, int y, int ry0, int ry1, int rx0, int rx1)
{
    const float cx = o->disc.cx, cy = o->disc.cy, r = o->disc.r, ro = r + 1, ri = r - 1;
    for (int yy = ry0; yy < ry1; yy++) {
        const float dy = yy + .5f - cy, dy2 = dy * dy;
        if (dy2 >= ro * ro) continue;
        const float ho = sqrtf(ro * ro - dy2);
        const float hi = (ri > 0 && dy2 < ri * ri) ? sqrtf(ri * ri - dy2) : -1;
        const int xa = maxi(rx0, (int)floorf(cx - ho)), xb = mini(rx1, (int)ceilf(cx + ho));
        uint16_t *row = row0 + (yy - y) * stride;
        for (int x = xa; x < xb; x++) {
            const float dx = x + .5f - cx;
            if (fabsf(dx) < hi) {
                blend(&row[x], &o->color, o->alpha);
            } else {
                const float cov = clampf(r - sqrtf(dx * dx + dy2) + .5f, 0, 1);
                blend(&row[x], &o->color, (int)(cov * o->alpha));
            }
        }
    }
}

static void ring_px(const zd_op_t *o, uint16_t *p, float dx, float dy2, float dy)
{
    const float d = sqrtf(dx * dx + dy2);
    float cov = clampf(o->ring.half_w - fabsf(d - o->ring.r) + .5f, 0, 1);
    if (cov <= 0) return;
    if (!o->ring.full) {
        float rel = atan2f(dy, dx) - o->ring.a0;
        if (rel < 0) rel += TAU;
        if (rel > o->ring.sweep) return;
    }
    blend(p, &o->color, (int)(cov * o->alpha));
}

static void r_ring(const zd_op_t *o, uint16_t *row0, int stride, int y, int ry0, int ry1, int rx0, int rx1)
{
    const float cx = o->ring.cx, cy = o->ring.cy;
    const float ro = o->ring.r + o->ring.half_w + 1, ri = o->ring.r - o->ring.half_w - 1;
    for (int yy = ry0; yy < ry1; yy++) {
        const float dy = yy + .5f - cy, dy2 = dy * dy;
        if (dy2 >= ro * ro) continue;
        const float ho = sqrtf(ro * ro - dy2);
        const float hi = (ri > 0 && dy2 < ri * ri) ? sqrtf(ri * ri - dy2) : 0;
        uint16_t *row = row0 + (yy - y) * stride;
        const int la = maxi(rx0, (int)floorf(cx - ho)), lb = mini(rx1, (int)ceilf(cx - hi));
        for (int x = la; x < lb; x++) ring_px(o, &row[x], x + .5f - cx, dy2, dy);
        const int ra = maxi(maxi(rx0, lb), (int)floorf(cx + hi)), rb = mini(rx1, (int)ceilf(cx + ho));
        for (int x = ra; x < rb; x++) ring_px(o, &row[x], x + .5f - cx, dy2, dy);
    }
}

static void r_rrect(const zd_op_t *o, uint16_t *row0, int stride, int y, int ry0, int ry1, int rx0, int rx1)
{
    const float bx = o->rrect.hw - o->rrect.rad, by = o->rrect.hh - o->rrect.rad;
    for (int yy = ry0; yy < ry1; yy++) {
        const float qy = fabsf(yy + .5f - o->rrect.cy) - by;
        uint16_t *row = row0 + (yy - y) * stride;
        for (int x = rx0; x < rx1; x++) {
            const float qx = fabsf(x + .5f - o->rrect.cx) - bx;
            const float ox = fmaxf(qx, 0), oy = fmaxf(qy, 0);
            const float d = sqrtf(ox * ox + oy * oy) + fminf(fmaxf(qx, qy), 0) - o->rrect.rad;
            const float cov = o->rrect.stroke > 0 ? clampf(o->rrect.stroke / 2 - fabsf(d) + .5f, 0, 1) : clampf(.5f - d, 0, 1);
            if (cov > 0) blend(&row[x], &o->color, (int)(cov * o->alpha));
        }
    }
}

static void r_mask(const zd_op_t *o, uint16_t *row0, int stride, int y, int ry0, int ry1, int rx0, int rx1)
{
    const int alpha = o->alpha * 257;
    for (int yy = ry0; yy < ry1; yy++) {
        const uint8_t *m = o->mask.a8 + (yy - o->mask.y) * o->mask.w - o->mask.x;
        uint16_t *row = row0 + (yy - y) * stride;
        for (int x = rx0; x < rx1; x++) {
            const int v = m[x];
            if (v) blend(&row[x], &o->color, (v * alpha) >> 16);
        }
    }
}

static inline int texel(const uint8_t *a8, int w, int h, int x, int y)
{
    return ((unsigned)x < (unsigned)w && (unsigned)y < (unsigned)h) ? a8[y * w + x] : 0;
}

static void r_glyph(const zd_op_t *o, uint16_t *row0, int stride, int y, int ry0, int ry1, int rx0, int rx1)
{
    const int w = o->glyph.w, h = o->glyph.h;
    const float ku = o->glyph.cs * o->glyph.inv, kv = -o->glyph.sn * o->glyph.inv;
    const float ju = o->glyph.sn * o->glyph.inv, jv = o->glyph.cs * o->glyph.inv;
    const float hw = w / 2.f - .5f, hh = h / 2.f - .5f;
    for (int yy = ry0; yy < ry1; yy++) {
        const float dx0 = rx0 + .5f - o->glyph.cx, dy = yy + .5f - o->glyph.cy;
        float u = dx0 * ku + dy * ju + hw, v = dx0 * kv + dy * jv + hh;
        uint16_t *row = row0 + (yy - y) * stride;
        for (int x = rx0; x < rx1; x++, u += ku, v += kv) {
            if (u <= -1 || v <= -1 || u >= w || v >= h) continue;
            const int iu = (int)floorf(u), iv = (int)floorf(v);
            const float fu = u - iu, fv = v - iv;
            const float top = texel(o->glyph.a8, w, h, iu, iv) * (1 - fu) + texel(o->glyph.a8, w, h, iu + 1, iv) * fu;
            const float bot = texel(o->glyph.a8, w, h, iu, iv + 1) * (1 - fu) + texel(o->glyph.a8, w, h, iu + 1, iv + 1) * fu;
            const float a = top + (bot - top) * fv;
            if (a >= 1) blend(&row[x], &o->color, (int)(a * o->alpha) >> 8);
        }
    }
}

void zd_render_scene(const zd_scene_t *s, uint16_t *out, int stride, int ox, int y, int rows, int cx0, int cx1)
{
    for (int r = 0; r < rows; r++) {
        uint16_t *row = out + r * stride;
        for (int c = cx0; c < cx1; c++) row[c] = s->base;
    }
    const int sx0 = ox + cx0, sx1 = ox + cx1, y1 = y + rows;
    uint16_t *row0 = out - ox;   /* row0[x] addresses screen column x of the strip's first row */
    for (int i = 0; i < s->count; i++) {
        const zd_op_t *o = &s->ops[i];
        if (o->y1 <= y || o->y0 >= y1 || o->x1 <= sx0 || o->x0 >= sx1) continue;
        const int ry0 = maxi(o->y0, y), ry1 = mini(o->y1, y1), rx0 = maxi(o->x0, sx0), rx1 = mini(o->x1, sx1);
        switch (o->kind) {
        case ZD_OP_DISC: r_disc(o, row0, stride, y, ry0, ry1, rx0, rx1); break;
        case ZD_OP_RING: r_ring(o, row0, stride, y, ry0, ry1, rx0, rx1); break;
        case ZD_OP_RRECT: r_rrect(o, row0, stride, y, ry0, ry1, rx0, rx1); break;
        case ZD_OP_MASK: r_mask(o, row0, stride, y, ry0, ry1, rx0, rx1); break;
        case ZD_OP_GLYPH: r_glyph(o, row0, stride, y, ry0, ry1, rx0, rx1); break;
        }
    }
}

void zd_blend_region(uint16_t *out, const uint16_t *over, int stride, int ox, int rows, int cx0, int cx1, int k)
{
    (void)ox;
    if (k <= 0) return;
    for (int r = 0; r < rows; r++) {
        uint16_t *a = out + r * stride;
        const uint16_t *b = over + r * stride;
        for (int c = cx0; c < cx1; c++) {
            if (k >= 256) { a[c] = b[c]; continue; }
            int r1, g1, b1;
            unpack(b[c], &r1, &g1, &b1);
            const zd_color_t col = {(uint8_t)r1, (uint8_t)g1, (uint8_t)b1, b[c]};
            blend(&a[c], &col, k);
        }
    }
}

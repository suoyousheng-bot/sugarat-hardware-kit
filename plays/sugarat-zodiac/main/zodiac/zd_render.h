/* Software compositor for the Zodiac Ring screens.
 *
 * A scene is a base colour plus a painter's-order list of anti-aliased operations (discs, rings,
 * rounded rectangles, A8 masks and transformed glyphs). zd_render_scene() rasterizes any rectangle
 * of a scene into byte-swapped RGB565, so the same scene feeds panel strips and the LVGL fallback
 * image. Building a scene does not touch pixels; rendering only reads the scene, so several tasks
 * may render disjoint column ranges of one scene at once. */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "zd_assets.h"

#ifdef __cplusplus
extern "C" {
#endif

enum { ZD_SCREEN = 360, ZD_MAX_OPS = 224, ZD_MAX_RIM = 160 };

/** An 8-bit colour with its byte-swapped RGB565 form. */
typedef struct { uint8_t r, g, b; uint16_t px; } zd_color_t;

zd_color_t zd_rgb(uint32_t hex);
zd_color_t zd_mix(zd_color_t a, zd_color_t b, float k);

typedef enum { ZD_OP_DISC, ZD_OP_RING, ZD_OP_RRECT, ZD_OP_MASK, ZD_OP_GLYPH } zd_op_kind_t;

typedef struct {
    zd_op_kind_t kind;
    zd_color_t color;
    uint16_t alpha;            /**< 0..256 */
    int16_t x0, y0, x1, y1;    /**< screen bounds, half-open */
    union {
        struct { float cx, cy, r; } disc;
        struct { float cx, cy, r, half_w, a0, sweep; bool full; } ring;
        struct { float cx, cy, hw, hh, rad, stroke; } rrect;   /**< stroke 0 fills */
        struct { const uint8_t *a8; int16_t x, y, w, h; } mask;
        struct { const uint8_t *a8; uint16_t w, h; float cx, cy, cs, sn, inv; } glyph;
    };
} zd_op_t;

typedef struct {
    uint16_t base;             /**< background, byte-swapped RGB565 */
    int count;
    bool overflow;             /**< an operation was dropped because the list was full */
    zd_op_t ops[ZD_MAX_OPS];
} zd_scene_t;

void zd_scene_begin(zd_scene_t *s, zd_color_t base);
void zd_disc(zd_scene_t *s, float cx, float cy, float r, zd_color_t c, float alpha);
/** Ring of radius r and stroke width w; sweep >= 2π draws the full circle, otherwise an arc from
 *  angle a0 (radians, 0 = 3 o'clock, clockwise) with round caps. */
void zd_ring(zd_scene_t *s, float cx, float cy, float r, float w, float a0, float sweep, zd_color_t c, float alpha);
void zd_rrect(zd_scene_t *s, float cx, float cy, float w, float h, float rad, float stroke, zd_color_t c, float alpha);
void zd_mask(zd_scene_t *s, const uint8_t *a8, int x, int y, int w, int h, zd_color_t c, float alpha);
/** Draws glyph g of face f with its bitmap centre at (cx, cy), rotated by angle and scaled. */
void zd_glyph(zd_scene_t *s, const zd_face_t *f, const zd_glyph_t *g, float cx, float cy, float angle, float scale,
              zd_color_t c, float alpha);

/** Finds code point cp in f or its fallback chain; sets *owner to the face that holds it. */
const zd_glyph_t *zd_find(const zd_face_t *f, uint32_t cp, const zd_face_t **owner);
/** Decodes one UTF-8 code point and advances *p; returns 0 at the terminator. */
uint32_t zd_utf8(const char **p);
/** Advance width of a UTF-8 string in pixels. */
float zd_text_width(const zd_face_t *f, const char *text);
/** Draws a string centred on (cx, cy): horizontally by advance, vertically on the face's visual middle. */
void zd_text(zd_scene_t *s, const zd_face_t *f, const char *text, float cx, float cy, float scale, zd_color_t c, float alpha);

/** Text laid around a circle: glyph centres and angles for one full turn at rotation 0. */
typedef struct {
    int count;
    const zd_face_t *owner[ZD_MAX_RIM];
    const zd_glyph_t *glyph[ZD_MAX_RIM];
    float angle[ZD_MAX_RIM];
    float mid;                 /**< baseline-to-middle offset of the primary face, px */
} zd_rim_t;

/** Repeats text to fill the circumference of radius r, spreading leftover space evenly. */
void zd_rim_layout(zd_rim_t *rim, const zd_face_t *f, const char *text, float r);
void zd_rim_draw(zd_scene_t *s, const zd_rim_t *rim, float r, float rotation, zd_color_t c, float alpha);

/** Cached constellation layer: lines and dots as A8 masks, redrawn only when its inputs change. */
typedef struct {
    uint8_t *lines, *stars;    /**< capacity cap_w x cap_h each */
    int cap_w, cap_h;
    int x, y, w, h;            /**< placement of the valid area on screen */
    int sign;
    float cx, cy, bw, bh, prog, lw, sr;
    bool spark;                /**< the brightest star is visible */
    float spark_x, spark_y, spark_r;
    uint8_t spark_a8[48 * 48]; /**< this frame's four-point star, redrawn by zd_const_layer_draw() */
} zd_const_layer_t;

bool zd_const_layer_init(zd_const_layer_t *l, int cap_w, int cap_h);
/** Fits figure `sign` into bw x bh around (cx, cy) and connects edges up to prog (0..1). */
void zd_const_layer_update(zd_const_layer_t *l, int sign, float cx, float cy, float bw, float bh, float prog, float lw, float sr);
/** Adds the cached lines, the dots and the twinkling brightest star to the scene. */
void zd_const_layer_draw(zd_scene_t *s, zd_const_layer_t *l, float t, zd_color_t ink, float line_alpha);

/** Renders scene rows [y, y+rows) and columns [cx0, cx1) into out, where out[r*stride + c] is screen
 *  pixel (ox + c, y + r). */
void zd_render_scene(const zd_scene_t *s, uint16_t *out, int stride, int ox, int y, int rows, int cx0, int cx1);
/** out = lerp(out, over, k) over the same region; k 0..256. */
void zd_blend_region(uint16_t *out, const uint16_t *over, int stride, int ox, int rows, int cx0, int cx1, int k);

#ifdef __cplusplus
}
#endif

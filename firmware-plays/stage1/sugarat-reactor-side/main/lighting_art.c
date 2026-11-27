/* Copyright (c) 2026 Deotaland. SPDX-License-Identifier: Apache-2.0 */
#include "lighting_art.h"

#include "esp_heap_caps.h"
#include "light_pixels.h"
#include "sdgoods_lcd.h"

_Static_assert(LCD_WIDTH == 360 && LCD_HEIGHT == 360, "Lighting art uses a 360px panel");
_Static_assert(LV_COLOR_DEPTH == 16 && LV_COLOR_16_SWAP, "Lighting art uses big-endian RGB565");
enum { PIXELS = LCD_WIDTH * LCD_HEIGHT };

#define ART(name) \
    extern const uint16_t name##_dark[] asm("_binary_" #name "_dark_rgb565_start"); \
    extern const uint16_t name##_bright[] asm("_binary_" #name "_bright_rgb565_start"); \
    extern const uint8_t name##_phase[] asm("_binary_" #name "_phase_bin_start")
ART(core);
ART(ring);
ART(arcs);

static const struct {
    const uint16_t *dark;
    const uint16_t *bright;
    const uint8_t *phase;
} styles[LIGHTING_STYLE_COUNT] = {
    [LIGHTING_CORE] = {core_dark, core_bright, core_phase},
    [LIGHTING_RING] = {ring_dark, ring_bright, ring_phase},
    [LIGHTING_ARCS] = {arcs_dark, arcs_bright, arcs_phase},
};

static struct {
    lv_obj_t *object;
    lv_img_dsc_t image;
    uint16_t *pixels;
    lighting_style_t style;
    uint8_t level;
    uint8_t gain;
} art = {.style = LIGHTING_STYLE_COUNT};

bool lighting_art_create(lv_obj_t *screen)
{
    art.pixels = heap_caps_malloc(PIXELS * sizeof(*art.pixels), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!art.pixels) return false;
    art.image = (lv_img_dsc_t){
        .header = {.cf = LV_IMG_CF_TRUE_COLOR, .w = LCD_WIDTH, .h = LCD_HEIGHT},
        .data = (const uint8_t *)art.pixels,
        .data_size = PIXELS * sizeof(*art.pixels),
    };
    art.object = lv_img_create(screen);
    lighting_art_draw(LIGHTING_DEFAULT_STYLE, 0, 0);
    lv_img_set_src(art.object, &art.image);
    return true;
}

void lighting_art_draw(lighting_style_t style, uint8_t level, uint8_t gain)
{
    if (style == art.style && level == art.level && gain == art.gain) return;
    light_pixels(art.pixels, styles[style].dark, styles[style].bright, styles[style].phase, PIXELS, level, gain);
    art.style = style;
    art.level = level;
    art.gain = gain;
    lv_obj_invalidate(art.object);
}

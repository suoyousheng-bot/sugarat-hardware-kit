/* Copyright (c) 2026 Deotaland. SPDX-License-Identifier: Apache-2.0 */
#include "light_pixels.h"

#include <string.h>

static inline uint16_t mix(uint16_t a, uint16_t b, unsigned alpha)
{
    const unsigned rg = ((a & 0xf81f) * (32 - alpha) + (b & 0xf81f) * alpha) >> 5;
    const unsigned g = ((a & 0x07e0) * (32 - alpha) + (b & 0x07e0) * alpha) >> 5;
    return (rg & 0xf81f) | (g & 0x07e0);
}

static inline uint16_t blend(uint16_t dark, uint16_t bright, unsigned alpha, unsigned gain)
{
    uint16_t pixel = mix(__builtin_bswap16(dark), __builtin_bswap16(bright), alpha);
    if (gain != LIGHT_FULL_GAIN) pixel = mix(0, pixel, gain);
    return __builtin_bswap16(pixel);
}

void light_pixels(uint16_t *out, const uint16_t *dark, const uint16_t *bright, const uint8_t *phase,
                  size_t pixels, uint8_t level, uint8_t gain)
{
    if (gain == 0) {
        memset(out, 0, pixels * sizeof(*out));
        return;
    }
    if (gain == LIGHT_FULL_GAIN && (level == 0 || level == UINT8_MAX)) {
        memcpy(out, level == 0 ? dark : bright, pixels * sizeof(*out));
        return;
    }

    unsigned opacity[LIGHT_PHASES];
    for (unsigned i = 0; i < LIGHT_PHASES; ++i) {
        const int x = (int)level - (int)i * 9;
        const unsigned t = x <= 0 ? 0 : x >= 120 ? 32 : (unsigned)x * 32 / 120;
        opacity[i] = (t * t * (96 - 2 * t) + 512) / 1024;
    }
    for (size_t i = 0; i < pixels; i += 2) {
        const uint8_t phases = phase[i / 2];
        out[i] = blend(dark[i], bright[i], opacity[phases & 15], gain);
        out[i + 1] = blend(dark[i + 1], bright[i + 1], opacity[phases >> 4], gain);
    }
}

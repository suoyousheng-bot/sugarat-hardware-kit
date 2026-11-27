/* Copyright (c) 2026 Deotaland. SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <stddef.h>
#include <stdint.h>

enum { LIGHT_PHASES = 16, LIGHT_FULL_GAIN = 32 };

/** Paint even-numbered pixels from two big-endian RGB565 images and a packed phase map. */
void light_pixels(uint16_t *out, const uint16_t *dark, const uint16_t *bright, const uint8_t *phase,
                  size_t pixels, uint8_t level, uint8_t gain);

/* Copyright (c) 2026 Deotaland. SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include "lighting_sync.h"
#include "lvgl.h"

/** Create a single PSRAM-backed screen image. Return false if its buffer cannot be allocated. */
bool lighting_art_create(lv_obj_t *screen);
/** Repaint only when the selected artwork, illumination, or crossfade has changed. */
void lighting_art_draw(lighting_style_t style, uint8_t level, uint8_t gain);

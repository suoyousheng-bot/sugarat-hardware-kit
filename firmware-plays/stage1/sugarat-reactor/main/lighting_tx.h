/* Copyright (c) 2026 Deotaland. SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include "lighting_sync.h"

/** Offer the current visual state after the servo radio's poll. The shared radio owns ESP-NOW. */
void lighting_tx_poll(uint32_t now, lighting_style_t style, uint8_t level, bool animating);

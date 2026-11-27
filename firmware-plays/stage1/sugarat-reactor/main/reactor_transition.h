/* Copyright (c) 2026 Deotaland. SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

enum {
    REACTOR_IGNITION_MS = 780,
    REACTOR_DISCHARGE_MS = 720,
};

/** Calculate the open artwork opacity for a gesture's sound cue. Endpoints require receiver confirmation. */
uint8_t reactor_light_at(uint8_t initial, bool opening, uint32_t elapsed_ms, bool settled);

/** Move the opacity toward its target by at most one frame's change. */
uint8_t reactor_light_step(uint8_t current, uint8_t target);

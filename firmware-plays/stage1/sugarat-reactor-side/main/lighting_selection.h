/* Copyright (c) 2026 Deotaland. SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include "lighting_sync.h"

typedef struct {
    lighting_style_t selected;
    lighting_style_t master_style;
    bool heard_master;
    bool local;
} lighting_selection_t;

/** Cycle the local selection one step. Positive direction advances through the styles. */
void lighting_selection_cycle(lighting_selection_t *selection, int direction);
/** Follow new choices from the controller. Repeated snapshots preserve a manual side selection. */
void lighting_selection_follow(lighting_selection_t *selection, lighting_style_t style);

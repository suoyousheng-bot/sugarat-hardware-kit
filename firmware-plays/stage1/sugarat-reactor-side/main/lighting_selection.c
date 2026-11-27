/* Copyright (c) 2026 Deotaland. SPDX-License-Identifier: Apache-2.0 */
#include "lighting_selection.h"

void lighting_selection_cycle(lighting_selection_t *selection, int direction)
{
    const unsigned step = direction > 0 ? 1 : LIGHTING_STYLE_COUNT - 1;
    selection->selected = (selection->selected + step) % LIGHTING_STYLE_COUNT;
    selection->local = true;
}

void lighting_selection_follow(lighting_selection_t *selection, lighting_style_t style)
{
    if (selection->heard_master && style != selection->master_style) selection->local = false;
    selection->master_style = style;
    selection->heard_master = true;
    if (!selection->local) selection->selected = style;
}

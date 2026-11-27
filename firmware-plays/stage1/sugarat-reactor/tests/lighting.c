/* Copyright (c) 2026 Deotaland. SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <stdint.h>

#include "light_pixels.h"
#include "lighting_selection.h"
#include "lighting_sync.h"

static void follows_one_fresh_controller(void)
{
    const uint8_t controller[LIGHTING_MAC_BYTES] = {1, 2, 3, 4, 5, 6};
    const uint8_t other[LIGHTING_MAC_BYTES] = {1, 2, 3, 4, 5, 7};
    const lighting_frame_t initial = {.boot = 7, .sequence = 20, .style = LIGHTING_CORE, .level = 70};
    lighting_frame_t current = initial;
    uint8_t wire[LIGHTING_PACKET_BYTES];
    lighting_encode(wire, &initial, 513);
    assert(!lighting_decode(&current, wire, sizeof(wire), 1));
    assert(!lighting_decode(&current, wire, sizeof(wire) - 1, 513));
    assert(lighting_decode(&current, wire, sizeof(wire), 513));
    assert(current.boot == 7 && current.sequence == 20 && current.style == LIGHTING_CORE && current.level == 70);

    lighting_link_t link = {0};
    assert(!lighting_receive(&link, controller, &current, 0));
    assert(!lighting_receive(&link, controller, &current, 20));
    assert(!lighting_online(&link, 20));
    current.sequence = 22;
    current.style = LIGHTING_ARCS;
    current.level = 200;
    assert(!lighting_receive(&link, other, &current, 100));
    assert(lighting_receive(&link, controller, &current, 150));
    assert(lighting_online(&link, 1649));
    assert(!lighting_receive(&link, controller, &initial, 700));
    assert(link.latest.style == LIGHTING_ARCS && link.latest.level == 200);
    assert(!lighting_online(&link, 1650));
    current.boot++;
    current.sequence = 1;
    assert(!lighting_receive(&link, controller, &current, 1650));
    current.sequence++;
    assert(lighting_receive(&link, controller, &current, 1700));
}

static void paints_the_light_in_phase_order(void)
{
    const uint16_t dark[] = {0, 0};
    const uint16_t bright[] = {0xffff, 0xffff};
    const uint8_t phase[] = {0xf0};
    uint16_t out[2];
    light_pixels(out, dark, bright, phase, 2, 0, LIGHT_FULL_GAIN);
    assert(out[0] == 0 && out[1] == 0);
    light_pixels(out, dark, bright, phase, 2, 120, LIGHT_FULL_GAIN);
    assert(out[0] == 0xffff && out[1] == 0);
    light_pixels(out, dark, bright, phase, 2, 200, LIGHT_FULL_GAIN);
    assert(out[0] == 0xffff && out[1] != 0 && out[1] != 0xffff);
    light_pixels(out, dark, bright, phase, 2, UINT8_MAX, LIGHT_FULL_GAIN);
    assert(out[0] == 0xffff && out[1] == 0xffff);
    light_pixels(out, dark, bright, phase, 2, UINT8_MAX, 0);
    assert(out[0] == 0 && out[1] == 0);
}

static void local_switch_survives_repeated_master_snapshots(void)
{
    lighting_selection_t selection = {.selected = LIGHTING_DEFAULT_STYLE};
    lighting_selection_cycle(&selection, -1);
    assert(selection.selected == LIGHTING_CORE);
    lighting_selection_cycle(&selection, -1);
    assert(selection.selected == LIGHTING_ARCS);
    lighting_selection_follow(&selection, LIGHTING_RING);
    lighting_selection_follow(&selection, LIGHTING_RING);
    assert(selection.selected == LIGHTING_ARCS);

    lighting_selection_follow(&selection, LIGHTING_CORE);
    assert(selection.selected == LIGHTING_CORE);
    lighting_selection_cycle(&selection, 1);
    assert(selection.selected == LIGHTING_RING);
    lighting_selection_follow(&selection, LIGHTING_CORE);
    assert(selection.selected == LIGHTING_RING);
    lighting_selection_follow(&selection, LIGHTING_ARCS);
    assert(selection.selected == LIGHTING_ARCS);
}

int main(void)
{
    follows_one_fresh_controller();
    paints_the_light_in_phase_order();
    local_switch_survives_repeated_master_snapshots();
}

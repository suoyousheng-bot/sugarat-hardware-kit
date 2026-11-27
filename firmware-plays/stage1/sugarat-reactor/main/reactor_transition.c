/* Copyright (c) 2026 Deotaland. SPDX-License-Identifier: Apache-2.0 */
#include "reactor_transition.h"

typedef struct {
    uint16_t ms;
    uint8_t progress;
} light_key_t;

/* The rising current and the discharge latch set the strongest changes in each cue. */
static const light_key_t IGNITION[] = {
    {0, 0}, {50, 12}, {110, 39}, {235, 127}, {420, 188}, {510, 233},
    {REACTOR_IGNITION_MS, 255},
};
static const light_key_t DISCHARGE[] = {
    {0, 0}, {50, 20}, {120, 89}, {315, 165}, {405, 190}, {480, 239},
    {REACTOR_DISCHARGE_MS, 255},
};

uint8_t reactor_light_at(uint8_t initial, bool opening, uint32_t elapsed_ms, bool settled)
{
    const light_key_t *keys = opening ? IGNITION : DISCHARGE;
    const unsigned count = opening ? sizeof(IGNITION) / sizeof(*IGNITION)
                                   : sizeof(DISCHARGE) / sizeof(*DISCHARGE);
    unsigned progress = 255;
    for (unsigned i = 1; i < count; ++i) {
        if (elapsed_ms < keys[i].ms) {
            const light_key_t lo = keys[i - 1];
            const light_key_t hi = keys[i];
            progress = lo.progress + (hi.progress - lo.progress) * (elapsed_ms - lo.ms) / (hi.ms - lo.ms);
            break;
        }
    }
    if (!settled) progress = progress * 210 / 255;

    const int endpoint = opening ? UINT8_MAX : 0;
    return (uint8_t)(initial + (endpoint - initial) * (int)progress / 255);
}

uint8_t reactor_light_step(uint8_t current, uint8_t target)
{
    const int delta = (int)target - current;
    enum { MAX_STEP = 28 };
    if (delta > MAX_STEP) return current + MAX_STEP;
    if (delta < -MAX_STEP) return current - MAX_STEP;
    return target;
}

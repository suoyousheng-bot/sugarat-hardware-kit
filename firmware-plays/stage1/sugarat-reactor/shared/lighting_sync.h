/* Copyright (c) 2026 Deotaland. SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    LIGHTING_CORE,
    LIGHTING_RING,
    LIGHTING_ARCS,
    LIGHTING_STYLE_COUNT,
} lighting_style_t;

enum {
    LIGHTING_DEFAULT_STYLE = LIGHTING_RING,
    LIGHTING_CHANNEL = 6,
    LIGHTING_PACKET_BYTES = 16,
    LIGHTING_MAC_BYTES = 6,
    LIGHTING_ANIMATION_MS = 50,
    LIGHTING_HEARTBEAT_MS = 500,
    LIGHTING_LINK_TIMEOUT_MS = 1500,
};

typedef struct {
    uint32_t boot;
    uint32_t sequence;
    lighting_style_t style;
    uint8_t level;
} lighting_frame_t;

typedef struct {
    lighting_frame_t latest;
    uint8_t source[LIGHTING_MAC_BYTES];
    uint32_t received_ms;
    bool candidate;
    bool bound;
} lighting_link_t;

/** Serialize the versioned snapshot in little-endian order. */
void lighting_encode(uint8_t out[LIGHTING_PACKET_BYTES], const lighting_frame_t *frame, uint16_t group);
/** Read one complete snapshot for the local group. Invalid frames leave out untouched. */
bool lighting_decode(lighting_frame_t *out, const uint8_t *data, size_t size, uint16_t group);
/** Accept increasing snapshots from one source. An unbound source needs two consistent frames. */
bool lighting_receive(lighting_link_t *link, const uint8_t source[LIGHTING_MAC_BYTES],
                      const lighting_frame_t *frame, uint32_t now);
/** Return true while the last accepted snapshot is recent. */
bool lighting_online(const lighting_link_t *link, uint32_t now);

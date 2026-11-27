/* Copyright (c) 2026 Deotaland. SPDX-License-Identifier: Apache-2.0 */
#include "lighting_sync.h"

#include <string.h>

static const uint8_t MAGIC[] = {'S', 'G', 'L', 1};

static void write_u32(uint8_t *out, uint32_t value)
{
    for (unsigned i = 0; i < 4; ++i) out[i] = (uint8_t)(value >> (8 * i));
}

static uint32_t read_u32(const uint8_t *data)
{
    return (uint32_t)data[0] | (uint32_t)data[1] << 8 |
           (uint32_t)data[2] << 16 | (uint32_t)data[3] << 24;
}

void lighting_encode(uint8_t out[LIGHTING_PACKET_BYTES], const lighting_frame_t *frame, uint16_t group)
{
    memcpy(out, MAGIC, sizeof MAGIC);
    out[4] = (uint8_t)group;
    out[5] = (uint8_t)(group >> 8);
    out[6] = (uint8_t)frame->style;
    out[7] = frame->level;
    write_u32(out + 8, frame->boot);
    write_u32(out + 12, frame->sequence);
}

bool lighting_decode(lighting_frame_t *out, const uint8_t *data, size_t size, uint16_t group)
{
    if (size != LIGHTING_PACKET_BYTES || memcmp(data, MAGIC, sizeof MAGIC) ||
        ((unsigned)data[4] | (unsigned)data[5] << 8) != group || data[6] >= LIGHTING_STYLE_COUNT) {
        return false;
    }
    *out = (lighting_frame_t){.boot = read_u32(data + 8), .sequence = read_u32(data + 12),
                             .style = (lighting_style_t)data[6], .level = data[7]};
    return true;
}

bool lighting_online(const lighting_link_t *link, uint32_t now)
{
    return link->bound && now - link->received_ms < LIGHTING_LINK_TIMEOUT_MS;
}

bool lighting_receive(lighting_link_t *link, const uint8_t source[LIGHTING_MAC_BYTES],
                      const lighting_frame_t *frame, uint32_t now)
{
    if (now - link->received_ms >= LIGHTING_LINK_TIMEOUT_MS) {
        link->candidate = false;
        link->bound = false;
    }
    if (!link->candidate) {
        memcpy(link->source, source, LIGHTING_MAC_BYTES);
        link->latest = *frame;
        link->received_ms = now;
        link->candidate = true;
        return false;
    }

    const uint32_t advance = frame->sequence - link->latest.sequence;
    if (memcmp(link->source, source, LIGHTING_MAC_BYTES) || frame->boot != link->latest.boot ||
        advance == 0 || advance >= UINT32_C(0x80000000)) {
        return false;
    }

    link->latest = *frame;
    link->received_ms = now;
    link->bound = true;
    return true;
}

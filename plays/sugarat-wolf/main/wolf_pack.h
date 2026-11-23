/*
 * Wolf Companion frame pack: 360x360 baseline JPEG frames cropped to their content and split into
 * independently decodable 16-row bands (layout in tools/build_pack.py), so two cores can decode two
 * bands of one panel strip at once. The decoder is esp_new_jpeg on the badge; WOLF_PACK_HOST builds
 * the same code against libjpeg for host tests.
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum { WOLF_PACK_HEADER_BYTES = 32, WOLF_MAX_OVERLAYS = 6, WOLF_MIX_MAX = 32 };

/** Screen rectangle [x0, x0 + w) x [y0, y0 + h); w == 0 is empty. */
typedef struct {
    int x0, y0, w, h;
} wolf_rect_t;

/** A validated pack; data must stay readable for the life of the application. */
typedef struct {
    const uint8_t *data;
    size_t size;
    int width, height, band_rows, band_count, frame_count, max_band_jpeg;
    const uint8_t *frame_table, *band_table;
} wolf_pack_t;

/** Sprite layer composited over the frame; alpha 0..255 scales the sprite's own alpha. */
typedef struct {
    int sprite, x, y, alpha;
} wolf_overlay_t;

/** What one screen frame shows: frame a blended toward frame b by mix/WOLF_MIX_MAX, plus overlays. */
typedef struct {
    int a, b, mix;
    int overlay_count;
    wolf_overlay_t overlays[WOLF_MAX_OVERLAYS];
} wolf_view_t;

/** Per-job working memory; jobs that run concurrently need separate scratch. */
typedef struct {
    void *decoder;             /* from wolf_pack_decoder_open() */
    uint8_t *jpeg;             /* max_band_jpeg bytes: one band reassembled as a JPEG */
    uint16_t *band_a, *band_b; /* wolf_pack_band_pixels() each, 16-byte aligned */
} wolf_scratch_t;

/** Overlay sprite at pixels + offset: w*h byte-swapped RGB565 pixels, then w*h alpha bytes. */
typedef struct {
    int w, h, offset;
} wolf_sprite_t;

/** Sprite table and pixels from wolf_sprites.h / wolf_sprites.bin. */
typedef struct {
    const wolf_sprite_t *table;
    int count;
    const uint8_t *pixels;
} wolf_sprite_set_t;

/**
 * Validates the header, the table checksum and every frame and band range.
 * @param pack Filled on success.
 * @param data Pack bytes.
 * @param size Bytes available at data.
 * @returns false for a missing, truncated or corrupt pack.
 */
bool wolf_pack_open(wolf_pack_t *pack, const uint8_t *data, size_t size);

/**
 * @param pack Open pack.
 * @param frame Frame index.
 * @returns The frame's content rectangle; everything outside it is black.
 */
wolf_rect_t wolf_pack_frame_rect(const wolf_pack_t *pack, int frame);

/**
 * @param pack Open pack.
 * @returns Pixels each wolf_scratch_t band buffer must hold (decoder output may pad width to 16).
 */
size_t wolf_pack_band_pixels(const wolf_pack_t *pack);

/**
 * Creates a decoder for one wolf_scratch_t.
 * @param decoder Receives the decoder handle.
 * @returns false when the decoder cannot be created.
 */
bool wolf_pack_decoder_open(void **decoder);

/**
 * Renders columns [x1, x2) of band `band` of a view into dst, byte-swapped RGB565 with stride x2 - x1.
 * The band has min(band_rows, height - band * band_rows) rows.
 * @param pack Open pack.
 * @param view Frames, blend weight and overlays; frame indexes must be valid.
 * @param sprites Overlay sprites referenced by view.
 * @param band Band index, 0..band_count-1.
 * @param x1 First column.
 * @param x2 Column after the last; 0 <= x1 < x2 <= width.
 * @param dst Output pixels.
 * @param scratch Working memory owned by the calling job.
 * @returns false when a band failed to decode; its pixels are then black.
 */
bool wolf_pack_compose_band(const wolf_pack_t *pack, const wolf_view_t *view, const wolf_sprite_set_t *sprites,
                            int band, int x1, int x2, uint16_t *dst, wolf_scratch_t *scratch);

#ifdef __cplusplus
}
#endif

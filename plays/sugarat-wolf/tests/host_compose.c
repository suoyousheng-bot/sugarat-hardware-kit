/* Host check for wolf_pack.c: renders views from the real pack with zlib, for tests/check_compose.py.
 * Usage: host_compose <pack> <sprites.bin> <out.raw> a b mix [sprite x y alpha]... */
#include <stdio.h>
#include <stdlib.h>
#include "wolf_pack.h"
#include "wolf_sprites.h"

static void *load(const char *path, size_t *size)
{
    FILE *f = fopen(path, "rb");
    if (!f) { perror(path); exit(2); }
    fseek(f, 0, SEEK_END);
    *size = (size_t)ftell(f);
    fseek(f, 0, SEEK_SET);
    void *data = malloc(*size);
    if (fread(data, 1, *size, f) != *size) exit(2);
    fclose(f);
    return data;
}

int main(int argc, char **argv)
{
    if (argc < 7) return 2;
    size_t size, sprite_size;
    const uint8_t *data = load(argv[1], &size);
    const uint8_t *sprite_px = load(argv[2], &sprite_size);
    wolf_pack_t pack;
    if (!wolf_pack_open(&pack, data, size)) { fprintf(stderr, "pack rejected\n"); return 1; }
    wolf_view_t view = { atoi(argv[4]), atoi(argv[5]), atoi(argv[6]), 0, { { 0 } } };
    for (int i = 7; i + 3 < argc && view.overlay_count < WOLF_MAX_OVERLAYS; i += 4)
        view.overlays[view.overlay_count++] = (wolf_overlay_t){ atoi(argv[i]), atoi(argv[i + 1]), atoi(argv[i + 2]), atoi(argv[i + 3]) };
    wolf_sprite_set_t sprites = { wolf_sprites, WOLF_SPRITE_COUNT, sprite_px };
    wolf_scratch_t scratch = { NULL, malloc(pack.max_band_jpeg), malloc(wolf_pack_band_pixels(&pack) * 2),
                               malloc(wolf_pack_band_pixels(&pack) * 2) };
    wolf_pack_decoder_open(&scratch.decoder);
    uint16_t *frame = calloc((size_t)pack.width * pack.height, 2);
    uint16_t *window = malloc((size_t)pack.width * pack.band_rows * 2);
    /* Two column windows per band exercise the windowed output used for partial panel updates. */
    const int split = 137, spans[2][2] = { { 0, split }, { split, 360 } };
    for (int band = 0; band < pack.band_count; band++) {
        const int rows = band * pack.band_rows + pack.band_rows <= pack.height ? pack.band_rows : pack.height - band * pack.band_rows;
        for (int s = 0; s < 2; s++) {
            const int x1 = spans[s][0], x2 = spans[s][1];
            if (!wolf_pack_compose_band(&pack, &view, &sprites, band, x1, x2, window, &scratch))
                { fprintf(stderr, "band %d failed\n", band); return 1; }
            for (int r = 0; r < rows; r++)
                memcpy(frame + (size_t)(band * pack.band_rows + r) * pack.width + x1, window + (size_t)r * (x2 - x1), (size_t)(x2 - x1) * 2);
        }
    }
    FILE *out = fopen(argv[3], "wb");
    fwrite(frame, 2, (size_t)pack.width * pack.height, out);
    fclose(out);
    /* A corrupt table must be rejected. */
    uint8_t *bad = malloc(size);
    memcpy(bad, data, size);
    bad[WOLF_PACK_HEADER_BYTES + 5] ^= 1; /* inside the frame table */
    if (wolf_pack_open(&pack, bad, size)) { fprintf(stderr, "corrupt pack accepted\n"); return 1; }
    if (wolf_pack_open(&pack, data, size - 1)) { fprintf(stderr, "truncated pack accepted\n"); return 1; }
    return 0;
}

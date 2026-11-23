#include "wolf_pack.h"

#include <stdlib.h>
#include <string.h>

#ifdef WOLF_PACK_HOST
#include <stdio.h>
#include <jpeglib.h>
#include <zlib.h>
#define WOLF_CRC32(data, len) ((uint32_t)crc32(0, (data), (uInt)(len)))
#else
#include "esp_jpeg_dec.h"
#include "esp_rom_crc.h"
#define WOLF_CRC32(data, len) esp_rom_crc32_le(0, (data), (len))
#endif

enum { FRAME_ENTRY = 16, BAND_ENTRY = 8, MAX_BAND_JPEG = 16384 };

static uint32_t rd16(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8; }
static uint32_t rd32(const uint8_t *p) { return rd16(p) | rd16(p + 2) << 16; }

typedef struct {
    wolf_rect_t rect;
    uint32_t header_offset, header_len, sof_pos;
} frame_info_t;

static frame_info_t frame_info(const wolf_pack_t *pack, int frame)
{
    const uint8_t *p = pack->frame_table + (size_t)frame * FRAME_ENTRY;
    return (frame_info_t){ { (int)rd16(p), (int)rd16(p + 2), (int)rd16(p + 4), (int)rd16(p + 6) },
                           rd32(p + 8), rd16(p + 12), rd16(p + 14) };
}

static void band_entry(const wolf_pack_t *pack, int frame, int band, uint32_t *offset, uint32_t *size)
{
    const uint8_t *p = pack->band_table + ((size_t)frame * pack->band_count + band) * BAND_ENTRY;
    *offset = rd32(p);
    *size = rd32(p + 4);
}

static int band_rows(const wolf_pack_t *pack, int band)
{
    const int left = pack->height - band * pack->band_rows;
    return left < pack->band_rows ? left : pack->band_rows;
}

bool wolf_pack_open(wolf_pack_t *pack, const uint8_t *data, size_t size)
{
    memset(pack, 0, sizeof(*pack));
    if (size < WOLF_PACK_HEADER_BYTES || memcmp(data, "WJPK", 4) != 0 || rd16(data + 4) != 2) return false;
    wolf_pack_t p = { data, 0, (int)rd16(data + 6), (int)rd16(data + 8), (int)rd16(data + 10), (int)rd16(data + 12),
                      (int)rd16(data + 14), (int)rd16(data + 28), NULL, NULL };
    const uint32_t table = rd32(data + 16), total = rd32(data + 20), crc = rd32(data + 24);
    if (p.width <= 0 || p.width > 1024 || p.band_rows != 16 || p.frame_count <= 0 ||
        p.band_count != (p.height + p.band_rows - 1) / p.band_rows || p.max_band_jpeg <= 4 || p.max_band_jpeg > MAX_BAND_JPEG)
        return false;
    const size_t tables = (size_t)p.frame_count * (FRAME_ENTRY + (size_t)p.band_count * BAND_ENTRY);
    if (total > size || table != WOLF_PACK_HEADER_BYTES || tables > total - table || WOLF_CRC32(data + table, tables) != crc)
        return false;
    p.size = total;
    p.frame_table = data + table;
    p.band_table = p.frame_table + (size_t)p.frame_count * FRAME_ENTRY;
    const uint32_t data_start = table + (uint32_t)tables;
    for (int f = 0; f < p.frame_count; f++) {
        const frame_info_t fi = frame_info(&p, f);
        const wolf_rect_t r = fi.rect;
        if (r.w && (r.x0 + r.w > p.width || r.y0 + r.h > p.height || r.h <= 0 || r.y0 % p.band_rows ||
                    fi.header_offset < data_start || fi.header_offset > total || fi.header_len > total - fi.header_offset ||
                    fi.sof_pos + 2 > fi.header_len))
            return false;
        for (int b = 0; b < p.band_count; b++) {
            uint32_t offset, bytes;
            band_entry(&p, f, b, &offset, &bytes);
            const int y = b * p.band_rows;
            const bool inside = r.w && y >= r.y0 && y < r.y0 + r.h;
            if (inside != (bytes > 0)) return false;
            if (inside && (offset < data_start || offset > total || bytes > total - offset ||
                           fi.header_len + bytes + 2 > (uint32_t)p.max_band_jpeg))
                return false;
        }
    }
    *pack = p;
    return true;
}

wolf_rect_t wolf_pack_frame_rect(const wolf_pack_t *pack, int frame)
{
    return frame_info(pack, frame).rect;
}

size_t wolf_pack_band_pixels(const wolf_pack_t *pack)
{
    return (size_t)((pack->width + 15) / 16 * 16) * pack->band_rows;
}

/* ------------------------------------------------------------------ band decoding */

#ifdef WOLF_PACK_HOST
bool wolf_pack_decoder_open(void **decoder)
{
    *decoder = (void *)1;
    return true;
}

/** libjpeg without fancy upsampling: a band then decodes exactly like the same rows of the whole frame. */
static bool decode_jpeg(void *decoder, uint8_t *jpeg, size_t len, int w, int rows, uint16_t *out, size_t capacity, int *stride)
{
    (void)decoder;
    struct jpeg_decompress_struct cinfo;
    struct jpeg_error_mgr jerr;
    cinfo.err = jpeg_std_error(&jerr);
    jpeg_create_decompress(&cinfo);
    jpeg_mem_src(&cinfo, jpeg, (unsigned long)len);
    bool ok = jpeg_read_header(&cinfo, TRUE) == JPEG_HEADER_OK && (int)cinfo.image_width == w &&
              (int)cinfo.image_height == rows && (size_t)w * rows <= capacity;
    if (ok) {
        cinfo.do_fancy_upsampling = FALSE;
        cinfo.dct_method = JDCT_ISLOW;
        cinfo.out_color_space = JCS_RGB;
        jpeg_start_decompress(&cinfo);
        uint8_t line[3 * 1024];
        while (cinfo.output_scanline < cinfo.output_height) {
            const int y = (int)cinfo.output_scanline;
            uint8_t *row = line;
            jpeg_read_scanlines(&cinfo, &row, 1);
            for (int x = 0; x < w; x++) {
                const uint16_t c = (uint16_t)((line[3 * x] >> 3) << 11 | (line[3 * x + 1] >> 2) << 5 | line[3 * x + 2] >> 3);
                out[(size_t)y * w + x] = (uint16_t)(c << 8 | c >> 8);
            }
        }
        jpeg_finish_decompress(&cinfo);
        *stride = w;
    }
    jpeg_destroy_decompress(&cinfo);
    return ok;
}
#else
bool wolf_pack_decoder_open(void **decoder)
{
    jpeg_dec_config_t config = DEFAULT_JPEG_DEC_CONFIG();
    config.output_type = JPEG_PIXEL_FORMAT_RGB565_BE; /* byte-swapped, as the panel takes it */
    return jpeg_dec_open(&config, (jpeg_dec_handle_t *)decoder) == JPEG_ERR_OK;
}

static int ceil16(int v) { return (v + 15) & ~15; }

static bool decode_jpeg(void *decoder, uint8_t *jpeg, size_t len, int w, int rows, uint16_t *out, size_t capacity, int *stride)
{
    jpeg_dec_io_t io = { .inbuf = jpeg, .inbuf_len = (int)len, .outbuf = (uint8_t *)out };
    jpeg_dec_header_info_t info;
    int out_len = 0;
    if (jpeg_dec_parse_header(decoder, &io, &info) != JPEG_ERR_OK || info.width != w || info.height != rows ||
        jpeg_dec_get_outbuf_len(decoder, &out_len) != JPEG_ERR_OK || out_len <= 0 || (size_t)out_len > capacity * 2)
        return false;
    /* The decoder may pad rows to whole MCUs; derive the row stride from the buffer it asks for. */
    const int px = out_len / 2;
    if (px == w * rows || px == w * ceil16(rows)) *stride = w;
    else if (px == ceil16(w) * rows || px == ceil16(w) * ceil16(rows)) *stride = ceil16(w);
    else return false;
    return jpeg_dec_process(decoder, &io) == JPEG_ERR_OK;
}
#endif

/** Decodes one band of a frame; returns its pixels with the row stride, or NULL outside the crop or on failure. */
static const uint16_t *decode_band(const wolf_pack_t *pack, int frame, const frame_info_t *fi, int band, uint16_t *out,
                                   wolf_scratch_t *scratch, int *stride, bool *failed)
{
    uint32_t offset, bytes;
    band_entry(pack, frame, band, &offset, &bytes);
    if (!bytes) return NULL;
    const int rows = band_rows(pack, band) < fi->rect.y0 + fi->rect.h - band * pack->band_rows
                         ? band_rows(pack, band) : fi->rect.y0 + fi->rect.h - band * pack->band_rows;
    uint8_t *j = scratch->jpeg;
    memcpy(j, pack->data + fi->header_offset, fi->header_len);
    j[fi->sof_pos] = (uint8_t)(rows >> 8);
    j[fi->sof_pos + 1] = (uint8_t)rows;
    memcpy(j + fi->header_len, pack->data + offset, bytes);
    j[fi->header_len + bytes] = 0xFF;
    j[fi->header_len + bytes + 1] = 0xD9;
    if (decode_jpeg(scratch->decoder, j, fi->header_len + bytes + 2, fi->rect.w, rows, out, wolf_pack_band_pixels(pack), stride))
        return out;
    *failed = true;
    return NULL;
}

/* ------------------------------------------------------------------ composition */

/** Blends two byte-swapped RGB565 pixels; w is the weight of b, 0..32. */
static inline uint16_t mix565(uint16_t a, uint16_t b, uint32_t w)
{
    uint32_t ca = __builtin_bswap16(a), cb = __builtin_bswap16(b);
    ca = (ca | ca << 16) & 0x07E0F81Fu;
    cb = (cb | cb << 16) & 0x07E0F81Fu;
    const uint32_t c = ((((cb - ca) * w) >> 5) + ca) & 0x07E0F81Fu;
    return __builtin_bswap16((uint16_t)(c | c >> 16));
}

static void draw_overlays(const wolf_view_t *view, const wolf_sprite_set_t *sprites, int x1, int x2, int y0, int rows,
                          uint16_t *dst)
{
    const int stride = x2 - x1;
    for (int i = 0; i < view->overlay_count; i++) {
        const wolf_overlay_t *o = &view->overlays[i];
        if (o->alpha <= 0 || o->sprite < 0 || o->sprite >= sprites->count) continue;
        const wolf_sprite_t *s = &sprites->table[o->sprite];
        const uint16_t *px = (const uint16_t *)(sprites->pixels + s->offset);
        const uint8_t *alpha = sprites->pixels + s->offset + (size_t)s->w * s->h * 2;
        const int sy0 = o->y > y0 ? o->y : y0, sy1 = o->y + s->h < y0 + rows ? o->y + s->h : y0 + rows;
        const int sx0 = o->x > x1 ? o->x : x1, sx1 = o->x + s->w < x2 ? o->x + s->w : x2;
        const uint32_t scale = (uint32_t)(o->alpha > 255 ? 255 : o->alpha);
        for (int y = sy0; y < sy1; y++) {
            uint16_t *row = dst + (size_t)(y - y0) * stride - x1;
            const int sr = (y - o->y) * s->w - o->x;
            for (int x = sx0; x < sx1; x++) {
                const uint32_t a = (alpha[sr + x] * scale + 255) >> 11; /* 0..32 */
                if (a) row[x] = mix565(row[x], px[sr + x], a);
            }
        }
    }
}

bool wolf_pack_compose_band(const wolf_pack_t *pack, const wolf_view_t *view, const wolf_sprite_set_t *sprites,
                            int band, int x1, int x2, uint16_t *dst, wolf_scratch_t *scratch)
{
    const int rows = band_rows(pack, band), stride = x2 - x1;
    int a = view->a, b = view->b, mix = view->mix;
    if (mix >= WOLF_MIX_MAX) {
        a = b;
        mix = 0;
    }
    bool failed = false;
    const frame_info_t fa = frame_info(pack, a);
    int sa = 0, sb = 0;
    const uint16_t *pa = decode_band(pack, a, &fa, band, scratch->band_a, scratch, &sa, &failed);
    frame_info_t fb = fa;
    const uint16_t *pb = NULL;
    if (mix > 0) {
        fb = frame_info(pack, b);
        pb = decode_band(pack, b, &fb, band, scratch->band_b, scratch, &sb, &failed);
    }
    const int ax0 = fa.rect.x0, ax1 = fa.rect.x0 + fa.rect.w, bx0 = fb.rect.x0, bx1 = fb.rect.x0 + fb.rect.w;

    if (mix == 0 || (!pa && !pb)) {
        /* One frame: black around the copied span of its crop. */
        const int lo = ax0 > x1 ? ax0 : x1, hi = ax1 < x2 ? ax1 : x2;
        for (int r = 0; r < rows; r++) {
            uint16_t *row = dst + (size_t)r * stride;
            if (!pa || lo >= hi) {
                memset(row, 0, (size_t)stride * 2);
                continue;
            }
            memset(row, 0, (size_t)(lo - x1) * 2);
            memcpy(row + lo - x1, pa + (size_t)r * sa + (lo - ax0), (size_t)(hi - lo) * 2);
            memset(row + hi - x1, 0, (size_t)(x2 - hi) * 2);
        }
    } else {
        /* Split the window at the crop edges once, so each span blends without per-pixel range checks. */
        int cuts[6] = { x1, x2, 0, 0, 0, 0 }, n = 2;
        const int edges[4] = { pa ? ax0 : x1, pa ? ax1 : x1, pb ? bx0 : x1, pb ? bx1 : x1 };
        for (int e = 0; e < 4; e++)
            if (edges[e] > x1 && edges[e] < x2) cuts[n++] = edges[e];
        for (int i = 1; i < n; i++)
            for (int k = i; k > 0 && cuts[k - 1] > cuts[k]; k--) {
                const int t = cuts[k];
                cuts[k] = cuts[k - 1];
                cuts[k - 1] = t;
            }
        const uint32_t w = (uint32_t)mix;
        for (int r = 0; r < rows; r++) {
            uint16_t *row = dst + (size_t)r * stride - x1;
            const uint16_t *ra = pa ? pa + (size_t)r * sa - ax0 : NULL;
            const uint16_t *rb = pb ? pb + (size_t)r * sb - bx0 : NULL;
            for (int i = 0; i + 1 < n; i++) {
                const int lo = cuts[i], hi = cuts[i + 1];
                if (lo >= hi) continue;
                const bool in_a = ra && lo >= ax0 && lo < ax1, in_b = rb && lo >= bx0 && lo < bx1;
                if (in_a && in_b) {
                    for (int x = lo; x < hi; x++) row[x] = mix565(ra[x], rb[x], w);
                } else if (in_a) {
                    for (int x = lo; x < hi; x++) row[x] = mix565(ra[x], 0, w);
                } else if (in_b) {
                    for (int x = lo; x < hi; x++) row[x] = mix565(0, rb[x], w);
                } else {
                    memset(row + lo, 0, (size_t)(hi - lo) * 2);
                }
            }
        }
    }
    draw_overlays(view, sprites, x1, x2, band * pack->band_rows, rows, dst);
    return !failed;
}

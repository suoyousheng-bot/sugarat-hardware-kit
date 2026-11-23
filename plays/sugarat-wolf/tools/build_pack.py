"""Build the Wolf Companion frame pack and overlay sprites.

The 287 360x360 baseline JPEG frames (4:2:0) from the original Wolf Companion 1.0.0 image are repacked
losslessly with jpegtran: each frame is cropped to the 16-pixel-aligned box around its non-black pixels
and re-entropy-coded with optimized Huffman tables and a restart marker after every MCU row. The scan
is then split at the restart markers, so every 16-row band is an independent entropy segment. On the
badge a band is decoded by joining the frame's shared header (SOF height patched to the band height,
DRI removed) with the band's segment and an EOI; two cores decode two bands at once.

Pack layout (little endian):
  0  "WJPK"         magic
  4  u16 version    2
  6  u16 width, u16 height, u16 band_rows, u16 band_count, u16 frame_count
  16 u32 frame_table_offset, u32 total_size, u32 crc32 of both tables, u16 max_band_jpeg, u16 0
  frame table: frame_count * 16 bytes: u16 x0, y0, w, h (w == 0: black frame), u32 header_offset,
               u16 header_len, u16 sof_height_pos (offset of the SOF height inside the header)
  band table:  frame_count * band_count * 8 bytes: u32 offset, u32 size (0: band outside the crop)
  data:        deduplicated headers and band segments

Usage: python3 build_pack.py <frames_dir> <out_pack> <out_sprites_bin> <out_sprites_header>
"""
import subprocess
import struct
import sys
import tempfile
import zlib
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw

WIDTH = HEIGHT = 360
BAND_ROWS = 16  # MCU height of 4:2:0
BAND_COUNT = (HEIGHT + BAND_ROWS - 1) // BAND_ROWS
HEADER_BYTES = 32
FRAME = struct.Struct("<HHHHIHH")
BAND = struct.Struct("<II")
# JPEG ringing around the black backdrop; pixels this dark show as black on the panel anyway.
BLACK_SUM = 24


def crop_box(path):
    a = np.asarray(Image.open(path).convert("RGB")).astype(np.int32)
    ys, xs = np.nonzero(a.sum(axis=2) >= BLACK_SUM)
    if ys.size == 0:
        return None
    x0, y0 = xs.min() // 16 * 16, ys.min() // 16 * 16
    x1, y1 = min(WIDTH, -(-(xs.max() + 1) // 16) * 16), min(HEIGHT, -(-(ys.max() + 1) // 16) * 16)
    return int(x0), int(y0), int(x1 - x0), int(y1 - y0)


def segments(path):
    """Returns the jpegtran header split into (header, sof_height_pos) and the per-MCU-row scan segments."""
    data = Path(path).read_bytes()
    assert data[:2] == b"\xff\xd8"
    pos, header, sof = 2, bytearray(b"\xff\xd8"), None
    while True:
        assert data[pos] == 0xFF
        marker = data[pos + 1]
        length = struct.unpack_from(">H", data, pos + 2)[0]
        segment = data[pos:pos + 2 + length]
        if marker == 0xC0:
            sof = len(header) + 5
            assert segment[9] == 3 and segment[11] == 0x22, "expect YCbCr 4:2:0 baseline"
        elif marker in (0xC1, 0xC2, 0xC3):
            raise SystemExit(f"{path}: only baseline JPEG is supported")
        pos += 2 + length
        if marker == 0xDD:  # DRI: a band has one MCU row, so it needs no restart markers
            continue
        header += segment
        if marker == 0xDA:
            break
    assert sof is not None
    scan_end = data.rindex(b"\xff\xd9")
    parts, start, i = [], pos, pos
    while True:
        i = data.find(b"\xff", i, scan_end)
        if i < 0:
            parts.append(data[start:scan_end])
            break
        nxt = data[i + 1]
        if 0xD0 <= nxt <= 0xD7:
            parts.append(data[start:i])
            start = i = i + 2
        else:
            i += 2  # stuffed 0xFF00 or fill bytes
    return bytes(header), sof, parts


def band_jpeg(header, sof, rows, segment):
    h = bytearray(header)
    struct.pack_into(">H", h, sof, rows)
    return bytes(h) + segment + b"\xff\xd9"


def djpeg(data, work):
    src = Path(work) / "in.jpg"
    src.write_bytes(data)
    out = subprocess.run(["djpeg", "-nosmooth", "-dct", "int", "-pnm", str(src)], check=True, capture_output=True).stdout
    return np.asarray(Image.open(__import__("io").BytesIO(out)).convert("RGB"))


def build_pack(frames, out, verify_every=4):
    frame_table, band_table, blob = bytearray(), bytearray(), bytearray()
    headers = {}
    max_band = 0
    data_start = HEADER_BYTES + len(frames) * (FRAME.size + BAND_COUNT * BAND.size)
    with tempfile.TemporaryDirectory() as work:
        for index, path in enumerate(frames):
            box = crop_box(path)
            if box is None:
                frame_table += FRAME.pack(0, 0, 0, 0, 0, 0, 0)
                band_table += BAND.pack(0, 0) * BAND_COUNT
                continue
            x0, y0, w, h = box
            cropped = Path(work) / "crop.jpg"
            cropped.write_bytes(subprocess.run(
                ["jpegtran", "-copy", "none", "-optimize", "-restart", "1", "-crop", f"{w}x{h}+{x0}+{y0}", str(path)],
                check=True, capture_output=True).stdout)
            header, sof, parts = segments(cropped)
            rows_total = -(-h // BAND_ROWS)
            assert len(parts) == rows_total, (path, len(parts), rows_total)
            if header not in headers:
                headers[header] = data_start + len(blob)
                blob += header
            frame_table += FRAME.pack(x0, y0, w, h, headers[header], len(header), sof)
            whole = djpeg(cropped.read_bytes(), work) if index % verify_every == 0 else None
            if whole is not None:
                full = djpeg(Path(path).read_bytes(), work)
                assert (full[y0:y0 + h, x0:x0 + w] == whole).all(), (path, "crop is not lossless")
                outside = full.astype(np.int32).sum(axis=2) >= BLACK_SUM
                outside[y0:y0 + h, x0:x0 + w] = False
                assert not outside.any(), (path, "crop drops visible pixels")
            for band in range(BAND_COUNT):
                k = band - y0 // BAND_ROWS
                if not 0 <= k < rows_total:
                    band_table += BAND.pack(0, 0)
                    continue
                band_table += BAND.pack(data_start + len(blob), len(parts[k]))
                blob += parts[k]
                rows = min(BAND_ROWS, h - k * BAND_ROWS)
                jpeg = band_jpeg(header, sof, rows, parts[k])
                max_band = max(max_band, len(jpeg))
                if whole is not None:
                    got = djpeg(jpeg, work)
                    assert (got == whole[k * BAND_ROWS:k * BAND_ROWS + rows]).all(), (path, band)
    tables = frame_table + band_table
    total = data_start + len(blob)
    header = b"WJPK" + struct.pack("<HHHHHHIIIHH", 2, WIDTH, HEIGHT, BAND_ROWS, BAND_COUNT, len(frames),
                                   HEADER_BYTES, total, zlib.crc32(tables), max_band, 0)
    assert len(header) == HEADER_BYTES
    Path(out).write_bytes(header + tables + blob)
    return total, len(headers), max_band


# ---------------------------------------------------------------- overlay sprites

SS = 4  # supersampling factor for antialiased strokes


def stroke(draw, points, width, fill):
    draw.line([(x * SS, y * SS) for x, y in points], fill=fill, width=width * SS, joint="curve")
    r = width * SS / 2
    for x, y in (points[0], points[-1]):
        draw.ellipse((x * SS - r, y * SS - r, x * SS + r, y * SS + r), fill=fill)


def arc_points(cx, cy, r, a0, a1, steps=24):
    t = np.linspace(np.radians(a0), np.radians(a1), steps)
    return list(zip(cx + r * np.cos(t), cy + r * np.sin(t)))


def sprite(w, h, paint):
    big = Image.new("RGBA", (w * SS, h * SS), (0, 0, 0, 0))
    paint(ImageDraw.Draw(big))
    return big.resize((w, h), Image.LANCZOS)


def question(draw):
    path = arc_points(30, 26, 15, 200, 360 + 50) + [(30, 50), (30, 56)]
    for width, colour in ((17, (40, 20, 10, 255)), (9, (255, 196, 64, 255))):
        stroke(draw, path, width, colour)
        r = width * SS / 2
        draw.ellipse((30 * SS - r, 70 * SS - r, 30 * SS + r, 70 * SS + r), fill=colour)


def letter_z(size):
    def paint(draw):
        m = size * 0.22
        path = [(m, m), (size - m, m), (m, size - m), (size - m, size - m)]
        width = max(4, int(size * 0.17))
        stroke(draw, path, width + 6, (20, 30, 60, 255))
        stroke(draw, path, width, (170, 215, 255, 255))
    return paint


def star(draw):
    c, r, k = 14, 13, 3.6
    pts = []
    for i in range(8):
        a = np.radians(i * 45 - 90)
        rr = r if i % 2 == 0 else k
        pts.append(((c + rr * np.cos(a)) * SS, (c + rr * np.sin(a)) * SS))
    draw.polygon(pts, fill=(255, 230, 110, 255))
    draw.ellipse(((c - 3) * SS, (c - 3) * SS, (c + 3) * SS, (c + 3) * SS), fill=(255, 255, 230, 255))


SPRITES = [("QMARK", 60, 86, question), ("ZBIG", 46, 46, letter_z(46)), ("ZSMALL", 30, 30, letter_z(30)),
           ("STAR", 28, 28, star)]


def build_sprites(bin_out, header_out):
    blob = bytearray()
    lines = ["/** Generated by tools/build_pack.py; do not edit. */", "#pragma once", "",
             '#include "wolf_pack.h"', "", "enum {"]
    for name, *_ in SPRITES:
        lines.append(f"    WOLF_SPRITE_{name},")
    lines += ["    WOLF_SPRITE_COUNT,", "};", "", "static const wolf_sprite_t wolf_sprites[WOLF_SPRITE_COUNT] = {"]
    for name, w, h, paint in SPRITES:
        rgba = np.asarray(sprite(w, h, paint)).astype(np.uint16)
        r, g, b, a = (rgba[..., i] for i in range(4))
        c = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)
        c = ((c & 0xFF) << 8) | (c >> 8)
        lines.append(f"    [WOLF_SPRITE_{name}] = {{ {w}, {h}, {len(blob)} }},")
        blob += c.astype("<u2").tobytes() + a.astype(np.uint8).tobytes()
        while len(blob) % 2:
            blob += b"\0"
    lines += ["};", ""]
    Path(bin_out).write_bytes(blob)
    Path(header_out).write_text("\n".join(lines))


def main():
    frames = sorted(Path(sys.argv[1]).glob("f*.jpg"))
    original = sum(p.stat().st_size for p in frames)
    total, headers, max_band = build_pack(frames, sys.argv[2])
    build_sprites(sys.argv[3], sys.argv[4])
    print(f"{len(frames)} frames ({original} bytes of JPEG) -> pack {total} bytes ({total / 1048576:.2f} MiB), "
          f"{headers} distinct headers, largest band JPEG {max_band} bytes; band split verified losslessly")


if __name__ == "__main__":
    main()

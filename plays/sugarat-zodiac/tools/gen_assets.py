#!/usr/bin/env python3
"""Generates main/zodiac/zd_assets.{h,c}: copy, constellation figures and A8 glyph atlases.

All on-screen text comes from the tables below, so the firmware carries only the glyphs it draws.
The daily-reading hash in main/zodiac/zd_fortune.c and the web preview index LINES in this order.

Usage: python3 tools/gen_assets.py   (needs Pillow with FreeType variable-font support)
"""
import math
import os
import sys

from PIL import ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FONTS = os.path.join(ROOT, "assets", "fonts")
OUT = os.path.join(ROOT, "main", "zodiac")

# zh name, latin caps, glyph, element, en name
SIGNS = [
    ("白羊座", "ARIES", "♈", "fire", "Aries"),
    ("金牛座", "TAURUS", "♉", "earth", "Taurus"),
    ("双子座", "GEMINI", "♊", "air", "Gemini"),
    ("巨蟹座", "CANCER", "♋", "water", "Cancer"),
    ("狮子座", "LEO", "♌", "fire", "Leo"),
    ("处女座", "VIRGO", "♍", "earth", "Virgo"),
    ("天秤座", "LIBRA", "♎", "air", "Libra"),
    ("天蝎座", "SCORPIO", "♏", "water", "Scorpio"),
    ("射手座", "SAGITTARIUS", "♐", "fire", "Sagittarius"),
    ("摩羯座", "CAPRICORN", "♑", "earth", "Capricorn"),
    ("水瓶座", "AQUARIUS", "♒", "air", "Aquarius"),
    ("双鱼座", "PISCES", "♓", "water", "Pisces"),
]
ELEMENTS = ["fire", "earth", "air", "water"]

LINES = [
    ("先做，再想", "Act first, think later"), ("好好吃饭，好好睡", "Eat well, sleep well"),
    ("话说一半就够了", "Say less, mean more"), ("给自己留一盏灯", "Keep a light on for you"),
    ("今天你最亮", "You shine brightest today"), ("不完美也很好", "Imperfect is fine"),
    ("按自己的节奏走", "Move at your own pace"), ("放下一件小事", "Let one small thing go"),
    ("出门走走吧", "Go take a walk"), ("一步一步就到了", "Step by step, you arrive"),
    ("换条路试试", "Try another road"), ("跟着直觉走", "Trust your gut"),
    ("宜主动开口", "Speak up first"), ("慢一点也会抵达", "Slow still gets there"),
    ("把夸奖说出口", "Say the compliment"), ("今天适合收尾", "A good day to finish"),
    ("留点时间发呆", "Leave time to daydream"), ("好运在路上", "Luck is on its way"),
    ("先照顾好自己", "Take care of you first"), ("答案会自己出现", "The answer will find you"),
    ("小事也值得庆祝", "Small wins count"), ("给老朋友发条消息", "Text an old friend"),
    ("今天别急着决定", "No big decisions today"), ("笑一下，会好的", "Smile, it gets better"),
]

UI = {
    "rim_idle": ("靠近 · 看今天的星运 · ", "COME CLOSER · TODAY'S STARS · "),
    "today": ("今日星运", "TODAY"),
    "confirm": ("点中间确认", "Tap to save"),
    "saved": ("已保存", "Saved"),
}

DATES = (
    ["3.21–4.19", "4.20–5.20", "5.21–6.21", "6.22–7.22", "7.23–8.22", "8.23–9.22",
     "9.23–10.23", "10.24–11.22", "11.23–12.21", "12.22–1.19", "1.20–2.18", "2.19–3.20"],
    ["Mar 21 – Apr 19", "Apr 20 – May 20", "May 21 – Jun 21", "Jun 22 – Jul 22", "Jul 23 – Aug 22",
     "Aug 23 – Sep 22", "Sep 23 – Oct 23", "Oct 24 – Nov 22", "Nov 23 – Dec 21", "Dec 22 – Jan 19",
     "Jan 20 – Feb 18", "Feb 19 – Mar 20"],
)

# Stick figures: stars as (RA hours, Dec degrees), edges in drawing order, brightest star index.
CONST = [
    ([(2.83, 27.3), (2.12, 23.5), (1.91, 20.8), (1.89, 19.3)], [(0, 1), (1, 2), (2, 3)], 1),
    ([(4.60, 16.5), (5.44, 28.6), (5.63, 21.1), (4.48, 15.9), (4.33, 15.6), (4.38, 17.5), (4.48, 19.2), (4.01, 12.5), (3.79, 24.1)],
     [(2, 0), (0, 3), (3, 4), (4, 5), (5, 6), (6, 1), (4, 7)], 0),
    ([(7.58, 31.9), (7.76, 28.0), (6.63, 16.4), (6.73, 25.1), (6.38, 22.5), (7.34, 22.0), (7.07, 20.6), (7.19, 30.2), (7.60, 26.9)],
     [(0, 7), (7, 3), (3, 4), (0, 1), (1, 8), (8, 5), (5, 6), (6, 2)], 1),
    ([(8.97, 11.9), (8.28, 9.2), (8.75, 18.2), (8.72, 21.5), (8.78, 28.8)], [(1, 2), (2, 0), (2, 3), (3, 4)], 1),
    ([(10.14, 12.0), (10.12, 16.8), (10.33, 19.8), (10.28, 23.4), (9.88, 26.0), (9.76, 23.8), (11.24, 20.5), (11.24, 15.4), (11.82, 14.6)],
     [(5, 4), (4, 3), (3, 2), (2, 1), (1, 0), (0, 7), (7, 8), (8, 6), (6, 2)], 0),
    ([(13.42, -11.2), (12.69, -1.45), (12.93, 3.4), (13.04, 11.0), (13.58, -0.6), (11.85, 1.8), (12.33, -0.67), (14.27, -6.0), (14.77, 1.9)],
     [(5, 6), (6, 1), (1, 2), (2, 3), (1, 0), (2, 4), (4, 8), (0, 7)], 0),
    ([(14.85, -16.0), (15.28, -9.4), (15.07, -25.3), (15.59, -14.8), (15.62, -28.1), (15.64, -29.8)],
     [(2, 0), (0, 1), (1, 3), (3, 0), (3, 4), (4, 5)], 1),
    ([(16.09, -19.8), (16.0, -22.6), (15.98, -26.1), (16.49, -26.4), (16.35, -25.6), (16.6, -28.2), (16.84, -34.3), (16.86, -38.0),
      (16.9, -42.4), (17.2, -43.2), (17.62, -43.0), (17.79, -40.1), (17.71, -39.0), (17.56, -37.1), (17.51, -37.3)],
     [(0, 1), (1, 2), (1, 4), (4, 3), (3, 5), (5, 6), (6, 7), (7, 8), (8, 9), (9, 10), (10, 11), (11, 12), (12, 13), (13, 14)], 3),
    ([(18.1, -30.4), (18.35, -29.8), (18.4, -34.4), (18.47, -25.4), (18.76, -27.0), (18.92, -26.3), (19.12, -27.7), (19.04, -29.9)],
     [(0, 1), (0, 2), (1, 2), (1, 3), (3, 4), (1, 4), (4, 7), (2, 7), (4, 5), (5, 6), (6, 7)], 2),
    ([(20.3, -12.5), (20.35, -14.8), (20.77, -25.3), (20.86, -26.9), (21.44, -22.4), (21.67, -16.7), (21.78, -16.1), (21.1, -17.2), (21.37, -16.8)],
     [(0, 1), (1, 2), (2, 3), (3, 4), (4, 6), (6, 5), (5, 8), (8, 7), (7, 1)], 6),
    ([(22.1, -0.3), (21.53, -5.6), (20.79, -9.5), (22.36, -1.4), (22.48, 0.0), (22.59, -0.1), (22.42, 1.4), (22.28, -7.8), (22.88, -7.6), (22.91, -15.8)],
     [(2, 1), (1, 0), (0, 3), (3, 4), (4, 5), (4, 6), (0, 7), (7, 8), (8, 9)], 1),
    ([(1.52, 15.3), (2.03, 2.8), (1.76, 9.2), (0.81, 7.6), (1.05, 7.9), (-0.02, 6.9), (-0.33, 5.6), (-0.53, 6.4), (-0.72, 3.3), (-0.55, 1.3),
      (-0.30, 1.8), (1.2, 30.1), (1.33, 27.3), (1.23, 24.6)],
     [(8, 7), (7, 6), (6, 10), (10, 9), (9, 8), (6, 5), (5, 3), (3, 4), (4, 1), (1, 2), (2, 0), (0, 13), (13, 12), (12, 11)], 0),
]

SC, NUNITO, SYM = "NotoSansSC.ttf", "Nunito-Variable.ttf", "NotoSansSymbols.ttf"


def is_latin(ch):
    return ord(ch) < 0x2000


def chars(strings, latin):
    return sorted({c for s in strings for c in s if is_latin(c) == latin and c != " "} | ({" "} if latin else set()))


def rim_strings():
    zh = [UI["rim_idle"][0]] + [f"{s[0]} · {s[1]} · {line[0]} · " for s in SIGNS for line in LINES]
    en = [UI["rim_idle"][1]] + [f"{s[1]} · {line[1].upper()} · " for s in SIGNS for line in LINES]
    return zh + en


# name, file, weight, size, characters, reference glyph for the visual middle, fallback face name
FACES = [
    ("rim_lat", NUNITO, 900, 20, chars(rim_strings(), True), "H", None),
    ("rim_cjk", SC, 900, 21, chars(rim_strings(), False), "中", "rim_lat"),
    ("num", NUNITO, 900, 112, list("0123456789"), "0", None),
    ("label_lat", NUNITO, 800, 14, chars([UI["today"][1]], True), "H", None),
    ("label_cjk", SC, 700, 14, chars([UI["today"][0]], False), "中", "label_lat"),
    ("name_lat", NUNITO, 900, 26, chars([s[4] for s in SIGNS], True), "H", None),
    ("name_cjk", SC, 900, 24, chars([s[0] for s in SIGNS], False), "中", "name_lat"),
    ("date", NUNITO, 800, 13, chars(DATES[0] + DATES[1], True) + ["–"], "0", None),
    ("hint_lat", NUNITO, 700, 11, chars([UI["confirm"][1], UI["saved"][1]], True), "H", None),
    ("hint_cjk", SC, 700, 11, chars([UI["confirm"][0], UI["saved"][0]], False), "中", "hint_lat"),
    ("pill_lat", NUNITO, 900, 13, list("EN "), "H", None),
    ("pill_cjk", SC, 900, 13, ["中"], "中", "pill_lat"),
    ("sym_big", SYM, 700, 150, [s[2] for s in SIGNS], None, None),
    ("sym_ring", SYM, 700, 30, [s[2] for s in SIGNS], None, None),
]


def load(file, weight, size):
    font = ImageFont.truetype(os.path.join(FONTS, file), size)
    font.set_variation_by_axes([weight])
    return font


def render_face(name, file, weight, size, charset, ref):
    font = load(file, weight, size)
    glyphs, bitmap = [], bytearray()
    for ch in sorted(set(charset), key=ord):
        mask, (ox, oy) = font.getmask2(ch, mode="L", anchor="ls")
        w, h = mask.size
        data = bytes(mask)
        if w and h and not any(data) and ch != " ":
            sys.exit(f"{name}: {ch!r} rendered empty; the font lacks it")
        glyphs.append((ord(ch), ox, oy, w, h, round(font.getlength(ch) * 16), len(bitmap)))
        bitmap += data
    mid = 0
    if ref:
        left, top, right, bottom = font.getbbox(ref, anchor="ls")
        mid = round((top + bottom) / 2 * 16)
    return glyphs, bitmap, mid


def project(stars, edges, bright):
    md = sum(d for _, d in stars) / len(stars) * math.pi / 180
    raw = [(-h * 15 * math.cos(md), -d) for h, d in stars]
    xs, ys = [p[0] for p in raw], [p[1] for p in raw]
    bw, bh = max(xs) - min(xs), max(ys) - min(ys)
    span = max(bw, bh)
    ox, oy = (max(xs) + min(xs)) / 2, (max(ys) + min(ys)) / 2
    pts = [((x - ox) / span, (y - oy) / span) for x, y in raw]
    return pts, edges, bright, bw / span, bh / span


def cstr(s):
    return '"' + "".join(f"\\x{b:02x}" for b in s.encode("utf-8")) + '"'


def main():
    os.makedirs(OUT, exist_ok=True)
    h = ["/* Generated by tools/gen_assets.py. Do not edit. */", "#pragma once", "#include <stdint.h>", "",
         "/** One A8 glyph; x/y place the bitmap's top-left relative to the pen on the baseline. */",
         "typedef struct { uint32_t cp; int16_t x, y; uint16_t w, h, adv16; uint32_t off; } zd_glyph_t;", "",
         "/** A rasterized face; mid16 is the baseline-to-visual-middle offset in 1/16 px (negative is up). */",
         "typedef struct zd_face { const zd_glyph_t *glyphs; uint16_t count; int16_t mid16;",
         "                         const uint8_t *bitmap; const struct zd_face *fallback; } zd_face_t;", "",
         "/** Constellation figure in a unit box centred on 0; aw/ah are its width and height in box units. */",
         "typedef struct { uint8_t stars, edges, bright; float aw, ah; const float (*pts)[2]; const uint8_t (*edge)[2]; } zd_const_t;", "",
         "enum { ZD_SIGNS = 12, ZD_LINES = %d, ZD_LANGS = 2 };" % len(LINES), ""]
    c = ['/* Generated by tools/gen_assets.py. Do not edit. */', '#include "zd_assets.h"', ""]

    for name, file, weight, size, charset, ref, fallback in FACES:
        glyphs, bitmap, mid = render_face(name, file, weight, size, charset, ref)
        h.append(f"extern const zd_face_t zd_face_{name};")
        c.append(f"static const uint8_t {name}_bits[{max(1, len(bitmap))}] = {{")
        for i in range(0, len(bitmap), 24):
            c.append("    " + ",".join(str(b) for b in bitmap[i:i + 24]) + ",")
        c.append("};")
        c.append(f"static const zd_glyph_t {name}_glyphs[{len(glyphs)}] = {{")
        for g in glyphs:
            c.append("    {%d,%d,%d,%d,%d,%d,%d}," % g)
        c.append("};")
        fb = f"&zd_face_{fallback}" if fallback else "0"
        c.append(f"const zd_face_t zd_face_{name} = {{{name}_glyphs, {len(glyphs)}, {mid}, {name}_bits, {fb}}};")
        c.append("")
        print(f"{name:10s} {len(glyphs):4d} glyphs {len(bitmap):7d} bytes")

    h += ["", "extern const char *const zd_sign_name[ZD_LANGS][ZD_SIGNS];", "extern const char *const zd_sign_caps[ZD_SIGNS];",
          "extern const uint32_t zd_sign_glyph[ZD_SIGNS];", "extern const uint8_t zd_sign_element[ZD_SIGNS];",
          "extern const char *const zd_line[ZD_LINES][ZD_LANGS];", "extern const char *const zd_dates[ZD_LANGS][ZD_SIGNS];"]
    for key in UI:
        h.append(f"extern const char *const zd_str_{key}[ZD_LANGS];")
    h.append("extern const zd_const_t zd_const[ZD_SIGNS];")

    c.append("const char *const zd_sign_name[ZD_LANGS][ZD_SIGNS] = {")
    c.append("    {" + ", ".join(cstr(s[0]) for s in SIGNS) + "},")
    c.append("    {" + ", ".join(cstr(s[4]) for s in SIGNS) + "},")
    c.append("};")
    c.append("const char *const zd_sign_caps[ZD_SIGNS] = {" + ", ".join(cstr(s[1]) for s in SIGNS) + "};")
    c.append("const uint32_t zd_sign_glyph[ZD_SIGNS] = {" + ", ".join(hex(ord(s[2])) for s in SIGNS) + "};")
    c.append("const uint8_t zd_sign_element[ZD_SIGNS] = {" + ", ".join(str(ELEMENTS.index(s[3])) for s in SIGNS) + "};")
    c.append("const char *const zd_line[ZD_LINES][ZD_LANGS] = {")
    for zh, en in LINES:
        c.append(f"    {{{cstr(zh)}, {cstr(en)}}},")
    c.append("};")
    c.append("const char *const zd_dates[ZD_LANGS][ZD_SIGNS] = {")
    for lang in DATES:
        c.append("    {" + ", ".join(cstr(d) for d in lang) + "},")
    c.append("};")
    for key, (zh, en) in UI.items():
        c.append(f"const char *const zd_str_{key}[ZD_LANGS] = {{{cstr(zh)}, {cstr(en)}}};")
    c.append("")
    for i, (stars, edges, bright) in enumerate(CONST):
        pts, edges, bright, aw, ah = project(stars, edges, bright)
        c.append(f"static const float const{i}_pts[][2] = {{" + ", ".join(f"{{{x:.5f}f,{y:.5f}f}}" for x, y in pts) + "};")
        c.append(f"static const uint8_t const{i}_edges[][2] = {{" + ", ".join(f"{{{a},{b}}}" for a, b in edges) + "};")
    c.append("const zd_const_t zd_const[ZD_SIGNS] = {")
    for i, (stars, edges, bright) in enumerate(CONST):
        _, _, _, aw, ah = project(stars, edges, bright)
        c.append(f"    {{{len(stars)}, {len(edges)}, {bright}, {aw:.5f}f, {ah:.5f}f, const{i}_pts, const{i}_edges}},")
    c.append("};")

    with open(os.path.join(OUT, "zd_assets.h"), "w") as f:
        f.write("\n".join(h) + "\n")
    with open(os.path.join(OUT, "zd_assets.c"), "w") as f:
        f.write("\n".join(c) + "\n")


if __name__ == "__main__":
    main()

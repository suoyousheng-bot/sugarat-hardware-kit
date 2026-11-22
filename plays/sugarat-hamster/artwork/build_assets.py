"""Pack the approved artwork for the 360x360 round display at native resolution."""
from pathlib import Path
import os
from PIL import Image, ImageDraw, ImageFont
import struct

root = Path(__file__).resolve().parent
output = root.parent / "main"
size = 360
scale = size / 1254
source = Image.open(root / "approved.png").convert("RGB").resize((size, size), Image.Resampling.LANCZOS)
background = Image.open(root / "empty.png").convert("RGB").resize((size, size), Image.Resampling.LANCZOS)

# Hamster torso outline in approved.png coordinates; the firmware draws the moving paws.
outline = [(413,802),(433,788),(457,802),(477,770),(520,740),
           (532,729),(585,729),(602,742),(640,742),(641,711),
           (660,699),(689,701),(706,688),(731,694),(751,720),
           (756,734),(788,743),(818,771),(836,800),(836,833),
           (816,854),(780,867),(744,873),(716,894),(689,903),
           (634,900),(610,913),(538,917),(495,914),(469,903),
           (440,871),(430,844),(414,835)]
coverage = Image.new("L", (1254, 1254))
ImageDraw.Draw(coverage).polygon(outline, fill=255)
coverage = coverage.resize((size, size), Image.Resampling.BOX)
crop = (116, 194, 242, 266)
PET_W, PET_H = crop[2] - crop[0], crop[3] - crop[1]


def rgb565(r, g, b):
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)


def pack(image, opacity=None):
    """RGB565, MSB first as LVGL stores it; 0 is reserved for transparent sprite pixels."""
    values = []
    for index, (r, g, b) in enumerate(image.getdata()):
        value = rgb565(r, g, b)
        if opacity is not None:
            value = (value or 0x0841) if opacity[index] else 0
        values.append(value)
    return struct.pack(">" + "H" * len(values), *values)


(output / "habitat.bin").write_bytes(pack(background))

pet = source.crop(crop)
pet_mask = [value >= 128 for value in coverage.crop(crop).getdata()]

# Locate the eye: the darkest blob in the head area.
eye = [(x, y) for y in range(26, 41) for x in range(92, 113)
       if pet_mask[y * PET_W + x] and sum(pet.getpixel((x, y))) < 150]
ex0, ey0 = min(x for x, _ in eye), min(y for _, y in eye)
ex1, ey1 = max(x for x, _ in eye), max(y for _, y in eye)
ring = [pet.getpixel((x, y)) for y in range(ey0 - 3, ey1 + 4) for x in range(ex0 - 3, ex1 + 4)
        if not (ex0 - 1 <= x <= ex1 + 1 and ey0 - 1 <= y <= ey1 + 1)]
fur = tuple(sorted(channel)[len(channel) // 2] for channel in zip(*ring))
ink = (54, 30, 24)


def eye_variant(kind):
    image = pet.copy()
    draw = ImageDraw.Draw(image)
    draw.rectangle((ex0 - 1, ey0 - 1, ex1 + 1, ey1 + 1), fill=fur)
    cx, cy = (ex0 + ex1) / 2, (ey0 + ey1) / 2
    if kind == "blink":
        draw.line((ex0 - 1, cy + 1, ex1 + 1, cy + 1), fill=ink, width=2)
        draw.point([(ex0 - 1, cy), (ex1 + 1, cy)], fill=ink)
    else:
        r = 3
        draw.line((cx - r, cy - r, cx + r, cy + r), fill=ink, width=2)
        draw.line((cx - r, cy + r, cx + r, cy - r), fill=ink, width=2)
    return image


pets = b"".join(pack(image, pet_mask) for image in (pet, eye_variant("blink"), eye_variant("dizzy")))
(output / "hamster.bin").write_bytes(pets)

# Curled-up hamster for tumbling, drawn on a 2-pixel grid to match the artwork.
BALL = 60
cell = Image.new("RGBA", (BALL // 2, BALL // 2))
d = ImageDraw.Draw(cell)
orange, cream, shade = (236, 160, 70), (252, 236, 206), (205, 124, 52)
dark, pink = (96, 50, 32), (238, 146, 146)
d.ellipse((2, 2, 27, 27), fill=dark)
d.ellipse((3, 3, 26, 26), fill=orange)
d.chord((3, 3, 26, 26), 20, 160, fill=cream)
d.arc((5, 5, 24, 24), 200, 300, fill=shade)
d.ellipse((17, 1, 22, 6), fill=dark)
d.ellipse((18, 2, 21, 5), fill=pink)
d.line((19, 11, 21, 12, 23, 11), fill=dark)
d.point([(24, 14)], fill=pink)
d.rectangle((10, 20, 11, 21), fill=pink)
d.rectangle((15, 21, 16, 22), fill=pink)
ball = cell.resize((BALL, BALL), Image.Resampling.NEAREST)
(output / "ball.bin").write_bytes(pack(ball.convert("RGB"), [a > 128 for a in ball.getchannel("A").getdata()]))

# Speech bubbles are screen-aligned; text is anti-aliased over white.
font = ImageFont.truetype(os.environ.get("HAMSTER_BUBBLE_FONT", "DejaVuSans-Bold.ttf"), 19)
bubbles = []
for text in ("Spin to make me run!", "Slow down, I'm dizzy!"):
    left, top, right, bottom = font.getbbox(text)
    width, height = right - left + 24, 34
    bubble = Image.new("RGBA", (width, height + 8))
    d = ImageDraw.Draw(bubble)
    d.rounded_rectangle((0, 0, width - 1, height - 1), radius=12, fill=(86, 46, 28))
    d.rounded_rectangle((2, 2, width - 3, height - 3), radius=10, fill=(255, 250, 238))
    tail = width // 2
    d.polygon([(tail - 8, height - 3), (tail + 8, height - 3), (tail, height + 7)], fill=(86, 46, 28))
    d.polygon([(tail - 5, height - 3), (tail + 5, height - 3), (tail, height + 3)], fill=(255, 250, 238))
    d.text((width / 2, height / 2), text, font=font, anchor="mm", fill=(96, 50, 32))
    bubbles.append(bubble)
header = []
blob = bytearray()
for index, bubble in enumerate(bubbles):
    header.append(f"enum {{ BUBBLE{index}_W = {bubble.width}, BUBBLE{index}_H = {bubble.height}, "
                  f"BUBBLE{index}_OFFSET = {len(blob) // 2} }};")
    blob.extend(pack(bubble.convert("RGB"), [a > 0 for a in bubble.getchannel("A").getdata()]))
(output / "bubble.bin").write_bytes(bytes(blob))

# Wheel rotation map. Level sets are ellipses whose centre slides from the hub (rho = 0)
# to the rim centre (rho = 1), so both stay fixed while spokes and bricks turn.
import math
HUB = (179.0, 170.5)
RIM = (177.5, 177.25)
RX, RY = 106.0, 110.0
DX, DY = RIM[0] - HUB[0], RIM[1] - HUB[1]
Q = 4096
rows, entries = [], bytearray()
y0 = int(RIM[1] - RY) - 1
y1 = int(RIM[1] + RY) + 1
c = DX * DX / (RX * RX) + DY * DY / (RY * RY) - 1
for y in range(y0, y1 + 1):
    span = []
    for x in range(size):
        vx, vy = x + 0.5 - HUB[0], y + 0.5 - HUB[1]
        a2 = vx * vx / (RX * RX) + vy * vy / (RY * RY)
        if a2 == 0:
            rho = 0.0
        else:
            b2 = vx * DX / (RX * RX) + vy * DY / (RY * RY)
            rho = a2 / (b2 + math.sqrt(b2 * b2 - a2 * c))
        if rho < 1:
            qx, qy = (vx - DX * rho) / RX, (vy - DY * rho) / RY
            span.append((x, round(qx * Q), round(qy * Q), round(rho * Q)))
    if span:
        assert span[-1][0] - span[0][0] + 1 == len(span)
        rows.append((span[0][0], span[-1][0], len(entries) // 6))
        for _, qx, qy, rho in span:
            entries.extend(struct.pack("<hhh", qx, qy, rho))
    else:
        rows.append((1, 0, len(entries) // 6))
(output / "wheel_map.bin").write_bytes(
    b"".join(struct.pack("<hhi", x0, x1, offset) for x0, x1, offset in rows) + bytes(entries))

(output / "hamster_assets.h").write_text(
    "/** Generated by artwork/build_assets.py; do not edit. */\n#pragma once\n\n"
    f"enum {{ PET_W = {PET_W}, PET_H = {PET_H}, PET_FRAMES = 3, BALL = {BALL} }};\n"
    f"enum {{ EYE_X = {(ex0 + ex1) // 2}, EYE_Y = {(ey0 + ey1) // 2} }};\n"
    + "\n".join(header) + "\n"
    f"enum {{ WHEEL_Y0 = {y0}, WHEEL_ROWS = {len(rows)}, WHEEL_MAP_PIXELS = {len(entries) // 6}, WHEEL_Q = {Q} }};\n"
    f"#define WHEEL_HUB_X {HUB[0]}f\n#define WHEEL_HUB_Y {HUB[1]}f\n"
    f"#define WHEEL_DX {DX}f\n#define WHEEL_DY {DY}f\n#define WHEEL_RX {RX}f\n#define WHEEL_RY {RY}f\n")
print("eye", (ex0, ey0, ex1, ey1), "fur", fur, "bubbles", [b.size for b in bubbles])

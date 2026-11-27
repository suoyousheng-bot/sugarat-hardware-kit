"""Convert the three approved preview strips into two 360px images and a phase map per effect."""
import argparse
import math
from pathlib import Path
import struct

from PIL import Image

SIZE = 360
STYLES = (
    ("core", (359, 1809), 359, 332),
    ("ring", (360, 1809), 359, 332),
    ("arcs", (367, 1809), 352, 323),
)


def phase(style, x, y):
    angle = math.atan2(x, -y)
    if style == "core":
        return min(15, round(math.hypot(x, y) * 15 / 155))
    if style == "ring":
        angle_to_origin = abs((angle + math.pi / 2) % math.pi - math.pi / 2)
        return min(15, round(angle_to_origin / (math.pi / 6)) * 5)
    return min(15, round(15 * ((angle + 0.7) % math.pi) / math.pi))


def convert(source, name, centers, center_y, radius, out):
    with Image.open(source) as strip:
        strip = strip.convert("RGB")
        for state, cx in zip(("dark", "bright"), centers):
            image = strip.crop((cx-radius, center_y-radius, cx+radius, center_y+radius))
            image = image.resize((SIZE, SIZE), Image.Resampling.LANCZOS)
            data = bytearray(SIZE * SIZE * 2)
            for i, (r, g, b) in enumerate(image.getdata()):
                struct.pack_into(">H", data, i * 2, ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3))
            (out / f"{name}_{state}.rgb565").write_bytes(data)

    axis = [(i + 0.5) - SIZE / 2 for i in range(SIZE)]
    phases = bytearray()
    for y in axis:
        for x in range(0, SIZE, 2):
            phases.append(phase(name, axis[x], y) | (phase(name, axis[x + 1], y) << 4))
    (out / f"{name}_phase.bin").write_bytes(phases)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name, *_ in STYLES:
        parser.add_argument(name, type=Path)
    args = parser.parse_args()
    output = Path(__file__).resolve().parents[1] / "main" / "assets"
    output.mkdir(exist_ok=True)
    for name, centers, center_y, radius in STYLES:
        convert(getattr(args, name), name, centers, center_y, radius, output)


if __name__ == "__main__":
    main()

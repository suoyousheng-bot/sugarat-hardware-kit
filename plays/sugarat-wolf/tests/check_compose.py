"""Compare host_compose output with a numpy reference of the same blend and overlay arithmetic."""
import io
import re
import struct
import subprocess
import sys
from pathlib import Path

import numpy as np
from PIL import Image

root = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(root / "tools"))
exe, pack, sprites_bin = sys.argv[1], root / "main/wolf_frames.pack", root / "main/wolf_sprites.bin"
pack_bytes = pack.read_bytes()
blob = sprites_bin.read_bytes()
table = [tuple(map(int, m)) for m in re.findall(r"\{ (\d+), (\d+), (\d+) \}", (root / "main/wolf_sprites.h").read_text())]
MASK = 0x07E0F81F


def frame(i):
    """Original frame decoded without fancy upsampling, black outside the pack's crop rectangle."""
    out = subprocess.run(["djpeg", "-nosmooth", "-dct", "int", "-pnm", str(root / f"artwork/frames_x2/f{i:03d}.jpg")],
                         check=True, capture_output=True).stdout
    rgb = np.asarray(Image.open(io.BytesIO(out)).convert("RGB")).astype(np.uint32)
    r, g, b = rgb[..., 0], rgb[..., 1], rgb[..., 2]
    c = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)
    c = ((c & 0xFF) << 8) | (c >> 8)
    x0, y0, w, h = struct.unpack_from("<HHHH", pack_bytes, 32 + 16 * i)
    mask = np.zeros_like(c, dtype=bool)
    mask[y0:y0 + h, x0:x0 + w] = True
    return np.where(mask, c, 0).astype(np.uint32)


def mix(a, b, w):
    a = a.astype(np.uint32); b = b.astype(np.uint32)
    ca = ((a & 0xFF) << 8) | (a >> 8); cb = ((b & 0xFF) << 8) | (b >> 8)
    ca = (ca | ca << 16) & MASK; cb = (cb | cb << 16) & MASK
    c = (((((cb - ca) & 0xFFFFFFFF) * w) & 0xFFFFFFFF) >> 5)
    c = (c + ca) & MASK
    c = (c | c >> 16) & 0xFFFF
    return ((c & 0xFF) << 8) | (c >> 8)


def reference(a, b, w, overlays):
    if w >= 32: a, w = b, 0
    out = frame(a) if w == 0 else mix(frame(a), frame(b), np.uint32(w))
    for s, x, y, alpha in overlays:
        sw, sh, off = table[s]
        px = np.frombuffer(blob, "<u2", sw * sh, off).reshape(sh, sw).astype(np.uint32)
        al = np.frombuffer(blob, np.uint8, sw * sh, off + 2 * sw * sh).reshape(sh, sw).astype(np.uint32)
        y0, y1, x0, x1 = max(y, 0), min(y + sh, 360), max(x, 0), min(x + sw, 360)
        k = (al[y0 - y:y1 - y, x0 - x:x1 - x] * min(alpha, 255) + 255) >> 11
        region = out[y0:y1, x0:x1]
        blended = np.array([[mix(np.uint32(region[r, c]), np.uint32(px[y0 - y + r, x0 - x + c]), np.uint32(k[r, c])) if k[r, c] else region[r, c]
                             for c in range(x1 - x0)] for r in range(y1 - y0)], dtype=np.uint32)
        out[y0:y1, x0:x1] = blended.reshape(region.shape)
    return out.astype(np.uint16)


cases = [(0, 0, 0, []), (529, 0, 0, []), (400, 401, 13, []), (500, 529, 31, []), (12, 560, 32, []), (568, 1, 20, []),
         (86, 87, 16, [(1, 230, 60, 255), (2, 290, 20, 128)]), (120, 121, 5, [(0, 330, 340, 200), (3, -10, -5, 255)])]
for a, b, w, ov in cases:
    args = [exe, str(pack), str(sprites_bin), "/tmp/wolf_compose.raw", str(a), str(b), str(w)]
    for o in ov: args += [str(v) for v in o]
    subprocess.run(args, check=True)
    got = np.fromfile("/tmp/wolf_compose.raw", "<u2").reshape(360, 360)
    want = reference(a, b, w, ov)
    bad = np.count_nonzero(got != want)
    print(f"view a={a} b={b} mix={w} overlays={len(ov)}: {'ok' if bad == 0 else f'{bad} pixels differ'}")
    assert bad == 0
print("compose matches reference")

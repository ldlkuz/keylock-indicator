"""Generate res/app.ico: dark rounded square with three green bars.

Each size is rendered at 4x and downsampled, then packed into a PNG-based ICO
so the small sizes stay crisp instead of being resampled from 256px.
"""
import io
import os
import struct
from PIL import Image, ImageDraw

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "res", "app.ico")
SIZES = [16, 20, 24, 32, 40, 48, 64, 96, 128, 256]

BG = (26, 26, 26, 255)
BAR = (71, 219, 115, 255)
BAR_SHADOW = (32, 150, 75, 255)


def render(size):
    s = size * 4
    img = Image.new("RGBA", (s, s), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)

    pad = int(s * 0.02)
    radius = int(s * 0.22)
    d.rounded_rectangle([pad, pad, s - pad - 1, s - pad - 1], radius=radius, fill=BG)

    bw = int(s * 0.58)
    bh = int(s * 0.105)
    gap = int(s * 0.075)
    cx = s // 2
    total = bh * 3 + gap * 2
    top = (s - total) // 2
    for i in range(3):
        y = top + i * (bh + gap)
        d.rounded_rectangle([cx - bw // 2, y, cx + bw // 2, y + bh],
                            radius=bh // 2, fill=BAR)
        # thin darker line under each bar for a bit of depth
        d.rounded_rectangle([cx - bw // 2, y + bh - max(1, bh // 6), cx + bw // 2, y + bh],
                            radius=bh // 2, fill=BAR_SHADOW)
    return img.resize((size, size), Image.LANCZOS)


images = [render(n) for n in SIZES]

# PNG-compressed ICO (Vista+). Each entry: width, height, colors, reserved,
# planes, bpp, size, offset. Width/height 0 means 256.
entries = []
blobs = []
offset = 6 + 16 * len(images)
for img in images:
    buf = io.BytesIO()
    img.save(buf, format="PNG", optimize=True)
    data = buf.getvalue()
    w = 0 if img.width >= 256 else img.width
    h = 0 if img.height >= 256 else img.height
    entries.append(struct.pack("<BBBBHHII", w, h, 0, 0, 1, 32, len(data), offset))
    blobs.append(data)
    offset += len(data)

with open(OUT, "wb") as f:
    f.write(struct.pack("<HHH", 0, 1, len(images)))
    for e in entries:
        f.write(e)
    for b in blobs:
        f.write(b)

print("wrote", os.path.normpath(OUT), os.path.getsize(OUT), "bytes")

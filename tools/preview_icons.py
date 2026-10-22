import sys, os
from PIL import Image, ImageDraw

here = os.path.dirname(os.path.abspath(__file__))
root = os.path.dirname(here)
src = sys.argv[1] if len(sys.argv) > 1 else os.path.join(root, "build", "selftest")
out = os.path.join(root, "build", "preview")
os.makedirs(out, exist_ok=True)

def load(size, idx):
    p = os.path.join(src, f"icon_s{size}_{idx}.rgba")
    with open(p, "rb") as f:
        data = f.read()
    assert len(data) == size * size * 4, (p, len(data))
    return Image.frombytes("RGBA", (size, size), data, "raw", "BGRA")

def cell(img, size, bg, scale, pad=16):
    side = size * scale + pad * 2
    canvas = Image.new("RGB", (side, side), bg)
    # checker-free flat background; alpha composite
    big = img.resize((size * scale, size * scale), Image.NEAREST)
    canvas.paste(big, (pad, pad), big)
    return canvas

rows = []
for size, scale in ((16, 10), (20, 8), (24, 7), (32, 5)):
    for bg, name in (((243, 243, 243), "light"), ((32, 32, 32), "dark")):
        row = [cell(load(size, i), size, bg, scale) for i in range(8)]
        rows.append((f"{size}px {name}", row))

cw = rows[0][1][0].width
grid = Image.new("RGB", (cw * 8 + 9 * 8, len(rows) * (cw + 24) + 8), (255, 255, 255))
d = ImageDraw.Draw(grid)
for r, (label, row) in enumerate(rows):
    y = 8 + r * (cw + 24)
    d.text((8, y), label, fill=(0, 0, 0))
    for c, im in enumerate(row):
        grid.paste(im, (8 + c * (cw + 8), y + 16))

grid.save(os.path.join(out, "icons-grid.png"))
print("wrote", os.path.join(out, "icons-grid.png"), grid.size)

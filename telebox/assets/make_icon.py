"""TeleBox app icon — dark ground, teal 'live' accent, 2x2 plugin-rack motif.
Matches the app's own palette (INK dark + teal toggle/live green)."""
from PIL import Image, ImageDraw
import os

SS = 4  # supersample for crisp anti-aliasing
S = 1024 * SS

img = Image.new("RGBA", (S, S), (0, 0, 0, 0))
d = ImageDraw.Draw(img)

def rrect(draw, box, r, fill):
    draw.rounded_rectangle(box, radius=r, fill=fill)

# rounded-square background with a subtle vertical gradient (top lighter -> bottom darker)
pad = int(S * 0.02)
corner = int(S * 0.225)
top = (34, 38, 46)     # #22262e
bot = (18, 20, 25)     # #121419
grad = Image.new("RGBA", (S, S), (0, 0, 0, 0))
gd = ImageDraw.Draw(grad)
for y in range(S):
    t = y / S
    c = (int(top[0] + (bot[0] - top[0]) * t),
         int(top[1] + (bot[1] - top[1]) * t),
         int(top[2] + (bot[2] - top[2]) * t), 255)
    gd.line([(0, y), (S, y)], fill=c)
mask = Image.new("L", (S, S), 0)
ImageDraw.Draw(mask).rounded_rectangle([pad, pad, S - pad, S - pad], radius=corner, fill=255)
img.paste(grad, (0, 0), mask)
d = ImageDraw.Draw(img)

# hairline inner border for depth
d.rounded_rectangle([pad, pad, S - pad, S - pad], radius=corner,
                    outline=(255, 255, 255, 22), width=max(2, S // 340))

# 2x2 plugin-rack: four rounded slots, top-left is the 'live' teal one
teal = (45, 212, 160)      # #2dd4a0
slate = (58, 66, 76)       # inactive slot
slate_edge = (86, 96, 108)
gutter = int(S * 0.075)
grid = int(S * 0.52)
gx = (S - grid) // 2
gy = (S - grid) // 2
cell = (grid - gutter) // 2
srad = int(cell * 0.28)
slots = [(0, 0, True), (1, 0, False), (0, 1, False), (1, 1, False)]
for col, row, live in slots:
    x0 = gx + col * (cell + gutter)
    y0 = gy + row * (cell + gutter)
    box = [x0, y0, x0 + cell, y0 + cell]
    if live:
        # soft teal glow (blurred halo behind the live slot)
        from PIL import ImageFilter
        glow = Image.new("RGBA", (S, S), (0, 0, 0, 0))
        gdr = ImageDraw.Draw(glow)
        gpad = int(cell * 0.22)
        gdr.rounded_rectangle([box[0]-gpad, box[1]-gpad, box[2]+gpad, box[3]+gpad],
                              radius=srad + gpad, fill=(45, 212, 160, 90))
        glow = glow.filter(ImageFilter.GaussianBlur(int(cell * 0.10)))
        img.alpha_composite(glow)
        d = ImageDraw.Draw(img)
        rrect(d, box, srad, teal)
        # inner 'live' dot cut in darker
        cxr = int(cell * 0.16)
        cx = (box[0] + box[2]) // 2
        cy = (box[1] + box[3]) // 2
        d.ellipse([cx - cxr, cy - cxr, cx + cxr, cy + cxr], fill=(16, 22, 20, 255))
    else:
        rrect(d, box, srad, slate)
        d.rounded_rectangle(box, radius=srad, outline=slate_edge, width=max(2, S // 380))

# downsample
out = img.resize((1024, 1024), Image.LANCZOS)
iconset = "/private/tmp/claude-501/-Users-pasha-xCode-tlgrm/ae53676f-b50a-419e-be56-ec91bbde118b/scratchpad/TeleBox.iconset"
os.makedirs(iconset, exist_ok=True)
sizes = [16, 32, 64, 128, 256, 512, 1024]
for s in sizes:
    out.resize((s, s), Image.LANCZOS).save(f"{iconset}/icon_{s}x{s}.png")
    if s <= 512:
        out.resize((s * 2, s * 2), Image.LANCZOS).save(f"{iconset}/icon_{s}x{s}@2x.png")
# preview
out.save("/private/tmp/claude-501/-Users-pasha-xCode-tlgrm/ae53676f-b50a-419e-be56-ec91bbde118b/scratchpad/telebox_icon_preview.png")
print("iconset written:", iconset)

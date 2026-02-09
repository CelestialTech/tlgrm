#!/usr/bin/env python3
from PIL import Image, ImageDraw

# Create 1024x680 image with beige color (#F5F5DC - classic beige)
width, height = 1024, 680
bg_color = (245, 245, 220)  # Beige

img = Image.new('RGB', (width, height), bg_color)
draw = ImageDraw.Draw(img)

# Add subtle gradient for depth
for y in range(height):
    shade = int(245 - (y / height) * 10)  # Subtle darkening
    draw.line([(0, y), (width, y)], fill=(shade, shade, 220))

img.save('/Users/pasha/xCode/tlgrm/tdesktop/dmg_background.png', 'PNG')
print("✓ Created beige background: dmg_background.png")

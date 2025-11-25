#!/bin/bash
# Create a beautiful DMG installer for Tlgrm

set -e

echo "Creating enhanced Tlgrm DMG installer..."

# Configuration
DMG_NAME="Tlgrm-Installer"
VOLUME_NAME="Tlgrm Installer"
SOURCE_DIR="dmg_build"
FINAL_DMG="${DMG_NAME}.dmg"
TEMP_DMG="${DMG_NAME}-temp.dmg"

# Clean up any existing build
rm -rf "${SOURCE_DIR}"
mkdir -p "${SOURCE_DIR}/.background"

echo "=== Copying files to staging directory ==="

# Copy app
echo "Copying Tlgrm.app..."
cp -R tdesktop/out/Release/Tlgrm.app "${SOURCE_DIR}/"

# Copy session installer
echo "Copying initiate.pkg..."
cp initiate.pkg "${SOURCE_DIR}/"

# Copy README files
echo "Copying README files..."
cp DMG_README.md "${SOURCE_DIR}/README.md"
cp ПРОЧТИ.md "${SOURCE_DIR}/ПРОЧТИ.md"

# Create Applications symlink
echo "Creating Applications symlink..."
ln -s /Applications "${SOURCE_DIR}/Applications"

# Create background with arrow and instructions
echo "Creating background with installation arrow..."

python3 << 'PYTHON_EOF'
from PIL import Image, ImageDraw, ImageFont

# Create beige background
width, height = 880, 540
bg_color = (245, 245, 220)  # Beige
img = Image.new('RGB', (width, height), bg_color)
draw = ImageDraw.Draw(img)

# Draw arrow from app position (160, 200) to Applications (480, 200)
arrow_color = (100, 100, 100)  # Gray
arrow_y = 180
arrow_start_x = 220
arrow_end_x = 420

# Draw arrow line
draw.line([(arrow_start_x, arrow_y), (arrow_end_x, arrow_y)], fill=arrow_color, width=3)

# Draw arrowhead
arrow_head = [
    (arrow_end_x, arrow_y),
    (arrow_end_x - 15, arrow_y - 10),
    (arrow_end_x - 15, arrow_y + 10)
]
draw.polygon(arrow_head, fill=arrow_color)

# Add text "Drag to install"
try:
    font = ImageFont.truetype("/System/Library/Fonts/Helvetica.ttc", 16)
except:
    font = ImageFont.load_default()

text = "Drag to install"
# Get text bbox for centering
bbox = draw.textbbox((0, 0), text, font=font)
text_width = bbox[2] - bbox[0]
text_x = (arrow_start_x + arrow_end_x - text_width) // 2
text_y = arrow_y - 30

draw.text((text_x, text_y), text, fill=arrow_color, font=font)

# Save
img.save('/tmp/beige_bg.png')
print("Created background with arrow")
PYTHON_EOF

# Move the background to the DMG staging area
if [ -f /tmp/beige_bg.png ]; then
    mv /tmp/beige_bg.png "${SOURCE_DIR}/.background/background.png"
    echo "Created beige background with arrow (880x540)"
else
    echo "Warning: Python failed, using simple beige background"
    # Fallback: simple beige
    printf '\x89\x50\x4e\x47\x0d\x0a\x1a\x0a\x00\x00\x00\x0d\x49\x48\x44\x52\x00\x00\x00\x01\x00\x00\x00\x01\x08\x02\x00\x00\x00\x90\x77\x53\xde\x00\x00\x00\x0c\x49\x44\x41\x54\x08\xd7\x63\xf8\xcf\xc0\xf0\x9f\x01\x00\x04\x84\x01\xfe\x0e\xb4\x96\xef\x00\x00\x00\x00\x49\x45\x4e\x44\xae\x42\x60\x82' > /tmp/beige_1x1.png
    sips -z 540 880 /tmp/beige_1x1.png --out "${SOURCE_DIR}/.background/background.png" >/dev/null 2>&1
fi

rm -f /tmp/beige_1x1.png

echo "=== Creating temporary DMG ==="
# Create temporary DMG
hdiutil create -srcfolder "${SOURCE_DIR}" -volname "${VOLUME_NAME}" -fs HFS+ \
    -fsargs "-c c=64,a=16,e=16" -format UDRW -size 800m "${TEMP_DMG}"

echo "=== Mounting temporary DMG ==="
# Mount it
DEVICE=$(hdiutil attach -readwrite -noverify -noautoopen "${TEMP_DMG}" | \
    egrep '^/dev/' | sed 1q | awk '{print $1}')

echo "Device: ${DEVICE}"
sleep 2

echo "=== Customizing DMG appearance with AppleScript ==="
# Customize appearance with AppleScript
cat > /tmp/dmg_style.applescript << 'APPLESCRIPT_EOF'
tell application "Finder"
    tell disk "Tlgrm Installer"
        open
        set current view of container window to icon view
        set toolbar visible of container window to false
        set statusbar visible of container window to false
        set the bounds of container window to {100, 100, 980, 640}
        set viewOptions to the icon view options of container window
        set arrangement of viewOptions to not arranged
        set icon size of viewOptions to 128
        set background picture of viewOptions to file ".background:background.png"

        -- Position icons
        set position of item "Tlgrm.app" of container window to {160, 200}
        set position of item "Applications" of container window to {480, 200}
        set position of item "initiate.pkg" of container window to {320, 350}
        set position of item "README.md" of container window to {580, 420}
        set position of item "ПРОЧТИ.md" of container window to {740, 420}

        close
        open
        update without registering applications
        delay 2
    end tell
end tell
APPLESCRIPT_EOF

# Run AppleScript
osascript /tmp/dmg_style.applescript || {
    echo "Warning: AppleScript customization failed, DMG will have default appearance"
}

sleep 2

# Make sure everything is synced
sync

echo "=== Unmounting DMG ==="
hdiutil detach "${DEVICE}"
sleep 1

echo "=== Converting to compressed DMG ==="
# Convert to compressed, read-only image
rm -f "${FINAL_DMG}"
hdiutil convert "${TEMP_DMG}" -format UDZO -imagekey zlib-level=9 -o "${FINAL_DMG}"

# Clean up
rm -f "${TEMP_DMG}"
rm -f /tmp/dmg_style.applescript
rm -f /tmp/beige_bg.png

# Get final size
DMG_SIZE=$(du -h "${FINAL_DMG}" | awk '{print $1}')

echo ""
echo "=== SUCCESS ==="
echo "Created: ${FINAL_DMG} (${DMG_SIZE})"
echo ""
echo "DMG includes:"
echo "  ✓ Tlgrm.app with custom icon"
echo "  ✓ TlgrmSessionData.pkg for session data"
echo "  ✓ README.md (English instructions)"
echo "  ✓ ПРОЧТИ.md (Russian instructions)"
echo "  ✓ Applications folder shortcut"
echo "  ✓ Beige background and layout"
echo ""
echo "Test it with: open ${FINAL_DMG}"

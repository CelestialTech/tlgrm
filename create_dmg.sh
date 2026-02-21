#!/bin/bash
# Minimal DMG creation script for Tlgrm
# Just the app + background + Applications link. No README, no initiate.pkg.
#
# Prerequisites:
#   brew install create-dmg
#
# Usage:
#   ./create_dmg.sh
#
# Output:
#   dmg_build/Tlgrm_<version>.dmg

set -e

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
BUILD_DIR="$SCRIPT_DIR/tdesktop/out/Release"
APP_NAME="Tlgrm"
APP_BUNDLE="$BUILD_DIR/$APP_NAME.app"
DMG_OUTPUT_DIR="$SCRIPT_DIR/dmg_build"
DMG_STAGING="$DMG_OUTPUT_DIR/staging_minimal"
BACKGROUND="$DMG_OUTPUT_DIR/dmg_background.png"
VERSION=$(date +"%Y.%m.%d")

echo "========================================="
echo "Tlgrm Minimal DMG Creation"
echo "========================================="
echo ""

# Check app bundle
if [ ! -d "$APP_BUNDLE" ]; then
    echo "Error: App bundle not found at $APP_BUNDLE"
    exit 1
fi

# Check background
if [ ! -f "$BACKGROUND" ]; then
    echo "Error: Background image not found at $BACKGROUND"
    exit 1
fi

# Get version from Info.plist
if [ -f "$APP_BUNDLE/Contents/Info.plist" ]; then
    PLIST_VERSION=$(/usr/libexec/PlistBuddy -c "Print CFBundleShortVersionString" "$APP_BUNDLE/Contents/Info.plist" 2>/dev/null || echo "")
    if [ -n "$PLIST_VERSION" ]; then
        VERSION="$PLIST_VERSION"
    fi
fi

DMG_NAME="${APP_NAME}_${VERSION}.dmg"
DMG_PATH="$DMG_OUTPUT_DIR/$DMG_NAME"

# Prepare staging
mkdir -p "$DMG_OUTPUT_DIR"
rm -rf "$DMG_STAGING"
mkdir -p "$DMG_STAGING"

echo "Copying app bundle..."
cp -R "$APP_BUNDLE" "$DMG_STAGING/"

# Remove old DMG
[ -f "$DMG_PATH" ] && rm -f "$DMG_PATH"

echo "Creating DMG for $APP_NAME v$VERSION..."
echo "Output: $DMG_PATH"
echo ""

# Create DMG: app on the left, Applications on the right
create-dmg \
    --volname "$APP_NAME" \
    --background "$BACKGROUND" \
    --window-pos 200 120 \
    --window-size 660 400 \
    --icon-size 128 \
    --icon "$APP_NAME.app" 180 180 \
    --hide-extension "$APP_NAME.app" \
    --app-drop-link 480 180 \
    --no-internet-enable \
    "$DMG_PATH" \
    "$DMG_STAGING" \
    2>&1

if [ $? -eq 0 ]; then
    DMG_SIZE=$(du -h "$DMG_PATH" | cut -f1)
    echo ""
    echo "========================================="
    echo "DMG created: $DMG_NAME ($DMG_SIZE)"
    echo "Location: $DMG_OUTPUT_DIR"
    echo "========================================="
    rm -rf "$DMG_STAGING"
else
    echo "Error: DMG creation failed"
    rm -rf "$DMG_STAGING"
    exit 1
fi

#!/bin/bash
# Post-build DMG creation script for Tlgrm (custom Telegram Desktop build)
# This script is called automatically after successful Xcode builds

set -e

# Configuration
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
PROJECT_ROOT="$SCRIPT_DIR/../.."
BUILD_DIR="$PROJECT_ROOT/out/Release"
APP_NAME="Tlgrm"
APP_BUNDLE="$BUILD_DIR/$APP_NAME.app"
DMG_OUTPUT_DIR="$PROJECT_ROOT/out/DMG"
VERSION=$(date +"%Y.%m.%d")
DMG_NAME="${APP_NAME}_${VERSION}.dmg"
DMG_PATH="$DMG_OUTPUT_DIR/$DMG_NAME"

echo "========================================="
echo "Tlgrm DMG Creation Script"
echo "========================================="
echo ""

# Check if app bundle exists
if [ ! -d "$APP_BUNDLE" ]; then
    echo "Error: App bundle not found at $APP_BUNDLE"
    echo "Skipping DMG creation."
    exit 0  # Exit gracefully (don't fail the build)
fi

# Get app version from Info.plist if available
if [ -f "$APP_BUNDLE/Contents/Info.plist" ]; then
    PLIST_VERSION=$(/usr/libexec/PlistBuddy -c "Print CFBundleShortVersionString" "$APP_BUNDLE/Contents/Info.plist" 2>/dev/null || echo "")
    if [ -n "$PLIST_VERSION" ]; then
        VERSION="$PLIST_VERSION"
        DMG_NAME="${APP_NAME}_${VERSION}.dmg"
        DMG_PATH="$DMG_OUTPUT_DIR/$DMG_NAME"
    fi
fi

# Create output directory
mkdir -p "$DMG_OUTPUT_DIR"

# Remove old DMG if exists
if [ -f "$DMG_PATH" ]; then
    echo "Removing existing DMG: $DMG_NAME"
    rm -f "$DMG_PATH"
fi

echo "Creating DMG for $APP_NAME v$VERSION..."
echo "App bundle: $APP_BUNDLE"
echo "Output: $DMG_PATH"
echo ""

# Create DMG using create-dmg tool
# Options:
#   --volname: Volume name shown when DMG is mounted
#   --window-pos: Initial window position
#   --window-size: Window size (width height)
#   --icon-size: Size of app icon in DMG
#   --icon: Position of app icon (x y from top-left)
#   --app-drop-link: Position of Applications symlink (x y)
create-dmg \
    --volname "$APP_NAME" \
    --window-pos 200 120 \
    --window-size 600 400 \
    --icon-size 100 \
    --icon "$APP_NAME.app" 175 150 \
    --hide-extension "$APP_NAME.app" \
    --app-drop-link 425 150 \
    "$DMG_PATH" \
    "$APP_BUNDLE" \
    2>&1

if [ $? -eq 0 ]; then
    DMG_SIZE=$(du -h "$DMG_PATH" | cut -f1)
    echo ""
    echo "========================================="
    echo "✓ DMG created successfully!"
    echo "========================================="
    echo "File: $DMG_NAME"
    echo "Size: $DMG_SIZE"
    echo "Location: $DMG_OUTPUT_DIR"
    echo ""
    echo "You can distribute this DMG file."
    echo "========================================="
else
    echo ""
    echo "Warning: DMG creation failed (non-fatal)"
    echo "Build completed successfully, but DMG could not be created."
    exit 0  # Don't fail the build
fi

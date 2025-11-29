#!/bin/bash
# Beautiful DMG creation script for Tlgrm with README files and beige background

set -e

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
BUILD_DIR="$SCRIPT_DIR/tdesktop/out/Release"
APP_NAME="Tlgrm"
APP_BUNDLE="$BUILD_DIR/$APP_NAME.app"
DMG_OUTPUT_DIR="$SCRIPT_DIR/dmg_build"
DMG_STAGING="$SCRIPT_DIR/dmg_build/staging"
VERSION=$(date +"%Y.%m.%d")
DMG_NAME="${APP_NAME}_${VERSION}.dmg"
DMG_PATH="$DMG_OUTPUT_DIR/$DMG_NAME"
BACKGROUND="$DMG_OUTPUT_DIR/dmg_background.png"
README_EN="$DMG_OUTPUT_DIR/README_EN.txt"
README_RU="$DMG_OUTPUT_DIR/README_RU.txt"
PKG_FILE="$DMG_OUTPUT_DIR/initiate.pkg"

echo "========================================="
echo "Tlgrm Beautiful DMG Creation"
echo "========================================="
echo ""

# Check if app bundle exists
if [ ! -d "$APP_BUNDLE" ]; then
    echo "Error: App bundle not found at $APP_BUNDLE"
    exit 1
fi

# Check if background exists
if [ ! -f "$BACKGROUND" ]; then
    echo "Error: Background image not found at $BACKGROUND"
    exit 1
fi

# Get app version from Info.plist
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

# ========================================
# Create initiate.pkg from ~/tdata.zip
# ========================================
TDATA_ZIP="$HOME/tdata.zip"
TDATA_TEMP="$DMG_OUTPUT_DIR/tdata_temp"

if [ -f "$TDATA_ZIP" ]; then
    echo "========================================="
    echo "Creating initiate.pkg from tdata.zip"
    echo "========================================="

    # Clean up any existing temp directory
    rm -rf "$TDATA_TEMP"
    mkdir -p "$TDATA_TEMP"

    # Extract tdata.zip to temp directory
    echo "Extracting $TDATA_ZIP..."
    unzip -q "$TDATA_ZIP" -d "$TDATA_TEMP"

    # Verify tdata directory exists in extracted files
    if [ ! -d "$TDATA_TEMP/tdata" ]; then
        echo "Error: tdata directory not found in $TDATA_ZIP"
        echo "Expected structure: tdata.zip containing tdata/ folder"
        rm -rf "$TDATA_TEMP"
        exit 1
    fi

    # Remove old pkg if exists
    [ -f "$PKG_FILE" ] && rm -f "$PKG_FILE"

    # Create package using pkgbuild with scripts
    # IMPORTANT: Use a fixed staging location, NOT $HOME (which is evaluated at build time)
    # The postinstall script will move files from staging to the actual user's home directory
    STAGING_LOCATION="/private/tmp/tlgrm_tdata_staging"
    echo "Building initiate.pkg with pre/postinstall scripts..."
    echo "  (Using staging location: $STAGING_LOCATION)"
    pkgbuild \
        --root "$TDATA_TEMP" \
        --identifier "com.telegram.tlgrm.session" \
        --version "1.0" \
        --install-location "$STAGING_LOCATION" \
        --scripts "$DMG_OUTPUT_DIR/scripts" \
        "$PKG_FILE"

    if [ $? -eq 0 ]; then
        PKG_SIZE=$(du -h "$PKG_FILE" | cut -f1)
        echo "✓ Package created: initiate.pkg ($PKG_SIZE)"
    else
        echo "Error: Package creation failed"
        rm -rf "$TDATA_TEMP"
        exit 1
    fi

    # Clean up temp directory
    rm -rf "$TDATA_TEMP"
    echo ""
else
    echo "Warning: ~/tdata.zip not found, skipping package creation"
    echo "  (Using existing initiate.pkg if available)"
    echo ""
fi

# Create staging directory
echo "Creating staging directory..."
rm -rf "$DMG_STAGING"
mkdir -p "$DMG_STAGING"

# Copy app to staging
echo "Copying app bundle..."
cp -R "$APP_BUNDLE" "$DMG_STAGING/"

# Copy README files to staging
echo "Adding README files..."
if [ -f "$README_EN" ]; then
    cp "$README_EN" "$DMG_STAGING/README.txt"
fi
if [ -f "$README_RU" ]; then
    cp "$README_RU" "$DMG_STAGING/ПРОЧТИ.txt"
fi

# Copy PKG file to staging
echo "Adding initiate.pkg installer..."
if [ -f "$PKG_FILE" ]; then
    cp "$PKG_FILE" "$DMG_STAGING/initiate.pkg"
fi

# Remove old DMG if exists
if [ -f "$DMG_PATH" ]; then
    echo "Removing existing DMG: $DMG_NAME"
    rm -f "$DMG_PATH"
fi

echo "Creating DMG for $APP_NAME v$VERSION..."
echo "Output: $DMG_PATH"
echo ""

# Create DMG with custom background
# Window size: 1024x680 to match background
# Positions:
#   - App icon: x=256 y=200 (left side, top)
#   - Applications link: x=768 y=200 (right side, top)
#   - README EN: x=180 y=450 (bottom left)
#   - README RU: x=400 y=450 (bottom center-left)
#   - initiate.pkg: x=640 y=450 (bottom center-right)
create-dmg \
    --volname "$APP_NAME" \
    --background "$BACKGROUND" \
    --window-pos 200 120 \
    --window-size 1024 680 \
    --icon-size 128 \
    --icon "$APP_NAME.app" 256 200 \
    --icon "README.txt" 180 450 \
    --icon "ПРОЧТИ.txt" 400 450 \
    --icon "initiate.pkg" 640 450 \
    --hide-extension "$APP_NAME.app" \
    --app-drop-link 768 200 \
    --no-internet-enable \
    "$DMG_PATH" \
    "$DMG_STAGING" \
    2>&1

if [ $? -eq 0 ]; then
    DMG_SIZE=$(du -h "$DMG_PATH" | cut -f1)
    echo ""
    echo "========================================="
    echo "✓ Beautiful DMG created successfully!"
    echo "========================================="
    echo "File: $DMG_NAME"
    echo "Size: $DMG_SIZE"
    echo "Location: $DMG_OUTPUT_DIR"
    echo ""
    echo "Features:"
    echo "  - Beige background with gradient"
    echo "  - README in English and Russian"
    echo "  - GPL v3 LICENSE included"
    echo "  - Professional layout"
    echo ""
    echo "You can distribute this DMG file."
    echo "========================================="

    # Clean up staging
    rm -rf "$DMG_STAGING"
else
    echo ""
    echo "Error: DMG creation failed"
    rm -rf "$DMG_STAGING"
    exit 1
fi

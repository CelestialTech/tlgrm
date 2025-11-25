#!/bin/bash
# Diagnostic script for tdata loading issues

echo "========================================="
echo "Tlgrm tdata Diagnostic Tool"
echo "========================================="
echo ""

# Check app bundle info
echo "[1] App Bundle Information:"
if [ -f "out/Release/Tlgrm.app/Contents/Info.plist" ]; then
    BUNDLE_ID=$(/usr/libexec/PlistBuddy -c "Print CFBundleIdentifier" "out/Release/Tlgrm.app/Contents/Info.plist" 2>/dev/null)
    VERSION=$(/usr/libexec/PlistBuddy -c "Print CFBundleShortVersionString" "out/Release/Tlgrm.app/Contents/Info.plist" 2>/dev/null)
    echo "  Bundle ID: $BUNDLE_ID"
    echo "  Version: $VERSION"
else
    echo "  ✗ Info.plist not found"
fi
echo ""

# Check tdata locations
echo "[2] tdata Directories Found:"
echo "  Standard location:"
STANDARD_TDATA="/Users/pasha/Library/Application Support/Telegram Desktop/tdata"
if [ -d "$STANDARD_TDATA" ]; then
    SIZE=$(du -sh "$STANDARD_TDATA" 2>/dev/null | cut -f1)
    FILES=$(find "$STANDARD_TDATA" -type f 2>/dev/null | wc -l | tr -d ' ')
    echo "    ✓ $STANDARD_TDATA"
    echo "      Size: $SIZE, Files: $FILES"

    # Check for key files
    if [ -f "$STANDARD_TDATA/key_datas" ]; then
        echo "      ✓ key_datas exists (session data)"
    else
        echo "      ✗ key_datas missing (NO SESSION DATA!)"
    fi

    if [ -f "$STANDARD_TDATA/settings0" ]; then
        echo "      ✓ settings0 exists"
    fi
else
    echo "    ✗ Not found at standard location"
fi
echo ""

# Check custom bundle ID location
if [ -n "$BUNDLE_ID" ] && [ "$BUNDLE_ID" != "com.tdesktop.Telegram" ]; then
    CUSTOM_TDATA="/Users/pasha/Library/Application Support/$BUNDLE_ID/tdata"
    echo "  Custom bundle ID location:"
    if [ -d "$CUSTOM_TDATA" ]; then
        SIZE=$(du -sh "$CUSTOM_TDATA" 2>/dev/null | cut -f1)
        echo "    ✓ $CUSTOM_TDATA"
        echo "      Size: $SIZE"
    else
        echo "    ✗ $CUSTOM_TDATA (not found)"
    fi
    echo ""
fi

# Check home directory tdata
echo "  Other tdata directories:"
find /Users/pasha -maxdepth 2 -type d -name "tdata" 2>/dev/null | while read dir; do
    SIZE=$(du -sh "$dir" 2>/dev/null | cut -f1)
    echo "    • $dir ($SIZE)"
done
echo ""

# Test app startup (without GUI)
echo "[3] Testing App Startup (headless):"
echo "  Starting Tlgrm with diagnostic flags..."
timeout 5 ./out/Release/Tlgrm.app/Contents/MacOS/Tlgrm --quit 2>&1 | grep -E "(storage|tdata|session|auth)" | head -10
echo ""

# Check for bundle ID conflicts
echo "[4] Checking for Bundle ID Conflicts:"
RUNNING=$(ps aux | grep Tlgrm.app | grep -v grep | wc -l | tr -d ' ')
echo "  Running Tlgrm instances: $RUNNING"
if [ "$RUNNING" -gt 0 ]; then
    echo "  ⚠ WARNING: Tlgrm is currently running"
    echo "    Kill all instances and try again"
fi
echo ""

# Check App Store version
if [ -d "/Applications/Telegram.app" ]; then
    echo "  ✓ Official Telegram found in /Applications"
    OFFICIAL_ID=$(/usr/libexec/PlistBuddy -c "Print CFBundleIdentifier" "/Applications/Telegram.app/Contents/Info.plist" 2>/dev/null)
    echo "    Bundle ID: $OFFICIAL_ID"
fi
echo ""

echo "========================================="
echo "Diagnostic Complete"
echo "========================================="
echo ""
echo "Next Steps:"
echo "1. If 'key_datas' is missing, you need to log in again"
echo "2. If Bundle IDs conflict, rename your custom app"
echo "3. If tdata exists but isn't loading, check permissions"
echo ""

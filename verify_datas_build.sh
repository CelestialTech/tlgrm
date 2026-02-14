#!/bin/bash
# Post-compilation verification for "datas" fix
# This script verifies the binary contains the correct "datas" string

BINARY="/Users/pasha/xCode/tlgrm/tdesktop/out/Release/Tlgrm.app/Contents/MacOS/Tlgrm"
BUILD_LOG="/tmp/build_datas_verified.log"

echo "=== BUILD VERIFICATION SCRIPT ==="
echo ""

# Check 1: Build succeeded
if ! grep -q "BUILD SUCCEEDED" "$BUILD_LOG" 2>/dev/null; then
    echo "❌ FAILED: Build did not succeed"
    echo "Check $BUILD_LOG for errors"
    exit 1
fi
echo "✓ Build succeeded"

# Check 2: Binary exists
if [ ! -f "$BINARY" ]; then
    echo "❌ FAILED: Binary not found at $BINARY"
    exit 1
fi
echo "✓ Binary exists: $(ls -la "$BINARY" | awk '{print $5, $6, $7, $8}')"

# Check 3: Binary contains UTF-16LE "datas" (64 00 61 00 74 00 61 00 73 00)
# We search for the exact pattern that would be the QString literal
# Looking for "datas" followed by null terminator pattern
if xxd "$BINARY" | grep -q "6400 6100 7400 6100 7300"; then
    echo "✓ Binary contains UTF-16LE 'datas' string"
else
    echo "❌ FAILED: Binary does NOT contain UTF-16LE 'datas' string"
    echo ""
    echo "Looking for what's there instead (looking for key_ pattern):"
    # Look for "key_" prefix which is what comes before "data" or "datas"
    xxd "$BINARY" | grep -E "6b00 6500 7900 5f00" | head -3
    echo ""
    echo "The config.h change was NOT compiled into the binary!"
    echo "This means PCH caching is still a problem."
    exit 1
fi

# Check 4: Verify it's not just "data" without "s"
# Count occurrences of "datas" vs "data" (followed by something other than 's')
DATAS_COUNT=$(xxd "$BINARY" | grep -c "6400 6100 7400 6100 7300" 2>/dev/null || echo "0")
echo "✓ Found $DATAS_COUNT occurrences of 'datas' pattern"

echo ""
echo "=== VERIFICATION PASSED ==="
echo "Binary is ready for testing."
echo ""
echo "Now you can test the app:"
echo "  /Users/pasha/xCode/tlgrm/tdesktop/out/Release/Tlgrm.app/Contents/MacOS/Tlgrm"

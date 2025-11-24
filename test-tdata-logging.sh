#!/bin/bash
# Test script for TData logging functionality

TELEGRAM_BIN="./out/Release/Telegram.app/Contents/MacOS/Telegram"
TEST_DIR="$HOME/tdata"

echo "================================================"
echo "Telegram TData Logging Test Script"
echo "================================================"
echo ""

# Check if binary exists
if [ ! -f "$TELEGRAM_BIN" ]; then
    echo "ERROR: Telegram binary not found at $TELEGRAM_BIN"
    echo "Please build the project first:"
    echo "  cd out && xcodebuild -project Telegram.xcodeproj -scheme Telegram -configuration Release build"
    exit 1
fi

echo "✓ Telegram binary found"
echo ""

# Test 1: Default location logging
echo "Test 1: Checking default location logging..."
echo "-------------------------------------------"
timeout 3 "$TELEGRAM_BIN" 2>&1 | grep "TData:" | head -5
echo ""

# Test 2: Custom location logging
echo "Test 2: Checking custom location logging..."
echo "-------------------------------------------"
echo "Creating test directory: $TEST_DIR"
mkdir -p "$TEST_DIR"

echo "Running Telegram with -workdir $TEST_DIR"
timeout 3 "$TELEGRAM_BIN" -workdir "$TEST_DIR" 2>&1 | grep "TData:" | head -10
echo ""

# Test 3: Verify directory was created
echo "Test 3: Verifying directory structure..."
echo "-------------------------------------------"
if [ -d "$TEST_DIR/tdata" ]; then
    echo "✓ TData directory created: $TEST_DIR/tdata"
    ls -la "$TEST_DIR/tdata" 2>/dev/null || echo "  (empty)"
else
    echo "✗ TData directory NOT created"
fi
echo ""

# Note: Keeping test directory for future use
echo "Note: Test directory preserved at: $TEST_DIR"
echo ""

echo "================================================"
echo "Test Complete!"
echo "================================================"
echo ""
echo "To view real-time logs while running Telegram:"
echo "  log stream --predicate 'process == \"Telegram\"' --level debug | grep TData"
echo ""
echo "To run with custom tdata location:"
echo "  $TELEGRAM_BIN -workdir /path/to/custom/location"
echo ""

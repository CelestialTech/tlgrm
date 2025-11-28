#!/bin/bash
#
# MCP Test Runner
# Runs the MCP test suite against the Telegram Desktop app
#
# Usage:
#   ./run_mcp_tests.sh              # Run all tests
#   ./run_mcp_tests.sh --quick      # Run quick tests only
#   ./run_mcp_tests.sh --verbose    # Run with verbose output
#

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
APP_PATH="$SCRIPT_DIR/out/Release/Tlgrm.app/Contents/MacOS/Tlgrm"
IPC_SOCKET="/tmp/tdesktop_mcp.sock"
TESTS_DIR="$SCRIPT_DIR/tests"

# Colors
GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

# Parse arguments
QUICK_MODE=false
VERBOSE=""
EXTRA_ARGS=""

while [[ $# -gt 0 ]]; do
    case $1 in
        --quick)
            QUICK_MODE=true
            EXTRA_ARGS="$EXTRA_ARGS -m 'not slow'"
            shift
            ;;
        --verbose|-v)
            VERBOSE="-v"
            shift
            ;;
        *)
            EXTRA_ARGS="$EXTRA_ARGS $1"
            shift
            ;;
    esac
done

echo -e "${BLUE}============================================${NC}"
echo -e "${BLUE}       MCP Test Suite Runner${NC}"
echo -e "${BLUE}============================================${NC}"

# Check if app exists
if [ ! -f "$APP_PATH" ]; then
    echo -e "${RED}Error: Telegram app not found at $APP_PATH${NC}"
    echo "Please build the app first with:"
    echo "  cd out && xcodebuild -project Telegram.xcodeproj -scheme Telegram -configuration Release build"
    exit 1
fi

# Check if pytest is available
if ! command -v pytest &> /dev/null; then
    echo -e "${YELLOW}pytest not found, installing...${NC}"
    pip3 install pytest
fi

# Function to check if Telegram is already running with MCP
check_telegram_running() {
    if [ -S "$IPC_SOCKET" ]; then
        # Try to ping
        response=$(echo '{"jsonrpc":"2.0","id":1,"method":"ping","params":{}}' | nc -U "$IPC_SOCKET" 2>/dev/null || true)
        if echo "$response" | grep -q "pong"; then
            return 0
        fi
    fi
    return 1
}

# Function to start Telegram
start_telegram() {
    echo -e "${YELLOW}Starting Telegram with --mcp flag...${NC}"

    # Kill any existing instances
    pkill -f "Tlgrm" 2>/dev/null || true
    sleep 1

    # Start new instance
    "$APP_PATH" --mcp &
    TELEGRAM_PID=$!

    # Wait for IPC socket
    echo -n "Waiting for MCP server"
    for i in {1..30}; do
        if check_telegram_running; then
            echo -e " ${GREEN}Ready!${NC}"
            return 0
        fi
        echo -n "."
        sleep 1
    done

    echo -e " ${RED}Failed!${NC}"
    return 1
}

# Function to cleanup
cleanup() {
    if [ -n "$STARTED_TELEGRAM" ] && [ "$STARTED_TELEGRAM" = "true" ]; then
        echo -e "\n${YELLOW}Stopping Telegram...${NC}"
        pkill -f "Tlgrm" 2>/dev/null || true
    fi
}

trap cleanup EXIT

# Check if Telegram is already running
STARTED_TELEGRAM=false
if check_telegram_running; then
    echo -e "${GREEN}Telegram already running with MCP${NC}"
else
    if start_telegram; then
        STARTED_TELEGRAM=true
    else
        echo -e "${RED}Failed to start Telegram. Please start it manually with:${NC}"
        echo "  $APP_PATH --mcp &"
        exit 1
    fi
fi

# Run tests
echo -e "\n${BLUE}Running tests...${NC}"
echo "============================================"

cd "$TESTS_DIR"

if [ "$QUICK_MODE" = true ]; then
    echo -e "${YELLOW}Running quick tests only${NC}"
fi

# Run pytest
if pytest $VERBOSE $EXTRA_ARGS; then
    echo -e "\n${GREEN}============================================${NC}"
    echo -e "${GREEN}       All tests passed!${NC}"
    echo -e "${GREEN}============================================${NC}"
    exit 0
else
    echo -e "\n${RED}============================================${NC}"
    echo -e "${RED}       Some tests failed${NC}"
    echo -e "${RED}============================================${NC}"
    exit 1
fi

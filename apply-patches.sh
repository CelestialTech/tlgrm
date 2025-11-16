#!/bin/bash
#
# Apply MCP Integration Patches to Telegram Desktop
#
# This script applies all necessary patches to add MCP server functionality
# to a fresh checkout of Telegram Desktop.
#
# Usage:
#   cd /path/to/tdesktop
#   ./apply-patches.sh
#

set -e  # Exit on error

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Script directory (should be parent of tdesktop)
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PATCHES_DIR="${SCRIPT_DIR}/tdesktop/patches"
TDESKTOP_DIR="${SCRIPT_DIR}/tdesktop"

echo -e "${BLUE}╔════════════════════════════════════════════════════════╗${NC}"
echo -e "${BLUE}║  Telegram Desktop MCP Integration Patch Installer     ║${NC}"
echo -e "${BLUE}╚════════════════════════════════════════════════════════╝${NC}"
echo ""

# Verify we're in the right directory
if [ ! -d "$TDESKTOP_DIR" ]; then
    echo -e "${RED}Error: tdesktop directory not found at $TDESKTOP_DIR${NC}"
    echo "Please run this script from the parent directory of tdesktop/"
    exit 1
fi

if [ ! -d "$PATCHES_DIR" ]; then
    echo -e "${RED}Error: patches directory not found at $PATCHES_DIR${NC}"
    exit 1
fi

cd "$TDESKTOP_DIR"

# Check if we're in a git repository
if ! git rev-parse --git-dir > /dev/null 2>&1; then
    echo -e "${RED}Error: Not a git repository${NC}"
    exit 1
fi

# Show current git status
echo -e "${BLUE}Current git status:${NC}"
git log --oneline -1
echo ""

# Check for uncommitted changes
if ! git diff-index --quiet HEAD --; then
    echo -e "${YELLOW}Warning: You have uncommitted changes.${NC}"
    echo "It's recommended to commit or stash them before applying patches."
    read -p "Continue anyway? (y/n) " -n 1 -r
    echo
    if [[ ! $REPLY =~ ^[Yy]$ ]]; then
        exit 1
    fi
fi

# Step 1: Check if patches can be applied
echo -e "${BLUE}[1/4] Checking if patches can be applied...${NC}"

PATCH_OK=true

if ! git apply --check "$PATCHES_DIR/0001-add-mcp-cmake-configuration.patch" 2>/dev/null; then
    echo -e "${YELLOW}  ⚠ CMake configuration patch may have conflicts${NC}"
    PATCH_OK=false
fi

if ! git apply --check "$PATCHES_DIR/0002-add-mcp-application-integration.patch" 2>/dev/null; then
    echo -e "${YELLOW}  ⚠ Application integration patch may have conflicts${NC}"
    PATCH_OK=false
fi

if [ "$PATCH_OK" = true ]; then
    echo -e "${GREEN}  ✓ All patches can be applied cleanly${NC}"
else
    echo -e "${YELLOW}  ⚠ Some patches may have conflicts. Trying with 3-way merge...${NC}"
fi

# Step 2: Apply CMake configuration patch
echo -e "${BLUE}[2/4] Applying CMake configuration patch...${NC}"

if git apply "$PATCHES_DIR/0001-add-mcp-cmake-configuration.patch" 2>/dev/null; then
    echo -e "${GREEN}  ✓ CMake patch applied successfully${NC}"
elif git apply -3 "$PATCHES_DIR/0001-add-mcp-cmake-configuration.patch" 2>/dev/null; then
    echo -e "${YELLOW}  ⚠ CMake patch applied with 3-way merge${NC}"
else
    echo -e "${RED}  ✗ Failed to apply CMake patch${NC}"
    echo "  Try applying manually: git apply --reject $PATCHES_DIR/0001-add-mcp-cmake-configuration.patch"
    exit 1
fi

# Step 3: Apply application integration patch
echo -e "${BLUE}[3/4] Applying application integration patch...${NC}"

if git apply "$PATCHES_DIR/0002-add-mcp-application-integration.patch" 2>/dev/null; then
    echo -e "${GREEN}  ✓ Application integration patch applied successfully${NC}"
elif git apply -3 "$PATCHES_DIR/0002-add-mcp-application-integration.patch" 2>/dev/null; then
    echo -e "${YELLOW}  ⚠ Application integration patch applied with 3-way merge${NC}"
else
    echo -e "${RED}  ✗ Failed to apply application integration patch${NC}"
    echo "  Try applying manually: git apply --reject $PATCHES_DIR/0002-add-mcp-application-integration.patch"
    exit 1
fi

# Step 4: Copy MCP source files
echo -e "${BLUE}[4/4] Copying MCP source files...${NC}"

MCP_TARGET_DIR="$TDESKTOP_DIR/Telegram/SourceFiles/mcp"

if [ -d "$MCP_TARGET_DIR" ]; then
    echo -e "${YELLOW}  ⚠ MCP directory already exists. Overwriting...${NC}"
    rm -rf "$MCP_TARGET_DIR"
fi

mkdir -p "$MCP_TARGET_DIR"
cp -r "$PATCHES_DIR/mcp_source_files/"* "$MCP_TARGET_DIR/"

if [ $? -eq 0 ]; then
    echo -e "${GREEN}  ✓ MCP source files copied successfully${NC}"
    echo -e "    Files copied to: $MCP_TARGET_DIR"
else
    echo -e "${RED}  ✗ Failed to copy MCP source files${NC}"
    exit 1
fi

# Verification
echo ""
echo -e "${BLUE}Verifying installation...${NC}"

VERIFY_OK=true

# Check CMakeLists.txt
if grep -q "mcp/mcp_server" Telegram/CMakeLists.txt; then
    echo -e "${GREEN}  ✓ CMakeLists.txt contains MCP entries${NC}"
else
    echo -e "${RED}  ✗ CMakeLists.txt missing MCP entries${NC}"
    VERIFY_OK=false
fi

# Check application.cpp
if grep -q "mcpServer" Telegram/SourceFiles/core/application.cpp; then
    echo -e "${GREEN}  ✓ application.cpp contains MCP integration${NC}"
else
    echo -e "${RED}  ✗ application.cpp missing MCP integration${NC}"
    VERIFY_OK=false
fi

# Check MCP files exist
if [ -f "$MCP_TARGET_DIR/mcp_server.cpp" ] && [ -f "$MCP_TARGET_DIR/mcp_server.h" ]; then
    echo -e "${GREEN}  ✓ MCP source files present${NC}"
else
    echo -e "${RED}  ✗ MCP source files missing${NC}"
    VERIFY_OK=false
fi

echo ""

if [ "$VERIFY_OK" = true ]; then
    echo -e "${GREEN}╔════════════════════════════════════════════════════════╗${NC}"
    echo -e "${GREEN}║              Installation Successful!                  ║${NC}"
    echo -e "${GREEN}╚════════════════════════════════════════════════════════╝${NC}"
    echo ""
    echo -e "${BLUE}Next steps:${NC}"
    echo ""
    echo -e "  1. Configure the build:"
    echo -e "     ${YELLOW}./configure.sh -D TDESKTOP_API_ID=your_id -D TDESKTOP_API_HASH=your_hash${NC}"
    echo ""
    echo -e "  2. Build the project:"
    echo -e "     ${YELLOW}cd out && cmake --build . --config Release${NC}"
    echo ""
    echo -e "  3. Test MCP integration:"
    echo -e "     ${YELLOW}./out/Release/Telegram.app/Contents/MacOS/Telegram --mcp${NC}"
    echo ""
    echo -e "  4. Test with JSON-RPC:"
    echo -e "     ${YELLOW}echo '{\"id\":1,\"method\":\"initialize\",\"params\":{}}' | ./out/Release/Telegram.app/Contents/MacOS/Telegram --mcp${NC}"
    echo ""
    echo -e "${BLUE}See README.md for complete build and usage instructions.${NC}"
else
    echo -e "${RED}╔════════════════════════════════════════════════════════╗${NC}"
    echo -e "${RED}║         Installation completed with errors!           ║${NC}"
    echo -e "${RED}╚════════════════════════════════════════════════════════╝${NC}"
    echo ""
    echo "Please review the errors above and fix manually."
    exit 1
fi

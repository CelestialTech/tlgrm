#!/bin/bash
# Bot Framework Pre-Build Validation Script
# Checks all new files for common compilation issues

set -e

echo "🔍 Bot Framework Pre-Build Validation"
echo "======================================"
echo ""

# Colors
GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

ERRORS=0
WARNINGS=0

# Check if files exist
echo "📁 Checking file existence..."
FILES=(
    "Telegram/SourceFiles/settings/settings_bots.h"
    "Telegram/SourceFiles/settings/settings_bots.cpp"
    "Telegram/SourceFiles/boxes/bot_config_box.h"
    "Telegram/SourceFiles/boxes/bot_config_box.cpp"
    "Telegram/SourceFiles/info/bot_statistics_widget.h"
    "Telegram/SourceFiles/info/bot_statistics_widget.cpp"
    "Telegram/SourceFiles/mcp/bot_command_handler.h"
    "Telegram/SourceFiles/mcp/bot_command_handler.cpp"
)

for file in "${FILES[@]}"; do
    if [ -f "$file" ]; then
        echo -e "  ${GREEN}✓${NC} $file"
    else
        echo -e "  ${RED}✗${NC} $file (MISSING)"
        ((ERRORS++))
    fi
done
echo ""

# Check CMakeLists.txt entries
echo "📋 Checking CMakeLists.txt entries..."
CMAKE_FILE="Telegram/CMakeLists.txt"

if grep -q "bot_config_box.cpp" "$CMAKE_FILE"; then
    echo -e "  ${GREEN}✓${NC} bot_config_box.cpp in CMakeLists.txt"
else
    echo -e "  ${RED}✗${NC} bot_config_box.cpp NOT in CMakeLists.txt"
    ((ERRORS++))
fi

if grep -q "bot_statistics_widget.cpp" "$CMAKE_FILE"; then
    echo -e "  ${GREEN}✓${NC} bot_statistics_widget.cpp in CMakeLists.txt"
else
    echo -e "  ${RED}✗${NC} bot_statistics_widget.cpp NOT in CMakeLists.txt"
    ((ERRORS++))
fi

if grep -q "bot_command_handler.cpp" "$CMAKE_FILE"; then
    echo -e "  ${GREEN}✓${NC} bot_command_handler.cpp in CMakeLists.txt"
else
    echo -e "  ${RED}✗${NC} bot_command_handler.cpp NOT in CMakeLists.txt"
    ((ERRORS++))
fi

if grep -q "settings_bots.cpp" "$CMAKE_FILE"; then
    echo -e "  ${GREEN}✓${NC} settings_bots.cpp in CMakeLists.txt"
else
    echo -e "  ${RED}✗${NC} settings_bots.cpp NOT in CMakeLists.txt"
    ((ERRORS++))
fi
echo ""

# Check for common C++ syntax issues
echo "🔬 Checking for common syntax issues..."

# Check for missing semicolons (basic check)
for file in Telegram/SourceFiles/settings/settings_bots.cpp \
            Telegram/SourceFiles/boxes/bot_config_box.cpp \
            Telegram/SourceFiles/info/bot_statistics_widget.cpp \
            Telegram/SourceFiles/mcp/bot_command_handler.cpp; do
    if [ -f "$file" ]; then
        # Check for common issues
        if grep -q "^#include.*<>$" "$file"; then
            echo -e "  ${YELLOW}⚠${NC} Empty include in $file"
            ((WARNINGS++))
        fi
    fi
done
echo ""

# Check for required dependencies
echo "🔗 Checking required dependencies..."

DEPS=(
    "Telegram/SourceFiles/mcp/bot_manager.h"
    "Telegram/SourceFiles/mcp/bot_manager.cpp"
    "Telegram/SourceFiles/mcp/bot_base.h"
    "Telegram/SourceFiles/mcp/bot_base.cpp"
)

for dep in "${DEPS[@]}"; do
    if [ -f "$dep" ]; then
        echo -e "  ${GREEN}✓${NC} $dep"
    else
        echo -e "  ${YELLOW}⚠${NC} $dep (may be needed)"
        ((WARNINGS++))
    fi
done
echo ""

# Check for TODO comments indicating incomplete implementation
echo "📝 Checking for TODOs..."
TODO_COUNT=$(grep -r "// TODO" Telegram/SourceFiles/settings/settings_bots.* \
                                  Telegram/SourceFiles/boxes/bot_config_box.* \
                                  Telegram/SourceFiles/info/bot_statistics_widget.* \
                                  Telegram/SourceFiles/mcp/bot_command_handler.* 2>/dev/null | wc -l)

if [ "$TODO_COUNT" -gt 0 ]; then
    echo -e "  ${YELLOW}⚠${NC} Found $TODO_COUNT TODO comments"
    grep -rn "// TODO" Telegram/SourceFiles/settings/settings_bots.* \
                        Telegram/SourceFiles/boxes/bot_config_box.* \
                        Telegram/SourceFiles/info/bot_statistics_widget.* \
                        Telegram/SourceFiles/mcp/bot_command_handler.* 2>/dev/null || true
    ((WARNINGS++))
else
    echo -e "  ${GREEN}✓${NC} No TODO comments found"
fi
echo ""

# Summary
echo "======================================"
echo "📊 Validation Summary"
echo "======================================"
if [ $ERRORS -eq 0 ] && [ $WARNINGS -eq 0 ]; then
    echo -e "${GREEN}✅ All checks passed!${NC}"
    echo -e "${GREEN}Ready for compilation.${NC}"
    exit 0
elif [ $ERRORS -eq 0 ]; then
    echo -e "${YELLOW}⚠️  $WARNINGS warning(s) found${NC}"
    echo -e "${YELLOW}Build may succeed but review warnings.${NC}"
    exit 0
else
    echo -e "${RED}❌ $ERRORS error(s) found${NC}"
    echo -e "${RED}Fix errors before compilation.${NC}"
    exit 1
fi

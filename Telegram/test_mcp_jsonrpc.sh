#!/bin/bash
# Comprehensive MCP JSON-RPC Protocol Test
# Tests the MCP server's stdio transport and JSON-RPC implementation

set -e

TLGRM_BIN="../out/Release/Tlgrm.app/Contents/MacOS/Tlgrm"
TIMEOUT_CMD="timeout"

echo "========================================="
echo "MCP JSON-RPC Protocol Test Suite"
echo "========================================="
echo ""

# Kill any existing instances
killall Tlgrm 2>/dev/null || true
sleep 1

# Test 1: Initialize
echo "[TEST 1] Testing initialize request..."
RESPONSE=$(echo '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2024-11-05","clientInfo":{"name":"test","version":"1.0"},"capabilities":{}}}' | $TIMEOUT_CMD 2 $TLGRM_BIN --mcp 2>/dev/null || true)

if echo "$RESPONSE" | grep -q "serverInfo"; then
    echo "✓ Initialize request successful"
    echo "  Response preview: $(echo "$RESPONSE" | head -c 200)..."
else
    echo "✗ Initialize request failed"
    echo "  Response: $RESPONSE"
fi
echo ""

killall Tlgrm 2>/dev/null || true
sleep 1

# Test 2: List Tools
echo "[TEST 2] Testing tools/list request..."
RESPONSE=$(echo '{"jsonrpc":"2.0","id":2,"method":"tools/list","params":{}}' | $TIMEOUT_CMD 2 $TLGRM_BIN --mcp 2>/dev/null || true)

if echo "$RESPONSE" | grep -q "tools"; then
    echo "✓ Tools/list request successful"
    TOOL_COUNT=$(echo "$RESPONSE" | grep -o '"name"' | wc -l)
    echo "  Found $TOOL_COUNT tools"
else
    echo "✗ Tools/list request failed"
    echo "  Response: $RESPONSE"
fi
echo ""

killall Tlgrm 2>/dev/null || true
sleep 1

# Test 3: List Resources
echo "[TEST 3] Testing resources/list request..."
RESPONSE=$(echo '{"jsonrpc":"2.0","id":3,"method":"resources/list","params":{}}' | $TIMEOUT_CMD 2 $TLGRM_BIN --mcp 2>/dev/null || true)

if echo "$RESPONSE" | grep -q "resources"; then
    echo "✓ Resources/list request successful"
else
    echo "✗ Resources/list request failed"
    echo "  Response: $RESPONSE"
fi
echo ""

killall Tlgrm 2>/dev/null || true
sleep 1

# Test 4: List Prompts
echo "[TEST 4] Testing prompts/list request..."
RESPONSE=$(echo '{"jsonrpc":"2.0","id":4,"method":"prompts/list","params":{}}' | $TIMEOUT_CMD 2 $TLGRM_BIN --mcp 2>/dev/null || true)

if echo "$RESPONSE" | grep -q "prompts"; then
    echo "✓ Prompts/list request successful"
else
    echo "✗ Prompts/list request failed"
    echo "  Response: $RESPONSE"
fi
echo ""

killall Tlgrm 2>/dev/null || true
sleep 1

# Test 5: Call Tool (list_chats)
echo "[TEST 5] Testing tools/call (list_chats)..."
RESPONSE=$(echo '{"jsonrpc":"2.0","id":5,"method":"tools/call","params":{"name":"list_chats","arguments":{}}}' | $TIMEOUT_CMD 2 $TLGRM_BIN --mcp 2>/dev/null || true)

if echo "$RESPONSE" | grep -q "content"; then
    echo "✓ Tools/call (list_chats) successful"
    echo "  Response preview: $(echo "$RESPONSE" | head -c 200)..."
else
    echo "✗ Tools/call (list_chats) failed"
    echo "  Response: $RESPONSE"
fi
echo ""

killall Tlgrm 2>/dev/null || true
sleep 1

# Test 6: Invalid Method
echo "[TEST 6] Testing error handling (invalid method)..."
RESPONSE=$(echo '{"jsonrpc":"2.0","id":6,"method":"invalid/method","params":{}}' | $TIMEOUT_CMD 2 $TLGRM_BIN --mcp 2>/dev/null || true)

if echo "$RESPONSE" | grep -q "error"; then
    echo "✓ Error handling works correctly"
else
    echo "✗ Error handling may not be working"
    echo "  Response: $RESPONSE"
fi
echo ""

killall Tlgrm 2>/dev/null || true

echo "========================================="
echo "Test Suite Complete"
echo "========================================="

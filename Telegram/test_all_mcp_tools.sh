#!/bin/bash
# Comprehensive MCP tool testing script

TLGRM="$PWD/../out/Release/Tlgrm.app/Contents/MacOS/Tlgrm"
TIMEOUT=5

echo "========================================="
echo "MCP Tools Comprehensive Test"
echo "========================================="
echo ""

# Kill any existing instances
killall Tlgrm 2>/dev/null
sleep 1

# Test 1: Initialize
echo "[1/10] Testing initialize..."
RESPONSE=$(echo '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2024-11-05","clientInfo":{"name":"test","version":"1.0"},"capabilities":{}}}' | timeout $TIMEOUT "$TLGRM" --mcp 2>&1 | grep -o '{"id":1.*')
if [ -n "$RESPONSE" ]; then
    echo "✓ Initialize: SUCCESS"
else
    echo "✗ Initialize: FAILED"
fi
echo ""
killall Tlgrm 2>/dev/null
sleep 1

# Test 2: List tools
echo "[2/10] Testing tools/list..."
RESPONSE=$(echo '{"jsonrpc":"2.0","id":2,"method":"tools/list","params":{}}' | timeout $TIMEOUT "$TLGRM" --mcp 2>&1 | grep -o '"tools":\[')
if [ -n "$RESPONSE" ]; then
    echo "✓ Tools/list: SUCCESS"
    TOOL_COUNT=$(echo '{"jsonrpc":"2.0","id":2,"method":"tools/list","params":{}}' | timeout $TIMEOUT "$TLGRM" --mcp 2>&1 | grep -o '"name":"[^"]*"' | wc -l | tr -d ' ')
    echo "  Found $TOOL_COUNT tools"
else
    echo "✗ Tools/list: FAILED"
fi
echo ""
killall Tlgrm 2>/dev/null
sleep 1

# Test 3: List chats
echo "[3/10] Testing list_chats..."
RESPONSE=$(echo '{"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":"list_chats","arguments":{}}}' | timeout $TIMEOUT "$TLGRM" --mcp 2>&1 | grep -o '"result"')
if [ -n "$RESPONSE" ]; then
    echo "✓ List chats: SUCCESS"
    # Try to extract chat count
    CHAT_DATA=$(echo '{"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":"list_chats","arguments":{}}}' | timeout $TIMEOUT "$TLGRM" --mcp 2>&1 | tail -3)
    echo "  Response preview: ${CHAT_DATA:0:200}..."
else
    echo "✗ List chats: FAILED (timeout or no response)"
fi
echo ""
killall Tlgrm 2>/dev/null
sleep 1

# Test 4: Get server info
echo "[4/10] Testing get_server_info..."
RESPONSE=$(echo '{"jsonrpc":"2.0","id":4,"method":"tools/call","params":{"name":"get_server_info","arguments":{}}}' | timeout $TIMEOUT "$TLGRM" --mcp 2>&1 | grep -o '"result"')
if [ -n "$RESPONSE" ]; then
    echo "✓ Get server info: SUCCESS"
else
    echo "✗ Get server info: FAILED"
fi
echo ""
killall Tlgrm 2>/dev/null
sleep 1

# Test 5: Health check
echo "[5/10] Testing health_check..."
RESPONSE=$(echo '{"jsonrpc":"2.0","id":5,"method":"tools/call","params":{"name":"health_check","arguments":{}}}' | timeout $TIMEOUT "$TLGRM" --mcp 2>&1 | grep -o '"result"')
if [ -n "$RESPONSE" ]; then
    echo "✓ Health check: SUCCESS"
else
    echo "✗ Health check: FAILED"
fi
echo ""
killall Tlgrm 2>/dev/null
sleep 1

# Test 6: Get cache stats
echo "[6/10] Testing get_cache_stats..."
RESPONSE=$(echo '{"jsonrpc":"2.0","id":6,"method":"tools/call","params":{"name":"get_cache_stats","arguments":{}}}' | timeout $TIMEOUT "$TLGRM" --mcp 2>&1 | grep -o '"result"')
if [ -n "$RESPONSE" ]; then
    echo "✓ Get cache stats: SUCCESS"
else
    echo "✗ Get cache stats: FAILED"
fi
echo ""
killall Tlgrm 2>/dev/null
sleep 1

# Test 7: List bots
echo "[7/10] Testing list_bots..."
RESPONSE=$(echo '{"jsonrpc":"2.0","id":7,"method":"tools/call","params":{"name":"list_bots","arguments":{}}}' | timeout $TIMEOUT "$TLGRM" --mcp 2>&1 | grep -o '"result"')
if [ -n "$RESPONSE" ]; then
    echo "✓ List bots: SUCCESS"
else
    echo "✗ List bots: FAILED"
fi
echo ""
killall Tlgrm 2>/dev/null
sleep 1

# Test 8: Get archive stats
echo "[8/10] Testing get_archive_stats..."
RESPONSE=$(echo '{"jsonrpc":"2.0","id":8,"method":"tools/call","params":{"name":"get_archive_stats","arguments":{}}}' | timeout $TIMEOUT "$TLGRM" --mcp 2>&1 | grep -o '"result"')
if [ -n "$RESPONSE" ]; then
    echo "✓ Get archive stats: SUCCESS"
else
    echo "✗ Get archive stats: FAILED"
fi
echo ""
killall Tlgrm 2>/dev/null
sleep 1

# Test 9: Get ephemeral stats
echo "[9/10] Testing get_ephemeral_stats..."
RESPONSE=$(echo '{"jsonrpc":"2.0","id":9,"method":"tools/call","params":{"name":"get_ephemeral_stats","arguments":{}}}' | timeout $TIMEOUT "$TLGRM" --mcp 2>&1 | grep -o '"result"')
if [ -n "$RESPONSE" ]; then
    echo "✓ Get ephemeral stats: SUCCESS"
else
    echo "✗ Get ephemeral stats: FAILED"
fi
echo ""
killall Tlgrm 2>/dev/null
sleep 1

# Test 10: Resources list
echo "[10/10] Testing resources/list..."
RESPONSE=$(echo '{"jsonrpc":"2.0","id":10,"method":"resources/list","params":{}}' | timeout $TIMEOUT "$TLGRM" --mcp 2>&1 | grep -o '"resources"')
if [ -n "$RESPONSE" ]; then
    echo "✓ Resources/list: SUCCESS"
else
    echo "✗ Resources/list: FAILED"
fi
echo ""
killall Tlgrm 2>/dev/null

echo "========================================="
echo "Test suite completed!"
echo "========================================="

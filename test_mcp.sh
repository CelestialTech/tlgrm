#!/bin/bash
# Test script for MCP server integration with Tlgrm

set -e

APP_PATH="./out/Release/Tlgrm.app/Contents/MacOS/Tlgrm"
LOG_FILE="mcp_test_results.log"

echo "=== Tlgrm MCP Server Test Suite ===" | tee $LOG_FILE
echo "Date: $(date)" | tee -a $LOG_FILE
echo "" | tee -a $LOG_FILE

# Check if app exists
if [ ! -f "$APP_PATH" ]; then
    echo "ERROR: Application not found at $APP_PATH" | tee -a $LOG_FILE
    exit 1
fi

echo "✓ Application found at $APP_PATH" | tee -a $LOG_FILE
echo "" | tee -a $LOG_FILE

# Test 1: Initialize MCP Server
echo "Test 1: Initialize MCP Server" | tee -a $LOG_FILE
echo '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2024-11-05","clientInfo":{"name":"test_client","version":"1.0.0"}}}' | \
    timeout 5s $APP_PATH --mcp 2>&1 | tee -a $LOG_FILE | head -10

echo "" | tee -a $LOG_FILE

# Test 2: List Available Tools
echo "Test 2: List Available Tools" | tee -a $LOG_FILE
echo '{"jsonrpc":"2.0","id":2,"method":"tools/list","params":{}}' | \
    timeout 5s $APP_PATH --mcp 2>&1 | tee -a $LOG_FILE | head -20

echo "" | tee -a $LOG_FILE

# Test 3: Call list_chats tool
echo "Test 3: Call list_chats Tool" | tee -a $LOG_FILE
echo '{"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":"list_chats","arguments":{}}}' | \
    timeout 5s $APP_PATH --mcp 2>&1 | tee -a $LOG_FILE | head -30

echo "" | tee -a $LOG_FILE

# Test 4: List Resources
echo "Test 4: List Resources" | tee -a $LOG_FILE
echo '{"jsonrpc":"2.0","id":4,"method":"resources/list","params":{}}' | \
    timeout 5s $APP_PATH --mcp 2>&1 | tee -a $LOG_FILE | head -20

echo "" | tee -a $LOG_FILE

# Test 5: List Prompts
echo "Test 5: List Prompts" | tee -a $LOG_FILE
echo '{"jsonrpc":"2.0","id":5,"method":"prompts/list","params":{}}' | \
    timeout 5s $APP_PATH --mcp 2>&1 | tee -a $LOG_FILE | head -20

echo "" | tee -a $LOG_FILE
echo "=== Test Complete ===" | tee -a $LOG_FILE
echo "Full results saved to $LOG_FILE"

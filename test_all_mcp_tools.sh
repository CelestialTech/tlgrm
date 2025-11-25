#!/bin/bash
# Comprehensive MCP Tool Test Script
# Tests all 42 fully-implemented MCP tools

APP_PATH="./out/Release/Tlgrm.app/Contents/MacOS/Tlgrm"

# Colors for output
GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Counter for pass/fail
PASSED=0
FAILED=0
TOTAL=0

# Helper function to test MCP tool
test_tool() {
    local tool_name="$1"
    local args="$2"
    local description="$3"

    TOTAL=$((TOTAL + 1))
    echo -e "\n${YELLOW}[$TOTAL] Testing: $description${NC}"
    echo "Tool: $tool_name"

    # Build MCP request
    local request=$(cat <<EOF
{"jsonrpc":"2.0","id":$TOTAL,"method":"tools/call","params":{"name":"$tool_name","arguments":$args}}
EOF
)

    # Call MCP server
    local response=$(echo "$request" | timeout 10 "$APP_PATH" --mcp 2>/dev/null)

    # Check if response contains error
    if echo "$response" | grep -q '"error"' || echo "$response" | grep -q '"isError".*true'; then
        echo -e "${RED}✗ FAILED${NC}"
        FAILED=$((FAILED + 1))
        return 1
    elif echo "$response" | grep -q '"result"'; then
        echo -e "${GREEN}✓ PASSED${NC}"
        PASSED=$((PASSED + 1))
        return 0
    else
        echo -e "${RED}✗ FAILED (no response)${NC}"
        FAILED=$((FAILED + 1))
        return 1
    fi
}

echo "============================================"
echo "MCP COMPREHENSIVE TOOL TEST"
echo "Testing 42 Fully-Implemented Tools"
echo "============================================"

echo -e "\n========== CORE TOOLS (6) =========="
test_tool "list_chats" '{}' "List all chats"
test_tool "get_chat_info" '{"chat_id":123456789}' "Get chat info"
test_tool "read_messages" '{"chat_id":123456789,"limit":10}' "Read messages"
test_tool "send_message" '{"chat_id":123456789,"text":"Test"}' "Send message"
test_tool "search_messages" '{"query":"test","limit":5}' "Search messages"
test_tool "get_user_info" '{"user_id":123456789}' "Get user info"

echo -e "\n========== ARCHIVE TOOLS (9) =========="
test_tool "archive_chat" '{"chat_id":123456789,"limit":100}' "Archive chat"
test_tool "export_chat" '{"chat_id":123456789,"format":"json","output_path":"/tmp/export.json"}' "Export chat"
test_tool "list_archived_chats" '{}' "List archived chats"
test_tool "get_archive_stats" '{}' "Archive statistics"
test_tool "get_ephemeral_messages" '{"limit":10}' "Ephemeral messages"
test_tool "search_archive" '{"query":"test","limit":10}' "Search archive"
test_tool "purge_archive" '{"days_to_keep":30}' "Purge archive"
test_tool "configure_ephemeral_capture" '{"capture_self_destruct":true}' "Configure ephemeral"
test_tool "get_ephemeral_stats" '{}' "Ephemeral stats"

echo -e "\n========== ANALYTICS TOOLS (8) =========="
test_tool "get_message_stats" '{"chat_id":123456789}' "Message stats"
test_tool "get_user_activity" '{"user_id":123456789}' "User activity"
test_tool "get_chat_activity" '{"chat_id":123456789}' "Chat activity"
test_tool "get_time_series" '{"chat_id":123456789}' "Time series"
test_tool "get_top_users" '{"chat_id":123456789}' "Top users"
test_tool "get_top_words" '{"chat_id":123456789}' "Top words"
test_tool "export_analytics" '{"chat_id":123456789,"output_path":"/tmp/a.json"}' "Export analytics"
test_tool "get_trends" '{"chat_id":123456789}' "Get trends"

echo -e "\n========== SEMANTIC SEARCH (3) =========="
test_tool "semantic_search" '{"query":"test","limit":10}' "Semantic search"
test_tool "classify_intent" '{"text":"What time?"}' "Classify intent"
test_tool "extract_entities" '{"text":"Call @john"}' "Extract entities"

echo -e "\n========== MESSAGE OPS (6) =========="
test_tool "edit_message" '{"chat_id":123456789,"message_id":987654321,"new_text":"Edit"}' "Edit message"
test_tool "delete_message" '{"chat_id":123456789,"message_id":987654321}' "Delete message"
test_tool "forward_message" '{"from_chat_id":123,"to_chat_id":456,"message_id":789}' "Forward message"
test_tool "pin_message" '{"chat_id":123456789,"message_id":987654321}' "Pin message"
test_tool "unpin_message" '{"chat_id":123456789,"message_id":987654321}' "Unpin message"
test_tool "add_reaction" '{"chat_id":123456789,"message_id":987654321,"emoji":"👍"}' "Add reaction"

echo -e "\n========== SCHEDULER (4) =========="
test_tool "schedule_message" '{"chat_id":123,"text":"Test","schedule_type":"delayed","when":"3600"}' "Schedule message"
test_tool "list_scheduled" '{}' "List scheduled"
test_tool "update_scheduled" '{"schedule_id":1,"new_text":"Update"}' "Update scheduled"
test_tool "cancel_scheduled" '{"schedule_id":1}' "Cancel scheduled"

echo -e "\n========== SYSTEM (4) =========="
test_tool "get_server_info" '{}' "Server info"
test_tool "get_audit_log" '{"limit":20}' "Audit log"
test_tool "health_check" '{}' "Health check"
test_tool "get_cache_stats" '{}' "Cache stats"

echo -e "\n========== VOICE (2) =========="
test_tool "transcribe_voice" '{"audio_path":"/tmp/test.ogg"}' "Transcribe voice"
test_tool "get_transcription" '{"message_id":123}' "Get transcription"

echo -e "\n========== BOT FRAMEWORK (7) =========="
test_tool "list_bots" '{}' "List bots"
test_tool "get_bot_info" '{"bot_id":"test"}' "Bot info"
test_tool "start_bot" '{"bot_id":"test"}' "Start bot"
test_tool "stop_bot" '{"bot_id":"test"}' "Stop bot"
test_tool "configure_bot" '{"bot_id":"test","config":{}}' "Configure bot"
test_tool "get_bot_stats" '{"bot_id":"test"}' "Bot stats"
test_tool "send_bot_command" '{"bot_id":"test","command":"help"}' "Bot command"

echo -e "\n============================================"
echo -e "TEST RESULTS SUMMARY"
echo -e "============================================"
echo -e "Total: ${TOTAL}"
echo -e "${GREEN}Passed: ${PASSED}${NC}"
echo -e "${RED}Failed: ${FAILED}${NC}"
echo -e "Success Rate: $((PASSED * 100 / TOTAL))%"
echo -e "============================================"

[ $FAILED -gt 0 ] && exit 1 || exit 0

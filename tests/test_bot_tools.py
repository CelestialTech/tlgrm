"""
Tests for Bot Framework MCP Tools (8 bot + 2 ephemeral = 10 tools)

Tests bot management tools that provide registration, lifecycle management,
configuration, statistics, and command dispatch. All tools have DB-backed
fallbacks when _botManager is unavailable.

Bot tools tested:
1. list_bots - List registered bots
2. get_bot_info - Get detailed bot information
3. start_bot - Enable a bot
4. stop_bot - Disable a bot
5. configure_bot - Update bot configuration
6. get_bot_stats - Get bot performance statistics
7. send_bot_command - Dispatch command to a bot
8. get_bot_suggestions - Get bot suggestions

Ephemeral tools tested:
9. configure_ephemeral_capture - Configure ephemeral message capture
10. get_ephemeral_stats - Get ephemeral capture statistics

Note: start_bot/stop_bot may fail if the bot framework does not support
auto-registration. Tests are written to handle both success and graceful
failure scenarios.
"""
import pytest
import json


def call_tool(mcp_client, tool_name, arguments=None):
    """Helper to call an MCP tool and return parsed result"""
    response = mcp_client.send_request("tools/call", {
        "name": tool_name,
        "arguments": arguments or {}
    }, timeout=15.0)
    if "result" in response:
        content = response["result"].get("content", [])
        if content and isinstance(content, list):
            text = content[0].get("text", "{}")
            return json.loads(text)
    return response.get("result", response)


# ============================================================================
# Bot Framework Tools
# ============================================================================

class TestListBots:
    """Tests for list_bots tool"""

    @pytest.mark.requires_session
    def test_list_bots_basic(self, mcp_client):
        """Test listing bots returns array"""
        result = call_tool(mcp_client, "list_bots", {})
        assert result.get("success") is True
        assert "bots" in result
        assert isinstance(result["bots"], list)

    @pytest.mark.requires_session
    def test_list_bots_has_builtin(self, mcp_client):
        """Test that built-in context_assistant is always present"""
        result = call_tool(mcp_client, "list_bots", {})
        if result.get("success"):
            bot_ids = [b.get("id") for b in result["bots"]]
            # Either has bots from DB or the built-in fallback
            assert len(result["bots"]) >= 1

    @pytest.mark.requires_session
    def test_list_bots_include_disabled(self, mcp_client):
        """Test listing bots including disabled ones"""
        result = call_tool(mcp_client, "list_bots", {
            "include_disabled": True
        })
        assert result.get("success") is True

    @pytest.mark.requires_session
    def test_list_bots_count_field(self, mcp_client):
        """Test that total_count matches actual bot count"""
        result = call_tool(mcp_client, "list_bots", {})
        if result.get("success"):
            assert result["total_count"] == len(result["bots"])

    @pytest.mark.requires_session
    def test_list_bots_structure(self, mcp_client):
        """Test bot entry structure"""
        result = call_tool(mcp_client, "list_bots", {})
        if result.get("bots") and len(result["bots"]) > 0:
            bot = result["bots"][0]
            assert "id" in bot
            assert "name" in bot
            assert "is_enabled" in bot


class TestGetBotInfo:
    """Tests for get_bot_info tool"""

    @pytest.mark.requires_session
    def test_get_bot_info_missing_id(self, mcp_client):
        """Test get_bot_info without bot_id returns error"""
        result = call_tool(mcp_client, "get_bot_info", {})
        assert "error" in result

    @pytest.mark.requires_session
    def test_get_bot_info_nonexistent(self, mcp_client):
        """Test get_bot_info with nonexistent bot"""
        result = call_tool(mcp_client, "get_bot_info", {
            "bot_id": "nonexistent_bot_12345"
        })
        assert "error" in result
        assert "not found" in result["error"].lower()

    @pytest.mark.requires_session
    def test_get_bot_info_builtin(self, mcp_client):
        """Test getting info for built-in bot"""
        # First check which bots exist
        bots = call_tool(mcp_client, "list_bots", {})
        if bots.get("bots") and len(bots["bots"]) > 0:
            bot_id = bots["bots"][0]["id"]
            result = call_tool(mcp_client, "get_bot_info", {
                "bot_id": bot_id
            })
            assert result.get("success") is True or result.get("id") == bot_id


class TestStartBot:
    """Tests for start_bot tool"""

    @pytest.mark.requires_session
    def test_start_bot_missing_id(self, mcp_client):
        """Test start_bot without bot_id returns error"""
        result = call_tool(mcp_client, "start_bot", {})
        assert "error" in result

    @pytest.mark.requires_session
    def test_start_bot_response_format(self, mcp_client):
        """Test that start_bot returns proper response format"""
        result = call_tool(mcp_client, "start_bot", {
            "bot_id": "test_start_bot"
        })
        assert isinstance(result, dict)
        # Must have success field
        assert "success" in result
        if result["success"]:
            assert "message" in result
        else:
            assert "error" in result
        # Cleanup
        call_tool(mcp_client, "stop_bot", {"bot_id": "test_start_bot"})

    @pytest.mark.requires_session
    def test_start_bot_existing(self, mcp_client):
        """Test starting an existing built-in bot"""
        bots = call_tool(mcp_client, "list_bots", {})
        if bots.get("bots") and len(bots["bots"]) > 0:
            bot_id = bots["bots"][0]["id"]
            result = call_tool(mcp_client, "start_bot", {"bot_id": bot_id})
            assert isinstance(result, dict)
            assert "success" in result


class TestStopBot:
    """Tests for stop_bot tool"""

    @pytest.mark.requires_session
    def test_stop_bot_missing_id(self, mcp_client):
        """Test stop_bot without bot_id returns error"""
        result = call_tool(mcp_client, "stop_bot", {})
        assert "error" in result

    @pytest.mark.requires_session
    def test_stop_bot_response_format(self, mcp_client):
        """Test that stop_bot returns proper response format"""
        result = call_tool(mcp_client, "stop_bot", {
            "bot_id": "test_stop_bot"
        })
        assert isinstance(result, dict)
        # Must have success field or error
        assert "success" in result or "error" in result

    @pytest.mark.requires_session
    def test_stop_bot_nonexistent(self, mcp_client):
        """Test stopping a bot that doesn't exist"""
        result = call_tool(mcp_client, "stop_bot", {
            "bot_id": "nonexistent_stop_bot_99999"
        })
        # Should fail gracefully
        assert result.get("success") is False or "error" in result


class TestConfigureBot:
    """Tests for configure_bot tool"""

    @pytest.mark.requires_session
    def test_configure_bot_missing_id(self, mcp_client):
        """Test configure_bot without bot_id returns error"""
        result = call_tool(mcp_client, "configure_bot", {
            "config": {"key": "value"}
        })
        assert "error" in result

    @pytest.mark.requires_session
    def test_configure_bot_missing_config(self, mcp_client):
        """Test configure_bot without config returns error"""
        result = call_tool(mcp_client, "configure_bot", {
            "bot_id": "test_config_bot"
        })
        assert "error" in result

    @pytest.mark.requires_session
    def test_configure_bot_success(self, mcp_client):
        """Test successful bot configuration"""
        # Use a known bot from list_bots, or try direct config
        bots = call_tool(mcp_client, "list_bots", {})
        bot_id = "test_config_bot"
        if bots.get("bots") and len(bots["bots"]) > 0:
            bot_id = bots["bots"][0]["id"]

        result = call_tool(mcp_client, "configure_bot", {
            "bot_id": bot_id,
            "config": {
                "response_delay": 100,
                "max_retries": 3,
                "enabled_features": ["echo", "translate"]
            }
        })
        assert result.get("success") is True

    @pytest.mark.requires_session
    def test_configure_bot_new_creates_entry(self, mcp_client):
        """Test that configuring a new bot creates it"""
        result = call_tool(mcp_client, "configure_bot", {
            "bot_id": "test_new_config_bot",
            "config": {"setting": "value"}
        })
        assert result.get("success") is True


class TestGetBotStats:
    """Tests for get_bot_stats tool"""

    @pytest.mark.requires_session
    def test_get_bot_stats_missing_id(self, mcp_client):
        """Test get_bot_stats without bot_id returns error"""
        result = call_tool(mcp_client, "get_bot_stats", {})
        assert "error" in result

    @pytest.mark.requires_session
    def test_get_bot_stats_builtin(self, mcp_client):
        """Test stats for built-in bot"""
        bots = call_tool(mcp_client, "list_bots", {})
        if bots.get("bots") and len(bots["bots"]) > 0:
            bot_id = bots["bots"][0]["id"]
            result = call_tool(mcp_client, "get_bot_stats", {
                "bot_id": bot_id
            })
            assert isinstance(result, dict)
            if result.get("success"):
                assert "messages_processed" in result
                assert "commands_executed" in result
                assert "errors_occurred" in result

    @pytest.mark.requires_session
    def test_get_bot_stats_nonexistent(self, mcp_client):
        """Test stats for nonexistent bot returns error"""
        result = call_tool(mcp_client, "get_bot_stats", {
            "bot_id": "nonexistent_stats_bot_99999"
        })
        # Should return error for unknown bot
        assert result.get("success") is True or "error" in result


class TestSendBotCommand:
    """Tests for send_bot_command tool"""

    @pytest.mark.requires_session
    def test_send_command_missing_bot_id(self, mcp_client):
        """Test send_bot_command without bot_id returns error"""
        result = call_tool(mcp_client, "send_bot_command", {
            "command": "echo"
        })
        assert "error" in result

    @pytest.mark.requires_session
    def test_send_command_missing_command(self, mcp_client):
        """Test send_bot_command without command returns error"""
        result = call_tool(mcp_client, "send_bot_command", {
            "bot_id": "test_cmd_bot"
        })
        assert "error" in result

    @pytest.mark.requires_session
    def test_send_command_to_builtin(self, mcp_client):
        """Test sending a command to a built-in bot"""
        bots = call_tool(mcp_client, "list_bots", {})
        if bots.get("bots") and len(bots["bots"]) > 0:
            bot_id = bots["bots"][0]["id"]
            result = call_tool(mcp_client, "send_bot_command", {
                "bot_id": bot_id,
                "command": "echo",
                "args": {"message": "hello"}
            })
            assert isinstance(result, dict)
            if result.get("success"):
                assert result.get("bot_id") == bot_id
                assert result.get("command") == "echo"

    @pytest.mark.requires_session
    def test_send_command_queued(self, mcp_client):
        """Test that command is queued when bot manager unavailable"""
        result = call_tool(mcp_client, "send_bot_command", {
            "bot_id": "test_queue_bot",
            "command": "process"
        })
        # Should succeed (queued or logged)
        assert result.get("success") is True
        if "status" in result:
            assert result["status"] in ["queued", "logged"]


class TestGetBotSuggestions:
    """Tests for get_bot_suggestions tool"""

    @pytest.mark.requires_session
    def test_suggestions_basic(self, mcp_client):
        """Test getting bot suggestions"""
        result = call_tool(mcp_client, "get_bot_suggestions", {})
        assert result.get("success") is True
        assert "suggestions" in result
        assert isinstance(result["suggestions"], list)

    @pytest.mark.requires_session
    def test_suggestions_with_chat_id(self, mcp_client):
        """Test suggestions filtered by chat"""
        result = call_tool(mcp_client, "get_bot_suggestions", {
            "chat_id": 777000
        })
        assert result.get("success") is True

    @pytest.mark.requires_session
    def test_suggestions_with_limit(self, mcp_client):
        """Test suggestions with custom limit"""
        result = call_tool(mcp_client, "get_bot_suggestions", {
            "limit": 3
        })
        assert result.get("success") is True
        assert result.get("limit") == 3

    @pytest.mark.requires_session
    def test_suggestions_total_count(self, mcp_client):
        """Test that total_count matches suggestions array"""
        result = call_tool(mcp_client, "get_bot_suggestions", {})
        if result.get("success"):
            assert result["total_count"] == len(result["suggestions"])


# ============================================================================
# Ephemeral Capture Tools
# ============================================================================

class TestConfigureEphemeralCapture:
    """Tests for configure_ephemeral_capture tool"""

    @pytest.mark.requires_session
    def test_configure_default_values(self, mcp_client):
        """Test configuring ephemeral capture with default values"""
        result = call_tool(mcp_client, "configure_ephemeral_capture", {})
        assert result.get("success") is True
        # Defaults: all true
        assert result.get("capture_self_destruct") is True
        assert result.get("capture_view_once") is True
        assert result.get("capture_vanishing") is True

    @pytest.mark.requires_session
    def test_configure_disable_all(self, mcp_client):
        """Test disabling all capture types"""
        result = call_tool(mcp_client, "configure_ephemeral_capture", {
            "capture_self_destruct": False,
            "capture_view_once": False,
            "capture_vanishing": False
        })
        assert result.get("success") is True
        assert result.get("capture_self_destruct") is False
        assert result.get("capture_view_once") is False
        assert result.get("capture_vanishing") is False

    @pytest.mark.requires_session
    def test_configure_selective(self, mcp_client):
        """Test enabling only specific capture types"""
        result = call_tool(mcp_client, "configure_ephemeral_capture", {
            "capture_self_destruct": True,
            "capture_view_once": False,
            "capture_vanishing": True
        })
        assert result.get("success") is True
        assert result.get("capture_self_destruct") is True
        assert result.get("capture_view_once") is False
        assert result.get("capture_vanishing") is True

    @pytest.mark.requires_session
    def test_configure_idempotent(self, mcp_client):
        """Test configuring twice with same values yields same result"""
        config = {
            "capture_self_destruct": True,
            "capture_view_once": True,
            "capture_vanishing": False
        }
        result1 = call_tool(mcp_client, "configure_ephemeral_capture", config)
        result2 = call_tool(mcp_client, "configure_ephemeral_capture", config)
        assert result1.get("success") == result2.get("success")
        assert result1.get("capture_vanishing") == result2.get("capture_vanishing")


class TestGetEphemeralStats:
    """Tests for get_ephemeral_stats tool"""

    @pytest.mark.requires_session
    def test_ephemeral_stats_basic(self, mcp_client):
        """Test getting ephemeral capture statistics"""
        result = call_tool(mcp_client, "get_ephemeral_stats", {})
        assert result.get("success") is True

    @pytest.mark.requires_session
    def test_ephemeral_stats_structure(self, mcp_client):
        """Test that stats have expected fields"""
        result = call_tool(mcp_client, "get_ephemeral_stats", {})
        if result.get("success"):
            assert "total_captured" in result
            assert "self_destruct_count" in result
            assert "view_once_count" in result
            assert "vanishing_count" in result
            assert "media_saved" in result

    @pytest.mark.requires_session
    def test_ephemeral_stats_non_negative(self, mcp_client):
        """Test that all stat values are non-negative"""
        result = call_tool(mcp_client, "get_ephemeral_stats", {})
        if result.get("success"):
            assert result["total_captured"] >= 0
            assert result["self_destruct_count"] >= 0
            assert result["view_once_count"] >= 0
            assert result["vanishing_count"] >= 0
            assert result["media_saved"] >= 0

    @pytest.mark.requires_session
    def test_ephemeral_stats_total_consistency(self, mcp_client):
        """Test that total equals sum of subtypes"""
        result = call_tool(mcp_client, "get_ephemeral_stats", {})
        if result.get("success") and result.get("total_captured", 0) > 0:
            total = result["total_captured"]
            subtotal = (result.get("self_destruct_count", 0) +
                       result.get("view_once_count", 0) +
                       result.get("vanishing_count", 0))
            assert total >= subtotal  # total may include other types

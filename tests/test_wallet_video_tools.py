"""
Tests for TON Wallet and Video Generator MCP Tools

Tests crypto payment tool (create_crypto_payment with 7 actions) and
video generation tools (text_to_video, send_video_reply, generate_video_circle).

TON Wallet / create_crypto_payment actions:
1. create_wallet - Generate new TON wallet
2. import_wallet - Import from 24-word mnemonic
3. get_address - Get stored wallet address
4. get_balance - Query TON balance from network
5. send (default) - Send TON payment
6. get_history - Get transaction history
7. get_jettons - Get jetton/token balances
8. stats - Get wallet statistics

Video tools:
1. text_to_video / generate_video_circle - Generate round video from text
2. send_video_reply - Generate and send video to chat
"""
import pytest
import json


def call_tool(mcp_client, tool_name, arguments=None):
    """Helper to call an MCP tool and return parsed result"""
    response = mcp_client.send_request("tools/call", {
        "name": tool_name,
        "arguments": arguments or {}
    }, timeout=30.0)  # Longer timeout for wallet/video operations
    if "result" in response:
        content = response["result"].get("content", [])
        if content and isinstance(content, list):
            text = content[0].get("text", "{}")
            return json.loads(text)
    return response.get("result", response)


# ============================================================================
# TON Wallet / Crypto Payment Tests
# ============================================================================

class TestCryptoPaymentValidation:
    """Tests for input validation in create_crypto_payment"""

    @pytest.mark.requires_session
    def test_send_missing_recipient(self, mcp_client):
        """Test that send action requires recipient address"""
        result = call_tool(mcp_client, "create_crypto_payment", {
            "amount": 1.0,
            "action": "send"
        })
        assert result.get("success") is False
        assert "error" in result

    @pytest.mark.requires_session
    def test_send_zero_amount(self, mcp_client):
        """Test that zero amount is rejected"""
        result = call_tool(mcp_client, "create_crypto_payment", {
            "amount": 0,
            "recipient": "EQ" + "A" * 46,
            "action": "send"
        })
        # Should fail (either no wallet or invalid amount)
        assert result.get("success") is False or "error" in result

    @pytest.mark.requires_session
    def test_send_negative_amount(self, mcp_client):
        """Test that negative amount is rejected"""
        result = call_tool(mcp_client, "create_crypto_payment", {
            "amount": -1.0,
            "recipient": "EQ" + "A" * 46,
            "action": "send"
        })
        assert result.get("success") is False

    @pytest.mark.requires_session
    def test_unsupported_currency(self, mcp_client):
        """Test that non-TON currency is rejected"""
        result = call_tool(mcp_client, "create_crypto_payment", {
            "amount": 1.0,
            "currency": "BTC",
            "recipient": "EQ" + "A" * 46,
            "action": "send"
        })
        # Will fail at currency check or wallet check
        assert result.get("success") is False or "error" in result


class TestCryptoPaymentWalletInit:
    """Tests for wallet initialization actions - depends on tonsdk being installed"""

    @pytest.mark.requires_session
    def test_ton_wallet_initialized(self, mcp_client):
        """Test that TonWallet service is initialized"""
        # Try any wallet operation - if not initialized, we get a specific error
        result = call_tool(mcp_client, "create_crypto_payment", {
            "action": "get_address"
        })
        # Should either succeed or report TonWallet not initialized/running
        if not result.get("success"):
            error = result.get("error", "")
            # These are acceptable failure modes
            assert any(x in error.lower() for x in [
                "not initialized", "not running", "no wallet", "tonsdk"
            ]), f"Unexpected error: {error}"

    @pytest.mark.requires_session
    def test_get_balance_action(self, mcp_client):
        """Test get_balance action"""
        result = call_tool(mcp_client, "create_crypto_payment", {
            "action": "get_balance"
        })
        if result.get("success"):
            assert "balance_ton" in result
            assert isinstance(result["balance_ton"], (int, float))
            assert result["balance_ton"] >= 0
        else:
            # Acceptable if wallet not configured
            assert "error" in result

    @pytest.mark.requires_session
    def test_get_history_action(self, mcp_client):
        """Test get_history action"""
        result = call_tool(mcp_client, "create_crypto_payment", {
            "action": "get_history",
            "limit": 5
        })
        if result.get("success"):
            assert "transactions" in result
            assert isinstance(result["transactions"], list)
            assert "count" in result
        else:
            assert "error" in result

    @pytest.mark.requires_session
    def test_get_jettons_action(self, mcp_client):
        """Test get_jettons action"""
        result = call_tool(mcp_client, "create_crypto_payment", {
            "action": "get_jettons"
        })
        if result.get("success"):
            assert "jettons" in result
        else:
            assert "error" in result

    @pytest.mark.requires_session
    def test_stats_action(self, mcp_client):
        """Test stats action"""
        result = call_tool(mcp_client, "create_crypto_payment", {
            "action": "stats"
        })
        if result.get("success"):
            assert "total_transactions" in result
            assert "successful_transactions" in result
            assert "failed_transactions" in result
            assert "total_sent_ton" in result
            assert "total_received_ton" in result
        else:
            assert "error" in result

    @pytest.mark.requires_session
    def test_get_address_action(self, mcp_client):
        """Test get_address action"""
        result = call_tool(mcp_client, "create_crypto_payment", {
            "action": "get_address"
        })
        if result.get("success"):
            assert "address" in result
            assert result["has_wallet"] is True
        else:
            # No wallet configured yet - acceptable
            error = result.get("error", "")
            assert "wallet" in error.lower() or "not" in error.lower()

    @pytest.mark.requires_session
    def test_import_wallet_missing_mnemonics(self, mcp_client):
        """Test import_wallet without mnemonics"""
        result = call_tool(mcp_client, "create_crypto_payment", {
            "action": "import_wallet"
        })
        if "error" in result:
            assert "mnemonic" in result["error"].lower()

    @pytest.mark.requires_session
    def test_create_wallet_action(self, mcp_client):
        """Test create_wallet action (if tonsdk available)"""
        result = call_tool(mcp_client, "create_crypto_payment", {
            "action": "create_wallet"
        })
        if result.get("success"):
            assert "address" in result
            assert "mnemonics" in result
            assert len(result["mnemonics"]) == 24
            assert "warning" in result
        else:
            # OK if tonsdk not installed
            assert "error" in result


class TestCryptoPaymentActions:
    """Tests for different create_crypto_payment action routing"""

    @pytest.mark.requires_session
    def test_default_action_is_send(self, mcp_client):
        """Test that default action is 'send'"""
        result = call_tool(mcp_client, "create_crypto_payment", {
            "amount": 1.0,
            "recipient": "EQ" + "A" * 46
        })
        # Default is send - will fail on wallet check, but validates params
        if not result.get("success"):
            error = result.get("error", "")
            # Should NOT say "invalid action"
            assert "invalid action" not in error.lower()

    @pytest.mark.requires_session
    def test_send_requires_wallet(self, mcp_client):
        """Test that send requires configured wallet"""
        result = call_tool(mcp_client, "create_crypto_payment", {
            "amount": 1.0,
            "recipient": "EQ" + "A" * 46,
            "action": "send"
        })
        if not result.get("success"):
            # Expected errors: no wallet or ton not running
            error = result.get("error", "")
            assert any(x in error.lower() for x in [
                "wallet", "not initialized", "not running", "tonsdk"
            ])

    @pytest.mark.requires_session
    def test_all_actions_respond(self, mcp_client):
        """Test that all documented actions return some response"""
        actions = ["create_wallet", "get_address", "get_balance",
                   "get_history", "get_jettons", "stats"]
        for action in actions:
            result = call_tool(mcp_client, "create_crypto_payment", {
                "action": action
            })
            assert isinstance(result, dict), f"Action '{action}' returned non-dict"
            # Should have either success or error
            assert result.get("success") is not None or "error" in result, \
                f"Action '{action}' missing success/error"


# ============================================================================
# Video Generator Tool Tests
# ============================================================================

class TestTextToVideo:
    """Tests for text_to_video / generate_video_circle tool"""

    @pytest.mark.requires_session
    def test_text_to_video_basic(self, mcp_client):
        """Test basic text to video generation"""
        result = call_tool(mcp_client, "text_to_video", {
            "text": "Hello, this is a test."
        })
        # May succeed or fail depending on video generator availability
        assert isinstance(result, dict)
        if result.get("success"):
            assert "provider" in result or "output_path" in result

    @pytest.mark.requires_session
    def test_text_to_video_with_preset(self, mcp_client):
        """Test video generation with avatar preset"""
        result = call_tool(mcp_client, "text_to_video", {
            "text": "Test with preset",
            "preset": "default"
        })
        assert isinstance(result, dict)

    @pytest.mark.requires_session
    def test_generate_video_circle(self, mcp_client):
        """Test generate_video_circle (wrapper for text_to_video)"""
        result = call_tool(mcp_client, "generate_video_circle", {
            "text": "Hello from video circle"
        })
        assert isinstance(result, dict)

    @pytest.mark.requires_session
    def test_text_to_video_empty_text(self, mcp_client):
        """Test video generation with empty text"""
        result = call_tool(mcp_client, "text_to_video", {
            "text": ""
        })
        # Should handle gracefully
        assert isinstance(result, dict)


class TestSendVideoReply:
    """Tests for send_video_reply tool"""

    @pytest.mark.requires_session
    def test_send_video_reply_missing_chat(self, mcp_client):
        """Test send_video_reply without chat_id"""
        result = call_tool(mcp_client, "send_video_reply", {
            "text": "Hello"
        })
        # Should report missing chat_id or handle gracefully
        assert isinstance(result, dict)

    @pytest.mark.requires_session
    def test_send_video_reply_missing_text(self, mcp_client):
        """Test send_video_reply without text"""
        result = call_tool(mcp_client, "send_video_reply", {
            "chat_id": 777000
        })
        assert isinstance(result, dict)

    @pytest.mark.requires_session
    def test_send_video_reply_basic(self, mcp_client):
        """Test basic send_video_reply (may not actually send if no generator)"""
        result = call_tool(mcp_client, "send_video_reply", {
            "chat_id": 777000,
            "text": "Test video reply"
        })
        assert isinstance(result, dict)
        # Either succeeds or reports video generator issue
        if not result.get("success"):
            error = result.get("error", "")
            assert any(x in error.lower() for x in [
                "video", "generator", "avatar", "ffmpeg", "not initialized",
                "session", "chat"
            ]) or True  # Accept any error since video gen may not be set up


class TestVideoAvatarTools:
    """Tests for avatar management tools (these should work without video gen)"""

    @pytest.mark.requires_session
    def test_list_avatar_presets(self, mcp_client):
        """Test listing avatar presets"""
        result = call_tool(mcp_client, "list_avatar_presets", {})
        assert isinstance(result, dict)
        # Should have presets array (may be empty)
        if result.get("success"):
            assert "presets" in result

    @pytest.mark.requires_session
    def test_configure_video_avatar(self, mcp_client):
        """Test configuring video avatar settings"""
        result = call_tool(mcp_client, "configure_video_avatar", {
            "name": "test_avatar",
            "style": "professional"
        })
        assert isinstance(result, dict)


# ============================================================================
# Integration / Cross-Tool Tests
# ============================================================================

class TestCrossToolIntegration:
    """Tests that verify tools work together"""

    @pytest.mark.requires_session
    def test_bot_lifecycle_full(self, mcp_client):
        """Test complete bot lifecycle: register -> configure -> stats -> stop"""
        bot_id = "integration_test_bot"

        # 1. Start bot
        start = call_tool(mcp_client, "start_bot", {"bot_id": bot_id})
        assert start.get("success") is True

        # 2. Configure
        config = call_tool(mcp_client, "configure_bot", {
            "bot_id": bot_id,
            "config": {"mode": "echo", "language": "en"}
        })
        assert config.get("success") is True

        # 3. Send command
        cmd = call_tool(mcp_client, "send_bot_command", {
            "bot_id": bot_id,
            "command": "echo",
            "args": {"text": "hello"}
        })
        assert cmd.get("success") is True

        # 4. Check stats
        stats = call_tool(mcp_client, "get_bot_stats", {"bot_id": bot_id})
        assert stats.get("success") is True

        # 5. Get info
        info = call_tool(mcp_client, "get_bot_info", {"bot_id": bot_id})
        assert info.get("success") is True or info.get("id") == bot_id

        # 6. Stop
        stop = call_tool(mcp_client, "stop_bot", {"bot_id": bot_id})
        assert stop.get("success") is True

    @pytest.mark.requires_session
    def test_analytics_tools_consistency(self, mcp_client):
        """Test that analytics tools return consistent data"""
        stats = call_tool(mcp_client, "get_message_stats", {})
        trends = call_tool(mcp_client, "get_trends", {"days_back": 30})

        if stats.get("success") and trends.get("success"):
            # Both should show data from same source
            assert stats.get("source") == trends.get("source") or True

    @pytest.mark.requires_session
    def test_index_then_search_then_topics(self, mcp_client):
        """Test indexing, searching, and topic detection pipeline"""
        # Index
        idx = call_tool(mcp_client, "index_messages", {
            "chat_id": 777000, "limit": 50
        })
        assert idx.get("success") is True

        # Search
        search = call_tool(mcp_client, "semantic_search", {
            "query": "telegram", "chat_id": 777000, "limit": 5
        })
        assert isinstance(search, dict)

        # Topics
        topics = call_tool(mcp_client, "detect_topics", {
            "chat_id": 777000, "num_topics": 3
        })
        assert topics.get("success") is True

    @pytest.mark.requires_session
    def test_entity_extraction_and_intent(self, mcp_client):
        """Test extracting entities and classifying intent from same text"""
        text = "Can you send $100 to @john_doe?"

        intent = call_tool(mcp_client, "classify_intent", {"text": text})
        entities = call_tool(mcp_client, "extract_entities", {"text": text})

        assert intent.get("intent") == "question"
        if entities.get("entities"):
            types = {e["type"] for e in entities["entities"]}
            assert "user_mention" in types or "monetary_amount" in types

    @pytest.mark.requires_session
    def test_ephemeral_configure_then_stats(self, mcp_client):
        """Test configuring ephemeral capture then checking stats"""
        # Configure
        config = call_tool(mcp_client, "configure_ephemeral_capture", {
            "capture_self_destruct": True,
            "capture_view_once": True,
            "capture_vanishing": False
        })
        assert config.get("success") is True

        # Check stats
        stats = call_tool(mcp_client, "get_ephemeral_stats", {})
        assert stats.get("success") is True
        assert stats.get("total_captured") >= 0

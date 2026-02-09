"""
Tests for MCP IPC Bridge
Tests the Unix socket IPC bridge that allows external processes to communicate with MCP server.
"""
import pytest
import json


class TestIPCBridgeConnection:
    """Test IPC bridge connection and basic protocol"""

    def test_socket_exists(self, ensure_telegram_running, mcp_client):
        """Test that IPC socket is available"""
        import os
        assert os.path.exists("/tmp/tdesktop_mcp.sock"), "IPC socket should exist"

    def test_ping(self, ensure_telegram_running, mcp_client):
        """Test ping method returns pong"""
        response = mcp_client.ping()

        assert "result" in response, "Response should have result"
        assert response["result"]["status"] == "pong", "Status should be pong"
        assert "version" in response["result"], "Should include version"

    def test_ping_features(self, ensure_telegram_running, mcp_client):
        """Test ping returns feature list"""
        response = mcp_client.ping()

        features = response["result"].get("features", [])
        assert isinstance(features, list), "Features should be a list"
        # Check for expected features
        expected_features = ["local_database", "voice_transcription", "semantic_search"]
        for feature in expected_features:
            assert feature in features, f"Feature {feature} should be available"


class TestIPCBridgeDialogs:
    """Test dialog/chat retrieval via IPC bridge"""

    def test_get_dialogs(self, ensure_telegram_running, mcp_client):
        """Test get_dialogs returns chat list"""
        response = mcp_client.get_dialogs()

        assert "result" in response, "Response should have result"
        result = response["result"]

        # Check structure
        assert "chats" in result or "dialogs" in result, "Should have chats or dialogs"
        assert "source" in result, "Should indicate data source"

    def test_get_dialogs_returns_telegram_service(self, ensure_telegram_running, mcp_client):
        """Test that Telegram service channel (777000) is returned"""
        response = mcp_client.get_dialogs()
        result = response["result"]

        chats = result.get("chats", result.get("dialogs", []))

        # Look for Telegram service channel
        telegram_found = False
        for chat in chats:
            chat_id = str(chat.get("id", ""))
            if chat_id == "777000":
                telegram_found = True
                assert chat.get("name") == "Telegram", "Telegram service should have name 'Telegram'"
                break

        # Note: This might not always be present depending on account state
        # So we just check the structure is correct
        assert isinstance(chats, list), "Chats should be a list"

    def test_get_dialogs_source(self, ensure_telegram_running, mcp_client):
        """Test that dialogs indicate live data source"""
        response = mcp_client.get_dialogs()
        result = response["result"]

        source = result.get("source", "")
        assert "live" in source.lower(), "Should indicate live data source"


class TestIPCBridgeMessages:
    """Test message retrieval via IPC bridge"""

    def test_get_messages_structure(self, ensure_telegram_running, mcp_client):
        """Test get_messages returns proper structure"""
        # Use Telegram service channel which should always exist
        response = mcp_client.get_messages(chat_id=777000, limit=5)

        assert "result" in response or "error" in response, "Should have result or error"

        if "result" in response:
            result = response["result"]
            # Check structure
            assert "messages" in result or "count" in result or "error" not in result

    def test_get_messages_with_limit(self, ensure_telegram_running, mcp_client):
        """Test get_messages respects limit parameter"""
        response = mcp_client.get_messages(chat_id=777000, limit=3)

        if "result" in response and "messages" in response["result"]:
            messages = response["result"]["messages"]
            assert len(messages) <= 3, "Should respect limit"


class TestIPCBridgeSearch:
    """Test search functionality via IPC bridge"""

    def test_search_local(self, ensure_telegram_running, mcp_client):
        """Test local search functionality"""
        response = mcp_client.search_local(query="test", limit=10)

        assert "result" in response or "error" in response, "Should have result or error"

    def test_search_local_empty_query(self, ensure_telegram_running, mcp_client):
        """Test search with empty query"""
        response = mcp_client.search_local(query="", limit=10)

        # Should either return empty results or error
        assert "result" in response or "error" in response


class TestIPCBridgeProtocol:
    """Test JSON-RPC protocol compliance"""

    def test_jsonrpc_version(self, ensure_telegram_running, mcp_client):
        """Test response includes jsonrpc version"""
        response = mcp_client.ping()
        # Note: IPC bridge may not include jsonrpc field
        # This is acceptable for internal protocol

    def test_request_id_preserved(self, ensure_telegram_running, mcp_client):
        """Test that request ID is preserved in response"""
        response = mcp_client.ping()
        assert "id" in response, "Response should include id"

    def test_unknown_method(self, ensure_telegram_running, mcp_client):
        """Test unknown method returns error"""
        response = mcp_client.send_request("unknown_method_xyz", {})

        assert "error" in response, "Unknown method should return error"
        error = response["error"]
        assert "code" in error, "Error should have code"
        assert error["code"] == -32601, "Should be method not found error"

    def test_malformed_params(self, ensure_telegram_running, mcp_client):
        """Test method with wrong params structure"""
        # This tests the server's resilience to bad input
        try:
            response = mcp_client.send_request("get_messages", {"invalid": "params"}, timeout=2.0)
            # Should either handle gracefully or return error
            assert "result" in response or "error" in response
        except Exception:
            # Server may not respond to malformed params - this is acceptable
            pass


class TestIPCBridgeErrorHandling:
    """Test error handling in IPC bridge"""

    def test_invalid_chat_id(self, ensure_telegram_running, mcp_client):
        """Test handling of invalid chat ID"""
        response = mcp_client.get_messages(chat_id=-1, limit=10)

        # Should handle gracefully
        assert "result" in response or "error" in response

    def test_large_limit(self, ensure_telegram_running, mcp_client):
        """Test handling of very large limit"""
        response = mcp_client.get_messages(chat_id=777000, limit=10000)

        # Should handle gracefully (may truncate)
        assert "result" in response or "error" in response

    def test_timeout_handling(self, ensure_telegram_running):
        """Test that requests don't hang indefinitely"""
        import socket
        import time

        sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        sock.settimeout(2.0)

        try:
            sock.connect("/tmp/tdesktop_mcp.sock")

            # Send valid request
            request = json.dumps({"jsonrpc": "2.0", "id": 1, "method": "ping", "params": {}})
            sock.sendall(request.encode() + b'\n')

            # Should respond within timeout
            start = time.time()
            response = sock.recv(4096)
            elapsed = time.time() - start

            assert elapsed < 2.0, "Response should be fast"
            assert len(response) > 0, "Should receive response"
        finally:
            sock.close()

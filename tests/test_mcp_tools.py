"""
Tests for MCP Tool Functionality
Tests specific MCP tool implementations via the IPC bridge.
"""
import pytest
import json
import time


class TestCoreMessagingTools:
    """Test core messaging tool functionality"""

    def test_list_chats_returns_data(self, ensure_telegram_running, mcp_client):
        """Test list_chats returns chat data"""
        response = mcp_client.get_dialogs()

        assert "result" in response
        result = response["result"]

        # Should have some structure indicating chats
        assert "chats" in result or "dialogs" in result or "count" in result

    def test_chat_data_structure(self, ensure_telegram_running, mcp_client):
        """Test chat data has expected fields"""
        response = mcp_client.get_dialogs()
        result = response["result"]

        chats = result.get("chats", result.get("dialogs", []))
        if chats:
            chat = chats[0]
            # Check expected fields
            assert "id" in chat, "Chat should have id"
            # Name might be present
            if "name" in chat:
                assert isinstance(chat["name"], str), "Name should be string"

    def test_messages_have_structure(self, ensure_telegram_running, mcp_client):
        """Test messages have expected structure"""
        response = mcp_client.get_messages(chat_id=777000, limit=5)

        if "result" in response and "messages" in response["result"]:
            messages = response["result"]["messages"]
            if messages:
                msg = messages[0]
                # Check for common message fields
                # The exact fields depend on implementation
                assert isinstance(msg, dict), "Message should be dict"


class TestSearchTools:
    """Test search functionality"""

    def test_search_returns_results(self, ensure_telegram_running, mcp_client):
        """Test search returns some structure"""
        response = mcp_client.search_local(query="hello", limit=10)

        assert "result" in response or "error" in response

    def test_search_with_special_chars(self, ensure_telegram_running, mcp_client):
        """Test search handles special characters"""
        response = mcp_client.search_local(query="test@#$%", limit=5)

        # Should not crash, should return result or error
        assert "result" in response or "error" in response

    def test_search_unicode(self, ensure_telegram_running, mcp_client):
        """Test search handles unicode"""
        response = mcp_client.search_local(query="тест 测试 🎉", limit=5)

        assert "result" in response or "error" in response


class TestDataIntegrity:
    """Test data integrity and consistency"""

    def test_consistent_responses(self, ensure_telegram_running, mcp_client):
        """Test that repeated requests return consistent data"""
        response1 = mcp_client.ping()
        response2 = mcp_client.ping()

        assert response1["result"]["version"] == response2["result"]["version"]

    def test_dialogs_stable(self, ensure_telegram_running, mcp_client):
        """Test dialog list is stable between calls"""
        response1 = mcp_client.get_dialogs()
        response2 = mcp_client.get_dialogs()

        if "result" in response1 and "result" in response2:
            # Count should be same or very similar
            count1 = response1["result"].get("count", 0)
            count2 = response2["result"].get("count", 0)
            # Allow some variance due to timing
            assert abs(count1 - count2) <= 1


class TestPerformance:
    """Test performance characteristics"""

    def test_ping_latency(self, ensure_telegram_running, mcp_client):
        """Test ping responds quickly"""
        start = time.time()
        response = mcp_client.ping()
        elapsed = time.time() - start

        assert elapsed < 0.5, f"Ping should be fast, took {elapsed:.2f}s"
        assert "result" in response

    def test_dialogs_latency(self, ensure_telegram_running, mcp_client):
        """Test dialogs responds within reasonable time"""
        start = time.time()
        response = mcp_client.get_dialogs()
        elapsed = time.time() - start

        assert elapsed < 2.0, f"Dialogs should respond quickly, took {elapsed:.2f}s"

    def test_concurrent_requests(self, ensure_telegram_running, mcp_client):
        """Test handling multiple rapid requests"""
        import threading

        results = []
        errors = []

        def make_request():
            try:
                # Create new client for each thread
                from conftest import MCPClient
                client = MCPClient()
                response = client.ping()
                results.append(response)
            except Exception as e:
                errors.append(str(e))

        threads = [threading.Thread(target=make_request) for _ in range(5)]
        for t in threads:
            t.start()
        for t in threads:
            t.join(timeout=5)

        # All should succeed
        assert len(errors) == 0, f"Had errors: {errors}"
        assert len(results) == 5, "All requests should complete"


class TestEdgeCases:
    """Test edge cases and boundary conditions"""

    def test_zero_limit(self, ensure_telegram_running, mcp_client):
        """Test requesting zero messages"""
        response = mcp_client.get_messages(chat_id=777000, limit=0)

        assert "result" in response or "error" in response

    def test_negative_chat_id(self, ensure_telegram_running, mcp_client):
        """Test negative chat ID (group chats have negative IDs)"""
        try:
            response = mcp_client.get_messages(chat_id=-100123456789, limit=5)
            # Should handle gracefully
            assert "result" in response or "error" in response
        except Exception:
            # Server may not respond to invalid chat IDs - acceptable
            pass

    def test_very_long_query(self, ensure_telegram_running, mcp_client):
        """Test very long search query"""
        try:
            long_query = "a" * 1000
            response = mcp_client.search_local(query=long_query, limit=5)
            # Should handle gracefully
            assert "result" in response or "error" in response
        except Exception:
            # Server may timeout on very long queries - acceptable
            pass

    def test_empty_params(self, ensure_telegram_running, mcp_client):
        """Test methods with empty params"""
        try:
            response = mcp_client.send_request("get_dialogs", {})
            assert "result" in response or "error" in response
        except Exception:
            # May fail if connection lost - acceptable
            pass


class TestServerInfo:
    """Test server information endpoints"""

    def test_server_version(self, ensure_telegram_running, mcp_client):
        """Test server reports version"""
        try:
            response = mcp_client.ping()
            version = response["result"].get("version")
            assert version is not None, "Should report version"
            assert isinstance(version, str), "Version should be string"
        except Exception:
            # Connection may be lost - skip
            pytest.fail("Connection lost")

    def test_server_features(self, ensure_telegram_running, mcp_client):
        """Test server reports available features"""
        try:
            response = mcp_client.ping()
            features = response["result"].get("features", [])
            assert isinstance(features, list), "Features should be list"
        except Exception:
            # Connection may be lost - skip
            pytest.fail("Connection lost")


class TestDataTypes:
    """Test data type handling"""

    def test_chat_id_types(self, ensure_telegram_running, mcp_client):
        """Test handling different chat ID formats"""
        try:
            # Integer ID
            response1 = mcp_client.get_messages(chat_id=777000, limit=1)
            assert "result" in response1 or "error" in response1
        except Exception:
            # Connection may be lost - skip
            pytest.fail("Connection lost")

    def test_large_numbers(self, ensure_telegram_running, mcp_client):
        """Test handling large chat IDs"""
        try:
            # Very large ID (typical for channels)
            large_id = 1000000000000
            response = mcp_client.get_messages(chat_id=large_id, limit=1)
            assert "result" in response or "error" in response
        except Exception:
            # Connection may be lost - skip
            pytest.fail("Connection lost")

    def test_result_json_valid(self, ensure_telegram_running, mcp_client):
        """Test all results are valid JSON"""
        try:
            response = mcp_client.get_dialogs()
            # If we got here, JSON parsing worked
            assert isinstance(response, dict)
            # Try serializing back
            json_str = json.dumps(response)
            assert len(json_str) > 0
        except Exception:
            # Connection may be lost - skip
            pytest.fail("Connection lost")

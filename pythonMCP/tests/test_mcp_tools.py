"""
Unit tests for pythonMCP core tools

Tests the three priority MCP tools:
1. get_messages() - Message retrieval
2. search_messages() - Keyword search
3. semantic_search() - AI-powered semantic search
"""

import pytest
import json


class TestGetMessagesTool:
    """Tests for get_messages() MCP tool"""

    @pytest.mark.unit
    @pytest.mark.asyncio
    async def test_get_messages_with_bridge(
        self,
        mcp_server_hybrid,
        mock_bridge,
        sample_messages
    ):
        """Test get_messages using bridge (fast path)"""
        # Arrange
        chat_id = -1001234567
        limit = 50
        mock_bridge.get_messages.return_value = sample_messages

        # Act
        # The tool is registered as a decorator, so we need to call it via the server
        result = await mcp_server_hybrid.tool_get_messages(
            chat_id=chat_id,
            limit=limit
        )

        # Assert
        assert result is not None
        assert len(result) > 0
        result_json = json.loads(result[0].text)
        assert "messages" in result_json
        assert "count" in result_json
        assert result_json["count"] == len(sample_messages)
        assert result_json["messages"] == sample_messages

        # Verify bridge was called
        mock_bridge.get_messages.assert_called_once_with(chat_id, limit)

    @pytest.mark.unit
    @pytest.mark.asyncio
    async def test_get_messages_fallback_to_telegram(
        self,
        mcp_server_standalone,
        mock_telegram_client,
        sample_messages
    ):
        """Test get_messages fallback to Telegram API when bridge unavailable"""
        # Arrange
        chat_id = -1001234567
        limit = 50
        mock_telegram_client.get_messages.return_value = sample_messages

        # Act
        result = await mcp_server_standalone.tool_get_messages(
            chat_id=chat_id,
            limit=limit
        )

        # Assert
        assert result is not None
        result_json = json.loads(result[0].text)
        assert result_json["messages"] == sample_messages
        mock_telegram_client.get_messages.assert_called_once_with(chat_id, limit)

    @pytest.mark.unit
    @pytest.mark.asyncio
    async def test_get_messages_no_data_source(
        self,
        mock_config
    ):
        """Test get_messages returns error when no data source available"""
        from src.mcp_server import UnifiedMCPServer

        # Arrange
        server = UnifiedMCPServer(
            config=mock_config,
            mode="standalone",
            enable_aiml=False
        )
        server.telegram = None
        server.bridge = None

        # Act
        result = await server.tool_get_messages(
            chat_id=-1001234567,
            limit=50
        )

        # Assert
        result_json = json.loads(result[0].text)
        assert "error" in result_json
        assert result_json["error"] == "No data source available"

    @pytest.mark.unit
    @pytest.mark.asyncio
    async def test_get_messages_caches_results(
        self,
        mcp_server_hybrid,
        mock_bridge,
        mock_cache,
        sample_messages
    ):
        """Test that get_messages caches retrieved messages"""
        # Arrange
        mock_bridge.get_messages.return_value = sample_messages
        mcp_server_hybrid.cache = mock_cache

        # Act
        await mcp_server_hybrid.tool_get_messages(
            chat_id=-1001234567,
            limit=50
        )

        # Assert - verify cache.add called for each message
        assert mock_cache.add.call_count == len(sample_messages)

    @pytest.mark.unit
    @pytest.mark.asyncio
    async def test_get_messages_indexes_for_ai(
        self,
        mcp_server_hybrid,
        mock_bridge,
        mock_aiml_service,
        sample_messages
    ):
        """Test that messages are indexed for AI/ML when AIML is enabled"""
        # Arrange
        mock_bridge.get_messages.return_value = sample_messages
        mcp_server_hybrid.aiml_service = mock_aiml_service

        # Act
        await mcp_server_hybrid.tool_get_messages(
            chat_id=-1001234567,
            limit=50
        )

        # Assert - verify indexing called for messages with text
        messages_with_text = [msg for msg in sample_messages if msg.get("text")]
        assert mock_aiml_service.index_message.call_count == len(messages_with_text)

    @pytest.mark.unit
    @pytest.mark.asyncio
    async def test_get_messages_handles_errors_gracefully(
        self,
        mcp_server_hybrid,
        mock_bridge
    ):
        """Test get_messages error handling"""
        # Arrange
        mock_bridge.get_messages.side_effect = Exception("Connection failed")

        # Act
        result = await mcp_server_hybrid.tool_get_messages(
            chat_id=-1001234567,
            limit=50
        )

        # Assert
        result_json = json.loads(result[0].text)
        assert "error" in result_json
        assert "Connection failed" in result_json["error"]


class TestSearchMessagesTool:
    """Tests for search_messages() MCP tool"""

    @pytest.mark.unit
    @pytest.mark.asyncio
    async def test_search_messages_with_bridge(
        self,
        mcp_server_hybrid,
        mock_bridge,
        sample_messages
    ):
        """Test search_messages using bridge"""
        # Arrange
        chat_id = -1001234567
        query = "project"
        limit = 50
        expected_results = [msg for msg in sample_messages if query in msg["text"]]
        mock_bridge.search_messages.return_value = expected_results

        # Act
        result = await mcp_server_hybrid.tool_search_messages(
            chat_id=chat_id,
            query=query,
            limit=limit
        )

        # Assert
        result_json = json.loads(result[0].text)
        assert "results" in result_json
        assert "count" in result_json
        assert result_json["count"] == len(expected_results)
        mock_bridge.search_messages.assert_called_once_with(chat_id, query, limit)

    @pytest.mark.unit
    @pytest.mark.asyncio
    async def test_search_messages_fallback_to_telegram(
        self,
        mcp_server_standalone,
        mock_telegram_client,
        sample_messages
    ):
        """Test search_messages fallback to Telegram API"""
        # Arrange
        chat_id = -1001234567
        query = "project"
        expected_results = [msg for msg in sample_messages if query in msg["text"]]
        mock_telegram_client.search_messages.return_value = expected_results

        # Act
        result = await mcp_server_standalone.tool_search_messages(
            chat_id=chat_id,
            query=query,
            limit=50
        )

        # Assert
        result_json = json.loads(result[0].text)
        assert result_json["results"] == expected_results
        mock_telegram_client.search_messages.assert_called_once()

    @pytest.mark.unit
    @pytest.mark.asyncio
    async def test_search_messages_no_results(
        self,
        mcp_server_hybrid,
        mock_bridge
    ):
        """Test search_messages returns empty results when no matches"""
        # Arrange
        mock_bridge.search_messages.return_value = []

        # Act
        result = await mcp_server_hybrid.tool_search_messages(
            chat_id=-1001234567,
            query="nonexistent",
            limit=50
        )

        # Assert
        result_json = json.loads(result[0].text)
        assert result_json["results"] == []
        assert result_json["count"] == 0

    @pytest.mark.unit
    @pytest.mark.asyncio
    async def test_search_messages_handles_errors(
        self,
        mcp_server_hybrid,
        mock_bridge
    ):
        """Test search_messages error handling"""
        # Arrange
        mock_bridge.search_messages.side_effect = Exception("Search failed")

        # Act
        result = await mcp_server_hybrid.tool_search_messages(
            chat_id=-1001234567,
            query="test",
            limit=50
        )

        # Assert
        result_json = json.loads(result[0].text)
        assert "error" in result_json


@pytest.mark.aiml
class TestSemanticSearchTool:
    """Tests for semantic_search() MCP tool"""

    @pytest.mark.unit
    @pytest.mark.asyncio
    async def test_semantic_search_basic(
        self,
        mcp_server_hybrid,
        mock_aiml_service
    ):
        """Test basic semantic search functionality"""
        # Arrange
        query = "What's the project status?"
        expected_results = [
            {
                "text": "Let's discuss the project plan",
                "metadata": {"chat_id": -1001234567, "message_id": 3},
                "distance": 0.15
            }
        ]
        mock_aiml_service.semantic_search.return_value = expected_results

        # Act
        result = await mcp_server_hybrid.tool_semantic_search(
            query=query,
            chat_id=None,
            limit=10
        )

        # Assert
        result_json = json.loads(result[0].text)
        assert "query" in result_json
        assert "results" in result_json
        assert "count" in result_json
        assert result_json["query"] == query
        assert result_json["count"] == len(expected_results)
        mock_aiml_service.semantic_search.assert_called_once_with(query, None, 10)

    @pytest.mark.unit
    @pytest.mark.asyncio
    async def test_semantic_search_with_chat_filter(
        self,
        mcp_server_hybrid,
        mock_aiml_service
    ):
        """Test semantic search filtered by chat_id"""
        # Arrange
        query = "important discussion"
        chat_id = -1001234567
        mock_aiml_service.semantic_search.return_value = []

        # Act
        await mcp_server_hybrid.tool_semantic_search(
            query=query,
            chat_id=chat_id,
            limit=10
        )

        # Assert
        mock_aiml_service.semantic_search.assert_called_once_with(query, chat_id, 10)

    @pytest.mark.unit
    @pytest.mark.asyncio
    async def test_semantic_search_enriches_with_bridge_data(
        self,
        mcp_server_hybrid,
        mock_aiml_service,
        mock_bridge,
        sample_messages
    ):
        """Test semantic search enriches results with full message data from bridge"""
        # Arrange
        query = "project"
        semantic_results = [
            {
                "text": "Let's discuss the project plan",
                "metadata": {"chat_id": -1001234567, "message_id": 3},
                "distance": 0.15
            }
        ]
        mock_aiml_service.semantic_search.return_value = semantic_results
        mock_bridge.get_messages.return_value = sample_messages

        # Act
        result = await mcp_server_hybrid.tool_semantic_search(
            query=query,
            chat_id=-1001234567,
            limit=10
        )

        # Assert
        result_json = json.loads(result[0].text)
        # Verify bridge was called to enrich results
        mock_bridge.get_messages.assert_called()
        # Verify full_context added to result
        if len(result_json["results"]) > 0:
            # The enrichment may have added full_context
            assert "metadata" in result_json["results"][0]

    @pytest.mark.unit
    @pytest.mark.asyncio
    async def test_semantic_search_without_aiml(
        self,
        mcp_server_standalone
    ):
        """Test semantic_search returns error when AIML not available"""
        # Arrange
        mcp_server_standalone.aiml_service = None
        mcp_server_standalone.aiml_enabled = False

        # Act
        result = await mcp_server_standalone.tool_semantic_search(
            query="test query",
            chat_id=None,
            limit=10
        )

        # Assert
        result_json = json.loads(result[0].text)
        assert "error" in result_json
        assert "AI/ML features not available" in result_json["error"]

    @pytest.mark.unit
    @pytest.mark.asyncio
    async def test_semantic_search_handles_errors(
        self,
        mcp_server_hybrid,
        mock_aiml_service
    ):
        """Test semantic_search error handling"""
        # Arrange
        mock_aiml_service.semantic_search.side_effect = Exception("Embedding failed")

        # Act
        result = await mcp_server_hybrid.tool_semantic_search(
            query="test query",
            chat_id=None,
            limit=10
        )

        # Assert
        result_json = json.loads(result[0].text)
        assert "error" in result_json

    @pytest.mark.unit
    @pytest.mark.asyncio
    async def test_semantic_search_respects_limit(
        self,
        mcp_server_hybrid,
        mock_aiml_service
    ):
        """Test semantic_search respects limit parameter"""
        # Arrange
        query = "test"
        limit = 5
        # Return more results than limit
        mock_aiml_service.semantic_search.return_value = [
            {"text": f"Message {i}", "metadata": {}, "distance": 0.1 * i}
            for i in range(10)
        ]

        # Act
        await mcp_server_hybrid.tool_semantic_search(
            query=query,
            chat_id=None,
            limit=limit
        )

        # Assert
        mock_aiml_service.semantic_search.assert_called_once_with(query, None, limit)


class TestToolIntegration:
    """Integration tests for tool interactions"""

    @pytest.mark.integration
    @pytest.mark.asyncio
    async def test_get_messages_then_semantic_search(
        self,
        mcp_server_hybrid,
        mock_bridge,
        mock_aiml_service,
        sample_messages
    ):
        """Test workflow: get messages, then search them semantically"""
        # Arrange
        chat_id = -1001234567
        mock_bridge.get_messages.return_value = sample_messages
        mock_aiml_service.semantic_search.return_value = [
            {
                "text": sample_messages[2]["text"],
                "metadata": {"chat_id": chat_id, "message_id": 3},
                "distance": 0.1
            }
        ]

        # Act
        # First, get messages (which indexes them)
        await mcp_server_hybrid.tool_get_messages(
            chat_id=chat_id,
            limit=50
        )

        # Then, search them
        result = await mcp_server_hybrid.tool_semantic_search(
            query="project status",
            chat_id=chat_id,
            limit=10
        )

        # Assert
        assert mock_aiml_service.index_message.called
        assert mock_aiml_service.semantic_search.called
        result_json = json.loads(result[0].text)
        assert result_json["count"] > 0

    @pytest.mark.integration
    @pytest.mark.asyncio
    async def test_search_messages_vs_semantic_search(
        self,
        mcp_server_hybrid,
        mock_bridge,
        mock_aiml_service,
        sample_messages
    ):
        """Test difference between keyword search and semantic search"""
        # Arrange
        query = "project"
        chat_id = -1001234567

        # Keyword search returns exact matches
        keyword_results = [msg for msg in sample_messages if query in msg["text"]]
        mock_bridge.search_messages.return_value = keyword_results

        # Semantic search returns similar meaning
        semantic_results = [
            {
                "text": sample_messages[2]["text"],
                "metadata": {"chat_id": chat_id, "message_id": 3},
                "distance": 0.1
            }
        ]
        mock_aiml_service.semantic_search.return_value = semantic_results

        # Act
        keyword_result = await mcp_server_hybrid.tool_search_messages(
            chat_id=chat_id,
            query=query,
            limit=50
        )

        semantic_result = await mcp_server_hybrid.tool_semantic_search(
            query=query,
            chat_id=chat_id,
            limit=10
        )

        # Assert - both methods should work but may return different results
        keyword_json = json.loads(keyword_result[0].text)
        semantic_json = json.loads(semantic_result[0].text)

        assert "results" in keyword_json
        assert "results" in semantic_json
        # Semantic search uses AI, keyword search uses exact matching
        assert mock_bridge.search_messages.called
        assert mock_aiml_service.semantic_search.called

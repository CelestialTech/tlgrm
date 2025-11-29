"""
Pytest configuration and shared fixtures for pythonMCP tests
"""

import asyncio
import pytest
from pathlib import Path
from unittest.mock import Mock, AsyncMock
from typing import Dict, List, Any

# Test data fixtures
@pytest.fixture
def sample_messages() -> List[Dict[str, Any]]:
    """Sample Telegram messages for testing"""
    return [
        {
            "message_id": 1,
            "chat_id": -1001234567,
            "text": "Hello, how are you?",
            "date": "2025-11-26T10:00:00",
            "from_user": {"id": 12345, "username": "testuser"}
        },
        {
            "message_id": 2,
            "chat_id": -1001234567,
            "text": "I'm fine, thanks!",
            "date": "2025-11-26T10:01:00",
            "from_user": {"id": 67890, "username": "anotheruser"}
        },
        {
            "message_id": 3,
            "chat_id": -1001234567,
            "text": "Let's discuss the project plan",
            "date": "2025-11-26T10:02:00",
            "from_user": {"id": 12345, "username": "testuser"}
        }
    ]


@pytest.fixture
def sample_chats() -> List[Dict[str, Any]]:
    """Sample chat list for testing"""
    return [
        {
            "id": -1001234567,
            "title": "Test Group",
            "type": "group",
            "member_count": 42
        },
        {
            "id": 12345,
            "title": "Test User",
            "type": "user",
            "username": "testuser"
        },
        {
            "id": -100987654,
            "title": "Another Channel",
            "type": "channel",
            "member_count": 1500
        }
    ]


# Mock fixtures
@pytest.fixture
def mock_telegram_client():
    """Mock TelegramClient for testing without real API calls"""
    client = AsyncMock()
    client.get_messages = AsyncMock(return_value=[])
    client.search_messages = AsyncMock(return_value=[])
    client.list_chats = AsyncMock(return_value=[])
    client.send_message = AsyncMock(return_value={"success": True})
    return client


@pytest.fixture
def mock_bridge():
    """Mock TDesktopBridge for testing without C++ backend"""
    bridge = AsyncMock()
    bridge.ping = AsyncMock(return_value=True)
    bridge.get_messages = AsyncMock(return_value=[])
    bridge.search_messages = AsyncMock(return_value=[])
    bridge.list_archived_chats = AsyncMock(return_value=[])
    bridge.get_dialogs = AsyncMock(return_value=[])
    # Message tags
    bridge.remove_message_tag = AsyncMock(return_value={"success": True})
    bridge.tag_message = AsyncMock(return_value={"success": True})
    bridge.get_message_tags = AsyncMock(return_value=[])
    bridge.list_tags = AsyncMock(return_value=[])
    bridge.get_tagged_messages = AsyncMock(return_value=[])
    # Translation
    bridge.configure_auto_translate = AsyncMock(return_value={"success": True})
    bridge.translate_message = AsyncMock(return_value={"original": "test", "translated": "test"})
    bridge.get_translation_languages = AsyncMock(return_value=[])
    # Transcription
    bridge.get_transcription_status = AsyncMock(return_value={"status": "completed", "text": "transcribed text"})
    bridge.transcribe_voice_message = AsyncMock(return_value={"text": "transcribed text"})
    bridge.get_transcription_history = AsyncMock(return_value=[])
    # Batch operations
    bridge.batch_operation = AsyncMock(return_value={"success": True, "results": []})
    bridge.batch_forward_messages = AsyncMock(return_value={"success": True})
    bridge.batch_delete_messages = AsyncMock(return_value={"success": True})
    # Scheduling
    bridge.schedule_message = AsyncMock(return_value={"success": True, "message_id": 123})
    bridge.list_scheduled_messages = AsyncMock(return_value=[])
    bridge.cancel_scheduled_message = AsyncMock(return_value={"success": True})
    return bridge


@pytest.fixture
def mock_aiml_service():
    """Mock AI/ML service for testing without heavy models"""
    service = AsyncMock()
    service.embed_text = AsyncMock(return_value=[0.1] * 384)  # Mock embedding
    service.semantic_search = AsyncMock(return_value=[])
    service.classify_intent = AsyncMock(return_value={
        "intent": "question",
        "confidence": 0.85
    })
    service.summarize_conversation = AsyncMock(return_value="Test summary")
    return service


@pytest.fixture
def mock_config():
    """Mock Config object with test settings"""
    config = Mock()

    # MCP config
    config.mcp = Mock()
    config.mcp.server_name = "test-mcp-server"
    config.mcp.version = "1.0.0"

    # Telegram config
    config.telegram = Mock()
    config.telegram.api_id = 2040
    config.telegram.api_hash = "test_hash"
    config.telegram.session_name = "test_session"

    # Cache config
    config.cache = Mock()
    config.cache.max_messages = 1000
    config.cache.ttl_seconds = 3600
    config.cache.use_sqlite = False

    # AIML config
    config.aiml = Mock()
    config.aiml.device = "cpu"
    config.aiml.embedding_model = "sentence-transformers/all-MiniLM-L6-v2"
    config.aiml.vector_db_path = "./test_data/chromadb"

    # Monitoring config - DISABLED for tests to avoid Prometheus registry collisions
    config.monitoring = Mock()
    config.monitoring.enable_metrics = False
    config.monitoring.enable_prometheus = False
    config.monitoring.enable_health_check = False
    config.monitoring.metrics_port = 9090
    config.monitoring.health_check_port = 8080

    return config


@pytest.fixture
def mock_cache():
    """Mock MessageCache for testing"""
    cache = Mock()
    cache.get = Mock(return_value=None)
    cache.add = Mock()
    cache.clear = Mock()
    cache.size = Mock(return_value=0)
    return cache


# MCP Server fixtures
@pytest.fixture
async def mcp_server_standalone(mock_config, mock_telegram_client, mock_cache):
    """Create MCP server in standalone mode with mocked dependencies"""
    from src.mcp_server import UnifiedMCPServer

    server = UnifiedMCPServer(
        config=mock_config,
        mode="standalone",
        enable_aiml=False
    )
    server.telegram = mock_telegram_client
    server.cache = mock_cache

    return server


@pytest.fixture
async def mcp_server_hybrid(mock_config, mock_telegram_client, mock_bridge, mock_cache, mock_aiml_service):
    """Create MCP server in hybrid mode with all mocked dependencies"""
    from src.mcp_server import UnifiedMCPServer

    server = UnifiedMCPServer(
        config=mock_config,
        mode="hybrid",
        enable_aiml=True
    )
    server.telegram = mock_telegram_client
    server.bridge = mock_bridge
    server.cache = mock_cache
    server.aiml_service = mock_aiml_service
    server.aiml_enabled = True

    return server


# Async event loop fixtures
@pytest.fixture(scope="session")
def event_loop():
    """Create event loop for async tests"""
    loop = asyncio.get_event_loop_policy().new_event_loop()
    yield loop
    loop.close()


# Path fixtures
@pytest.fixture
def test_data_dir(tmp_path) -> Path:
    """Temporary directory for test data"""
    test_dir = tmp_path / "test_data"
    test_dir.mkdir()
    return test_dir


@pytest.fixture
def test_config_file(test_data_dir) -> Path:
    """Create a test configuration file"""
    config_path = test_data_dir / "test_config.toml"
    config_content = """
[telegram]
api_id = 2040
api_hash = "test_hash"
session_name = "test_session"

[mcp]
server_name = "test-mcp-server"
version = "1.0.0"

[cache]
max_messages = 1000
ttl_seconds = 3600
"""
    config_path.write_text(config_content)
    return config_path

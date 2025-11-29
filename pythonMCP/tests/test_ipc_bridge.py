"""
Tests for src/ipc_bridge.py - TDesktopBridge

Tests the IPC bridge that communicates with the C++ tdesktop MCP server
via Unix domain socket.
"""

import json
import pytest
import socket
from unittest.mock import patch, MagicMock

from src.ipc_bridge import (
    TDesktopBridge,
    TDesktopBridgeError,
    get_tdesktop_bridge,
    initialize_tdesktop_bridge,
)


# Fixtures

@pytest.fixture
def mock_socket():
    """Mock socket for testing without real IPC"""
    mock_sock = MagicMock(spec=socket.socket)
    mock_sock.recv.return_value = b'{"result": {"status": "ok"}}\n'
    return mock_sock


@pytest.fixture
def bridge():
    """Create a TDesktopBridge instance"""
    return TDesktopBridge(socket_path="/tmp/test_socket.sock")


# Basic Initialization Tests

def test_bridge_initialization():
    """Test TDesktopBridge initialization"""
    bridge = TDesktopBridge(socket_path="/custom/path.sock")

    assert bridge.socket_path == "/custom/path.sock"
    assert bridge._request_id == 0


def test_bridge_default_socket_path():
    """Test default socket path"""
    bridge = TDesktopBridge()

    assert bridge.socket_path == "/tmp/tdesktop_mcp.sock"


# Request ID Tracking Tests

@pytest.mark.asyncio
async def test_request_id_increments(bridge, mock_socket):
    """Test that request IDs increment with each call"""
    with patch('socket.socket', return_value=mock_socket), \
         patch('pathlib.Path.exists', return_value=True):

        # First call - should be ID 1
        await bridge.call_method("test_method")
        assert bridge._request_id == 1

        # Second call - should be ID 2
        await bridge.call_method("another_method")
        assert bridge._request_id == 2

        # Third call - should be ID 3
        await bridge.call_method("yet_another")
        assert bridge._request_id == 3


# Socket Communication Tests

@pytest.mark.asyncio
async def test_call_method_sends_correct_json(bridge, mock_socket):
    """Test that call_method sends correctly formatted JSON-RPC request"""
    with patch('socket.socket', return_value=mock_socket), \
         patch('pathlib.Path.exists', return_value=True):

        await bridge.call_method("test_method", {"param1": "value1"})

        # Verify sendall was called
        mock_socket.sendall.assert_called_once()

        # Extract and verify the sent data
        sent_data = mock_socket.sendall.call_args[0][0]
        sent_json = json.loads(sent_data.decode().strip())

        assert sent_json["id"] == 1
        assert sent_json["method"] == "test_method"
        assert sent_json["params"] == {"param1": "value1"}


@pytest.mark.asyncio
async def test_call_method_with_no_params(bridge, mock_socket):
    """Test call_method with no parameters (should send empty dict)"""
    with patch('socket.socket', return_value=mock_socket), \
         patch('pathlib.Path.exists', return_value=True):

        await bridge.call_method("ping")

        sent_data = mock_socket.sendall.call_args[0][0]
        sent_json = json.loads(sent_data.decode().strip())

        assert sent_json["params"] == {}


@pytest.mark.asyncio
async def test_call_method_parses_response(bridge, mock_socket):
    """Test that call_method correctly parses JSON response"""
    mock_socket.recv.return_value = b'{"result": {"status": "success", "data": [1, 2, 3]}}\n'

    with patch('socket.socket', return_value=mock_socket), \
         patch('pathlib.Path.exists', return_value=True):

        result = await bridge.call_method("test")

        assert result == {"status": "success", "data": [1, 2, 3]}


@pytest.mark.asyncio
async def test_call_method_receives_chunks(bridge):
    """Test that call_method can receive response in multiple chunks"""
    mock_sock = MagicMock(spec=socket.socket)

    # Simulate chunked response
    response_chunks = [
        b'{"result": {"status": ',
        b'"ok", "data": ',
        b'[1, 2, 3]}}\n'
    ]
    mock_sock.recv.side_effect = response_chunks + [b'']

    with patch('socket.socket', return_value=mock_sock), \
         patch('pathlib.Path.exists', return_value=True):

        result = await bridge.call_method("test")

        assert result == {"status": "ok", "data": [1, 2, 3]}


@pytest.mark.asyncio
async def test_socket_timeout_configured(bridge, mock_socket):
    """Test that socket timeout is set to 10 seconds"""
    with patch('socket.socket', return_value=mock_socket), \
         patch('pathlib.Path.exists', return_value=True):

        await bridge.call_method("test")

        mock_socket.settimeout.assert_called_once_with(10.0)


@pytest.mark.asyncio
async def test_socket_closed_after_call(bridge, mock_socket):
    """Test that socket is properly closed after call"""
    with patch('socket.socket', return_value=mock_socket), \
         patch('pathlib.Path.exists', return_value=True):

        await bridge.call_method("test")

        mock_socket.close.assert_called_once()


# Error Handling Tests

@pytest.mark.asyncio
async def test_socket_not_found_error(bridge, mock_socket):
    """Test error when socket file doesn't exist"""
    with patch('socket.socket', return_value=mock_socket), \
         patch('pathlib.Path.exists', return_value=False):

        with pytest.raises(TDesktopBridgeError) as exc_info:
            await bridge.call_method("test")

        assert "Bridge socket not found" in str(exc_info.value)
        assert "/tmp/test_socket.sock" in str(exc_info.value)
        assert "Is modified Telegram Desktop running?" in str(exc_info.value)


@pytest.mark.asyncio
async def test_socket_timeout_error(bridge, mock_socket):
    """Test handling of socket timeout"""
    mock_socket.connect.side_effect = socket.timeout()

    with patch('socket.socket', return_value=mock_socket), \
         patch('pathlib.Path.exists', return_value=True):

        with pytest.raises(TDesktopBridgeError) as exc_info:
            await bridge.call_method("test")

        assert "Timeout connecting to bridge" in str(exc_info.value)


@pytest.mark.asyncio
async def test_socket_error(bridge, mock_socket):
    """Test handling of socket errors"""
    mock_socket.connect.side_effect = socket.error("Connection refused")

    with patch('socket.socket', return_value=mock_socket), \
         patch('pathlib.Path.exists', return_value=True):

        with pytest.raises(TDesktopBridgeError) as exc_info:
            await bridge.call_method("test")

        assert "Socket error" in str(exc_info.value)
        assert "Is modified Telegram Desktop running?" in str(exc_info.value)


@pytest.mark.asyncio
async def test_invalid_json_response(bridge, mock_socket):
    """Test handling of invalid JSON in response"""
    mock_socket.recv.return_value = b'invalid json{{}}\n'

    with patch('socket.socket', return_value=mock_socket), \
         patch('pathlib.Path.exists', return_value=True):

        with pytest.raises(TDesktopBridgeError) as exc_info:
            await bridge.call_method("test")

        assert "Invalid JSON response" in str(exc_info.value)


@pytest.mark.asyncio
async def test_bridge_error_in_response(bridge, mock_socket):
    """Test handling of error response from bridge"""
    error_response = {
        "error": {
            "code": -32600,
            "message": "Invalid request"
        }
    }
    mock_socket.recv.return_value = (json.dumps(error_response) + '\n').encode()

    with patch('socket.socket', return_value=mock_socket), \
         patch('pathlib.Path.exists', return_value=True):

        with pytest.raises(TDesktopBridgeError) as exc_info:
            await bridge.call_method("test")

        assert "Bridge error" in str(exc_info.value)
        assert "Invalid request" in str(exc_info.value)
        assert "-32600" in str(exc_info.value)


# is_available() Tests

@pytest.mark.asyncio
async def test_is_available_when_bridge_responds(bridge, mock_socket):
    """Test is_available returns True when bridge responds to ping"""
    mock_socket.recv.return_value = b'{"result": {"status": "pong"}}\n'

    with patch('socket.socket', return_value=mock_socket), \
         patch('pathlib.Path.exists', return_value=True):

        available = await bridge.is_available()

        assert available is True


@pytest.mark.asyncio
async def test_is_available_when_bridge_not_running(bridge, mock_socket):
    """Test is_available returns False when bridge is not running"""
    mock_socket.connect.side_effect = socket.error("Connection refused")

    with patch('socket.socket', return_value=mock_socket), \
         patch('pathlib.Path.exists', return_value=True):

        available = await bridge.is_available()

        assert available is False


@pytest.mark.asyncio
async def test_is_available_when_socket_missing(bridge, mock_socket):
    """Test is_available returns False when socket file doesn't exist"""
    with patch('socket.socket', return_value=mock_socket), \
         patch('pathlib.Path.exists', return_value=False):

        available = await bridge.is_available()

        assert available is False


@pytest.mark.asyncio
async def test_is_available_wrong_status(bridge, mock_socket):
    """Test is_available returns False when status is not 'pong'"""
    mock_socket.recv.return_value = b'{"result": {"status": "error"}}\n'

    with patch('socket.socket', return_value=mock_socket), \
         patch('pathlib.Path.exists', return_value=True):

        available = await bridge.is_available()

        assert available is False


# Method Tests

@pytest.mark.asyncio
async def test_ping(bridge, mock_socket):
    """Test ping method"""
    mock_socket.recv.return_value = b'{"result": {"status": "pong", "version": "1.0"}}\n'

    with patch('socket.socket', return_value=mock_socket), \
         patch('pathlib.Path.exists', return_value=True):

        result = await bridge.ping()

        assert result["status"] == "pong"
        assert result["version"] == "1.0"


@pytest.mark.asyncio
async def test_get_messages_local(bridge, mock_socket):
    """Test get_messages_local method"""
    messages = [
        {"id": 1, "text": "Hello"},
        {"id": 2, "text": "World"}
    ]
    response = {"result": {"messages": messages}}
    mock_socket.recv.return_value = (json.dumps(response) + '\n').encode()

    with patch('socket.socket', return_value=mock_socket), \
         patch('pathlib.Path.exists', return_value=True):

        result = await bridge.get_messages_local(
            chat_id=-1001234567,
            limit=50,
            offset=0
        )

        assert result == messages

        # Verify correct parameters were sent
        sent_data = mock_socket.sendall.call_args[0][0]
        sent_json = json.loads(sent_data.decode().strip())

        assert sent_json["method"] == "get_messages"
        assert sent_json["params"]["chat_id"] == -1001234567
        assert sent_json["params"]["limit"] == 50
        assert sent_json["params"]["offset"] == 0


@pytest.mark.asyncio
async def test_get_messages_local_default_params(bridge, mock_socket):
    """Test get_messages_local with default parameters"""
    response = {"result": {"messages": []}}
    mock_socket.recv.return_value = (json.dumps(response) + '\n').encode()

    with patch('socket.socket', return_value=mock_socket), \
         patch('pathlib.Path.exists', return_value=True):

        await bridge.get_messages_local(chat_id=12345)

        sent_data = mock_socket.sendall.call_args[0][0]
        sent_json = json.loads(sent_data.decode().strip())

        assert sent_json["params"]["limit"] == 50
        assert sent_json["params"]["offset"] == 0


@pytest.mark.asyncio
async def test_search_local(bridge, mock_socket):
    """Test search_local method"""
    results = [{"id": 1, "text": "Found message"}]
    response = {"result": {"results": results}}
    mock_socket.recv.return_value = (json.dumps(response) + '\n').encode()

    with patch('socket.socket', return_value=mock_socket), \
         patch('pathlib.Path.exists', return_value=True):

        result = await bridge.search_local(
            query="test query",
            chat_id=-1001234567,
            limit=20
        )

        assert result == results

        sent_data = mock_socket.sendall.call_args[0][0]
        sent_json = json.loads(sent_data.decode().strip())

        assert sent_json["method"] == "search_local"
        assert sent_json["params"]["query"] == "test query"
        assert sent_json["params"]["chat_id"] == -1001234567
        assert sent_json["params"]["limit"] == 20


@pytest.mark.asyncio
async def test_search_local_without_chat_id(bridge, mock_socket):
    """Test search_local without chat_id (search all chats)"""
    response = {"result": {"results": []}}
    mock_socket.recv.return_value = (json.dumps(response) + '\n').encode()

    with patch('socket.socket', return_value=mock_socket), \
         patch('pathlib.Path.exists', return_value=True):

        await bridge.search_local(query="test", limit=10)

        sent_data = mock_socket.sendall.call_args[0][0]
        sent_json = json.loads(sent_data.decode().strip())

        # chat_id should not be in params
        assert "chat_id" not in sent_json["params"]
        assert sent_json["params"]["query"] == "test"
        assert sent_json["params"]["limit"] == 10


@pytest.mark.asyncio
async def test_get_dialogs(bridge, mock_socket):
    """Test get_dialogs method"""
    dialogs = [
        {"id": 1, "title": "Chat 1"},
        {"id": 2, "title": "Chat 2"}
    ]
    response = {"result": {"dialogs": dialogs}}
    mock_socket.recv.return_value = (json.dumps(response) + '\n').encode()

    with patch('socket.socket', return_value=mock_socket), \
         patch('pathlib.Path.exists', return_value=True):

        result = await bridge.get_dialogs(limit=100, offset=0)

        assert result == dialogs

        sent_data = mock_socket.sendall.call_args[0][0]
        sent_json = json.loads(sent_data.decode().strip())

        assert sent_json["method"] == "get_dialogs"
        assert sent_json["params"]["limit"] == 100
        assert sent_json["params"]["offset"] == 0


@pytest.mark.asyncio
async def test_transcribe_voice(bridge, mock_socket):
    """Test transcribe_voice method"""
    response = {"result": {"text": "Transcribed text here"}}
    mock_socket.recv.return_value = (json.dumps(response) + '\n').encode()

    with patch('socket.socket', return_value=mock_socket), \
         patch('pathlib.Path.exists', return_value=True):

        result = await bridge.transcribe_voice(file_path="/path/to/audio.ogg")

        assert result == "Transcribed text here"

        sent_data = mock_socket.sendall.call_args[0][0]
        sent_json = json.loads(sent_data.decode().strip())

        assert sent_json["method"] == "transcribe_voice"
        assert sent_json["params"]["file_path"] == "/path/to/audio.ogg"


@pytest.mark.asyncio
async def test_transcribe_voice_empty_result(bridge, mock_socket):
    """Test transcribe_voice with empty/missing text"""
    response = {"result": {}}
    mock_socket.recv.return_value = (json.dumps(response) + '\n').encode()

    with patch('socket.socket', return_value=mock_socket), \
         patch('pathlib.Path.exists', return_value=True):

        result = await bridge.transcribe_voice(file_path="/path/to/audio.ogg")

        assert result == ""


@pytest.mark.asyncio
async def test_semantic_search(bridge, mock_socket):
    """Test semantic_search method"""
    results = [
        {"id": 1, "text": "Similar message 1", "score": 0.95},
        {"id": 2, "text": "Similar message 2", "score": 0.87}
    ]
    response = {"result": {"results": results}}
    mock_socket.recv.return_value = (json.dumps(response) + '\n').encode()

    with patch('socket.socket', return_value=mock_socket), \
         patch('pathlib.Path.exists', return_value=True):

        result = await bridge.semantic_search(query="find similar", limit=10)

        assert result == results

        sent_data = mock_socket.sendall.call_args[0][0]
        sent_json = json.loads(sent_data.decode().strip())

        assert sent_json["method"] == "semantic_search"
        assert sent_json["params"]["query"] == "find similar"
        assert sent_json["params"]["limit"] == 10


@pytest.mark.asyncio
async def test_extract_media_text(bridge, mock_socket):
    """Test extract_media_text method"""
    response = {"result": {"text": "Extracted text from image"}}
    mock_socket.recv.return_value = (json.dumps(response) + '\n').encode()

    with patch('socket.socket', return_value=mock_socket), \
         patch('pathlib.Path.exists', return_value=True):

        result = await bridge.extract_media_text(
            file_path="/path/to/image.png",
            media_type="image"
        )

        assert result == "Extracted text from image"

        sent_data = mock_socket.sendall.call_args[0][0]
        sent_json = json.loads(sent_data.decode().strip())

        assert sent_json["method"] == "extract_media_text"
        assert sent_json["params"]["file_path"] == "/path/to/image.png"
        assert sent_json["params"]["media_type"] == "image"


@pytest.mark.asyncio
async def test_extract_media_text_default_type(bridge, mock_socket):
    """Test extract_media_text with default media_type"""
    response = {"result": {"text": ""}}
    mock_socket.recv.return_value = (json.dumps(response) + '\n').encode()

    with patch('socket.socket', return_value=mock_socket), \
         patch('pathlib.Path.exists', return_value=True):

        await bridge.extract_media_text(file_path="/path/to/file.pdf")

        sent_data = mock_socket.sendall.call_args[0][0]
        sent_json = json.loads(sent_data.decode().strip())

        assert sent_json["params"]["media_type"] == "auto"


# Statistics Tests

def test_get_stats(bridge):
    """Test get_stats method"""
    # Make a few calls to increment request_id
    bridge._request_id = 5

    stats = bridge.get_stats()

    assert stats["socket_path"] == "/tmp/test_socket.sock"
    assert stats["request_count"] == 5


def test_get_stats_initial_state():
    """Test get_stats in initial state"""
    bridge = TDesktopBridge()

    stats = bridge.get_stats()

    assert stats["socket_path"] == "/tmp/tdesktop_mcp.sock"
    assert stats["request_count"] == 0


# Global Instance Tests

def test_get_tdesktop_bridge_none():
    """Test get_tdesktop_bridge returns None initially"""
    # Reset global instance
    import src.ipc_bridge
    src.ipc_bridge._tdesktop_bridge = None

    bridge = get_tdesktop_bridge()
    assert bridge is None


def test_initialize_tdesktop_bridge():
    """Test initialize_tdesktop_bridge creates global instance"""
    bridge = initialize_tdesktop_bridge(socket_path="/custom/path.sock")

    assert bridge is not None
    assert bridge.socket_path == "/custom/path.sock"

    # Verify it's the global instance
    assert get_tdesktop_bridge() is bridge


def test_initialize_tdesktop_bridge_default_path():
    """Test initialize_tdesktop_bridge with default path"""
    bridge = initialize_tdesktop_bridge()

    assert bridge.socket_path == "/tmp/tdesktop_mcp.sock"


def test_initialize_tdesktop_bridge_replaces_instance():
    """Test that initialize_tdesktop_bridge replaces existing instance"""
    bridge1 = initialize_tdesktop_bridge(socket_path="/path1.sock")
    bridge2 = initialize_tdesktop_bridge(socket_path="/path2.sock")

    # Should be different instances
    assert bridge1 is not bridge2

    # Global instance should be the new one
    assert get_tdesktop_bridge() is bridge2
    assert get_tdesktop_bridge().socket_path == "/path2.sock"


# Edge Cases and Integration Tests

@pytest.mark.asyncio
async def test_empty_response_handling(bridge, mock_socket):
    """Test handling of empty responses"""
    mock_socket.recv.return_value = b'{"result": {}}\n'

    with patch('socket.socket', return_value=mock_socket), \
         patch('pathlib.Path.exists', return_value=True):

        result = await bridge.call_method("test")

        assert result == {}


@pytest.mark.asyncio
async def test_multiple_method_calls_same_bridge(bridge, mock_socket):
    """Test making multiple method calls with the same bridge instance"""
    with patch('socket.socket', return_value=mock_socket), \
         patch('pathlib.Path.exists', return_value=True):

        # Each call should increment request ID
        await bridge.ping()
        assert bridge._request_id == 1

        await bridge.get_dialogs()
        assert bridge._request_id == 2

        await bridge.search_local(query="test")
        assert bridge._request_id == 3


@pytest.mark.asyncio
async def test_call_method_with_complex_params(bridge, mock_socket):
    """Test call_method with complex nested parameters"""
    complex_params = {
        "chat_id": -1001234567,
        "filters": {
            "from_user": 12345,
            "date_range": {
                "start": "2025-01-01",
                "end": "2025-12-31"
            }
        },
        "tags": ["important", "work"]
    }

    with patch('socket.socket', return_value=mock_socket), \
         patch('pathlib.Path.exists', return_value=True):

        await bridge.call_method("complex_search", complex_params)

        sent_data = mock_socket.sendall.call_args[0][0]
        sent_json = json.loads(sent_data.decode().strip())

        # Verify complex structure is preserved
        assert sent_json["params"] == complex_params


@pytest.mark.asyncio
async def test_unicode_in_parameters(bridge, mock_socket):
    """Test handling of Unicode characters in parameters"""
    with patch('socket.socket', return_value=mock_socket), \
         patch('pathlib.Path.exists', return_value=True):

        await bridge.search_local(query="Привет мир 🌍")

        sent_data = mock_socket.sendall.call_args[0][0]
        sent_json = json.loads(sent_data.decode().strip())

        assert sent_json["params"]["query"] == "Привет мир 🌍"

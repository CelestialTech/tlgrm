"""
Unit tests for IPC Bridge MCP tools

Tests the new MCP tools that interact with C++ backend:
1. remove_message_tag() - Remove tags from messages
2. configure_auto_translate() - Configure auto-translation
3. get_transcription_status() - Check transcription status
"""

import pytest
import json


class TestRemoveMessageTagTool:
    """Tests for remove_message_tag() MCP tool"""

    @pytest.mark.unit
    @pytest.mark.asyncio
    async def test_remove_message_tag_success(
        self,
        mcp_server_hybrid,
        mock_bridge
    ):
        """Test successful tag removal"""
        # Arrange
        chat_id = -1001234567
        message_id = 123
        tag = "important"
        mock_bridge.remove_message_tag.return_value = {"success": True}

        # Act
        result = await mcp_server_hybrid.tool_remove_message_tag(
            chat_id=chat_id,
            message_id=message_id,
            tag=tag
        )

        # Assert
        assert result is not None
        assert len(result) > 0
        result_json = json.loads(result[0].text)
        assert result_json.get("success") is True

        # Verify bridge was called
        mock_bridge.remove_message_tag.assert_called_once_with(
            chat_id, message_id, tag
        )

    @pytest.mark.unit
    @pytest.mark.asyncio
    async def test_remove_message_tag_bridge_error(
        self,
        mcp_server_hybrid,
        mock_bridge
    ):
        """Test tag removal when bridge returns error"""
        # Arrange
        mock_bridge.remove_message_tag.side_effect = Exception("Tag not found")

        # Act
        result = await mcp_server_hybrid.tool_remove_message_tag(
            chat_id=-1001234567,
            message_id=123,
            tag="nonexistent"
        )

        # Assert
        result_json = json.loads(result[0].text)
        assert "error" in result_json
        assert "Tag not found" in result_json["error"]

    @pytest.mark.unit
    @pytest.mark.asyncio
    async def test_remove_message_tag_no_bridge(
        self,
        mock_config
    ):
        """Test tag removal when bridge is not available"""
        from src.mcp_server import UnifiedMCPServer

        # Arrange
        server = UnifiedMCPServer(
            config=mock_config,
            mode="standalone",
            enable_aiml=False
        )
        server.bridge = None

        # Act
        result = await server.tool_remove_message_tag(
            chat_id=-1001234567,
            message_id=123,
            tag="test"
        )

        # Assert
        result_json = json.loads(result[0].text)
        assert "error" in result_json
        assert "Bridge not available" in result_json["error"]


class TestConfigureAutoTranslateTool:
    """Tests for configure_auto_translate() MCP tool"""

    @pytest.mark.unit
    @pytest.mark.asyncio
    async def test_configure_auto_translate_enable(
        self,
        mcp_server_hybrid,
        mock_bridge
    ):
        """Test enabling auto-translation"""
        # Arrange
        chat_id = -1001234567
        enabled = True
        target_language = "en"
        mock_bridge.configure_auto_translate.return_value = {"success": True}

        # Act
        result = await mcp_server_hybrid.tool_configure_auto_translate(
            chat_id=chat_id,
            enabled=enabled,
            target_language=target_language
        )

        # Assert
        assert result is not None
        result_json = json.loads(result[0].text)
        assert result_json.get("success") is True

        # Verify bridge was called
        mock_bridge.configure_auto_translate.assert_called_once_with(
            chat_id, enabled, target_language
        )

    @pytest.mark.unit
    @pytest.mark.asyncio
    async def test_configure_auto_translate_disable(
        self,
        mcp_server_hybrid,
        mock_bridge
    ):
        """Test disabling auto-translation"""
        # Arrange
        chat_id = -1001234567
        mock_bridge.configure_auto_translate.return_value = {"success": True}

        # Act
        result = await mcp_server_hybrid.tool_configure_auto_translate(
            chat_id=chat_id,
            enabled=False,
            target_language=None
        )

        # Assert
        result_json = json.loads(result[0].text)
        assert result_json.get("success") is True
        mock_bridge.configure_auto_translate.assert_called_once()

    @pytest.mark.unit
    @pytest.mark.asyncio
    async def test_configure_auto_translate_bridge_error(
        self,
        mcp_server_hybrid,
        mock_bridge
    ):
        """Test auto-translate configuration when bridge fails"""
        # Arrange
        mock_bridge.configure_auto_translate.side_effect = Exception(
            "Unsupported language"
        )

        # Act
        result = await mcp_server_hybrid.tool_configure_auto_translate(
            chat_id=-1001234567,
            enabled=True,
            target_language="invalid"
        )

        # Assert
        result_json = json.loads(result[0].text)
        assert "error" in result_json
        assert "Unsupported language" in result_json["error"]

    @pytest.mark.unit
    @pytest.mark.asyncio
    async def test_configure_auto_translate_no_bridge(
        self,
        mock_config
    ):
        """Test auto-translate when bridge is not available"""
        from src.mcp_server import UnifiedMCPServer

        # Arrange
        server = UnifiedMCPServer(
            config=mock_config,
            mode="standalone",
            enable_aiml=False
        )
        server.bridge = None

        # Act
        result = await server.tool_configure_auto_translate(
            chat_id=-1001234567,
            enabled=True,
            target_language="en"
        )

        # Assert
        result_json = json.loads(result[0].text)
        assert "error" in result_json
        assert "Bridge not available" in result_json["error"]


class TestGetTranscriptionStatusTool:
    """Tests for get_transcription_status() MCP tool"""

    @pytest.mark.unit
    @pytest.mark.asyncio
    async def test_get_transcription_status_completed(
        self,
        mcp_server_hybrid,
        mock_bridge
    ):
        """Test getting completed transcription status"""
        # Arrange
        task_id = "task_12345"
        mock_bridge.get_transcription_status.return_value = {
            "status": "completed",
            "text": "Hello, this is a test transcription.",
            "confidence": 0.95,
            "duration_seconds": 15.5
        }

        # Act
        result = await mcp_server_hybrid.tool_get_transcription_status(
            task_id=task_id
        )

        # Assert
        assert result is not None
        result_json = json.loads(result[0].text)
        assert result_json.get("status") == "completed"
        assert "text" in result_json
        assert result_json["confidence"] == 0.95

        # Verify bridge was called
        mock_bridge.get_transcription_status.assert_called_once_with(task_id)

    @pytest.mark.unit
    @pytest.mark.asyncio
    async def test_get_transcription_status_pending(
        self,
        mcp_server_hybrid,
        mock_bridge
    ):
        """Test getting pending transcription status"""
        # Arrange
        task_id = "task_67890"
        mock_bridge.get_transcription_status.return_value = {
            "status": "pending",
            "progress": 0.45,
            "estimated_remaining_seconds": 10
        }

        # Act
        result = await mcp_server_hybrid.tool_get_transcription_status(
            task_id=task_id
        )

        # Assert
        result_json = json.loads(result[0].text)
        assert result_json.get("status") == "pending"
        assert result_json.get("progress") == 0.45

    @pytest.mark.unit
    @pytest.mark.asyncio
    async def test_get_transcription_status_failed(
        self,
        mcp_server_hybrid,
        mock_bridge
    ):
        """Test getting failed transcription status"""
        # Arrange
        mock_bridge.get_transcription_status.return_value = {
            "status": "failed",
            "error": "Audio file corrupted"
        }

        # Act
        result = await mcp_server_hybrid.tool_get_transcription_status(
            task_id="task_failed"
        )

        # Assert
        result_json = json.loads(result[0].text)
        assert result_json.get("status") == "failed"
        assert "Audio file corrupted" in result_json.get("error", "")

    @pytest.mark.unit
    @pytest.mark.asyncio
    async def test_get_transcription_status_bridge_error(
        self,
        mcp_server_hybrid,
        mock_bridge
    ):
        """Test transcription status when bridge fails"""
        # Arrange
        mock_bridge.get_transcription_status.side_effect = Exception(
            "Task not found"
        )

        # Act
        result = await mcp_server_hybrid.tool_get_transcription_status(
            task_id="nonexistent_task"
        )

        # Assert
        result_json = json.loads(result[0].text)
        assert "error" in result_json
        assert "Task not found" in result_json["error"]

    @pytest.mark.unit
    @pytest.mark.asyncio
    async def test_get_transcription_status_no_bridge(
        self,
        mock_config
    ):
        """Test transcription status when bridge not available"""
        from src.mcp_server import UnifiedMCPServer

        # Arrange
        server = UnifiedMCPServer(
            config=mock_config,
            mode="standalone",
            enable_aiml=False
        )
        server.bridge = None

        # Act
        result = await server.tool_get_transcription_status(
            task_id="test_task"
        )

        # Assert
        result_json = json.loads(result[0].text)
        assert "error" in result_json
        assert "Bridge not available" in result_json["error"]


class TestBridgeToolIntegration:
    """Integration tests for bridge tools"""

    @pytest.mark.integration
    @pytest.mark.asyncio
    async def test_tag_then_remove_workflow(
        self,
        mcp_server_hybrid,
        mock_bridge
    ):
        """Test workflow: tag a message, then remove the tag"""
        # Arrange
        chat_id = -1001234567
        message_id = 123
        tag = "followup"

        mock_bridge.tag_message.return_value = {"success": True}
        mock_bridge.remove_message_tag.return_value = {"success": True}

        # Act - tag the message
        await mcp_server_hybrid.tool_tag_message(
            chat_id=chat_id,
            message_id=message_id,
            tag=tag
        )

        # Act - remove the tag
        result = await mcp_server_hybrid.tool_remove_message_tag(
            chat_id=chat_id,
            message_id=message_id,
            tag=tag
        )

        # Assert
        result_json = json.loads(result[0].text)
        assert result_json.get("success") is True
        mock_bridge.tag_message.assert_called_once()
        mock_bridge.remove_message_tag.assert_called_once()

    @pytest.mark.integration
    @pytest.mark.asyncio
    async def test_translate_and_configure_workflow(
        self,
        mcp_server_hybrid,
        mock_bridge
    ):
        """Test workflow: translate message, then configure auto-translate"""
        # Arrange
        chat_id = -1001234567
        message_id = 123

        mock_bridge.translate_message.return_value = {
            "original": "Hola mundo",
            "translated": "Hello world",
            "source_language": "es",
            "target_language": "en"
        }
        mock_bridge.configure_auto_translate.return_value = {"success": True}

        # Act - translate a message
        translate_result = await mcp_server_hybrid.tool_translate_message(
            chat_id=chat_id,
            message_id=message_id,
            target_language="en"
        )

        # Act - enable auto-translate for this chat
        config_result = await mcp_server_hybrid.tool_configure_auto_translate(
            chat_id=chat_id,
            enabled=True,
            target_language="en"
        )

        # Assert
        translate_json = json.loads(translate_result[0].text)
        config_json = json.loads(config_result[0].text)

        assert translate_json.get("translated") == "Hello world"
        assert config_json.get("success") is True
        mock_bridge.translate_message.assert_called_once()
        mock_bridge.configure_auto_translate.assert_called_once()


class TestMetricsRecording:
    """Tests for metrics recording in bridge tools"""

    @pytest.mark.unit
    @pytest.mark.asyncio
    async def test_remove_tag_records_metrics(
        self,
        mcp_server_hybrid,
        mock_bridge
    ):
        """Test that remove_message_tag records metrics"""
        # Arrange
        from unittest.mock import Mock
        mock_metrics = Mock()
        mcp_server_hybrid.metrics = mock_metrics
        mock_bridge.remove_message_tag.return_value = {"success": True}

        # Act
        await mcp_server_hybrid.tool_remove_message_tag(
            chat_id=-1001234567,
            message_id=123,
            tag="test"
        )

        # Assert - metrics should be recorded
        mock_metrics.record_tool_call.assert_called_once()
        call_args = mock_metrics.record_tool_call.call_args
        assert call_args[0][0] == "remove_message_tag"
        assert call_args[0][2] == "success"

    @pytest.mark.unit
    @pytest.mark.asyncio
    async def test_configure_translate_records_metrics(
        self,
        mcp_server_hybrid,
        mock_bridge
    ):
        """Test that configure_auto_translate records metrics"""
        # Arrange
        from unittest.mock import Mock
        mock_metrics = Mock()
        mcp_server_hybrid.metrics = mock_metrics
        mock_bridge.configure_auto_translate.return_value = {"success": True}

        # Act
        await mcp_server_hybrid.tool_configure_auto_translate(
            chat_id=-1001234567,
            enabled=True,
            target_language="en"
        )

        # Assert
        mock_metrics.record_tool_call.assert_called_once()
        call_args = mock_metrics.record_tool_call.call_args
        assert call_args[0][0] == "configure_auto_translate"
        assert call_args[0][2] == "success"

    @pytest.mark.unit
    @pytest.mark.asyncio
    async def test_transcription_status_records_metrics(
        self,
        mcp_server_hybrid,
        mock_bridge
    ):
        """Test that get_transcription_status records metrics"""
        # Arrange
        from unittest.mock import Mock
        mock_metrics = Mock()
        mcp_server_hybrid.metrics = mock_metrics
        mock_bridge.get_transcription_status.return_value = {
            "status": "completed",
            "text": "test"
        }

        # Act
        await mcp_server_hybrid.tool_get_transcription_status(
            task_id="test_task"
        )

        # Assert
        mock_metrics.record_tool_call.assert_called_once()
        call_args = mock_metrics.record_tool_call.call_args
        assert call_args[0][0] == "get_transcription_status"
        assert call_args[0][2] == "success"

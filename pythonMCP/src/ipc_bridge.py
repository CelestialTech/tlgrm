"""
Telegram Desktop Bridge Client

Communicates with modified Telegram Desktop (tdesktop) via Unix domain socket
to access advanced features:
- Local database access (instant message history)
- Voice transcription
- Semantic search
- Media processing

This provides much faster access than API calls and works offline.
"""

import json
import socket
from typing import Dict, Any, List, Optional
from pathlib import Path

import structlog

logger = structlog.get_logger()


class TDesktopBridgeError(Exception):
    """Error communicating with tdesktop bridge."""
    pass


class TDesktopBridge:
    """
    Client for modified Telegram Desktop MCP bridge.

    Communicates via Unix domain socket with the tdesktop MCP bridge service.
    """

    def __init__(self, socket_path: str = "/tmp/tdesktop_mcp.sock"):
        """
        Initialize bridge client.

        Args:
            socket_path: Path to Unix domain socket
        """
        self.socket_path = socket_path
        self._request_id = 0

    async def is_available(self) -> bool:
        """
        Check if tdesktop bridge is running.

        Returns:
            True if bridge is available
        """
        try:
            result = await self.ping()
            return result.get("status") == "pong"
        except Exception:
            return False

    async def call_method(
        self,
        method: str,
        params: Optional[Dict[str, Any]] = None,
    ) -> Any:
        """
        Call a method on tdesktop MCP bridge.

        Args:
            method: Method name
            params: Method parameters

        Returns:
            Method result

        Raises:
            TDesktopBridgeError: If communication fails
        """
        self._request_id += 1

        request = {
            "id": self._request_id,
            "method": method,
            "params": params or {}
        }

        try:
            # Connect to Unix socket
            sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            sock.settimeout(10.0)  # 10 second timeout

            # Check if socket exists
            if not Path(self.socket_path).exists():
                raise TDesktopBridgeError(
                    f"Bridge socket not found: {self.socket_path}. "
                    "Is modified Telegram Desktop running?"
                )

            sock.connect(self.socket_path)

            # Send request
            request_data = json.dumps(request).encode() + b'\n'
            sock.sendall(request_data)

            # Receive response
            response = b""
            while True:
                chunk = sock.recv(4096)
                if not chunk:
                    break
                response += chunk
                if b'\n' in chunk:
                    break

            sock.close()

            # Parse response
            response_data = json.loads(response.decode().strip())

            # Check for errors
            if "error" in response_data:
                error = response_data["error"]
                raise TDesktopBridgeError(
                    f"Bridge error: {error.get('message')} (code: {error.get('code')})"
                )

            logger.debug(
                "tdesktop_bridge.call",
                method=method,
                params=params,
                result_keys=list(response_data.get("result", {}).keys()),
            )

            return response_data.get("result")

        except socket.timeout:
            raise TDesktopBridgeError(
                f"Timeout connecting to bridge at {self.socket_path}"
            )
        except socket.error as e:
            raise TDesktopBridgeError(
                f"Socket error: {e}. Is modified Telegram Desktop running?"
            )
        except json.JSONDecodeError as e:
            raise TDesktopBridgeError(f"Invalid JSON response: {e}")

    async def ping(self) -> Dict[str, Any]:
        """
        Ping the bridge to check availability.

        Returns:
            Ping response with status and features
        """
        return await self.call_method("ping")

    async def get_messages_local(
        self,
        chat_id: int,
        limit: int = 50,
        offset: int = 0,
    ) -> List[Dict[str, Any]]:
        """
        Get messages from local tdesktop database.

        This is MUCH faster than API calls and works offline!

        Args:
            chat_id: Telegram chat ID
            limit: Number of messages to fetch
            offset: Offset for pagination

        Returns:
            List of message dictionaries
        """
        result = await self.call_method("get_messages", {
            "chat_id": chat_id,
            "limit": limit,
            "offset": offset,
        })

        return result.get("messages", [])

    async def search_local(
        self,
        query: str,
        chat_id: Optional[int] = None,
        limit: int = 50,
    ) -> List[Dict[str, Any]]:
        """
        Search messages in local tdesktop cache.

        Args:
            query: Search query
            chat_id: Optional chat ID to limit search
            limit: Maximum number of results

        Returns:
            List of matching messages
        """
        params = {
            "query": query,
            "limit": limit,
        }

        if chat_id:
            params["chat_id"] = chat_id

        result = await self.call_method("search_local", params)
        return result.get("results", [])

    async def get_dialogs(
        self,
        limit: int = 100,
        offset: int = 0,
    ) -> List[Dict[str, Any]]:
        """
        Get list of dialogs (chats) from tdesktop.

        Args:
            limit: Number of dialogs to fetch
            offset: Offset for pagination

        Returns:
            List of dialog dictionaries
        """
        result = await self.call_method("get_dialogs", {
            "limit": limit,
            "offset": offset,
        })

        return result.get("dialogs", [])

    async def transcribe_voice(self, file_path: str) -> str:
        """
        Transcribe voice message using tdesktop's Whisper integration.

        Args:
            file_path: Path to audio file

        Returns:
            Transcription text
        """
        result = await self.call_method("transcribe_voice", {
            "file_path": file_path,
        })

        return result.get("text", "")

    async def semantic_search(
        self,
        query: str,
        limit: int = 10,
    ) -> List[Dict[str, Any]]:
        """
        Semantic search using tdesktop's FAISS vector index.

        Args:
            query: Search query
            limit: Maximum number of results

        Returns:
            List of semantically similar messages
        """
        result = await self.call_method("semantic_search", {
            "query": query,
            "limit": limit,
        })

        return result.get("results", [])

    async def extract_media_text(
        self,
        file_path: str,
        media_type: str = "auto",
    ) -> str:
        """
        Extract text from media (OCR, document parsing, etc.).

        Args:
            file_path: Path to media file
            media_type: Type of media (auto, image, document, video)

        Returns:
            Extracted text
        """
        result = await self.call_method("extract_media_text", {
            "file_path": file_path,
            "media_type": media_type,
        })

        return result.get("text", "")

    def get_stats(self) -> Dict[str, Any]:
        """Get bridge statistics."""
        return {
            "socket_path": self.socket_path,
            "request_count": self._request_id,
        }

    # ==================== Batch Operations ====================

    async def batch_operation(
        self,
        operation: str,
        targets: List[int],
        options: Optional[Dict[str, Any]] = None,
    ) -> Dict[str, Any]:
        """
        Execute a batch operation on multiple targets.

        Args:
            operation: Operation type (mark_read, archive, mute, delete, etc.)
            targets: List of chat IDs to apply operation to
            options: Additional options for the operation

        Returns:
            Results with success/failure per target
        """
        params = {
            "operation": operation,
            "targets": targets,
        }
        if options:
            params["options"] = options

        return await self.call_method("batch_operation", params)

    async def batch_forward_messages(
        self,
        source_chat_id: int,
        message_ids: List[int],
        target_chat_ids: List[int],
    ) -> Dict[str, Any]:
        """
        Forward multiple messages to multiple chats.

        Args:
            source_chat_id: Source chat ID
            message_ids: List of message IDs to forward
            target_chat_ids: List of target chat IDs

        Returns:
            Forward results per target
        """
        return await self.call_method("batch_forward_messages", {
            "source_chat_id": source_chat_id,
            "message_ids": message_ids,
            "target_chat_ids": target_chat_ids,
        })

    async def batch_delete_messages(
        self,
        chat_id: int,
        message_ids: List[int],
        revoke: bool = True,
    ) -> Dict[str, Any]:
        """
        Delete multiple messages at once.

        Args:
            chat_id: Chat ID containing the messages
            message_ids: List of message IDs to delete
            revoke: Whether to delete for everyone

        Returns:
            Deletion results
        """
        return await self.call_method("batch_delete_messages", {
            "chat_id": chat_id,
            "message_ids": message_ids,
            "revoke": revoke,
        })

    # ==================== Message Scheduler ====================

    async def schedule_message(
        self,
        chat_id: int,
        text: str,
        send_at: int,
    ) -> Dict[str, Any]:
        """
        Schedule a message for later delivery.

        Args:
            chat_id: Target chat ID
            text: Message text
            send_at: Unix timestamp for delivery

        Returns:
            Scheduled message info
        """
        return await self.call_method("schedule_message", {
            "chat_id": chat_id,
            "text": text,
            "send_at": send_at,
        })

    async def list_scheduled_messages(
        self,
        chat_id: int,
    ) -> List[Dict[str, Any]]:
        """
        List all scheduled messages for a chat.

        Args:
            chat_id: Chat ID to list scheduled messages for

        Returns:
            List of scheduled messages
        """
        result = await self.call_method("list_scheduled_messages", {
            "chat_id": chat_id,
        })
        return result.get("scheduled_messages", [])

    async def cancel_scheduled_message(
        self,
        chat_id: int,
        message_id: int,
    ) -> Dict[str, Any]:
        """
        Cancel a scheduled message.

        Args:
            chat_id: Chat ID
            message_id: Scheduled message ID

        Returns:
            Cancellation result
        """
        return await self.call_method("cancel_scheduled_message", {
            "chat_id": chat_id,
            "message_id": message_id,
        })

    # ==================== Message Tags ====================

    async def tag_message(
        self,
        chat_id: int,
        message_id: int,
        tag: str,
    ) -> Dict[str, Any]:
        """
        Add a tag to a message.

        Args:
            chat_id: Chat ID
            message_id: Message ID
            tag: Tag name to add

        Returns:
            Tagging result
        """
        return await self.call_method("tag_message", {
            "chat_id": chat_id,
            "message_id": message_id,
            "tag": tag,
        })

    async def get_message_tags(
        self,
        chat_id: int,
        message_id: int,
    ) -> List[str]:
        """
        Get all tags for a message.

        Args:
            chat_id: Chat ID
            message_id: Message ID

        Returns:
            List of tag names
        """
        result = await self.call_method("get_message_tags", {
            "chat_id": chat_id,
            "message_id": message_id,
        })
        return result.get("tags", [])

    async def remove_message_tag(
        self,
        chat_id: int,
        message_id: int,
        tag: str,
    ) -> Dict[str, Any]:
        """
        Remove a tag from a message.

        Args:
            chat_id: Chat ID
            message_id: Message ID
            tag: Tag name to remove

        Returns:
            Removal result
        """
        return await self.call_method("remove_message_tag", {
            "chat_id": chat_id,
            "message_id": message_id,
            "tag": tag,
        })

    async def list_tags(self) -> List[Dict[str, Any]]:
        """
        List all available tags.

        Returns:
            List of tags with name, color, count
        """
        result = await self.call_method("list_tags", {})
        return result.get("tags", [])

    async def get_tagged_messages(
        self,
        tag: str,
        limit: int = 50,
    ) -> List[Dict[str, Any]]:
        """
        Get all messages with a specific tag.

        Args:
            tag: Tag name to search for
            limit: Maximum number of results

        Returns:
            List of tagged messages
        """
        result = await self.call_method("get_tagged_messages", {
            "tag": tag,
            "limit": limit,
        })
        return result.get("messages", [])

    # ==================== Translation ====================

    async def translate_message(
        self,
        chat_id: int,
        message_id: int,
        target_language: str,
    ) -> Dict[str, Any]:
        """
        Translate a message to another language.

        Args:
            chat_id: Chat ID
            message_id: Message ID
            target_language: Target language code (e.g., 'en', 'es', 'ru')

        Returns:
            Translation result with original and translated text
        """
        return await self.call_method("translate_message", {
            "chat_id": chat_id,
            "message_id": message_id,
            "target_language": target_language,
        })

    async def get_translation_languages(self) -> List[Dict[str, str]]:
        """
        Get list of supported translation languages.

        Returns:
            List of languages with code and name
        """
        result = await self.call_method("get_translation_languages", {})
        return result.get("languages", [])

    async def configure_auto_translate(
        self,
        chat_id: int,
        enabled: bool,
        target_language: Optional[str] = None,
    ) -> Dict[str, Any]:
        """
        Configure auto-translation for a chat.

        Args:
            chat_id: Chat ID
            enabled: Whether to enable auto-translation
            target_language: Target language for auto-translation

        Returns:
            Configuration result
        """
        params = {
            "chat_id": chat_id,
            "enabled": enabled,
        }
        if target_language:
            params["target_language"] = target_language

        return await self.call_method("configure_auto_translate", params)

    # ==================== Voice Transcription ====================

    async def transcribe_voice_message(
        self,
        chat_id: int,
        message_id: int,
    ) -> Dict[str, Any]:
        """
        Transcribe a voice message.

        Args:
            chat_id: Chat ID
            message_id: Voice message ID

        Returns:
            Transcription result with text and confidence
        """
        return await self.call_method("transcribe_voice_message", {
            "chat_id": chat_id,
            "message_id": message_id,
        })

    async def get_transcription_status(
        self,
        task_id: str,
    ) -> Dict[str, Any]:
        """
        Get status of a transcription task.

        Args:
            task_id: Transcription task ID

        Returns:
            Task status and result if complete
        """
        return await self.call_method("get_transcription_status", {
            "task_id": task_id,
        })

    async def get_transcription_history(
        self,
        chat_id: Optional[int] = None,
        limit: int = 50,
    ) -> List[Dict[str, Any]]:
        """
        Get history of transcribed messages.

        Args:
            chat_id: Optional chat ID to filter by
            limit: Maximum number of results

        Returns:
            List of transcription records
        """
        params = {"limit": limit}
        if chat_id:
            params["chat_id"] = chat_id

        result = await self.call_method("get_transcription_history", params)
        return result.get("transcriptions", [])


# Global instance (optional)
_tdesktop_bridge: Optional[TDesktopBridge] = None


def get_tdesktop_bridge() -> Optional[TDesktopBridge]:
    """Get the global tdesktop bridge instance."""
    return _tdesktop_bridge


def initialize_tdesktop_bridge(
    socket_path: str = "/tmp/tdesktop_mcp.sock",
) -> TDesktopBridge:
    """
    Initialize tdesktop bridge.

    Args:
        socket_path: Path to Unix domain socket

    Returns:
        TDesktopBridge instance
    """
    global _tdesktop_bridge

    _tdesktop_bridge = TDesktopBridge(socket_path=socket_path)

    logger.info(
        "tdesktop_bridge.initialized",
        socket_path=socket_path,
    )

    return _tdesktop_bridge

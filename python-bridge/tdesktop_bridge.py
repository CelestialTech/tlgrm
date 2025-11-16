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

import asyncio
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

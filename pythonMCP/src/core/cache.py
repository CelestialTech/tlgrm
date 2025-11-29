"""
Message caching for Telegram MCP Server.

Provides:
- In-memory message cache with LRU eviction
- Optional SQLite persistent cache for durability across restarts
- Thread-safe operations
- TTL-based expiration
"""

import asyncio
import time
from collections import OrderedDict
from dataclasses import dataclass
from typing import Any, Dict, List, Optional

import structlog

from .config import CacheConfig
from .sqlite_cache import SQLiteCache

logger = structlog.get_logger()


@dataclass
class CachedMessage:
    """Cached message with metadata."""

    message_id: str
    chat_id: str
    data: Dict[str, Any]
    timestamp: float
    ttl: int


class MessageCache:
    """
    Thread-safe message cache.

    Features:
    - LRU eviction when max size reached
    - TTL-based expiration
    - Per-chat message storage
    - Optional SQLite persistence
    """

    def __init__(self, config: CacheConfig):
        """
        Initialize message cache.

        Args:
            config: Cache configuration
        """
        self.config = config
        self.max_messages = config.max_messages
        self.ttl = config.ttl_seconds
        self.use_sqlite = config.use_sqlite

        # SQLite backend (if enabled)
        self._sqlite: Optional[SQLiteCache] = None
        if self.use_sqlite:
            self._sqlite = SQLiteCache(
                db_path=config.db_path,
                max_messages_per_chat=config.max_messages,
                ttl_seconds=config.ttl_seconds,
            )

        # Per-chat caches (chat_id -> OrderedDict of messages) - only for in-memory mode
        self._cache: Dict[str, OrderedDict[str, CachedMessage]] = {}
        self._lock = asyncio.Lock()

        logger.info(
            "message_cache.initialized",
            max_messages=self.max_messages,
            ttl_seconds=self.ttl,
            use_sqlite=config.use_sqlite,
            backend="sqlite" if config.use_sqlite else "memory",
        )

    async def get_messages(
        self,
        chat_id: str,
        limit: Optional[int] = None,
    ) -> List[Dict[str, Any]]:
        """
        Get cached messages for a chat.

        Args:
            chat_id: Chat ID
            limit: Maximum number of messages to return

        Returns:
            List of message dictionaries
        """
        # Delegate to SQLite if enabled
        if self._sqlite:
            return await self._sqlite.get_messages(chat_id, limit)

        # In-memory implementation
        async with self._lock:
            if chat_id not in self._cache:
                return []

            chat_cache = self._cache[chat_id]
            current_time = time.time()

            # Filter expired messages
            valid_messages = [
                msg.data
                for msg in chat_cache.values()
                if self.ttl == 0 or (current_time - msg.timestamp) < self.ttl
            ]

            # Apply limit
            if limit:
                valid_messages = valid_messages[-limit:]

            return valid_messages

    async def add_message(
        self,
        chat_id: str,
        message_id: str,
        message_data: Dict[str, Any],
    ) -> None:
        """
        Add a message to the cache.

        Args:
            chat_id: Chat ID
            message_id: Message ID
            message_data: Message data dictionary
        """
        # Delegate to SQLite if enabled
        if self._sqlite:
            await self._sqlite.add_message(chat_id, message_id, message_data)
            return

        # In-memory implementation
        async with self._lock:
            # Create chat cache if not exists
            if chat_id not in self._cache:
                self._cache[chat_id] = OrderedDict()

            chat_cache = self._cache[chat_id]

            # Create cached message
            cached_msg = CachedMessage(
                message_id=message_id,
                chat_id=chat_id,
                data=message_data,
                timestamp=time.time(),
                ttl=self.ttl,
            )

            # Add to cache (or update if exists)
            if message_id in chat_cache:
                # Move to end (most recent)
                chat_cache.move_to_end(message_id)
            else:
                chat_cache[message_id] = cached_msg

            # Evict oldest if over limit
            while len(chat_cache) > self.max_messages:
                oldest_id = next(iter(chat_cache))
                del chat_cache[oldest_id]

    async def clear_chat(self, chat_id: str) -> None:
        """
        Clear cache for a specific chat.

        Args:
            chat_id: Chat ID
        """
        # Delegate to SQLite if enabled
        if self._sqlite:
            await self._sqlite.clear_chat(chat_id)
            logger.debug("message_cache.cleared", chat_id=chat_id)
            return

        # In-memory implementation
        async with self._lock:
            if chat_id in self._cache:
                del self._cache[chat_id]

        logger.debug("message_cache.cleared", chat_id=chat_id)

    async def clear_all(self) -> None:
        """Clear entire cache."""
        # Delegate to SQLite if enabled
        if self._sqlite:
            await self._sqlite.clear_all()
            logger.info("message_cache.cleared_all")
            return

        # In-memory implementation
        async with self._lock:
            self._cache.clear()

        logger.info("message_cache.cleared_all")

    async def cleanup_expired(self) -> int:
        """
        Remove expired messages from cache.

        Returns:
            Number of messages removed
        """
        if self.ttl == 0:
            return 0

        # Delegate to SQLite if enabled
        if self._sqlite:
            return await self._sqlite.cleanup_expired()

        # In-memory implementation
        removed_count = 0
        current_time = time.time()

        async with self._lock:
            for chat_id in list(self._cache.keys()):
                chat_cache = self._cache[chat_id]

                # Find expired messages
                expired_ids = [
                    msg_id
                    for msg_id, msg in chat_cache.items()
                    if (current_time - msg.timestamp) >= self.ttl
                ]

                # Remove expired
                for msg_id in expired_ids:
                    del chat_cache[msg_id]
                    removed_count += 1

                # Remove empty chat caches
                if not chat_cache:
                    del self._cache[chat_id]

        if removed_count > 0:
            logger.debug(
                "message_cache.expired_cleanup",
                removed_count=removed_count,
            )

        return removed_count

    async def get_stats(self) -> Dict[str, Any]:
        """
        Get cache statistics.

        Returns:
            Statistics dictionary
        """
        # Delegate to SQLite if enabled
        if self._sqlite:
            return await self._sqlite.get_stats()

        # In-memory implementation
        total_messages = sum(len(chat_cache) for chat_cache in self._cache.values())

        return {
            "total_chats": len(self._cache),
            "total_messages": total_messages,
            "max_messages_per_chat": self.max_messages,
            "ttl_seconds": self.ttl,
        }

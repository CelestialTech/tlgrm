"""
SQLite-backed persistent cache for messages.

Provides durable storage for message cache with:
- Automatic schema creation and migration
- Thread-safe async operations
- TTL-based expiration
- LRU eviction per-chat
- FTS5 full-text search
"""

import aiosqlite
import asyncio
import json
import time
from pathlib import Path
from typing import Any, Dict, List, Optional

import structlog

logger = structlog.get_logger()


class SQLiteCache:
    """
    SQLite-backed persistent cache.

    Features:
    - Persistent storage across restarts
    - TTL-based expiration
    - LRU eviction when max size reached
    - Thread-safe async operations
    - FTS5 full-text search for message content
    """

    SCHEMA_VERSION = 2  # Bumped for FTS5 support

    def __init__(
        self,
        db_path: str,
        max_messages_per_chat: int = 1000,
        ttl_seconds: int = 3600,
    ):
        """
        Initialize SQLite cache.

        Args:
            db_path: Path to SQLite database file
            max_messages_per_chat: Maximum messages per chat
            ttl_seconds: Time-to-live for messages (0 = no expiration)
        """
        self.db_path = Path(db_path)
        self.max_messages = max_messages_per_chat
        self.ttl = ttl_seconds
        self._db: Optional[aiosqlite.Connection] = None
        self._lock = asyncio.Lock()

        logger.info(
            "sqlite_cache.initialized",
            db_path=str(self.db_path),
            max_messages=max_messages_per_chat,
            ttl_seconds=ttl_seconds,
        )

    async def connect(self) -> None:
        """Initialize database connection and create schema."""
        async with self._lock:
            if self._db is not None:
                return

            # Ensure parent directory exists
            self.db_path.parent.mkdir(parents=True, exist_ok=True)

            # Connect to database
            self._db = await aiosqlite.connect(str(self.db_path))
            self._db.row_factory = aiosqlite.Row

            # Create schema
            await self._create_schema()

            logger.info("sqlite_cache.connected", db_path=str(self.db_path))

    async def _create_schema(self) -> None:
        """Create database schema if it doesn't exist."""
        assert self._db is not None

        await self._db.execute(
            """
            CREATE TABLE IF NOT EXISTS cached_messages (
                chat_id TEXT NOT NULL,
                message_id TEXT NOT NULL,
                message_data TEXT NOT NULL,
                timestamp REAL NOT NULL,
                ttl INTEGER NOT NULL,
                PRIMARY KEY (chat_id, message_id)
            )
            """
        )

        # Create indices for efficient queries
        await self._db.execute(
            """
            CREATE INDEX IF NOT EXISTS idx_chat_timestamp
            ON cached_messages(chat_id, timestamp DESC)
            """
        )

        await self._db.execute(
            """
            CREATE INDEX IF NOT EXISTS idx_timestamp_ttl
            ON cached_messages(timestamp, ttl)
            """
        )

        # FTS5 virtual table for full-text search
        # We store: chat_id, message_id, text content, sender_name
        await self._db.execute(
            """
            CREATE VIRTUAL TABLE IF NOT EXISTS messages_fts USING fts5(
                chat_id,
                message_id,
                text_content,
                sender_name,
                content='cached_messages',
                content_rowid='rowid',
                tokenize='unicode61 remove_diacritics 2'
            )
            """
        )

        # Triggers to keep FTS in sync with main table
        # Note: We need rowid for content sync, so we'll manually manage FTS
        await self._db.execute(
            """
            CREATE TRIGGER IF NOT EXISTS cached_messages_ai AFTER INSERT ON cached_messages BEGIN
                INSERT INTO messages_fts(rowid, chat_id, message_id, text_content, sender_name)
                SELECT rowid, chat_id, message_id,
                    json_extract(message_data, '$.text'),
                    json_extract(message_data, '$.sender_name')
                FROM cached_messages
                WHERE chat_id = NEW.chat_id AND message_id = NEW.message_id;
            END
            """
        )

        await self._db.execute(
            """
            CREATE TRIGGER IF NOT EXISTS cached_messages_ad AFTER DELETE ON cached_messages BEGIN
                INSERT INTO messages_fts(messages_fts, rowid, chat_id, message_id, text_content, sender_name)
                VALUES('delete', OLD.rowid, OLD.chat_id, OLD.message_id,
                    json_extract(OLD.message_data, '$.text'),
                    json_extract(OLD.message_data, '$.sender_name'));
            END
            """
        )

        await self._db.execute(
            """
            CREATE TRIGGER IF NOT EXISTS cached_messages_au AFTER UPDATE ON cached_messages BEGIN
                INSERT INTO messages_fts(messages_fts, rowid, chat_id, message_id, text_content, sender_name)
                VALUES('delete', OLD.rowid, OLD.chat_id, OLD.message_id,
                    json_extract(OLD.message_data, '$.text'),
                    json_extract(OLD.message_data, '$.sender_name'));
                INSERT INTO messages_fts(rowid, chat_id, message_id, text_content, sender_name)
                SELECT rowid, chat_id, message_id,
                    json_extract(message_data, '$.text'),
                    json_extract(message_data, '$.sender_name')
                FROM cached_messages
                WHERE chat_id = NEW.chat_id AND message_id = NEW.message_id;
            END
            """
        )

        # Schema version tracking
        await self._db.execute(
            """
            CREATE TABLE IF NOT EXISTS schema_version (
                version INTEGER PRIMARY KEY
            )
            """
        )

        # Check if we need to migrate
        await self._migrate_schema()

        # Insert or update schema version
        await self._db.execute(
            "INSERT OR REPLACE INTO schema_version (version) VALUES (?)",
            (self.SCHEMA_VERSION,),
        )

        await self._db.commit()

    async def _migrate_schema(self) -> None:
        """Migrate schema if needed."""
        assert self._db is not None

        cursor = await self._db.execute(
            "SELECT version FROM schema_version LIMIT 1"
        )
        row = await cursor.fetchone()

        if row is None:
            # Fresh install, no migration needed
            return

        old_version = row[0] if row else 1

        if old_version < 2:
            # Migrate from v1 to v2: rebuild FTS index from existing data
            logger.info("sqlite_cache.migrating", from_version=old_version, to_version=2)

            # Rebuild FTS index
            await self._db.execute("DELETE FROM messages_fts")
            await self._db.execute(
                """
                INSERT INTO messages_fts(rowid, chat_id, message_id, text_content, sender_name)
                SELECT rowid, chat_id, message_id,
                    json_extract(message_data, '$.text'),
                    json_extract(message_data, '$.sender_name')
                FROM cached_messages
                """
            )

            logger.info("sqlite_cache.migration_complete", to_version=2)

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
        if self._db is None:
            await self.connect()

        current_time = time.time()

        # Query with TTL filtering
        if self.ttl > 0:
            query = """
                SELECT message_data
                FROM cached_messages
                WHERE chat_id = ? AND (? - timestamp) < ttl
                ORDER BY timestamp DESC
            """
            params = [chat_id, current_time]
        else:
            query = """
                SELECT message_data
                FROM cached_messages
                WHERE chat_id = ?
                ORDER BY timestamp DESC
            """
            params = [chat_id]

        if limit:
            query += " LIMIT ?"
            params.append(limit)

        async with self._lock:
            cursor = await self._db.execute(query, params)
            rows = await cursor.fetchall()

            messages = []
            for row in rows:
                try:
                    msg_data = json.loads(row["message_data"])
                    messages.append(msg_data)
                except json.JSONDecodeError as e:
                    logger.error(
                        "sqlite_cache.json_decode_error",
                        chat_id=chat_id,
                        error=str(e),
                    )

            return messages

    async def search_messages(
        self,
        query: str,
        chat_id: Optional[str] = None,
        limit: int = 50,
    ) -> List[Dict[str, Any]]:
        """
        Full-text search for messages using FTS5.

        Args:
            query: Search query (supports FTS5 syntax like "word1 word2", "word1 OR word2")
            chat_id: Optional chat ID to limit search to
            limit: Maximum number of results

        Returns:
            List of matching messages with relevance ranking
        """
        if self._db is None:
            await self.connect()

        # Escape special FTS5 characters and prepare query
        # FTS5 uses: AND OR NOT ( ) * " for special meaning
        safe_query = query.replace('"', '""')

        async with self._lock:
            if chat_id:
                # Search within specific chat
                cursor = await self._db.execute(
                    """
                    SELECT
                        m.chat_id,
                        m.message_id,
                        m.message_data,
                        m.timestamp,
                        bm25(messages_fts) as rank
                    FROM messages_fts f
                    JOIN cached_messages m ON f.rowid = m.rowid
                    WHERE messages_fts MATCH ? AND f.chat_id = ?
                    ORDER BY rank
                    LIMIT ?
                    """,
                    (safe_query, chat_id, limit),
                )
            else:
                # Search across all chats
                cursor = await self._db.execute(
                    """
                    SELECT
                        m.chat_id,
                        m.message_id,
                        m.message_data,
                        m.timestamp,
                        bm25(messages_fts) as rank
                    FROM messages_fts f
                    JOIN cached_messages m ON f.rowid = m.rowid
                    WHERE messages_fts MATCH ?
                    ORDER BY rank
                    LIMIT ?
                    """,
                    (safe_query, limit),
                )

            rows = await cursor.fetchall()

            results = []
            for row in rows:
                try:
                    msg_data = json.loads(row["message_data"])
                    msg_data["_search_rank"] = row["rank"]
                    msg_data["_chat_id"] = row["chat_id"]
                    msg_data["_message_id"] = row["message_id"]
                    results.append(msg_data)
                except json.JSONDecodeError as e:
                    logger.error(
                        "sqlite_cache.search_json_error",
                        error=str(e),
                    )

            logger.debug(
                "sqlite_cache.search_complete",
                query=query,
                chat_id=chat_id,
                result_count=len(results),
            )

            return results

    async def search_by_sender(
        self,
        sender_name: str,
        chat_id: Optional[str] = None,
        limit: int = 50,
    ) -> List[Dict[str, Any]]:
        """
        Search messages by sender name using FTS5.

        Args:
            sender_name: Sender name to search for
            chat_id: Optional chat ID to limit search
            limit: Maximum number of results

        Returns:
            List of matching messages
        """
        if self._db is None:
            await self.connect()

        safe_name = sender_name.replace('"', '""')
        # Use column filter for sender_name column
        fts_query = f'sender_name:"{safe_name}"'

        async with self._lock:
            if chat_id:
                cursor = await self._db.execute(
                    """
                    SELECT
                        m.chat_id,
                        m.message_id,
                        m.message_data,
                        m.timestamp
                    FROM messages_fts f
                    JOIN cached_messages m ON f.rowid = m.rowid
                    WHERE messages_fts MATCH ? AND f.chat_id = ?
                    ORDER BY m.timestamp DESC
                    LIMIT ?
                    """,
                    (fts_query, chat_id, limit),
                )
            else:
                cursor = await self._db.execute(
                    """
                    SELECT
                        m.chat_id,
                        m.message_id,
                        m.message_data,
                        m.timestamp
                    FROM messages_fts f
                    JOIN cached_messages m ON f.rowid = m.rowid
                    WHERE messages_fts MATCH ?
                    ORDER BY m.timestamp DESC
                    LIMIT ?
                    """,
                    (fts_query, limit),
                )

            rows = await cursor.fetchall()

            results = []
            for row in rows:
                try:
                    msg_data = json.loads(row["message_data"])
                    msg_data["_chat_id"] = row["chat_id"]
                    msg_data["_message_id"] = row["message_id"]
                    results.append(msg_data)
                except json.JSONDecodeError:
                    pass

            return results

    async def rebuild_fts_index(self) -> int:
        """
        Rebuild the FTS5 index from scratch.

        Returns:
            Number of messages indexed
        """
        if self._db is None:
            await self.connect()

        async with self._lock:
            # Clear existing FTS data
            await self._db.execute("DELETE FROM messages_fts")

            # Rebuild from main table
            await self._db.execute(
                """
                INSERT INTO messages_fts(rowid, chat_id, message_id, text_content, sender_name)
                SELECT rowid, chat_id, message_id,
                    json_extract(message_data, '$.text'),
                    json_extract(message_data, '$.sender_name')
                FROM cached_messages
                """
            )

            # Get count
            cursor = await self._db.execute("SELECT COUNT(*) FROM messages_fts")
            row = await cursor.fetchone()
            count = row[0] if row else 0

            await self._db.commit()

            logger.info("sqlite_cache.fts_rebuilt", indexed_count=count)

            return count

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
        if self._db is None:
            await self.connect()

        async with self._lock:
            # Serialize message data
            try:
                message_json = json.dumps(message_data)
            except TypeError as e:
                logger.error(
                    "sqlite_cache.json_encode_error",
                    chat_id=chat_id,
                    message_id=message_id,
                    error=str(e),
                )
                return

            # Insert or replace message
            await self._db.execute(
                """
                INSERT OR REPLACE INTO cached_messages
                (chat_id, message_id, message_data, timestamp, ttl)
                VALUES (?, ?, ?, ?, ?)
                """,
                (chat_id, message_id, message_json, time.time(), self.ttl),
            )

            # Check if we need to evict old messages (LRU)
            cursor = await self._db.execute(
                "SELECT COUNT(*) as count FROM cached_messages WHERE chat_id = ?",
                (chat_id,),
            )
            row = await cursor.fetchone()
            count = row["count"]

            if count > self.max_messages:
                # Delete oldest messages
                to_delete = count - self.max_messages
                await self._db.execute(
                    """
                    DELETE FROM cached_messages
                    WHERE chat_id = ? AND message_id IN (
                        SELECT message_id FROM cached_messages
                        WHERE chat_id = ?
                        ORDER BY timestamp ASC
                        LIMIT ?
                    )
                    """,
                    (chat_id, chat_id, to_delete),
                )

            await self._db.commit()

    async def clear_chat(self, chat_id: str) -> None:
        """
        Clear cache for a specific chat.

        Args:
            chat_id: Chat ID
        """
        if self._db is None:
            await self.connect()

        async with self._lock:
            await self._db.execute(
                "DELETE FROM cached_messages WHERE chat_id = ?",
                (chat_id,),
            )
            await self._db.commit()

        logger.debug("sqlite_cache.cleared", chat_id=chat_id)

    async def clear_all(self) -> None:
        """Clear entire cache."""
        if self._db is None:
            await self.connect()

        async with self._lock:
            await self._db.execute("DELETE FROM cached_messages")
            await self._db.commit()

        logger.info("sqlite_cache.cleared_all")

    async def cleanup_expired(self) -> int:
        """
        Remove expired messages from cache.

        Returns:
            Number of messages removed
        """
        if self.ttl == 0:
            return 0

        if self._db is None:
            await self.connect()

        current_time = time.time()

        async with self._lock:
            # Delete expired messages
            cursor = await self._db.execute(
                """
                DELETE FROM cached_messages
                WHERE (? - timestamp) >= ttl
                RETURNING chat_id
                """,
                (current_time,),
            )
            rows = await cursor.fetchall()
            removed_count = len(rows)

            await self._db.commit()

        if removed_count > 0:
            logger.debug(
                "sqlite_cache.expired_cleanup",
                removed_count=removed_count,
            )

        return removed_count

    async def get_stats(self) -> Dict[str, Any]:
        """
        Get cache statistics.

        Returns:
            Statistics dictionary
        """
        if self._db is None:
            await self.connect()

        async with self._lock:
            # Total messages
            cursor = await self._db.execute(
                "SELECT COUNT(*) as total FROM cached_messages"
            )
            row = await cursor.fetchone()
            total_messages = row["total"]

            # Total chats
            cursor = await self._db.execute(
                "SELECT COUNT(DISTINCT chat_id) as total FROM cached_messages"
            )
            row = await cursor.fetchone()
            total_chats = row["total"]

            # FTS index count
            try:
                cursor = await self._db.execute(
                    "SELECT COUNT(*) as total FROM messages_fts"
                )
                row = await cursor.fetchone()
                fts_indexed = row["total"] if row else 0
            except Exception:
                fts_indexed = 0

            # Database size
            db_size = self.db_path.stat().st_size if self.db_path.exists() else 0

            return {
                "total_chats": total_chats,
                "total_messages": total_messages,
                "fts_indexed_messages": fts_indexed,
                "fts_enabled": True,
                "max_messages_per_chat": self.max_messages,
                "ttl_seconds": self.ttl,
                "db_size_bytes": db_size,
                "db_path": str(self.db_path),
                "schema_version": self.SCHEMA_VERSION,
            }

    async def close(self) -> None:
        """Close database connection."""
        async with self._lock:
            if self._db is not None:
                await self._db.close()
                self._db = None

        logger.info("sqlite_cache.closed")

    async def vacuum(self) -> None:
        """Optimize database file size (VACUUM)."""
        if self._db is None:
            await self.connect()

        async with self._lock:
            await self._db.execute("VACUUM")
            await self._db.commit()

        logger.info("sqlite_cache.vacuumed")

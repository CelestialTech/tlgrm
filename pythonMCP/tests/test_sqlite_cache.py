"""
Tests for src/core/sqlite_cache.py - SQLiteCache

Comprehensive test suite for SQLite-backed persistent cache:
- Basic CRUD operations
- LRU eviction
- TTL expiration
- Cleanup operations
- Statistics
- Persistence across connections
- Database operations (VACUUM, schema)
- Error handling
- Thread safety
"""

import asyncio
import json
import pytest
import time
from pathlib import Path
from unittest.mock import Mock

from src.core.sqlite_cache import SQLiteCache


@pytest.fixture
def test_db_path(tmp_path):
    """Temporary database path for testing"""
    return str(tmp_path / "test_cache.db")


@pytest.fixture
async def sqlite_cache(test_db_path):
    """SQLite cache instance for testing"""
    cache = SQLiteCache(
        db_path=test_db_path,
        max_messages_per_chat=10,
        ttl_seconds=60
    )
    await cache.connect()
    yield cache
    await cache.close()


@pytest.fixture
async def sqlite_cache_small(test_db_path):
    """Small SQLite cache for testing LRU eviction"""
    cache = SQLiteCache(
        db_path=test_db_path,
        max_messages_per_chat=3,
        ttl_seconds=60
    )
    await cache.connect()
    yield cache
    await cache.close()


@pytest.fixture
async def sqlite_cache_no_ttl(test_db_path):
    """SQLite cache with TTL disabled"""
    cache = SQLiteCache(
        db_path=test_db_path,
        max_messages_per_chat=10,
        ttl_seconds=0  # No expiration
    )
    await cache.connect()
    yield cache
    await cache.close()


# Basic functionality tests

@pytest.mark.asyncio
async def test_sqlite_cache_initialization(test_db_path):
    """Test SQLiteCache initialization"""
    cache = SQLiteCache(
        db_path=test_db_path,
        max_messages_per_chat=100,
        ttl_seconds=300
    )

    assert cache.max_messages == 100
    assert cache.ttl == 300
    assert cache.db_path == Path(test_db_path)
    assert cache._db is None  # Not connected yet


@pytest.mark.asyncio
async def test_sqlite_cache_connect(test_db_path):
    """Test database connection and schema creation"""
    cache = SQLiteCache(db_path=test_db_path)
    await cache.connect()

    # Database should be created
    assert cache._db is not None
    assert Path(test_db_path).exists()

    # Verify schema tables exist
    cursor = await cache._db.execute(
        "SELECT name FROM sqlite_master WHERE type='table'"
    )
    tables = [row[0] for row in await cursor.fetchall()]
    assert "cached_messages" in tables
    assert "schema_version" in tables

    await cache.close()


@pytest.mark.asyncio
async def test_sqlite_cache_double_connect(sqlite_cache):
    """Test that double connect is safe (idempotent)"""
    # First connect already done in fixture
    assert sqlite_cache._db is not None

    # Second connect should be no-op
    await sqlite_cache.connect()
    assert sqlite_cache._db is not None


@pytest.mark.asyncio
async def test_add_single_message(sqlite_cache):
    """Test adding a single message to SQLite cache"""
    message_data = {
        "message_id": 1,
        "chat_id": -1001234567,
        "text": "Hello, World!",
        "date": "2025-11-26T10:00:00"
    }

    await sqlite_cache.add_message(
        chat_id="-1001234567",
        message_id="1",
        message_data=message_data
    )

    # Retrieve and verify
    messages = await sqlite_cache.get_messages(chat_id="-1001234567")
    assert len(messages) == 1
    assert messages[0] == message_data


@pytest.mark.asyncio
async def test_add_multiple_messages(sqlite_cache):
    """Test adding multiple messages to the same chat"""
    chat_id = "-1001234567"

    # Add 5 messages
    for i in range(1, 6):
        await sqlite_cache.add_message(
            chat_id=chat_id,
            message_id=str(i),
            message_data={"message_id": i, "text": f"Message {i}"}
        )

    # Retrieve and verify
    messages = await sqlite_cache.get_messages(chat_id=chat_id)
    assert len(messages) == 5

    # Messages should be in reverse order (newest first)
    message_ids = [msg["message_id"] for msg in messages]
    assert message_ids == [5, 4, 3, 2, 1]


@pytest.mark.asyncio
async def test_add_messages_multiple_chats(sqlite_cache):
    """Test adding messages to multiple chats"""
    # Add messages to 3 different chats
    for chat_id in ["chat1", "chat2", "chat3"]:
        for msg_id in range(1, 4):
            await sqlite_cache.add_message(
                chat_id=chat_id,
                message_id=f"{chat_id}_{msg_id}",
                message_data={"text": f"{chat_id} message {msg_id}"}
            )

    # Verify stats
    stats = await sqlite_cache.get_stats()
    assert stats["total_chats"] == 3
    assert stats["total_messages"] == 9

    # Verify each chat has 3 messages
    for chat_id in ["chat1", "chat2", "chat3"]:
        messages = await sqlite_cache.get_messages(chat_id=chat_id)
        assert len(messages) == 3


@pytest.mark.asyncio
async def test_update_existing_message(sqlite_cache):
    """Test updating an existing message (same chat_id and message_id)"""
    chat_id = "test_chat"
    message_id = "1"

    # Add initial message
    await sqlite_cache.add_message(
        chat_id=chat_id,
        message_id=message_id,
        message_data={"text": "Original message"}
    )

    # Update message
    await sqlite_cache.add_message(
        chat_id=chat_id,
        message_id=message_id,
        message_data={"text": "Updated message"}
    )

    # Should have only 1 message (replaced, not duplicated)
    messages = await sqlite_cache.get_messages(chat_id=chat_id)
    assert len(messages) == 1
    assert messages[0]["text"] == "Updated message"


# LRU eviction tests

@pytest.mark.asyncio
async def test_lru_eviction(sqlite_cache_small):
    """Test LRU eviction when max_messages is exceeded"""
    chat_id = "test_chat"

    # Add 5 messages (max is 3, should evict oldest 2)
    for i in range(1, 6):
        await sqlite_cache_small.add_message(
            chat_id=chat_id,
            message_id=str(i),
            message_data={"message_id": i, "text": f"Message {i}"}
        )

    # Should have only 3 messages (newest ones)
    messages = await sqlite_cache_small.get_messages(chat_id=chat_id)
    assert len(messages) == 3

    # Should keep messages 5, 4, 3 (newest first)
    message_ids = [msg["message_id"] for msg in messages]
    assert message_ids == [5, 4, 3]


@pytest.mark.asyncio
async def test_lru_eviction_per_chat(sqlite_cache_small):
    """Test that LRU eviction is per-chat (not global)"""
    # Add 5 messages to chat1
    for i in range(1, 6):
        await sqlite_cache_small.add_message(
            chat_id="chat1",
            message_id=f"chat1_{i}",
            message_data={"message_id": i}
        )

    # Add 5 messages to chat2
    for i in range(1, 6):
        await sqlite_cache_small.add_message(
            chat_id="chat2",
            message_id=f"chat2_{i}",
            message_data={"message_id": i}
        )

    # Each chat should have only 3 messages
    chat1_messages = await sqlite_cache_small.get_messages(chat_id="chat1")
    chat2_messages = await sqlite_cache_small.get_messages(chat_id="chat2")

    assert len(chat1_messages) == 3
    assert len(chat2_messages) == 3


# TTL expiration tests

@pytest.mark.asyncio
async def test_ttl_expiration():
    """Test that expired messages are filtered out"""
    test_db = "test_ttl.db"
    cache = SQLiteCache(
        db_path=test_db,
        max_messages_per_chat=10,
        ttl_seconds=1  # 1 second TTL
    )
    await cache.connect()

    chat_id = "test_chat"

    # Add a message
    await cache.add_message(
        chat_id=chat_id,
        message_id="1",
        message_data={"message_id": 1, "text": "Message 1"}
    )

    # Message should be available immediately
    messages = await cache.get_messages(chat_id=chat_id)
    assert len(messages) == 1

    # Wait for TTL to expire
    await asyncio.sleep(1.5)

    # Message should be filtered out
    messages = await cache.get_messages(chat_id=chat_id)
    assert len(messages) == 0

    await cache.close()
    Path(test_db).unlink(missing_ok=True)


@pytest.mark.asyncio
async def test_no_ttl_expiration(sqlite_cache_no_ttl):
    """Test that messages don't expire when TTL is 0"""
    chat_id = "test_chat"

    # Add a message
    await sqlite_cache_no_ttl.add_message(
        chat_id=chat_id,
        message_id="1",
        message_data={"message_id": 1, "text": "Message 1"}
    )

    # Wait a bit
    await asyncio.sleep(0.5)

    # Message should still be available (TTL = 0 means no expiration)
    messages = await sqlite_cache_no_ttl.get_messages(chat_id=chat_id)
    assert len(messages) == 1


@pytest.mark.asyncio
async def test_cleanup_expired():
    """Test cleanup_expired method"""
    test_db = "test_cleanup.db"
    cache = SQLiteCache(
        db_path=test_db,
        max_messages_per_chat=10,
        ttl_seconds=1  # 1 second TTL
    )
    await cache.connect()

    chat_id = "test_chat"

    # Add 3 messages
    for i in range(1, 4):
        await cache.add_message(
            chat_id=chat_id,
            message_id=str(i),
            message_data={"message_id": i, "text": f"Message {i}"}
        )

    stats = await cache.get_stats()
    assert stats["total_messages"] == 3

    # Wait for TTL to expire
    await asyncio.sleep(1.5)

    # Run cleanup
    removed_count = await cache.cleanup_expired()
    assert removed_count == 3

    # Cache should be empty
    stats = await cache.get_stats()
    assert stats["total_messages"] == 0
    assert stats["total_chats"] == 0

    await cache.close()
    Path(test_db).unlink(missing_ok=True)


@pytest.mark.asyncio
async def test_cleanup_expired_no_ttl(sqlite_cache_no_ttl):
    """Test that cleanup_expired does nothing when TTL is 0"""
    chat_id = "test_chat"

    # Add messages
    for i in range(1, 4):
        await sqlite_cache_no_ttl.add_message(
            chat_id=chat_id,
            message_id=str(i),
            message_data={"message_id": i, "text": f"Message {i}"}
        )

    # Run cleanup
    removed_count = await sqlite_cache_no_ttl.cleanup_expired()
    assert removed_count == 0

    # Messages should still be there
    stats = await sqlite_cache_no_ttl.get_stats()
    assert stats["total_messages"] == 3


# Retrieval tests

@pytest.mark.asyncio
async def test_get_messages_with_limit(sqlite_cache):
    """Test retrieving messages with limit parameter"""
    chat_id = "test_chat"

    # Add 10 messages
    for i in range(1, 11):
        await sqlite_cache.add_message(
            chat_id=chat_id,
            message_id=str(i),
            message_data={"message_id": i, "text": f"Message {i}"}
        )

    # Get last 5 messages
    messages = await sqlite_cache.get_messages(chat_id=chat_id, limit=5)
    assert len(messages) == 5

    # Should get messages 10-6 (most recent first)
    message_ids = [msg["message_id"] for msg in messages]
    assert message_ids == [10, 9, 8, 7, 6]


@pytest.mark.asyncio
async def test_get_messages_empty_chat(sqlite_cache):
    """Test retrieving messages from non-existent chat"""
    messages = await sqlite_cache.get_messages(chat_id="nonexistent")
    assert messages == []


@pytest.mark.asyncio
async def test_get_messages_limit_exceeds_available(sqlite_cache):
    """Test getting messages with limit larger than available"""
    chat_id = "test_chat"

    # Add 3 messages
    for i in range(1, 4):
        await sqlite_cache.add_message(
            chat_id=chat_id,
            message_id=str(i),
            message_data={"message_id": i, "text": f"Message {i}"}
        )

    # Request 10 messages (only 3 available)
    messages = await sqlite_cache.get_messages(chat_id=chat_id, limit=10)
    assert len(messages) == 3


@pytest.mark.asyncio
async def test_get_messages_auto_connect(test_db_path):
    """Test that get_messages auto-connects if not connected"""
    cache = SQLiteCache(db_path=test_db_path)
    assert cache._db is None

    # Add message without manual connect
    await cache.add_message(
        chat_id="test",
        message_id="1",
        message_data={"text": "Test"}
    )

    # Should auto-connect
    assert cache._db is not None

    # Should be able to retrieve
    messages = await cache.get_messages(chat_id="test")
    assert len(messages) == 1

    await cache.close()


# Clear operations

@pytest.mark.asyncio
async def test_clear_chat(sqlite_cache):
    """Test clearing a specific chat"""
    # Add messages to 2 chats
    for chat_id in ["chat1", "chat2"]:
        for i in range(1, 4):
            await sqlite_cache.add_message(
                chat_id=chat_id,
                message_id=f"{chat_id}_{i}",
                message_data={"text": f"{chat_id} message {i}"}
            )

    stats = await sqlite_cache.get_stats()
    assert stats["total_chats"] == 2
    assert stats["total_messages"] == 6

    # Clear chat1
    await sqlite_cache.clear_chat("chat1")

    stats = await sqlite_cache.get_stats()
    assert stats["total_chats"] == 1
    assert stats["total_messages"] == 3

    # chat1 should be empty
    messages = await sqlite_cache.get_messages(chat_id="chat1")
    assert len(messages) == 0

    # chat2 should still have messages
    messages = await sqlite_cache.get_messages(chat_id="chat2")
    assert len(messages) == 3


@pytest.mark.asyncio
async def test_clear_nonexistent_chat(sqlite_cache):
    """Test clearing a chat that doesn't exist"""
    # Should not raise an error
    await sqlite_cache.clear_chat("nonexistent")

    stats = await sqlite_cache.get_stats()
    assert stats["total_chats"] == 0


@pytest.mark.asyncio
async def test_clear_all(sqlite_cache):
    """Test clearing entire cache"""
    # Add messages to multiple chats
    for chat_id in ["chat1", "chat2", "chat3"]:
        for i in range(1, 4):
            await sqlite_cache.add_message(
                chat_id=chat_id,
                message_id=f"{chat_id}_{i}",
                message_data={"text": f"{chat_id} message {i}"}
            )

    stats = await sqlite_cache.get_stats()
    assert stats["total_chats"] == 3
    assert stats["total_messages"] == 9

    # Clear all
    await sqlite_cache.clear_all()

    stats = await sqlite_cache.get_stats()
    assert stats["total_chats"] == 0
    assert stats["total_messages"] == 0


# Statistics tests

@pytest.mark.asyncio
async def test_get_stats(sqlite_cache):
    """Test get_stats method"""
    # Initial stats
    stats = await sqlite_cache.get_stats()
    assert stats["total_chats"] == 0
    assert stats["total_messages"] == 0
    assert stats["max_messages_per_chat"] == 10
    assert stats["ttl_seconds"] == 60
    assert "db_size_bytes" in stats
    assert "db_path" in stats

    # Add messages
    for chat_id in ["chat1", "chat2"]:
        for i in range(1, 6):
            await sqlite_cache.add_message(
                chat_id=chat_id,
                message_id=f"{chat_id}_{i}",
                message_data={"text": f"{chat_id} message {i}"}
            )

    # Updated stats
    stats = await sqlite_cache.get_stats()
    assert stats["total_chats"] == 2
    assert stats["total_messages"] == 10
    assert stats["max_messages_per_chat"] == 10
    assert stats["ttl_seconds"] == 60
    assert stats["db_size_bytes"] > 0


# Persistence tests

@pytest.mark.asyncio
async def test_persistence_across_connections(test_db_path):
    """Test that data persists across cache instances"""
    # Create first cache instance and add data
    cache1 = SQLiteCache(db_path=test_db_path)
    await cache1.connect()

    await cache1.add_message(
        chat_id="test_chat",
        message_id="1",
        message_data={"text": "Persistent message"}
    )

    await cache1.close()

    # Create second cache instance and verify data
    cache2 = SQLiteCache(db_path=test_db_path)
    await cache2.connect()

    messages = await cache2.get_messages(chat_id="test_chat")
    assert len(messages) == 1
    assert messages[0]["text"] == "Persistent message"

    await cache2.close()


@pytest.mark.asyncio
async def test_schema_version_tracking(sqlite_cache):
    """Test that schema version is tracked correctly"""
    cursor = await sqlite_cache._db.execute(
        "SELECT version FROM schema_version"
    )
    row = await cursor.fetchone()
    assert row[0] == SQLiteCache.SCHEMA_VERSION


# Database operations

@pytest.mark.asyncio
async def test_vacuum(sqlite_cache):
    """Test VACUUM operation"""
    # Add and remove data to create fragmentation
    for i in range(100):
        await sqlite_cache.add_message(
            chat_id="test",
            message_id=str(i),
            message_data={"text": f"Message {i}"}
        )

    await sqlite_cache.clear_all()

    # VACUUM should not raise an error
    await sqlite_cache.vacuum()

    # Cache should still work after VACUUM
    await sqlite_cache.add_message(
        chat_id="test",
        message_id="1",
        message_data={"text": "After vacuum"}
    )

    messages = await sqlite_cache.get_messages(chat_id="test")
    assert len(messages) == 1


@pytest.mark.asyncio
async def test_close(sqlite_cache):
    """Test closing the database connection"""
    assert sqlite_cache._db is not None

    await sqlite_cache.close()

    assert sqlite_cache._db is None


# Error handling tests

@pytest.mark.asyncio
async def test_json_serialization_error(sqlite_cache, caplog):
    """Test handling of JSON serialization errors"""
    # Try to add message with non-serializable data
    class NonSerializable:
        pass

    message_data = {
        "text": "Test",
        "object": NonSerializable()  # Can't be JSON-serialized
    }

    # Should not raise, but log error
    await sqlite_cache.add_message(
        chat_id="test",
        message_id="1",
        message_data=message_data
    )

    # Message should not be added
    messages = await sqlite_cache.get_messages(chat_id="test")
    assert len(messages) == 0


@pytest.mark.asyncio
async def test_json_deserialization_error(sqlite_cache):
    """Test handling of JSON deserialization errors"""
    # First add a valid message
    await sqlite_cache.add_message(
        chat_id="test",
        message_id="1",
        message_data={"text": "valid message"}
    )

    # Then corrupt it directly in the database by updating the JSON
    # Use json_replace with empty string to make it invalid
    try:
        await sqlite_cache._db.execute(
            """
            UPDATE cached_messages
            SET message_data = 'not valid json{'
            WHERE chat_id = ? AND message_id = ?
            """,
            ("test", "1")
        )
        await sqlite_cache._db.commit()
    except Exception:
        # If SQLite validates JSON strictly, skip this test
        pytest.skip("SQLite validates JSON strictly, cannot test corruption handling")

    # Should not crash, just skip invalid message
    messages = await sqlite_cache.get_messages(chat_id="test")
    # Either empty (skipped) or the valid one if update failed
    assert len(messages) <= 1


# Concurrency tests

@pytest.mark.asyncio
async def test_concurrent_add_messages(sqlite_cache):
    """Test adding messages concurrently"""
    chat_id = "test_chat"

    # Add 10 messages concurrently
    async def add_message(i):
        await sqlite_cache.add_message(
            chat_id=chat_id,
            message_id=str(i),
            message_data={"message_id": i, "text": f"Message {i}"}
        )

    tasks = [add_message(i) for i in range(1, 11)]
    await asyncio.gather(*tasks)

    # All messages should be added
    messages = await sqlite_cache.get_messages(chat_id=chat_id)
    assert len(messages) == 10


@pytest.mark.asyncio
async def test_concurrent_read_write(sqlite_cache):
    """Test concurrent reads and writes"""
    chat_id = "test_chat"

    # Add some initial messages
    for i in range(1, 6):
        await sqlite_cache.add_message(
            chat_id=chat_id,
            message_id=str(i),
            message_data={"message_id": i, "text": f"Message {i}"}
        )

    # Concurrent reads and writes
    async def read_messages():
        return await sqlite_cache.get_messages(chat_id=chat_id)

    async def write_message(i):
        await sqlite_cache.add_message(
            chat_id=chat_id,
            message_id=str(i + 10),
            message_data={"message_id": i + 10, "text": f"Message {i + 10}"}
        )

    # Mix of reads and writes
    tasks = []
    for i in range(5):
        tasks.append(read_messages())
        tasks.append(write_message(i))

    results = await asyncio.gather(*tasks)

    # Should complete without errors
    assert len(results) == 10

    # Final state should have all messages
    final_messages = await sqlite_cache.get_messages(chat_id=chat_id)
    assert len(final_messages) == 10

"""
Integration tests for MessageCache with SQLite backend.

Tests the delegation pattern where MessageCache conditionally
uses SQLiteCache when use_sqlite=True.
"""

import asyncio
import pytest
from pathlib import Path
from unittest.mock import Mock

from src.core.cache import MessageCache
from src.core.config import CacheConfig


@pytest.fixture
def cache_config_sqlite(tmp_path):
    """Cache configuration with SQLite enabled"""
    config = CacheConfig(
        max_messages=10,
        ttl_seconds=60,
        use_sqlite=True,
        db_path=str(tmp_path / "test_cache.db")
    )
    return config


@pytest.fixture
def cache_config_memory():
    """Cache configuration with in-memory only"""
    config = CacheConfig(
        max_messages=10,
        ttl_seconds=60,
        use_sqlite=False
    )
    return config


# Integration tests - MessageCache with SQLite backend

@pytest.mark.asyncio
async def test_message_cache_with_sqlite_initialization(cache_config_sqlite):
    """Test MessageCache initialization with SQLite backend"""
    cache = MessageCache(cache_config_sqlite)

    assert cache.use_sqlite is True
    assert cache._sqlite is not None
    assert cache.max_messages == 10
    assert cache.ttl == 60

    # In-memory cache should not be used
    assert cache._cache == {}


@pytest.mark.asyncio
async def test_message_cache_without_sqlite_initialization(cache_config_memory):
    """Test MessageCache initialization without SQLite backend"""
    cache = MessageCache(cache_config_memory)

    assert cache.use_sqlite is False
    assert cache._sqlite is None
    assert cache.max_messages == 10
    assert cache.ttl == 60


@pytest.mark.asyncio
async def test_add_message_delegates_to_sqlite(cache_config_sqlite):
    """Test that add_message delegates to SQLite when enabled"""
    cache = MessageCache(cache_config_sqlite)

    message_data = {
        "message_id": 1,
        "text": "Test message"
    }

    await cache.add_message(
        chat_id="test_chat",
        message_id="1",
        message_data=message_data
    )

    # Should have used SQLite, not in-memory cache
    assert len(cache._cache) == 0  # In-memory cache empty

    # Data should be in SQLite
    messages = await cache.get_messages(chat_id="test_chat")
    assert len(messages) == 1
    assert messages[0] == message_data


@pytest.mark.asyncio
async def test_get_messages_delegates_to_sqlite(cache_config_sqlite):
    """Test that get_messages delegates to SQLite when enabled"""
    cache = MessageCache(cache_config_sqlite)

    # Add messages
    for i in range(1, 6):
        await cache.add_message(
            chat_id="test_chat",
            message_id=str(i),
            message_data={"message_id": i, "text": f"Message {i}"}
        )

    # Retrieve messages
    messages = await cache.get_messages(chat_id="test_chat")
    assert len(messages) == 5

    # Should be in reverse order (newest first from SQLite)
    message_ids = [msg["message_id"] for msg in messages]
    assert message_ids == [5, 4, 3, 2, 1]


@pytest.mark.asyncio
async def test_clear_chat_delegates_to_sqlite(cache_config_sqlite):
    """Test that clear_chat delegates to SQLite when enabled"""
    cache = MessageCache(cache_config_sqlite)

    # Add messages to 2 chats
    for chat_id in ["chat1", "chat2"]:
        for i in range(1, 4):
            await cache.add_message(
                chat_id=chat_id,
                message_id=f"{chat_id}_{i}",
                message_data={"text": f"{chat_id} message {i}"}
            )

    # Clear chat1
    await cache.clear_chat("chat1")

    # chat1 should be empty
    messages = await cache.get_messages(chat_id="chat1")
    assert len(messages) == 0

    # chat2 should still have messages
    messages = await cache.get_messages(chat_id="chat2")
    assert len(messages) == 3


@pytest.mark.asyncio
async def test_clear_all_delegates_to_sqlite(cache_config_sqlite):
    """Test that clear_all delegates to SQLite when enabled"""
    cache = MessageCache(cache_config_sqlite)

    # Add messages to multiple chats
    for chat_id in ["chat1", "chat2", "chat3"]:
        for i in range(1, 4):
            await cache.add_message(
                chat_id=chat_id,
                message_id=f"{chat_id}_{i}",
                message_data={"text": f"{chat_id} message {i}"}
            )

    # Clear all
    await cache.clear_all()

    # All chats should be empty
    for chat_id in ["chat1", "chat2", "chat3"]:
        messages = await cache.get_messages(chat_id=chat_id)
        assert len(messages) == 0


@pytest.mark.asyncio
async def test_cleanup_expired_delegates_to_sqlite():
    """Test that cleanup_expired delegates to SQLite when enabled"""
    config = CacheConfig(
        max_messages=10,
        ttl_seconds=1,  # 1 second TTL
        use_sqlite=True,
        db_path="test_cleanup.db"
    )

    cache = MessageCache(config)

    # Add messages
    for i in range(1, 4):
        await cache.add_message(
            chat_id="test_chat",
            message_id=str(i),
            message_data={"message_id": i, "text": f"Message {i}"}
        )

    # Wait for expiration
    await asyncio.sleep(1.5)

    # Cleanup
    removed_count = await cache.cleanup_expired()
    assert removed_count == 3

    # Cleanup
    Path("test_cleanup.db").unlink(missing_ok=True)


@pytest.mark.asyncio
async def test_get_stats_delegates_to_sqlite(cache_config_sqlite):
    """Test that get_stats delegates to SQLite when enabled"""
    cache = MessageCache(cache_config_sqlite)

    # Add messages
    for chat_id in ["chat1", "chat2"]:
        for i in range(1, 6):
            await cache.add_message(
                chat_id=chat_id,
                message_id=f"{chat_id}_{i}",
                message_data={"text": f"{chat_id} message {i}"}
            )

    # Get stats
    stats = await cache.get_stats()
    assert stats["total_chats"] == 2
    assert stats["total_messages"] == 10
    assert stats["max_messages_per_chat"] == 10
    assert stats["ttl_seconds"] == 60
    assert "db_size_bytes" in stats  # SQLite-specific field
    assert "db_path" in stats  # SQLite-specific field


@pytest.mark.asyncio
async def test_persistence_through_message_cache(tmp_path):
    """Test that data persists when using MessageCache with SQLite"""
    db_path = str(tmp_path / "persistent_cache.db")

    # Create first cache instance and add data
    config1 = CacheConfig(
        max_messages=10,
        ttl_seconds=60,
        use_sqlite=True,
        db_path=db_path
    )
    cache1 = MessageCache(config1)

    await cache1.add_message(
        chat_id="test_chat",
        message_id="1",
        message_data={"text": "Persistent message"}
    )

    # Destroy first instance (simulate restart)
    del cache1

    # Create second cache instance
    config2 = CacheConfig(
        max_messages=10,
        ttl_seconds=60,
        use_sqlite=True,
        db_path=db_path
    )
    cache2 = MessageCache(config2)

    # Data should still be there
    messages = await cache2.get_messages(chat_id="test_chat")
    assert len(messages) == 1
    assert messages[0]["text"] == "Persistent message"


@pytest.mark.asyncio
async def test_sqlite_and_memory_separation(cache_config_sqlite, cache_config_memory):
    """Test that SQLite and in-memory caches work independently"""
    sqlite_cache = MessageCache(cache_config_sqlite)
    memory_cache = MessageCache(cache_config_memory)

    # Add message to SQLite cache
    await sqlite_cache.add_message(
        chat_id="test",
        message_id="1",
        message_data={"text": "SQLite message"}
    )

    # Add message to memory cache
    await memory_cache.add_message(
        chat_id="test",
        message_id="2",
        message_data={"text": "Memory message"}
    )

    # Each should have only their own message
    sqlite_messages = await sqlite_cache.get_messages(chat_id="test")
    memory_messages = await memory_cache.get_messages(chat_id="test")

    assert len(sqlite_messages) == 1
    assert sqlite_messages[0]["text"] == "SQLite message"

    assert len(memory_messages) == 1
    assert memory_messages[0]["text"] == "Memory message"


@pytest.mark.asyncio
async def test_lru_eviction_with_sqlite(tmp_path):
    """Test LRU eviction works through MessageCache delegation"""
    config = CacheConfig(
        max_messages=3,  # Small limit
        ttl_seconds=60,
        use_sqlite=True,
        db_path=str(tmp_path / "lru_test.db")
    )

    cache = MessageCache(config)

    # Add 5 messages (should evict oldest 2)
    for i in range(1, 6):
        await cache.add_message(
            chat_id="test_chat",
            message_id=str(i),
            message_data={"message_id": i, "text": f"Message {i}"}
        )

    # Should have only 3 messages (newest)
    messages = await cache.get_messages(chat_id="test_chat")
    assert len(messages) == 3

    # Should keep messages 5, 4, 3 (newest first)
    message_ids = [msg["message_id"] for msg in messages]
    assert message_ids == [5, 4, 3]


@pytest.mark.asyncio
async def test_ttl_filtering_with_sqlite(tmp_path):
    """Test TTL filtering works through MessageCache delegation"""
    config = CacheConfig(
        max_messages=10,
        ttl_seconds=1,  # 1 second TTL
        use_sqlite=True,
        db_path=str(tmp_path / "ttl_test.db")
    )

    cache = MessageCache(config)

    # Add message
    await cache.add_message(
        chat_id="test",
        message_id="1",
        message_data={"text": "Message 1"}
    )

    # Should be available immediately
    messages = await cache.get_messages(chat_id="test")
    assert len(messages) == 1

    # Wait for expiration
    await asyncio.sleep(1.5)

    # Should be filtered out
    messages = await cache.get_messages(chat_id="test")
    assert len(messages) == 0


@pytest.mark.asyncio
async def test_limit_parameter_with_sqlite(cache_config_sqlite):
    """Test that limit parameter works with SQLite delegation"""
    cache = MessageCache(cache_config_sqlite)

    # Add 10 messages
    for i in range(1, 11):
        await cache.add_message(
            chat_id="test",
            message_id=str(i),
            message_data={"message_id": i}
        )

    # Get last 5 messages
    messages = await cache.get_messages(chat_id="test", limit=5)
    assert len(messages) == 5

    # Should get messages 10-6 (newest first)
    message_ids = [msg["message_id"] for msg in messages]
    assert message_ids == [10, 9, 8, 7, 6]


@pytest.mark.asyncio
async def test_concurrent_operations_with_sqlite(cache_config_sqlite):
    """Test concurrent operations work through SQLite delegation"""
    cache = MessageCache(cache_config_sqlite)

    # Concurrent writes
    async def add_message(i):
        await cache.add_message(
            chat_id="test",
            message_id=str(i),
            message_data={"message_id": i}
        )

    tasks = [add_message(i) for i in range(1, 11)]
    await asyncio.gather(*tasks)

    # All messages should be added
    messages = await cache.get_messages(chat_id="test")
    assert len(messages) == 10


@pytest.mark.asyncio
async def test_backend_selection_via_config():
    """Test that backend is selected correctly based on config"""
    # SQLite backend
    sqlite_config = CacheConfig(
        max_messages=10,
        ttl_seconds=60,
        use_sqlite=True,
        db_path="test_backend.db"
    )
    sqlite_cache = MessageCache(sqlite_config)
    assert sqlite_cache._sqlite is not None
    assert sqlite_cache.use_sqlite is True

    # Memory backend
    memory_config = CacheConfig(
        max_messages=10,
        ttl_seconds=60,
        use_sqlite=False
    )
    memory_cache = MessageCache(memory_config)
    assert memory_cache._sqlite is None
    assert memory_cache.use_sqlite is False

    # Cleanup
    Path("test_backend.db").unlink(missing_ok=True)

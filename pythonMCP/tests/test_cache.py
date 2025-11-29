"""
Tests for src/core/cache.py - MessageCache
"""

import asyncio
import pytest
import time
from unittest.mock import Mock

from src.core.cache import MessageCache, CachedMessage


@pytest.fixture
def cache_config():
    """Cache configuration for testing"""
    config = Mock()
    config.max_messages = 10
    config.ttl_seconds = 60  # 1 minute
    config.use_sqlite = False
    return config


@pytest.fixture
def cache_config_no_ttl():
    """Cache configuration with TTL disabled"""
    config = Mock()
    config.max_messages = 10
    config.ttl_seconds = 0  # No expiration
    config.use_sqlite = False
    return config


@pytest.fixture
def cache_config_small():
    """Small cache for testing LRU eviction"""
    config = Mock()
    config.max_messages = 3
    config.ttl_seconds = 60
    config.use_sqlite = False
    return config


# Basic functionality tests

@pytest.mark.asyncio
async def test_cache_initialization(cache_config):
    """Test MessageCache initialization"""
    cache = MessageCache(cache_config)

    assert cache.max_messages == 10
    assert cache.ttl == 60
    assert cache._cache == {}

    stats = (await cache.get_stats())
    assert stats["total_chats"] == 0
    assert stats["total_messages"] == 0
    assert stats["max_messages_per_chat"] == 10
    assert stats["ttl_seconds"] == 60


@pytest.mark.asyncio
async def test_add_single_message(cache_config):
    """Test adding a single message to cache"""
    cache = MessageCache(cache_config)

    message_data = {
        "message_id": 1,
        "chat_id": -1001234567,
        "text": "Hello, World!",
        "date": "2025-11-26T10:00:00"
    }

    await cache.add_message(
        chat_id="-1001234567",
        message_id="1",
        message_data=message_data
    )

    stats = (await cache.get_stats())
    assert stats["total_chats"] == 1
    assert stats["total_messages"] == 1

    messages = await cache.get_messages(chat_id="-1001234567")
    assert len(messages) == 1
    assert messages[0] == message_data


@pytest.mark.asyncio
async def test_add_multiple_messages(cache_config):
    """Test adding multiple messages to the same chat"""
    cache = MessageCache(cache_config)
    chat_id = "-1001234567"

    # Add 5 messages
    for i in range(1, 6):
        await cache.add_message(
            chat_id=chat_id,
            message_id=str(i),
            message_data={"message_id": i, "text": f"Message {i}"}
        )

    stats = (await cache.get_stats())
    assert stats["total_chats"] == 1
    assert stats["total_messages"] == 5

    messages = await cache.get_messages(chat_id=chat_id)
    assert len(messages) == 5


@pytest.mark.asyncio
async def test_add_messages_multiple_chats(cache_config):
    """Test adding messages to multiple chats"""
    cache = MessageCache(cache_config)

    # Add messages to 3 different chats
    for chat_id in ["chat1", "chat2", "chat3"]:
        for msg_id in range(1, 4):
            await cache.add_message(
                chat_id=chat_id,
                message_id=f"{chat_id}_{msg_id}",
                message_data={"text": f"{chat_id} message {msg_id}"}
            )

    stats = (await cache.get_stats())
    assert stats["total_chats"] == 3
    assert stats["total_messages"] == 9  # 3 chats * 3 messages

    # Verify each chat has 3 messages
    for chat_id in ["chat1", "chat2", "chat3"]:
        messages = await cache.get_messages(chat_id=chat_id)
        assert len(messages) == 3


# LRU eviction tests

@pytest.mark.asyncio
async def test_lru_eviction(cache_config_small):
    """Test LRU eviction when max_messages is exceeded"""
    cache = MessageCache(cache_config_small)  # max_messages = 3
    chat_id = "test_chat"

    # Add 5 messages (should evict oldest 2)
    for i in range(1, 6):
        await cache.add_message(
            chat_id=chat_id,
            message_id=str(i),
            message_data={"message_id": i, "text": f"Message {i}"}
        )

    messages = await cache.get_messages(chat_id=chat_id)
    assert len(messages) == 3

    # Should keep messages 3, 4, 5 (newest)
    message_ids = [msg["message_id"] for msg in messages]
    assert message_ids == [3, 4, 5]


@pytest.mark.asyncio
async def test_message_update_moves_to_end(cache_config_small):
    """Test that updating an existing message moves it to the end (most recent)"""
    cache = MessageCache(cache_config_small)  # max_messages = 3
    chat_id = "test_chat"

    # Add 3 messages
    for i in range(1, 4):
        await cache.add_message(
            chat_id=chat_id,
            message_id=str(i),
            message_data={"message_id": i, "text": f"Message {i}"}
        )

    # Update message 1 (should move to end)
    await cache.add_message(
        chat_id=chat_id,
        message_id="1",
        message_data={"message_id": 1, "text": "Updated Message 1"}
    )

    # Add message 4 (should evict message 2, not message 1)
    await cache.add_message(
        chat_id=chat_id,
        message_id="4",
        message_data={"message_id": 4, "text": "Message 4"}
    )

    messages = await cache.get_messages(chat_id=chat_id)
    assert len(messages) == 3

    # Should have messages 3, 1 (updated), 4
    message_ids = [msg["message_id"] for msg in messages]
    assert 2 not in message_ids  # Message 2 was evicted
    assert 1 in message_ids  # Message 1 was kept (recently updated)
    assert 3 in message_ids
    assert 4 in message_ids


# TTL expiration tests

@pytest.mark.asyncio
async def test_ttl_expiration(cache_config):
    """Test that expired messages are filtered out"""
    config = Mock()
    config.max_messages = 10
    config.ttl_seconds = 1  # 1 second TTL
    config.use_sqlite = False

    cache = MessageCache(config)
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


@pytest.mark.asyncio
async def test_no_ttl_expiration(cache_config_no_ttl):
    """Test that messages don't expire when TTL is 0"""
    cache = MessageCache(cache_config_no_ttl)
    chat_id = "test_chat"

    # Add a message
    await cache.add_message(
        chat_id=chat_id,
        message_id="1",
        message_data={"message_id": 1, "text": "Message 1"}
    )

    # Wait a bit
    await asyncio.sleep(0.5)

    # Message should still be available (TTL = 0 means no expiration)
    messages = await cache.get_messages(chat_id=chat_id)
    assert len(messages) == 1


@pytest.mark.asyncio
async def test_cleanup_expired():
    """Test cleanup_expired method"""
    config = Mock()
    config.max_messages = 10
    config.ttl_seconds = 1  # 1 second TTL
    config.use_sqlite = False

    cache = MessageCache(config)
    chat_id = "test_chat"

    # Add 3 messages
    for i in range(1, 4):
        await cache.add_message(
            chat_id=chat_id,
            message_id=str(i),
            message_data={"message_id": i, "text": f"Message {i}"}
        )

    assert (await cache.get_stats())["total_messages"] == 3

    # Wait for TTL to expire
    await asyncio.sleep(1.5)

    # Run cleanup
    removed_count = await cache.cleanup_expired()
    assert removed_count == 3

    # Cache should be empty
    assert (await cache.get_stats())["total_messages"] == 0
    assert (await cache.get_stats())["total_chats"] == 0


@pytest.mark.asyncio
async def test_cleanup_expired_no_ttl(cache_config_no_ttl):
    """Test that cleanup_expired does nothing when TTL is 0"""
    cache = MessageCache(cache_config_no_ttl)
    chat_id = "test_chat"

    # Add messages
    for i in range(1, 4):
        await cache.add_message(
            chat_id=chat_id,
            message_id=str(i),
            message_data={"message_id": i, "text": f"Message {i}"}
        )

    # Run cleanup
    removed_count = await cache.cleanup_expired()
    assert removed_count == 0

    # Messages should still be there
    assert (await cache.get_stats())["total_messages"] == 3


# Retrieval tests

@pytest.mark.asyncio
async def test_get_messages_with_limit(cache_config):
    """Test retrieving messages with limit parameter"""
    cache = MessageCache(cache_config)
    chat_id = "test_chat"

    # Add 10 messages
    for i in range(1, 11):
        await cache.add_message(
            chat_id=chat_id,
            message_id=str(i),
            message_data={"message_id": i, "text": f"Message {i}"}
        )

    # Get last 5 messages
    messages = await cache.get_messages(chat_id=chat_id, limit=5)
    assert len(messages) == 5

    # Should get messages 6-10 (most recent)
    message_ids = [msg["message_id"] for msg in messages]
    assert message_ids == [6, 7, 8, 9, 10]


@pytest.mark.asyncio
async def test_get_messages_empty_chat(cache_config):
    """Test retrieving messages from non-existent chat"""
    cache = MessageCache(cache_config)

    messages = await cache.get_messages(chat_id="nonexistent")
    assert messages == []


@pytest.mark.asyncio
async def test_get_messages_limit_exceeds_available(cache_config):
    """Test getting messages with limit larger than available"""
    cache = MessageCache(cache_config)
    chat_id = "test_chat"

    # Add 3 messages
    for i in range(1, 4):
        await cache.add_message(
            chat_id=chat_id,
            message_id=str(i),
            message_data={"message_id": i, "text": f"Message {i}"}
        )

    # Request 10 messages (only 3 available)
    messages = await cache.get_messages(chat_id=chat_id, limit=10)
    assert len(messages) == 3


# Clear operations

@pytest.mark.asyncio
async def test_clear_chat(cache_config):
    """Test clearing a specific chat"""
    cache = MessageCache(cache_config)

    # Add messages to 2 chats
    for chat_id in ["chat1", "chat2"]:
        for i in range(1, 4):
            await cache.add_message(
                chat_id=chat_id,
                message_id=f"{chat_id}_{i}",
                message_data={"text": f"{chat_id} message {i}"}
            )

    assert (await cache.get_stats())["total_chats"] == 2
    assert (await cache.get_stats())["total_messages"] == 6

    # Clear chat1
    await cache.clear_chat("chat1")

    assert (await cache.get_stats())["total_chats"] == 1
    assert (await cache.get_stats())["total_messages"] == 3

    # chat1 should be empty
    messages = await cache.get_messages(chat_id="chat1")
    assert len(messages) == 0

    # chat2 should still have messages
    messages = await cache.get_messages(chat_id="chat2")
    assert len(messages) == 3


@pytest.mark.asyncio
async def test_clear_nonexistent_chat(cache_config):
    """Test clearing a chat that doesn't exist"""
    cache = MessageCache(cache_config)

    # Should not raise an error
    await cache.clear_chat("nonexistent")

    assert (await cache.get_stats())["total_chats"] == 0


@pytest.mark.asyncio
async def test_clear_all(cache_config):
    """Test clearing entire cache"""
    cache = MessageCache(cache_config)

    # Add messages to multiple chats
    for chat_id in ["chat1", "chat2", "chat3"]:
        for i in range(1, 4):
            await cache.add_message(
                chat_id=chat_id,
                message_id=f"{chat_id}_{i}",
                message_data={"text": f"{chat_id} message {i}"}
            )

    assert (await cache.get_stats())["total_chats"] == 3
    assert (await cache.get_stats())["total_messages"] == 9

    # Clear all
    await cache.clear_all()

    assert (await cache.get_stats())["total_chats"] == 0
    assert (await cache.get_stats())["total_messages"] == 0


# Statistics tests

@pytest.mark.asyncio
async def test_get_stats(cache_config):
    """Test get_stats method"""
    cache = MessageCache(cache_config)

    # Initial stats
    stats = (await cache.get_stats())
    assert stats["total_chats"] == 0
    assert stats["total_messages"] == 0
    assert stats["max_messages_per_chat"] == 10
    assert stats["ttl_seconds"] == 60

    # Add messages
    for chat_id in ["chat1", "chat2"]:
        for i in range(1, 6):
            await cache.add_message(
                chat_id=chat_id,
                message_id=f"{chat_id}_{i}",
                message_data={"text": f"{chat_id} message {i}"}
            )

    # Updated stats
    stats = (await cache.get_stats())
    assert stats["total_chats"] == 2
    assert stats["total_messages"] == 10
    assert stats["max_messages_per_chat"] == 10
    assert stats["ttl_seconds"] == 60


# CachedMessage dataclass tests

def test_cached_message_creation():
    """Test CachedMessage dataclass creation"""
    msg = CachedMessage(
        message_id="123",
        chat_id="-1001234567",
        data={"text": "Hello"},
        timestamp=time.time(),
        ttl=60
    )

    assert msg.message_id == "123"
    assert msg.chat_id == "-1001234567"
    assert msg.data == {"text": "Hello"}
    assert isinstance(msg.timestamp, float)
    assert msg.ttl == 60


# Thread safety tests (basic concurrency)

@pytest.mark.asyncio
async def test_concurrent_add_messages(cache_config):
    """Test adding messages concurrently"""
    cache = MessageCache(cache_config)
    chat_id = "test_chat"

    # Add 10 messages concurrently
    async def add_message(i):
        await cache.add_message(
            chat_id=chat_id,
            message_id=str(i),
            message_data={"message_id": i, "text": f"Message {i}"}
        )

    tasks = [add_message(i) for i in range(1, 11)]
    await asyncio.gather(*tasks)

    # All messages should be added
    messages = await cache.get_messages(chat_id=chat_id)
    assert len(messages) == 10


@pytest.mark.asyncio
async def test_concurrent_read_write(cache_config):
    """Test concurrent reads and writes"""
    cache = MessageCache(cache_config)
    chat_id = "test_chat"

    # Add some initial messages
    for i in range(1, 6):
        await cache.add_message(
            chat_id=chat_id,
            message_id=str(i),
            message_data={"message_id": i, "text": f"Message {i}"}
        )

    # Concurrent reads and writes
    async def read_messages():
        return await cache.get_messages(chat_id=chat_id)

    async def write_message(i):
        await cache.add_message(
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
    final_messages = await cache.get_messages(chat_id=chat_id)
    assert len(final_messages) == 10

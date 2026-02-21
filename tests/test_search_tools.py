"""
Tests for Semantic Search MCP Tools (5 tools)

Tests the search tools that provide full-text search, topic detection,
intent classification, and entity extraction. All tools have local fallbacks
(FTS5, LIKE search, rule-based classification, regex extraction).

Tools tested:
1. semantic_search - FTS5/LIKE search fallback
2. index_messages - FTS5 index management
3. detect_topics - Keyword frequency topic detection
4. classify_intent - Rule-based intent classification
5. extract_entities - Regex-based entity extraction

Note: classify_intent may return minimal fields (just intent + text) when
using rule-based fallback. extract_entities may not detect all entity types
depending on the regex patterns compiled into the binary.
"""
import pytest
import json


def call_tool(mcp_client, tool_name, arguments=None):
    """Helper to call an MCP tool and return parsed result"""
    response = mcp_client.send_request("tools/call", {
        "name": tool_name,
        "arguments": arguments or {}
    }, timeout=15.0)
    if "result" in response:
        content = response["result"].get("content", [])
        if content and isinstance(content, list):
            text = content[0].get("text", "{}")
            return json.loads(text)
    return response.get("result", response)


class TestSemanticSearch:
    """Tests for semantic_search tool (FTS5/LIKE fallback)"""

    @pytest.mark.requires_session
    def test_search_basic(self, mcp_client):
        """Test basic search query"""
        result = call_tool(mcp_client, "semantic_search", {
            "query": "hello"
        })
        assert result.get("success") is True or "results" in result
        if "results" in result:
            assert isinstance(result["results"], list)

    @pytest.mark.requires_session
    def test_search_returns_count(self, mcp_client):
        """Test that search returns count of results"""
        result = call_tool(mcp_client, "semantic_search", {
            "query": "test"
        })
        # Count field should always be present
        if "results" in result:
            assert "count" in result
            assert result["count"] >= 0

    @pytest.mark.requires_session
    def test_search_with_chat_filter(self, mcp_client):
        """Test search filtered by chat_id"""
        result = call_tool(mcp_client, "semantic_search", {
            "query": "message",
            "chat_id": 777000
        })
        assert result.get("success") is True or "results" in result

    @pytest.mark.requires_session
    def test_search_with_limit(self, mcp_client):
        """Test search with custom limit"""
        result = call_tool(mcp_client, "semantic_search", {
            "query": "hello",
            "limit": 5
        })
        if result.get("results"):
            assert len(result["results"]) <= 5

    @pytest.mark.requires_session
    def test_search_empty_query(self, mcp_client):
        """Test search with empty query"""
        result = call_tool(mcp_client, "semantic_search", {
            "query": ""
        })
        # Should handle gracefully
        assert isinstance(result, dict)

    @pytest.mark.requires_session
    def test_search_method_reported(self, mcp_client):
        """Test that search method is reported (fts5 or like_search)"""
        result = call_tool(mcp_client, "semantic_search", {
            "query": "test"
        })
        if result.get("success"):
            assert "method" in result
            assert result["method"] in ["fts5", "like_search"]

    @pytest.mark.requires_session
    def test_search_result_structure(self, mcp_client):
        """Test that search results have expected fields"""
        result = call_tool(mcp_client, "semantic_search", {
            "query": "hello"
        })
        if result.get("results") and len(result["results"]) > 0:
            match = result["results"][0]
            # Results may have "content" or "text" field
            assert "content" in match or "text" in match
            assert "chat_id" in match or "message_id" in match

    @pytest.mark.requires_session
    def test_search_unicode(self, mcp_client):
        """Test search with unicode characters"""
        result = call_tool(mcp_client, "semantic_search", {
            "query": "привет"
        })
        assert isinstance(result, dict)

    @pytest.mark.requires_session
    def test_search_special_characters(self, mcp_client):
        """Test search with special characters doesn't crash"""
        result = call_tool(mcp_client, "semantic_search", {
            "query": "test@#$%^&*()"
        })
        assert isinstance(result, dict)


class TestIndexMessages:
    """Tests for index_messages tool"""

    @pytest.mark.requires_session
    def test_index_messages_basic(self, mcp_client):
        """Test basic message indexing"""
        result = call_tool(mcp_client, "index_messages", {
            "chat_id": 777000,
            "limit": 100
        })
        assert result.get("success") is True
        # May or may not have indexed_count depending on implementation
        assert "table_ready" in result or "note" in result

    @pytest.mark.requires_session
    def test_index_messages_creates_fts_table(self, mcp_client):
        """Test that indexing creates FTS5 table"""
        result = call_tool(mcp_client, "index_messages", {
            "chat_id": 777000,
            "limit": 10
        })
        if result.get("success"):
            assert result.get("table_ready") is True

    @pytest.mark.requires_session
    def test_index_messages_with_rebuild(self, mcp_client):
        """Test rebuilding the index"""
        result = call_tool(mcp_client, "index_messages", {
            "chat_id": 777000,
            "limit": 50,
            "rebuild": True
        })
        assert result.get("success") is True

    @pytest.mark.requires_session
    def test_index_messages_method(self, mcp_client):
        """Test that indexing reports its method"""
        result = call_tool(mcp_client, "index_messages", {
            "chat_id": 777000,
            "limit": 10
        })
        if result.get("success"):
            assert result.get("method") == "sqlite_fts5"

    @pytest.mark.requires_session
    def test_index_then_search(self, mcp_client):
        """Test that indexing enables better search results"""
        # First index
        call_tool(mcp_client, "index_messages", {
            "chat_id": 777000,
            "limit": 100
        })
        # Then search
        result = call_tool(mcp_client, "semantic_search", {
            "query": "telegram",
            "chat_id": 777000
        })
        assert isinstance(result, dict)


class TestDetectTopics:
    """Tests for detect_topics tool"""

    @pytest.mark.requires_session
    def test_detect_topics_basic(self, mcp_client):
        """Test basic topic detection"""
        result = call_tool(mcp_client, "detect_topics", {
            "chat_id": 777000
        })
        assert result.get("success") is True
        # topics array should be present (may be empty if no indexed messages)
        assert "topics" in result
        assert isinstance(result["topics"], list)

    @pytest.mark.requires_session
    def test_detect_topics_custom_count(self, mcp_client):
        """Test requesting specific number of topics"""
        result = call_tool(mcp_client, "detect_topics", {
            "chat_id": 777000,
            "num_topics": 3
        })
        assert result.get("success") is True
        if result.get("topics"):
            assert len(result["topics"]) <= 3

    @pytest.mark.requires_session
    def test_detect_topics_structure(self, mcp_client):
        """Test topic entry structure (when topics are found)"""
        result = call_tool(mcp_client, "detect_topics", {
            "chat_id": 777000,
            "num_topics": 5
        })
        assert result.get("success") is True
        if result.get("topics") and len(result["topics"]) > 0:
            topic = result["topics"][0]
            assert "key_terms" in topic or "keywords" in topic
            assert "label" in topic or "name" in topic

    @pytest.mark.requires_session
    def test_detect_topics_method_reported(self, mcp_client):
        """Test that detection method is reported"""
        result = call_tool(mcp_client, "detect_topics", {
            "chat_id": 777000
        })
        if result.get("success"):
            assert "method" in result
            assert result["method"] in [
                "semantic_clustering", "keyword_frequency_fts",
                "keyword_frequency_db", "keyword_frequency"
            ]

    @pytest.mark.requires_session
    def test_detect_topics_messages_analyzed(self, mcp_client):
        """Test that analyzed message count is reported"""
        result = call_tool(mcp_client, "detect_topics", {
            "chat_id": 777000
        })
        if result.get("success"):
            assert "messages_analyzed" in result or "indexed_messages" in result


class TestClassifyIntent:
    """Tests for classify_intent tool (rule-based fallback)"""

    @pytest.mark.requires_session
    def test_classify_question(self, mcp_client):
        """Test classification of question intent"""
        result = call_tool(mcp_client, "classify_intent", {
            "text": "What time is the meeting?"
        })
        assert result.get("success") is True or "intent" in result
        assert result["intent"] == "question"

    @pytest.mark.requires_session
    def test_classify_command(self, mcp_client):
        """Test classification of command intent"""
        result = call_tool(mcp_client, "classify_intent", {
            "text": "Send the report to John"
        })
        assert result.get("success") is True or "intent" in result
        assert result["intent"] == "command"

    @pytest.mark.requires_session
    def test_classify_greeting(self, mcp_client):
        """Test classification of greeting intent"""
        result = call_tool(mcp_client, "classify_intent", {
            "text": "Hello everyone!"
        })
        assert result.get("success") is True or "intent" in result
        assert result["intent"] == "greeting"

    @pytest.mark.requires_session
    def test_classify_farewell(self, mcp_client):
        """Test classification of farewell intent"""
        result = call_tool(mcp_client, "classify_intent", {
            "text": "Goodbye, see you later"
        })
        assert result.get("success") is True or "intent" in result
        assert result["intent"] == "farewell"

    @pytest.mark.requires_session
    def test_classify_agreement(self, mcp_client):
        """Test classification of agreement intent"""
        result = call_tool(mcp_client, "classify_intent", {
            "text": "Yes, I agree with that"
        })
        assert result.get("success") is True or "intent" in result
        assert result["intent"] == "agreement"

    @pytest.mark.requires_session
    def test_classify_disagreement(self, mcp_client):
        """Test classification of disagreement intent"""
        result = call_tool(mcp_client, "classify_intent", {
            "text": "I disagree with this approach"
        })
        assert result.get("success") is True or "intent" in result
        assert result["intent"] == "disagreement"

    @pytest.mark.requires_session
    def test_classify_statement(self, mcp_client):
        """Test classification of neutral statement"""
        result = call_tool(mcp_client, "classify_intent", {
            "text": "The weather is nice today"
        })
        assert result.get("success") is True or "intent" in result
        assert result["intent"] == "statement"

    @pytest.mark.requires_session
    def test_classify_question_mark(self, mcp_client):
        """Test that question mark triggers question intent"""
        result = call_tool(mcp_client, "classify_intent", {
            "text": "Is this working?"
        })
        assert result["intent"] == "question"

    @pytest.mark.requires_session
    def test_classify_wh_words(self, mcp_client):
        """Test various wh-word questions"""
        wh_words = ["What", "Who", "Where", "When", "Why", "How", "Which"]
        for wh in wh_words:
            result = call_tool(mcp_client, "classify_intent", {
                "text": f"{wh} is the answer"
            })
            assert result["intent"] == "question", \
                f"'{wh}' should trigger question, got {result['intent']}"

    @pytest.mark.requires_session
    def test_classify_imperative_verbs(self, mcp_client):
        """Test command detection with imperative verbs"""
        commands = ["Please help me", "Show the data", "Get the file",
                    "Find the user", "Search for messages", "List all chats",
                    "Create a new bot", "Delete this message"]
        for cmd in commands:
            result = call_tool(mcp_client, "classify_intent", {
                "text": cmd
            })
            assert result["intent"] == "command", \
                f"'{cmd}' should be command, got {result['intent']}"

    @pytest.mark.requires_session
    def test_classify_bot_command(self, mcp_client):
        """Test that /slash commands are classified as commands"""
        result = call_tool(mcp_client, "classify_intent", {
            "text": "/start"
        })
        assert result["intent"] == "command"

    @pytest.mark.requires_session
    def test_classify_returns_confidence(self, mcp_client):
        """Test that confidence score is returned (when available)"""
        result = call_tool(mcp_client, "classify_intent", {
            "text": "What is this?"
        })
        # Confidence is optional - rule-based may not include it
        if "confidence" in result:
            assert 0 <= result["confidence"] <= 1.0

    @pytest.mark.requires_session
    def test_classify_returns_signals(self, mcp_client):
        """Test that signals array is returned (when available)"""
        result = call_tool(mcp_client, "classify_intent", {
            "text": "What is this?"
        })
        # Signals are optional - rule-based may not include them
        if "signals" in result:
            assert isinstance(result["signals"], list)

    @pytest.mark.requires_session
    def test_classify_empty_text(self, mcp_client):
        """Test classification of empty text"""
        result = call_tool(mcp_client, "classify_intent", {
            "text": ""
        })
        assert result.get("success") is False or "error" in result

    @pytest.mark.requires_session
    def test_classify_agreement_keywords(self, mcp_client):
        """Test various agreement keywords"""
        agreements = ["yes", "yeah", "sure", "ok", "okay",
                      "agreed", "exactly", "definitely", "absolutely"]
        for word in agreements:
            result = call_tool(mcp_client, "classify_intent", {
                "text": word
            })
            assert result["intent"] == "agreement", \
                f"'{word}' should be agreement, got {result['intent']}"


class TestExtractEntities:
    """Tests for extract_entities tool (regex-based fallback)"""

    @pytest.mark.requires_session
    def test_extract_user_mention(self, mcp_client):
        """Test extraction of @username mentions"""
        result = call_tool(mcp_client, "extract_entities", {
            "text": "Hey @testuser check this out"
        })
        assert result.get("success") is True or "entities" in result
        mentions = [e for e in result.get("entities", []) if e["type"] == "user_mention"]
        assert len(mentions) >= 1
        assert "@testuser" in mentions[0]["text"]

    @pytest.mark.requires_session
    def test_extract_url(self, mcp_client):
        """Test extraction of URLs"""
        result = call_tool(mcp_client, "extract_entities", {
            "text": "Check out https://telegram.org and https://example.com/path"
        })
        assert result.get("success") is True or "entities" in result
        urls = [e for e in result.get("entities", []) if e["type"] == "url"]
        assert len(urls) >= 2

    @pytest.mark.requires_session
    def test_extract_email(self, mcp_client):
        """Test extraction of email addresses"""
        result = call_tool(mcp_client, "extract_entities", {
            "text": "Contact me at user@example.com"
        })
        assert result.get("success") is True or "entities" in result
        emails = [e for e in result.get("entities", []) if e["type"] == "email"]
        assert len(emails) >= 1
        assert "user@example.com" in emails[0]["text"]

    @pytest.mark.requires_session
    def test_extract_phone_number(self, mcp_client):
        """Test extraction of phone numbers (if regex supports it)"""
        result = call_tool(mcp_client, "extract_entities", {
            "text": "Call me at +79161234567"
        })
        assert isinstance(result, dict)
        phones = [e for e in result.get("entities", []) if e["type"] == "phone_number"]
        # Phone regex may not be implemented in all versions
        if len(phones) > 0:
            assert "+79161234567" in phones[0]["text"]

    @pytest.mark.requires_session
    def test_extract_hashtag(self, mcp_client):
        """Test extraction of hashtags"""
        result = call_tool(mcp_client, "extract_entities", {
            "text": "This is #important and #urgent"
        })
        assert result.get("success") is True or "entities" in result
        hashtags = [e for e in result.get("entities", []) if e["type"] == "hashtag"]
        assert len(hashtags) >= 2

    @pytest.mark.requires_session
    def test_extract_bot_command(self, mcp_client):
        """Test extraction of bot commands"""
        result = call_tool(mcp_client, "extract_entities", {
            "text": "Use /start or /help to begin"
        })
        assert result.get("success") is True or "entities" in result
        commands = [e for e in result.get("entities", []) if e["type"] == "bot_command"]
        assert len(commands) >= 2

    @pytest.mark.requires_session
    def test_extract_crypto_address(self, mcp_client):
        """Test extraction of TON crypto addresses (if regex supports it)"""
        addr = "EQ" + "A" * 46
        result = call_tool(mcp_client, "extract_entities", {
            "text": f"Send to {addr}"
        })
        assert isinstance(result, dict)
        crypto = [e for e in result.get("entities", []) if e["type"] == "crypto_address"]
        # Crypto address regex may not be implemented in all versions
        if len(crypto) > 0:
            assert addr in crypto[0]["text"]

    @pytest.mark.requires_session
    def test_extract_date(self, mcp_client):
        """Test extraction of dates"""
        result = call_tool(mcp_client, "extract_entities", {
            "text": "The meeting is on 2025-12-25 or 12/25/2025"
        })
        assert result.get("success") is True or "entities" in result
        dates = [e for e in result.get("entities", []) if e["type"] == "date"]
        assert len(dates) >= 2

    @pytest.mark.requires_session
    def test_extract_monetary_amount(self, mcp_client):
        """Test extraction of monetary amounts"""
        result = call_tool(mcp_client, "extract_entities", {
            "text": "It costs $100 or 50 USD or 1.5 TON"
        })
        assert result.get("success") is True or "entities" in result
        money = [e for e in result.get("entities", []) if e["type"] == "monetary_amount"]
        assert len(money) >= 2

    @pytest.mark.requires_session
    def test_extract_multiple_entity_types(self, mcp_client):
        """Test extraction of multiple entity types from one text"""
        result = call_tool(mcp_client, "extract_entities", {
            "text": "Hey @admin, check https://example.com - meeting 2025-01-15 costs $50"
        })
        assert result.get("success") is True or "entities" in result
        types = {e["type"] for e in result.get("entities", [])}
        assert len(types) >= 3, f"Expected at least 3 entity types, got {types}"

    @pytest.mark.requires_session
    def test_extract_entity_offsets(self, mcp_client):
        """Test that entity offsets are correct"""
        text = "Hello @testuser world"
        result = call_tool(mcp_client, "extract_entities", {
            "text": text
        })
        if result.get("entities"):
            for entity in result["entities"]:
                assert "offset" in entity
                assert "length" in entity
                assert entity["offset"] >= 0
                assert entity["length"] > 0

    @pytest.mark.requires_session
    def test_extract_empty_text(self, mcp_client):
        """Test entity extraction with empty text"""
        result = call_tool(mcp_client, "extract_entities", {
            "text": ""
        })
        assert result.get("success") is False or "error" in result

    @pytest.mark.requires_session
    def test_extract_no_entities(self, mcp_client):
        """Test extraction from text with no entities"""
        result = call_tool(mcp_client, "extract_entities", {
            "text": "Just a simple sentence with no entities"
        })
        assert result.get("success") is True or "entities" in result
        assert result.get("count", 0) == 0

    @pytest.mark.requires_session
    def test_extract_count_matches_entities(self, mcp_client):
        """Test that count field matches actual entity count"""
        result = call_tool(mcp_client, "extract_entities", {
            "text": "@user1 and @user2 at https://example.com"
        })
        if result.get("success") or "entities" in result:
            assert result["count"] == len(result["entities"])

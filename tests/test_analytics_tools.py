"""
Tests for Analytics MCP Tools (8 tools)

Tests the analytics tools that provide message statistics, activity tracking,
time-series data, word frequency, and trend analysis from the local database.
All tools have SQL fallback when _analytics component is unavailable.

Tools tested:
1. get_message_stats - Message statistics with time filtering
2. get_user_activity - Per-user activity with hourly breakdown
3. get_chat_activity - Chat-level activity (daily, top senders, types)
4. get_time_series - Configurable granularity time-series data
5. get_top_users - Ranked most active users
6. get_top_words - Word frequency analysis with stop word filtering
7. export_analytics - Export analytics to JSON file
8. get_trends - Trend detection (increasing/decreasing/stable)
"""
import pytest
import json
import os


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


class TestGetMessageStats:
    """Tests for get_message_stats tool"""

    @pytest.mark.requires_session
    def test_message_stats_no_filter(self, mcp_client):
        """Test getting all-time message stats without chat filter"""
        result = call_tool(mcp_client, "get_message_stats", {})
        assert result.get("success") is True or "total_messages" in result
        if "total_messages" in result:
            assert isinstance(result["total_messages"], (int, float))
            assert result["total_messages"] >= 0

    @pytest.mark.requires_session
    def test_message_stats_with_period_day(self, mcp_client):
        """Test message stats filtered to last day"""
        result = call_tool(mcp_client, "get_message_stats", {
            "period": "day"
        })
        assert "period" in result or result.get("success") is True

    @pytest.mark.requires_session
    def test_message_stats_with_period_week(self, mcp_client):
        """Test message stats filtered to last week"""
        result = call_tool(mcp_client, "get_message_stats", {
            "period": "week"
        })
        assert "period" in result or result.get("success") is True

    @pytest.mark.requires_session
    def test_message_stats_with_period_month(self, mcp_client):
        """Test message stats filtered to last month"""
        result = call_tool(mcp_client, "get_message_stats", {
            "period": "month"
        })
        assert "period" in result or result.get("success") is True

    @pytest.mark.requires_session
    def test_message_stats_with_chat_id(self, mcp_client):
        """Test message stats filtered by specific chat"""
        result = call_tool(mcp_client, "get_message_stats", {
            "chat_id": 777000
        })
        # Should return stats even if empty for that chat
        assert result.get("success") is True or "total_messages" in result

    @pytest.mark.requires_session
    def test_message_stats_returns_expected_fields(self, mcp_client):
        """Test that message stats include standard fields"""
        result = call_tool(mcp_client, "get_message_stats", {})
        if result.get("success"):
            # Should contain at least total_messages and unique_senders
            expected_fields = ["total_messages", "unique_senders"]
            for field in expected_fields:
                assert field in result, f"Missing field: {field}"

    @pytest.mark.requires_session
    def test_message_stats_messages_per_day(self, mcp_client):
        """Test that messages_per_day is calculated correctly"""
        result = call_tool(mcp_client, "get_message_stats", {})
        if result.get("success") and "messages_per_day" in result:
            assert isinstance(result["messages_per_day"], (int, float))
            assert result["messages_per_day"] >= 0


class TestGetUserActivity:
    """Tests for get_user_activity tool"""

    @pytest.mark.requires_session
    def test_user_activity_basic(self, mcp_client):
        """Test getting user activity with user_id"""
        result = call_tool(mcp_client, "get_user_activity", {
            "user_id": 777000
        })
        assert result.get("success") is True or "total_messages" in result

    @pytest.mark.requires_session
    def test_user_activity_with_chat_filter(self, mcp_client):
        """Test user activity filtered by chat"""
        result = call_tool(mcp_client, "get_user_activity", {
            "user_id": 777000,
            "chat_id": 777000
        })
        assert result.get("success") is True or "user_id" in result

    @pytest.mark.requires_session
    def test_user_activity_hourly_breakdown(self, mcp_client):
        """Test that hourly activity breakdown is returned"""
        result = call_tool(mcp_client, "get_user_activity", {
            "user_id": 777000
        })
        # If there's activity, should have hourly breakdown
        if result.get("total_messages", 0) > 0:
            assert "hourly_activity" in result

    @pytest.mark.requires_session
    def test_user_activity_nonexistent_user(self, mcp_client):
        """Test activity for user with no messages"""
        result = call_tool(mcp_client, "get_user_activity", {
            "user_id": 999999999
        })
        # Should still return successfully, just with zero counts
        assert result.get("success") is True or result.get("total_messages", 0) == 0


class TestGetChatActivity:
    """Tests for get_chat_activity tool"""

    @pytest.mark.requires_session
    def test_chat_activity_basic(self, mcp_client):
        """Test getting chat activity"""
        result = call_tool(mcp_client, "get_chat_activity", {
            "chat_id": 777000
        })
        assert result.get("success") is True

    @pytest.mark.requires_session
    def test_chat_activity_returns_daily_data(self, mcp_client):
        """Test that daily activity data is returned"""
        result = call_tool(mcp_client, "get_chat_activity", {
            "chat_id": 777000
        })
        if result.get("success"):
            assert "daily_activity" in result
            assert isinstance(result["daily_activity"], list)

    @pytest.mark.requires_session
    def test_chat_activity_top_senders(self, mcp_client):
        """Test that top senders are returned"""
        result = call_tool(mcp_client, "get_chat_activity", {
            "chat_id": 777000
        })
        if result.get("success"):
            assert "top_senders" in result
            assert isinstance(result["top_senders"], list)

    @pytest.mark.requires_session
    def test_chat_activity_message_types(self, mcp_client):
        """Test text/media message type breakdown"""
        result = call_tool(mcp_client, "get_chat_activity", {
            "chat_id": 777000
        })
        if result.get("success") and "total_messages" in result:
            assert "text_messages" in result
            assert result["text_messages"] >= 0

    @pytest.mark.requires_session
    def test_chat_activity_global(self, mcp_client):
        """Test global chat activity (no chat_id filter)"""
        result = call_tool(mcp_client, "get_chat_activity", {})
        assert result.get("success") is True


class TestGetTimeSeries:
    """Tests for get_time_series tool"""

    @pytest.mark.requires_session
    def test_time_series_daily(self, mcp_client):
        """Test daily granularity time series"""
        result = call_tool(mcp_client, "get_time_series", {
            "granularity": "daily"
        })
        assert result.get("success") is True or "data_points" in result
        if "data_points" in result:
            assert isinstance(result["data_points"], list)

    @pytest.mark.requires_session
    def test_time_series_hourly(self, mcp_client):
        """Test hourly granularity"""
        result = call_tool(mcp_client, "get_time_series", {
            "granularity": "hourly"
        })
        assert "data_points" in result or "count" in result

    @pytest.mark.requires_session
    def test_time_series_weekly(self, mcp_client):
        """Test weekly granularity"""
        result = call_tool(mcp_client, "get_time_series", {
            "granularity": "weekly"
        })
        assert "data_points" in result or "count" in result

    @pytest.mark.requires_session
    def test_time_series_monthly(self, mcp_client):
        """Test monthly granularity"""
        result = call_tool(mcp_client, "get_time_series", {
            "granularity": "monthly"
        })
        assert "data_points" in result or "count" in result

    @pytest.mark.requires_session
    def test_time_series_default_granularity(self, mcp_client):
        """Test default granularity (should be daily)"""
        result = call_tool(mcp_client, "get_time_series", {})
        if "granularity" in result:
            assert result["granularity"] == "daily"

    @pytest.mark.requires_session
    def test_time_series_with_chat_id(self, mcp_client):
        """Test time series filtered by chat"""
        result = call_tool(mcp_client, "get_time_series", {
            "chat_id": 777000,
            "granularity": "daily"
        })
        assert "data_points" in result or "count" in result

    @pytest.mark.requires_session
    def test_time_series_data_point_structure(self, mcp_client):
        """Test that data points have period and count fields"""
        result = call_tool(mcp_client, "get_time_series", {
            "granularity": "daily"
        })
        if result.get("data_points") and len(result["data_points"]) > 0:
            point = result["data_points"][0]
            assert "period" in point
            assert "count" in point


class TestGetTopUsers:
    """Tests for get_top_users tool"""

    @pytest.mark.requires_session
    def test_top_users_default(self, mcp_client):
        """Test getting top users with default limit"""
        result = call_tool(mcp_client, "get_top_users", {})
        assert result.get("success") is True or "users" in result

    @pytest.mark.requires_session
    def test_top_users_custom_limit(self, mcp_client):
        """Test top users with custom limit"""
        result = call_tool(mcp_client, "get_top_users", {"limit": 5})
        if "users" in result:
            assert len(result["users"]) <= 5

    @pytest.mark.requires_session
    def test_top_users_with_chat_id(self, mcp_client):
        """Test top users filtered by chat"""
        result = call_tool(mcp_client, "get_top_users", {
            "chat_id": 777000,
            "limit": 10
        })
        assert result.get("success") is True or "users" in result

    @pytest.mark.requires_session
    def test_top_users_structure(self, mcp_client):
        """Test that user entries have expected fields"""
        result = call_tool(mcp_client, "get_top_users", {"limit": 3})
        if result.get("users") and len(result["users"]) > 0:
            user = result["users"][0]
            assert "rank" in user
            assert "message_count" in user
            assert user["rank"] == 1

    @pytest.mark.requires_session
    def test_top_users_ordered_by_count(self, mcp_client):
        """Test that users are ordered by message count descending"""
        result = call_tool(mcp_client, "get_top_users", {"limit": 10})
        if result.get("users") and len(result["users"]) > 1:
            counts = [u["message_count"] for u in result["users"]]
            assert counts == sorted(counts, reverse=True)


class TestGetTopWords:
    """Tests for get_top_words tool"""

    @pytest.mark.requires_session
    def test_top_words_default(self, mcp_client):
        """Test getting top words with default limit"""
        result = call_tool(mcp_client, "get_top_words", {})
        assert result.get("success") is True or "words" in result

    @pytest.mark.requires_session
    def test_top_words_custom_limit(self, mcp_client):
        """Test top words with custom limit"""
        result = call_tool(mcp_client, "get_top_words", {"limit": 10})
        if "words" in result:
            assert len(result["words"]) <= 10

    @pytest.mark.requires_session
    def test_top_words_structure(self, mcp_client):
        """Test word entries have expected fields"""
        result = call_tool(mcp_client, "get_top_words", {"limit": 5})
        if result.get("words") and len(result["words"]) > 0:
            word = result["words"][0]
            assert "word" in word
            assert "count" in word
            assert "rank" in word
            assert word["rank"] == 1

    @pytest.mark.requires_session
    def test_top_words_filters_stop_words(self, mcp_client):
        """Test that common stop words are filtered out"""
        result = call_tool(mcp_client, "get_top_words", {"limit": 50})
        stop_words = {"the", "a", "an", "and", "or", "but", "in", "on", "at",
                      "to", "for", "of", "with", "by", "from", "is", "are"}
        if result.get("words"):
            returned_words = {w["word"] for w in result["words"]}
            # None of the returned words should be stop words
            assert len(returned_words & stop_words) == 0, \
                f"Stop words found in results: {returned_words & stop_words}"

    @pytest.mark.requires_session
    def test_top_words_minimum_length(self, mcp_client):
        """Test that words shorter than 3 characters are filtered"""
        result = call_tool(mcp_client, "get_top_words", {"limit": 50})
        if result.get("words"):
            for word_entry in result["words"]:
                assert len(word_entry["word"]) >= 3, \
                    f"Word too short: '{word_entry['word']}'"

    @pytest.mark.requires_session
    def test_top_words_messages_analyzed(self, mcp_client):
        """Test that messages_analyzed count is returned"""
        result = call_tool(mcp_client, "get_top_words", {})
        if result.get("success"):
            assert "messages_analyzed" in result
            assert result["messages_analyzed"] >= 0


class TestExportAnalytics:
    """Tests for export_analytics tool"""

    @pytest.mark.requires_session
    def test_export_analytics_default_path(self, mcp_client):
        """Test export analytics to default temp path"""
        result = call_tool(mcp_client, "export_analytics", {
            "chat_id": 777000
        })
        if result.get("success"):
            assert "output_path" in result

    @pytest.mark.requires_session
    def test_export_analytics_custom_path(self, mcp_client):
        """Test export to custom path"""
        output = "/tmp/test_analytics_export.json"
        result = call_tool(mcp_client, "export_analytics", {
            "chat_id": 777000,
            "output_path": output
        })
        if result.get("success"):
            assert result["output_path"] == output
            # Clean up
            if os.path.exists(output):
                os.remove(output)

    @pytest.mark.requires_session
    def test_export_analytics_json_format(self, mcp_client):
        """Test export in JSON format"""
        result = call_tool(mcp_client, "export_analytics", {
            "format": "json"
        })
        if result.get("success") and result.get("output_path"):
            # Verify the file is valid JSON
            path = result["output_path"]
            if os.path.exists(path):
                with open(path) as f:
                    data = json.load(f)
                assert isinstance(data, dict)
                os.remove(path)

    @pytest.mark.requires_session
    def test_export_analytics_includes_size(self, mcp_client):
        """Test that export returns file size"""
        result = call_tool(mcp_client, "export_analytics", {
            "chat_id": 777000
        })
        if result.get("success") and "size_bytes" in result:
            assert result["size_bytes"] > 0


class TestGetTrends:
    """Tests for get_trends tool"""

    @pytest.mark.requires_session
    def test_trends_default(self, mcp_client):
        """Test getting trends with default parameters"""
        result = call_tool(mcp_client, "get_trends", {})
        assert result.get("success") is True

    @pytest.mark.requires_session
    def test_trends_custom_days(self, mcp_client):
        """Test trends with custom days_back"""
        result = call_tool(mcp_client, "get_trends", {
            "days_back": 7
        })
        if "days_back" in result:
            assert result["days_back"] == 7

    @pytest.mark.requires_session
    def test_trends_with_chat_id(self, mcp_client):
        """Test trends filtered by chat"""
        result = call_tool(mcp_client, "get_trends", {
            "chat_id": 777000,
            "days_back": 30
        })
        assert result.get("success") is True

    @pytest.mark.requires_session
    def test_trends_direction(self, mcp_client):
        """Test that trend direction is one of the expected values"""
        result = call_tool(mcp_client, "get_trends", {})
        if "trend" in result:
            assert result["trend"] in ["increasing", "decreasing", "stable"]

    @pytest.mark.requires_session
    def test_trends_data_points(self, mcp_client):
        """Test that trend data points are returned"""
        result = call_tool(mcp_client, "get_trends", {})
        if result.get("success"):
            assert "data_points" in result
            assert isinstance(result["data_points"], list)

    @pytest.mark.requires_session
    def test_trends_daily_average(self, mcp_client):
        """Test that daily average is calculated"""
        result = call_tool(mcp_client, "get_trends", {})
        if result.get("success") and result.get("day_count", 0) > 0:
            assert "daily_average" in result
            assert result["daily_average"] >= 0

    @pytest.mark.requires_session
    def test_trends_increasing_decreasing_counts(self, mcp_client):
        """Test that increasing/decreasing day counts are present"""
        result = call_tool(mcp_client, "get_trends", {})
        if result.get("success"):
            assert "increasing_days" in result
            assert "decreasing_days" in result
            assert result["increasing_days"] >= 0
            assert result["decreasing_days"] >= 0

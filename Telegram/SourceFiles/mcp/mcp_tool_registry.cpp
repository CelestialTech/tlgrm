// This file is part of Telegram Desktop MCP integration.
// Extracted from mcp_server_complete.cpp for modular organization.
//
// For license and copyright information please follow this link:
// https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL

#include "mcp_server_includes.h"

namespace MCP {
void Server::registerTools() {
	_tools = {
		// ===== CORE TOOLS (6) =====
		Tool{
			"list_chats",
			"Get a list of all Telegram chats (direct access to local database)",
			QJsonObject{
				{"type", "object"},
				{"properties", QJsonObject{}},
			}
		},
		Tool{
			"get_chat_info",
			"Get detailed information about a specific chat",
			QJsonObject{
				{"type", "object"},
				{"properties", QJsonObject{
					{"chat_id", QJsonObject{
						{"type", "integer"},
						{"description", "Telegram chat ID"}
					}}
				}},
				{"required", QJsonArray{"chat_id"}},
			}
		},
		Tool{
			"read_messages",
			"Read messages from local database (instant, no API calls!)",
			QJsonObject{
				{"type", "object"},
				{"properties", QJsonObject{
					{"chat_id", QJsonObject{
						{"type", "integer"},
						{"description", "Chat ID"}
					}},
					{"limit", QJsonObject{
						{"type", "integer"},
						{"description", "Number of messages"},
						{"default", 50}
					}},
					{"before_timestamp", QJsonObject{
						{"type", "integer"},
						{"description", "Get messages before this timestamp"},
						{"default", 0}
					}}
				}},
				{"required", QJsonArray{"chat_id"}},
			}
		},
		Tool{
			"send_message",
			"Send a message to a chat",
			QJsonObject{
				{"type", "object"},
				{"properties", QJsonObject{
					{"chat_id", QJsonObject{
						{"type", "integer"},
						{"description", "Chat ID"}
					}},
					{"text", QJsonObject{
						{"type", "string"},
						{"description", "Message text"}
					}}
				}},
				{"required", QJsonArray{"chat_id", "text"}},
			}
		},
		Tool{
			"send_document",
			"Send a local file to a chat as a document, preserving its "
				"bytes and filename. Upload is asynchronous: success means "
				"queued, not delivered.",
			QJsonObject{
				{"type", "object"},
				{"properties", QJsonObject{
					{"chat_id", QJsonObject{
						{"type", "integer"},
						{"description", "Chat ID"}
					}},
					{"file_path", QJsonObject{
						{"type", "string"},
						{"description", "Absolute path to an existing local file"}
					}},
					{"caption", QJsonObject{
						{"type", "string"},
						{"description", "Optional caption for the document"}
					}}
				}},
				{"required", QJsonArray{"chat_id", "file_path"}},
			}
		},
		Tool{
			"search_messages",
			"Search messages in local database",
			QJsonObject{
				{"type", "object"},
				{"properties", QJsonObject{
					{"query", QJsonObject{
						{"type", "string"},
						{"description", "Search query"}
					}},
					{"chat_id", QJsonObject{
						{"type", "integer"},
						{"description", "Optional: limit to specific chat"}
					}},
					{"limit", QJsonObject{
						{"type", "integer"},
						{"default", 50}, {"description", "Maximum number of results to return."}}}
				}},
				{"required", QJsonArray{"query"}},
			}
		},
		Tool{
			"get_user_info",
			"Get information about a specific user",
			QJsonObject{
				{"type", "object"},
				{"properties", QJsonObject{
					{"user_id", QJsonObject{
						{"type", "integer"},
						{"description", "User ID"}
					}}
				}},
				{"required", QJsonArray{"user_id"}},
			}
		},

		// ===== ARCHIVE TOOLS (7) =====
		Tool{
			"archive_chat",
			"Archive all messages from a chat to the local database",
			QJsonObject{
				{"type", "object"},
				{"properties", QJsonObject{
					{"chat_id", QJsonObject{
						{"type", "integer"},
						{"description", "Chat ID to archive"}
					}},
					{"limit", QJsonObject{
						{"type", "integer"},
						{"description", "Max messages to archive (-1 = all)"},
						{"default", 1000}
					}}
				}},
				{"required", QJsonArray{"chat_id"}},
			}
		},
		Tool{
			"export_chat",
			"Export chat via gradual export (mimics user behavior). Opens export UI panel. Auto-detects resume point from previous export.",
			QJsonObject{
				{"type", "object"},
				{"properties", QJsonObject{
					{"chat_id", QJsonObject{
						{"type", "integer"},
						{"description", "Chat ID"}
					}},
					{"format", QJsonObject{
						{"type", "string"},
						{"enum", QJsonArray{"html", "json"}},
						{"description", "Export format (default: html)"}
					}},
					{"output_path", QJsonObject{
						{"type", "string"},
						{"description", "Output directory path (optional)"}
					}},
					{"resume_from_message_id", QJsonObject{
						{"type", "integer"},
						{"description", "Resume from this message ID. If omitted, auto-detects from previous export."}
					}},
					{"messages_written", QJsonObject{{"type", "integer"}, {"description", "Resume hint: how many messages a previous run already wrote, so the export continues from there instead of starting over."}}},
				}},
				{"required", QJsonArray{"chat_id"}},
			}
		},
		Tool{
			"get_export_status",
			"Get the status of an ongoing or completed chat export",
			QJsonObject{
				{"type", "object"},
				{"properties", QJsonObject{}},
			}
		},
		Tool{
			"list_archived_chats",
			"List all chats that have been archived",
			QJsonObject{
				{"type", "object"},
				{"properties", QJsonObject{}},
			}
		},
		Tool{
			"get_archive_stats",
			"Get statistics about archived data",
			QJsonObject{
				{"type", "object"},
				{"properties", QJsonObject{}},
			}
		},
		Tool{
			"configure_ephemeral_capture",
			"Configure which types of ephemeral messages to capture",
			QJsonObject{
				{"type", "object"},
				{"properties", QJsonObject{
					{"capture_self_destruct", QJsonObject{
						{"type", "boolean"},
						{"description", "Capture self-destruct messages"},
						{"default", true}
					}},
					{"capture_view_once", QJsonObject{
						{"type", "boolean"},
						{"description", "Capture view-once messages"},
						{"default", true}
					}},
					{"capture_vanishing", QJsonObject{
						{"type", "boolean"},
						{"description", "Capture vanishing messages"},
						{"default", true}
					}}
				}},
			}
		},
		Tool{
			"get_ephemeral_stats",
			"Get statistics about captured ephemeral messages",
			QJsonObject{
				{"type", "object"},
				{"properties", QJsonObject{}},
			}
		},
		Tool{
			"get_ephemeral_messages",
			"Get captured ephemeral messages (self-destruct, view-once, vanishing)",
			QJsonObject{
				{"type", "object"},
				{"properties", QJsonObject{
					{"chat_id", QJsonObject{
						{"type", "integer"},
						{"description", "Optional: filter by chat"}
					}},
					{"type", QJsonObject{
						{"type", "string"},
						{"description", "Optional: filter by type (self_destruct, view_once, vanishing)"}
					}},
					{"limit", QJsonObject{
						{"type", "integer"},
						{"description", "Max messages to return"},
						{"default", 50}
					}}
				}},
			}
		},
		Tool{
			"search_archive",
			"Search archived messages (faster than live search)",
			QJsonObject{
				{"type", "object"},
				{"properties", QJsonObject{
					{"query", QJsonObject{
						{"type", "string"},
						{"description", "Search query"}
					}},
					{"chat_id", QJsonObject{
						{"type", "integer"},
						{"description", "Optional: limit to chat"}
					}},
					{"limit", QJsonObject{
						{"type", "integer"},
						{"default", 50}, {"description", "Maximum number of results to return."}}}
				}},
				{"required", QJsonArray{"query"}},
			}
		},
		Tool{
			"purge_archive",
			"Delete old archived messages",
			QJsonObject{
				{"type", "object"},
				{"properties", QJsonObject{
					{"days_to_keep", QJsonObject{
						{"type", "integer"},
						{"description", "Keep messages newer than N days"}
					}}
				}},
				{"required", QJsonArray{"days_to_keep"}},
			}
		},

		// ===== ANALYTICS TOOLS (8) =====
		Tool{
			"get_message_stats",
			"Get message statistics for a chat",
			QJsonObject{
				{"type", "object"},
				{"properties", QJsonObject{
					{"chat_id", QJsonObject{
						{"type", "integer"},
						{"description", "Chat ID"}
					}},
					{"period", QJsonObject{
						{"type", "string"},
						{"enum", QJsonArray{"all", "day", "week", "month"}},
						{"default", "all"}, {"description", "Time window to report over, such as day, week or month."}}}
				}},
				{"required", QJsonArray{"chat_id"}},
			}
		},
		Tool{
			"get_user_activity",
			"Analyze user activity in a chat",
			QJsonObject{
				{"type", "object"},
				{"properties", QJsonObject{
					{"user_id", QJsonObject{
						{"type", "integer"},
						{"description", "User ID"}
					}},
					{"chat_id", QJsonObject{
						{"type", "integer"},
						{"description", "Optional: specific chat (0 = all chats)"},
						{"default", 0}
					}}
				}},
				{"required", QJsonArray{"user_id"}},
			}
		},
		Tool{
			"get_chat_activity",
			"Analyze chat activity and trends",
			QJsonObject{
				{"type", "object"},
				{"properties", QJsonObject{
					{"chat_id", QJsonObject{
						{"type", "integer"},
						{"description", "Chat ID"}
					}}
				}},
				{"required", QJsonArray{"chat_id"}},
			}
		},
		Tool{
			"get_time_series",
			"Get time series data for visualization",
			QJsonObject{
				{"type", "object"},
				{"properties", QJsonObject{
					{"chat_id", QJsonObject{
						{"type", "integer"},
						{"description", "Chat ID"}
					}},
					{"granularity", QJsonObject{
						{"type", "string"},
						{"enum", QJsonArray{"hourly", "daily", "weekly", "monthly"}},
						{"default", "daily"}, {"description", "Bucket size for the series, such as hour, day or week."}}}
				}},
				{"required", QJsonArray{"chat_id"}},
			}
		},
		Tool{
			"get_top_users",
			"Get most active users in a chat",
			QJsonObject{
				{"type", "object"},
				{"properties", QJsonObject{
					{"chat_id", QJsonObject{
						{"type", "integer"},
						{"description", "Chat ID"}
					}},
					{"limit", QJsonObject{
						{"type", "integer"},
						{"default", 10}, {"description", "Maximum number of results to return."}}}
				}},
				{"required", QJsonArray{"chat_id"}},
			}
		},
		Tool{
			"get_top_words",
			"Get most frequently used words in a chat",
			QJsonObject{
				{"type", "object"},
				{"properties", QJsonObject{
					{"chat_id", QJsonObject{
						{"type", "integer"},
						{"description", "Chat ID"}
					}},
					{"limit", QJsonObject{
						{"type", "integer"},
						{"default", 20}, {"description", "Maximum number of results to return."}}}
				}},
				{"required", QJsonArray{"chat_id"}},
			}
		},
		Tool{
			"export_analytics",
			"Export analytics data to CSV",
			QJsonObject{
				{"type", "object"},
				{"properties", QJsonObject{
					{"chat_id", QJsonObject{
						{"type", "integer"},
						{"description", "Chat ID"}
					}},
					{"output_path", QJsonObject{
						{"type", "string"},
						{"description", "Output CSV file path"}
					}},
					{"format", QJsonObject{{"type", "string"}, {"description", "Output format (default json)."}}},
					{"period", QJsonObject{{"type", "string"}, {"description", "Time window to report over, such as week or month."}}},
				}},
				{"required", QJsonArray{"chat_id", "output_path"}},
			}
		},
		Tool{
			"get_trends",
			"Detect activity trends (increasing/decreasing/stable)",
			QJsonObject{
				{"type", "object"},
				{"properties", QJsonObject{
					{"chat_id", QJsonObject{
						{"type", "integer"},
						{"description", "Chat ID"}
					}},
					{"days_back", QJsonObject{{"type", "integer"}, {"description", "How many days back to cover (default 30)."}}},
					{"metric", QJsonObject{{"type", "string"}, {"description", "Which series to trend (default messages)."}}},
				}},
				{"required", QJsonArray{"chat_id"}},
			}
		},

		// ===== SEMANTIC SEARCH TOOLS (5) =====
		Tool{
			"semantic_search",
			"Search messages by meaning (AI-powered)",
			QJsonObject{
				{"type", "object"},
				{"properties", QJsonObject{
					{"query", QJsonObject{
						{"type", "string"},
						{"description", "Search query"}
					}},
					{"chat_id", QJsonObject{
						{"type", "integer"},
						{"description", "Optional: limit to chat"}
					}},
					{"limit", QJsonObject{
						{"type", "integer"},
						{"default", 10}, {"description", "Maximum number of results to return."}}},
					{"min_similarity", QJsonObject{
						{"type", "number"},
						{"default", 0.7}, {"description", "Minimum similarity score, 0.0 to 1.0; lower returns more, looser matches."}}}
				}},
				{"required", QJsonArray{"query"}},
			}
		},
		Tool{
			"index_messages",
			"Index messages for semantic search",
			QJsonObject{
				{"type", "object"},
				{"properties", QJsonObject{
					{"chat_id", QJsonObject{
						{"type", "integer"},
						{"description", "Chat ID to index"}
					}},
					{"limit", QJsonObject{
						{"type", "integer"},
						{"description", "Max messages to index (-1 = all)"},
						{"default", 1000}
					}},
					{"rebuild", QJsonObject{{"type", "boolean"}, {"description", "Rebuild the index from scratch instead of updating it (default false)."}}},
				}},
				{"required", QJsonArray{"chat_id"}},
			}
		},
		Tool{
			"detect_topics",
			"Auto-detect conversation topics using clustering",
			QJsonObject{
				{"type", "object"},
				{"properties", QJsonObject{
					{"chat_id", QJsonObject{
						{"type", "integer"},
						{"description", "Chat ID"}
					}},
					{"num_topics", QJsonObject{
						{"type", "integer"},
						{"default", 5}, {"description", "How many topics to extract."}}},
					{"message_limit", QJsonObject{{"type", "integer"}, {"description", "How many recent messages to analyse (default 500)."}}},
				}},
				{"required", QJsonArray{"chat_id"}},
			}
		},
		Tool{
			"classify_intent",
			"Classify message intent (question/answer/command/etc)",
			QJsonObject{
				{"type", "object"},
				{"properties", QJsonObject{
					{"text", QJsonObject{
						{"type", "string"},
						{"description", "Message text to classify"}
					}}
				}},
				{"required", QJsonArray{"text"}},
			}
		},
		Tool{
			"extract_entities",
			"Extract entities (mentions, URLs, hashtags, commands)",
			QJsonObject{
				{"type", "object"},
				{"properties", QJsonObject{
					{"text", QJsonObject{
						{"type", "string"},
						{"description", "Text to analyze"}
					}}
				}},
				{"required", QJsonArray{"text"}},
			}
		},

		// ===== MESSAGE OPERATIONS (6) =====
		Tool{
			"edit_message",
			"Edit an existing message",
			QJsonObject{
				{"type", "object"},
				{"properties", QJsonObject{
					{"chat_id", QJsonObject{
						{"type", "integer"},
						{"description", "Chat ID"}
					}},
					{"message_id", QJsonObject{
						{"type", "integer"},
						{"description", "Message ID to edit"}
					}},
					{"new_text", QJsonObject{
						{"type", "string"},
						{"description", "New message text"}
					}}
				}},
				{"required", QJsonArray{"chat_id", "message_id", "new_text"}},
			}
		},
		Tool{
			"delete_message",
			"Delete a message",
			QJsonObject{
				{"type", "object"},
				{"properties", QJsonObject{
					{"chat_id", QJsonObject{
						{"type", "integer"},
						{"description", "Chat ID"}
					}},
					{"message_id", QJsonObject{
						{"type", "integer"},
						{"description", "Message ID to delete"}
					}},
					{"revoke", QJsonObject{{"type", "boolean"}, {"description", "Also delete the message for everyone rather than only locally (default true)."}}},
				}},
				{"required", QJsonArray{"chat_id", "message_id"}},
			}
		},
		Tool{
			"forward_message",
			"Forward a message to another chat",
			QJsonObject{
				{"type", "object"},
				{"properties", QJsonObject{
					{"from_chat_id", QJsonObject{
						{"type", "integer"},
						{"description", "Source chat ID"}
					}},
					{"to_chat_id", QJsonObject{
						{"type", "integer"},
						{"description", "Destination chat ID"}
					}},
					{"message_id", QJsonObject{
						{"type", "integer"},
						{"description", "Message ID to forward"}
					}}
				}},
				{"required", QJsonArray{"from_chat_id", "to_chat_id", "message_id"}},
			}
		},
		Tool{
			"pin_message",
			"Pin a message in a chat",
			QJsonObject{
				{"type", "object"},
				{"properties", QJsonObject{
					{"chat_id", QJsonObject{
						{"type", "integer"},
						{"description", "Chat ID"}
					}},
					{"message_id", QJsonObject{
						{"type", "integer"},
						{"description", "Message ID to pin"}
					}},
					{"notify", QJsonObject{
						{"type", "boolean"},
						{"default", false}, {"description", "Notify chat members."}}}
				}},
				{"required", QJsonArray{"chat_id", "message_id"}},
			}
		},
		Tool{
			"unpin_message",
			"Unpin a message",
			QJsonObject{
				{"type", "object"},
				{"properties", QJsonObject{
					{"chat_id", QJsonObject{
						{"type", "integer"},
						{"description", "Chat ID"}
					}},
					{"message_id", QJsonObject{
						{"type", "integer"},
						{"description", "Message ID to unpin"}
					}}
				}},
				{"required", QJsonArray{"chat_id", "message_id"}},
			}
		},
		Tool{
			"add_reaction",
			"Add a reaction to a message",
			QJsonObject{
				{"type", "object"},
				{"properties", QJsonObject{
					{"chat_id", QJsonObject{
						{"type", "integer"},
						{"description", "Chat ID"}
					}},
					{"message_id", QJsonObject{
						{"type", "integer"},
						{"description", "Message ID"}
					}},
					{"emoji", QJsonObject{
						{"type", "string"},
						{"description", "Emoji reaction"}
					}}
				}},
				{"required", QJsonArray{"chat_id", "message_id", "emoji"}},
			}
		},

		Tool{
			"rename_chat",
			"Rename a group or channel title",
			QJsonObject{
				{"type", "object"},
				{"properties", QJsonObject{
					{"chat_id", QJsonObject{
						{"type", "integer"},
						{"description", "Chat/group/channel ID"}
					}},
					{"title", QJsonObject{
						{"type", "string"},
						{"description", "New title"}
					}}
				}},
				{"required", QJsonArray{"chat_id", "title"}},
			}
		},

		// ===== BATCH OPERATIONS (5) =====
		Tool{
			"batch_send",
			"Send messages to multiple chats",
			QJsonObject{
				{"type", "object"},
				{"properties", QJsonObject{
					{"chat_ids", QJsonObject{
						{"type", "array"},
						{"items", QJsonObject{{"type", "integer"}}},
						{"description", "List of chat IDs"}
					}},
					{"message", QJsonObject{
						{"type", "string"},
						{"description", "Message to send"}
					}}
				}},
				{"required", QJsonArray{"chat_ids", "message"}},
			}
		},
		Tool{
			"batch_delete",
			"Delete multiple messages",
			QJsonObject{
				{"type", "object"},
				{"properties", QJsonObject{
					{"chat_id", QJsonObject{
						{"type", "integer"},
						{"description", "Chat ID"}
					}},
					{"message_ids", QJsonObject{
						{"type", "array"},
						{"items", QJsonObject{{"type", "integer"}}},
						{"description", "List of message IDs"}
					}},
					{"revoke", QJsonObject{{"type", "boolean"}, {"description", "Also delete the message for everyone rather than only locally (default true)."}}},
				}},
				{"required", QJsonArray{"chat_id", "message_ids"}},
			}
		},
		Tool{
			"batch_forward",
			"Forward multiple messages",
			QJsonObject{
				{"type", "object"},
				{"properties", QJsonObject{
					{"from_chat_id", QJsonObject{
						{"type", "integer"},
						{"description", "Source chat ID"}
					}},
					{"to_chat_id", QJsonObject{
						{"type", "integer"},
						{"description", "Destination chat ID"}
					}},
					{"message_ids", QJsonObject{
						{"type", "array"},
						{"items", QJsonObject{{"type", "integer"}}},
						{"description", "List of message IDs"}
					}}
				}},
				{"required", QJsonArray{"from_chat_id", "to_chat_id", "message_ids"}},
			}
		},
		Tool{
			"batch_pin",
			"Pin multiple messages",
			QJsonObject{
				{"type", "object"},
				{"properties", QJsonObject{
					{"chat_id", QJsonObject{
						{"type", "integer"},
						{"description", "Chat ID"}
					}},
					{"message_ids", QJsonObject{
						{"type", "array"},
						{"items", QJsonObject{{"type", "integer"}}},
						{"description", "List of message IDs"}
					}},
					{"notify", QJsonObject{{"type", "boolean"}, {"description", "Notify chat members (default false)."}}},
				}},
				{"required", QJsonArray{"chat_id", "message_ids"}},
			}
		},
		Tool{
			"batch_reaction",
			"Add reactions to multiple messages",
			QJsonObject{
				{"type", "object"},
				{"properties", QJsonObject{
					{"chat_id", QJsonObject{
						{"type", "integer"},
						{"description", "Chat ID"}
					}},
					{"message_ids", QJsonObject{
						{"type", "array"},
						{"items", QJsonObject{{"type", "integer"}}},
						{"description", "List of message IDs"}
					}},
					{"emoji", QJsonObject{
						{"type", "string"},
						{"description", "Emoji reaction"}
					}}
				}},
				{"required", QJsonArray{"chat_id", "message_ids", "emoji"}},
			}
		},

		// ===== SCHEDULER TOOLS (4) =====
		Tool{
			"schedule_message",
			"Schedule a message for future delivery",
			QJsonObject{
				{"type", "object"},
				{"properties", QJsonObject{
					{"chat_id", QJsonObject{
						{"type", "integer"},
						{"description", "Chat ID"}
					}},
					{"text", QJsonObject{
						{"type", "string"},
						{"description", "Message text"}
					}},
					{"schedule_type", QJsonObject{
						{"type", "string"},
						{"enum", QJsonArray{"once", "recurring", "delayed"}},
						{"description", "Schedule type"}
					}},
					{"when", QJsonObject{
						{"type", "string"},
						{"description", "ISO datetime or delay in seconds"}
					}},
					{"pattern", QJsonObject{
						{"type", "string"},
						{"enum", QJsonArray{"hourly", "daily", "weekly", "monthly"}},
						{"description", "Recurrence pattern (for recurring)"}
					}}
				}},
				{"required", QJsonArray{"chat_id", "text", "schedule_type", "when"}},
			}
		},
		Tool{
			"cancel_scheduled",
			"Cancel a scheduled message",
			QJsonObject{
				{"type", "object"},
				{"properties", QJsonObject{
					{"schedule_id", QJsonObject{
						{"type", "integer"},
						{"description", "Schedule ID to cancel"}
					}}
				}},
				{"required", QJsonArray{"schedule_id"}},
			}
		},
		Tool{
			"list_scheduled",
			"List all scheduled messages",
			QJsonObject{
				{"type", "object"},
				{"properties", QJsonObject{
					{"chat_id", QJsonObject{
						{"type", "integer"},
						{"description", "Optional: filter by chat"}
					}}
				}},
			}
		},
		Tool{
			"update_scheduled",
			"Update a scheduled message",
			QJsonObject{
				{"type", "object"},
				{"properties", QJsonObject{
					{"schedule_id", QJsonObject{
						{"type", "integer"},
						{"description", "Schedule ID"}
					}},
					{"new_text", QJsonObject{
						{"type", "string"},
						{"description", "New message text"}
					}},
					{"new_pattern", QJsonObject{{"type", "string"}, {"description", "Replacement recurrence pattern for the schedule."}}},
					{"new_time", QJsonObject{{"type", "string"}, {"description", "Replacement time for the schedule."}}},
				}},
				{"required", QJsonArray{"schedule_id", "new_text"}},
			}
		},

		// ===== SYSTEM TOOLS (4) =====
		Tool{
			"get_cache_stats",
			"Get cache statistics",
			QJsonObject{
				{"type", "object"},
				{"properties", QJsonObject{}},
			}
		},
		Tool{
			"get_server_info",
			"Get MCP server information and capabilities",
			QJsonObject{
				{"type", "object"},
				{"properties", QJsonObject{}},
			}
		},
		Tool{
			"get_audit_log",
			"Get audit log entries",
			QJsonObject{
				{"type", "object"},
				{"properties", QJsonObject{
					{"limit", QJsonObject{
						{"type", "integer"},
						{"default", 50}, {"description", "Maximum number of results to return."}}},
					{"event_type", QJsonObject{
						{"type", "string"},
						{"description", "Filter by event type"}
					}}
				}},
			}
		},
		Tool{
			"health_check",
			"Check server health status",
			QJsonObject{
				{"type", "object"},
				{"properties", QJsonObject{}},
			}
		},

		// ===== VOICE TOOLS (2) =====
		Tool{
			"transcribe_voice",
			"Transcribe a voice message",
			QJsonObject{
				{"type", "object"},
				{"properties", QJsonObject{
					{"message_id", QJsonObject{
						{"type", "integer"},
						{"description", "Voice message ID"}
					}},
					{"audio_path", QJsonObject{
						{"type", "string"},
						{"description", "Path to audio file"}
					}}
				}},
				{"required", QJsonArray{"audio_path"}},
			}
		},
		Tool{
			"get_transcription",
			"Get stored transcription for a message",
			QJsonObject{
				{"type", "object"},
				{"properties", QJsonObject{
					{"message_id", QJsonObject{
						{"type", "integer"},
						{"description", "Message ID"}
					}}
				}},
				{"required", QJsonArray{"message_id"}},
			}
		},

		// ===== BOT FRAMEWORK TOOLS (8) =====
		Tool{
			"list_bots",
			"List all registered bots",
			QJsonObject{
				{"type", "object"},
				{"properties", QJsonObject{
					{"include_disabled", QJsonObject{
						{"type", "boolean"},
						{"description", "Include disabled bots"},
						{"default", false}
					}}
				}}
			}
		},
		Tool{
			"get_bot_info",
			"Get detailed information about a bot",
			QJsonObject{
				{"type", "object"},
				{"properties", QJsonObject{
					{"bot_id", QJsonObject{
						{"type", "string"},
						{"description", "Bot identifier"}
					}}
				}},
				{"required", QJsonArray{"bot_id"}}
			}
		},
		Tool{
			"start_bot",
			"Start a registered bot",
			QJsonObject{
				{"type", "object"},
				{"properties", QJsonObject{
					{"bot_id", QJsonObject{
						{"type", "string"},
						{"description", "Bot identifier"}
					}}
				}},
				{"required", QJsonArray{"bot_id"}}
			}
		},
		Tool{
			"stop_bot",
			"Stop a running bot",
			QJsonObject{
				{"type", "object"},
				{"properties", QJsonObject{
					{"bot_id", QJsonObject{
						{"type", "string"},
						{"description", "Bot identifier"}
					}}
				}},
				{"required", QJsonArray{"bot_id"}}
			}
		},
		Tool{
			"configure_bot",
			"Update bot configuration",
			QJsonObject{
				{"type", "object"},
				{"properties", QJsonObject{
					{"bot_id", QJsonObject{
						{"type", "string"},
						{"description", "Bot identifier"}
					}},
					{"config", QJsonObject{
						{"type", "object"},
						{"description", "Bot configuration (JSON object)"}
					}}
				}},
				{"required", QJsonArray{"bot_id", "config"}}
			}
		},
		Tool{
			"get_bot_stats",
			"Get performance statistics for a bot",
			QJsonObject{
				{"type", "object"},
				{"properties", QJsonObject{
					{"bot_id", QJsonObject{
						{"type", "string"},
						{"description", "Bot identifier"}
					}}
				}},
				{"required", QJsonArray{"bot_id"}}
			}
		},
		Tool{
			"send_bot_command",
			"Send a command to a specific bot",
			QJsonObject{
				{"type", "object"},
				{"properties", QJsonObject{
					{"bot_id", QJsonObject{
						{"type", "string"},
						{"description", "Bot identifier"}
					}},
					{"command", QJsonObject{
						{"type", "string"},
						{"description", "Command name"}
					}},
					{"args", QJsonObject{
						{"type", "object"},
						{"description", "Command arguments (JSON object)"}
					}}
				}},
				{"required", QJsonArray{"bot_id", "command"}}
			}
		},
		Tool{
			"get_bot_suggestions",
			"Get suggestions offered by bots",
			QJsonObject{
				{"type", "object"},
				{"properties", QJsonObject{
					{"chat_id", QJsonObject{
						{"type", "integer"},
						{"description", "Chat ID (optional)"}
					}},
					{"limit", QJsonObject{
						{"type", "integer"},
						{"description", "Maximum number of suggestions"},
						{"default", 10}
					}}
				}}
			}
		},

		// ===== PREMIUM EQUIVALENT FEATURES (17 tools) =====

		// Voice-to-Text (local Whisper) - 2 tools
		Tool{
			"transcribe_voice_message",
			"Transcribe a voice message using local Whisper AI",
			QJsonObject{
				{"type", "object"},
				{"properties", QJsonObject{
					{"chat_id", QJsonObject{{"type", "integer"}, {"description", "Chat ID"}}},
					{"message_id", QJsonObject{{"type", "integer"}, {"description", "Message ID"}}},
					{"language", QJsonObject{{"type", "string"}, {"description", "Language code (auto-detect if empty)"}, {"default", "auto"}}}
				}},
				{"required", QJsonArray{"chat_id", "message_id"}}
			}
		},
		Tool{
			"get_transcription_status",
			"Get status of a transcription job",
			QJsonObject{
				{"type", "object"},
				{"properties", QJsonObject{
					{"transcription_id", QJsonObject{{"type", "string"}, {"description", "Transcription job ID"}}},
					{"message_id", QJsonObject{{"type", "integer"}, {"description", "Message this applies to."}}},
				}},
				{"required", QJsonArray{"transcription_id"}}
			}
		},

		// Translation (local) - 3 tools
		Tool{
			"translate_messages",
			"Translate messages using local AI translation",
			QJsonObject{
				{"type", "object"},
				{"properties", QJsonObject{
					{"chat_id", QJsonObject{{"type", "integer"}, {"description", "Chat ID"}}},
					{"message_id", QJsonObject{{"type", "array"}, {"items", QJsonObject{{"type", "integer"}}}, {"description", "Message IDs to translate"}}},
					{"target_language", QJsonObject{{"type", "string"}, {"description", "Target language code"}}},
					{"source_language", QJsonObject{{"type", "string"}, {"description", "Language of the source text; auto detects it."}}},
				}},
				{"required", QJsonArray{"chat_id", "message_id", "target_language"}}
			}
		},
		Tool{
			"auto_translate_chat",
			"Enable/disable automatic translation for a chat",
			QJsonObject{
				{"type", "object"},
				{"properties", QJsonObject{
					{"chat_id", QJsonObject{{"type", "integer"}, {"description", "Chat ID"}}},
					{"target_language", QJsonObject{{"type", "string"}, {"description", "Target language"}}},
					{"enabled", QJsonObject{{"type", "boolean"}, {"description", "Enable or disable"}}}
				}},
				{"required", QJsonArray{"chat_id", "target_language", "enabled"}}
			}
		},
		Tool{
			"get_translation_languages",
			"Get available translation languages",
			QJsonObject{{"type", "object"}, {"properties", QJsonObject{}}}
		},

		// Message Tags - 4 tools
		Tool{
			"tag_message",
			"Add a tag to a message",
			QJsonObject{
				{"type", "object"},
				{"properties", QJsonObject{
					{"chat_id", QJsonObject{{"type", "integer"}, {"description", "Chat ID"}}},
					{"message_id", QJsonObject{{"type", "integer"}, {"description", "Message ID"}}},
					{"tag", QJsonObject{{"type", "array"}, {"items", QJsonObject{{"type", "string"}}}, {"description", "Tags to add"}}},
					{"color", QJsonObject{{"type", "string"}, {"description", "Tag colour as a hex string (default #3390ec)."}}},
				}},
				{"required", QJsonArray{"chat_id", "message_id", "tag"}}
			}
		},
		Tool{
			"get_tagged_messages",
			"Get messages with specific tags",
			QJsonObject{
				{"type", "object"},
				{"properties", QJsonObject{
					{"tag", QJsonObject{{"type", "array"}, {"items", QJsonObject{{"type", "string"}}}, {"description", "Tags to filter by"}}},
					{"limit", QJsonObject{{"type", "integer"}, {"default", 50}, {"description", "Maximum number of results to return."}}}
				}},
				{"required", QJsonArray{"tag"}}
			}
		},
		Tool{
			"list_tags",
			"List all tags with usage counts",
			QJsonObject{{"type", "object"}, {"properties", QJsonObject{
					{"chat_id", QJsonObject{{"type", "integer"}, {"description", "Chat this applies to."}}},
					{"message_id", QJsonObject{{"type", "integer"}, {"description", "Message this applies to."}}},
				}}}
		},
		Tool{
			"delete_tag",
			"Delete a tag from all messages",
			QJsonObject{
				{"type", "object"},
				{"properties", QJsonObject{
					{"tag", QJsonObject{{"type", "string"}, {"description", "Tag to delete"}}},
					{"chat_id", QJsonObject{{"type", "integer"}, {"description", "Chat this applies to."}}},
					{"message_id", QJsonObject{{"type", "integer"}, {"description", "Message this applies to."}}},
				}},
				{"required", QJsonArray{"tag"}}
			}
		},

		// Ad Filtering - 2 tools
		Tool{
			"configure_ad_filter",
			"Configure ad filtering settings",
			QJsonObject{
				{"type", "object"},
				{"properties", QJsonObject{
					{"enabled", QJsonObject{{"type", "boolean"}, {"description", "Turn the feature on or off (default true)."}}},
					{"exclude_chats", QJsonObject{{"type", "array"}, {"description", "Chat ids to exempt, as an array."}}},
					{"keywords", QJsonObject{{"type", "array"}, {"description", "Keywords to match, as an array of strings."}}},
				}}
			}
		},
		Tool{
			"get_filtered_ads",
			"Get aggregate ad-filter statistics (counts, not the individual ads)",
			QJsonObject{
				{"type", "object"},
				{"properties", QJsonObject{
				}}
			}
		},

		// Chat Rules Engine - 4 tools
		Tool{
			"create_chat_rule",
			"Create an auto-management rule for chats",
			QJsonObject{
				{"type", "object"},
				{"properties", QJsonObject{
					{"rule_name", QJsonObject{{"type", "string"}, {"description", "Rule name"}}},
					{"conditions", QJsonObject{{"type", "object"}, {"description", "Conditions JSON"}}},
					{"actions", QJsonObject{{"type", "object"}, {"description", "Actions JSON"}}},
					{"chat_id", QJsonObject{{"type", "integer"}, {"description", "Chat this applies to."}}},
					{"rule_type", QJsonObject{{"type", "string"}, {"description", "Kind of rule to create."}}},
				}},
				{"required", QJsonArray{"rule_name", "conditions", "actions"}}
			}
		},
		Tool{
			"list_chat_rules",
			"List all chat management rules",
			QJsonObject{{"type", "object"}, {"properties", QJsonObject{
					{"chat_id", QJsonObject{{"type", "integer"}, {"description", "Chat this applies to."}}},
				}}}
		},
		Tool{
			"execute_chat_rules",
			"Manually execute chat rules",
			QJsonObject{{"type", "object"}, {"properties", QJsonObject{
					{"chat_id", QJsonObject{{"type", "integer"}, {"description", "Chat this applies to."}}},
					{"test_message", QJsonObject{{"type", "string"}, {"description", "Message text to run the rules against, for testing."}}},
				}}}
		},
		Tool{
			"delete_chat_rule",
			"Delete a chat rule",
			QJsonObject{
				{"type", "object"},
				{"properties", QJsonObject{
					{"rule_id", QJsonObject{{"type", "integer"}, {"description", "Rule ID"}}},
					{"rule_name", QJsonObject{{"type", "string"}, {"description", "Name identifying the rule."}}},
				}},
				{"required", QJsonArray{"rule_id"}}
			}
		},

		// Local Task Management - 2 tools
		Tool{
			"create_task",
			"Create a task/todo item",
			QJsonObject{
				{"type", "object"},
				{"properties", QJsonObject{
					{"title", QJsonObject{{"type", "string"}, {"description", "Task title"}}},
					{"chat_id", QJsonObject{{"type", "integer"}, {"description", "Associated chat ID"}}},
					{"message_id", QJsonObject{{"type", "integer"}, {"description", "Associated message ID"}}},
					{"due_date", QJsonObject{{"type", "integer"}, {"description", "Due date (Unix timestamp)"}}},
					{"priority", QJsonObject{{"type", "integer"}, {"description", "Priority; higher is more urgent."}}},
				}},
				{"required", QJsonArray{"title"}}
			}
		},
		Tool{
			"list_tasks",
			"List tasks with optional filtering",
			QJsonObject{
				{"type", "object"},
				{"properties", QJsonObject{
					{"status", QJsonObject{{"type", "string"}, {"description", "Filter by status (pending, completed)"}}},
					{"chat_id", QJsonObject{{"type", "integer"}, {"description", "Filter by chat"}}},
					{"limit", QJsonObject{{"type", "integer"}, {"description", "Maximum number of results to return (default 50)."}}},
				}}
			}
		},

		// ===== BUSINESS EQUIVALENT FEATURES (36 tools) =====

		// Quick Replies - 5 tools
		Tool{
			"create_quick_reply",
			"Create a quick reply template",
			QJsonObject{
				{"type", "object"},
				{"properties", QJsonObject{
					{"shortcut", QJsonObject{{"type", "string"}, {"description", "Shortcut command (e.g., /hello)"}}},
					{"text", QJsonObject{{"type", "string"}, {"description", "Reply text"}}},
					{"category", QJsonObject{{"type", "string"}, {"description", "Category for organization"}}}
				}},
				{"required", QJsonArray{"shortcut", "text"}}
			}
		},
		Tool{
			"list_quick_replies",
			"List all quick replies",
			QJsonObject{
				{"type", "object"},
				{"properties", QJsonObject{
					{"category", QJsonObject{{"type", "string"}, {"description", "Filter by category"}}},
					{"limit", QJsonObject{{"type", "integer"}, {"description", "Maximum number of results to return (default 50)."}}},
				}}
			}
		},
		Tool{
			"send_quick_reply",
			"Send a quick reply to a chat",
			QJsonObject{
				{"type", "object"},
				{"properties", QJsonObject{
					{"chat_id", QJsonObject{{"type", "integer"}, {"description", "Chat ID"}}},
					{"shortcut", QJsonObject{{"type", "string"}, {"description", "Quick reply shortcut"}}}
				}},
				{"required", QJsonArray{"chat_id", "shortcut"}}
			}
		},
		Tool{
			"edit_quick_reply",
			"Edit an existing quick reply",
			QJsonObject{
				{"type", "object"},
				{"properties", QJsonObject{
					{"id", QJsonObject{{"type", "integer"}, {"description", "Quick reply ID"}}},
					{"shortcut", QJsonObject{{"type", "string"}, {"description", "Shortcut name identifying the quick reply."}}},
					{"text", QJsonObject{{"type", "string"}, {"description", "Replacement text for the quick reply."}}},
					{"category", QJsonObject{{"type", "string"}, {"description", "Category the quick reply is filed under."}}}
				}},
				{"required", QJsonArray{"id"}}
			}
		},
		Tool{
			"delete_quick_reply",
			"Delete a quick reply",
			QJsonObject{
				{"type", "object"},
				{"properties", QJsonObject{
					{"id", QJsonObject{{"type", "integer"}, {"description", "Quick reply ID"}}}
				}},
				{"required", QJsonArray{"id"}}
			}
		},

		// Greeting Messages - 4 tools
		Tool{
			"configure_greeting",
			"Configure automatic greeting message",
			QJsonObject{
				{"type", "object"},
				{"properties", QJsonObject{
					{"enabled", QJsonObject{{"type", "boolean"}, {"description", "Turn the greeting message on or off."}}},
					{"message", QJsonObject{{"type", "string"}, {"description", "Greeting message text"}}},
					{"delay_seconds", QJsonObject{{"type", "integer"}, {"default", 0}, {"description", "How long to wait before sending."}}},
					{"trigger_chats", QJsonObject{{"type", "array"}, {"description", "Chat ids this applies to, as an array."}}},
				}},
				{"required", QJsonArray{"enabled", "message"}}
			}
		},
		Tool{
			"get_greeting_config",
			"Get current greeting configuration",
			QJsonObject{{"type", "object"}, {"properties", QJsonObject{}}}
		},
		Tool{
			"test_greeting",
			"Test the greeting message (send to yourself)",
			QJsonObject{{"type", "object"}, {"properties", QJsonObject{
					{"chat_id", QJsonObject{{"type", "integer"}, {"description", "Chat this applies to."}}},
				}}}
		},
		Tool{
			"get_greeting_stats",
			"Get greeting message statistics",
			QJsonObject{{"type", "object"}, {"properties", QJsonObject{}}}
		},

		// Away Messages - 5 tools
		Tool{
			"configure_away_message",
			"Configure automatic away message",
			QJsonObject{
				{"type", "object"},
				{"properties", QJsonObject{
					{"enabled", QJsonObject{{"type", "boolean"}, {"description", "Turn the away message on or off."}}},
					{"message", QJsonObject{{"type", "string"}, {"description", "Away message text"}}},
					{"start_time", QJsonObject{{"type", "integer"}, {"description", "Start time (Unix)"}}},
					{"end_time", QJsonObject{{"type", "integer"}, {"description", "End time (Unix)"}}}
				}},
				{"required", QJsonArray{"enabled", "message"}}
			}
		},
		Tool{
			"get_away_config",
			"Get current away configuration",
			QJsonObject{{"type", "object"}, {"properties", QJsonObject{}}}
		},
		Tool{
			"set_away_now",
			"Enable away mode immediately",
			QJsonObject{
				{"type", "object"},
				{"properties", QJsonObject{
					{"message", QJsonObject{{"type", "string"}, {"description", "Away message"}}},
				}},
				{"required", QJsonArray{"message"}}
			}
		},
		Tool{
			"disable_away",
			"Disable away mode",
			QJsonObject{{"type", "object"}, {"properties", QJsonObject{}}}
		},
		Tool{
			"get_away_stats",
			"Get away message statistics",
			QJsonObject{{"type", "object"}, {"properties", QJsonObject{}}}
		},

		// Business Hours - 3 tools
		Tool{
			"set_business_hours",
			"Set business hours schedule",
			QJsonObject{
				{"type", "object"},
				{"properties", QJsonObject{
					{"schedule", QJsonObject{{"type", "array"}, {"description", "Array of day schedules"}}},
					{"timezone", QJsonObject{{"type", "string"}, {"default", "UTC"}, {"description", "IANA timezone name, such as Europe/Berlin."}}}
				}},
				{"required", QJsonArray{"schedule"}}
			}
		},
		Tool{
			"get_business_hours",
			"Get business hours configuration",
			QJsonObject{{"type", "object"}, {"properties", QJsonObject{}}}
		},
		Tool{
			"is_open_now",
			"Check if currently within business hours",
			QJsonObject{{"type", "object"}, {"properties", QJsonObject{}}}
		},

		// Business Location - 2 tools
		Tool{
			"set_business_location",
			"Set business location",
			QJsonObject{
				{"type", "object"},
				{"properties", QJsonObject{
					{"address", QJsonObject{{"type", "string"}, {"description", "Street address"}}},
					{"latitude", QJsonObject{{"type", "number"}, {"description", "Latitude in decimal degrees."}}},
					{"longitude", QJsonObject{{"type", "number"}, {"description", "Longitude in decimal degrees."}}}
				}},
				{"required", QJsonArray{"address"}}
			}
		},
		Tool{
			"get_business_location",
			"Get business location",
			QJsonObject{{"type", "object"}, {"properties", QJsonObject{}}}
		},

		// AI Chatbot - 7 tools
		Tool{
			"configure_ai_chatbot",
			"Configure AI chatbot settings",
			QJsonObject{
				{"type", "object"},
				{"properties", QJsonObject{
					{"name", QJsonObject{{"type", "string"}, {"description", "Human-readable name."}}},
					{"personality", QJsonObject{{"type", "string"}, {"description", "System persona the chatbot answers in."}}},
					{"response_style", QJsonObject{{"type", "string"}, {"description", "Reply length and tone."}}},
					{"trigger_keywords", QJsonObject{{"type", "array"}, {"description", "Keywords that trigger a reply, as an array of strings."}}},
				}},
				{"required", QJsonArray{}}
			}
		},
		Tool{
			"get_chatbot_config",
			"Get AI chatbot configuration",
			QJsonObject{{"type", "object"}, {"properties", QJsonObject{}}}
		},
		Tool{
			"pause_chatbot",
			"Pause the AI chatbot",
			QJsonObject{{"type", "object"}, {"properties", QJsonObject{}}}
		},
		Tool{
			"resume_chatbot",
			"Resume the AI chatbot",
			QJsonObject{{"type", "object"}, {"properties", QJsonObject{}}}
		},
		Tool{
			"set_chatbot_prompt",
			"Update the chatbot system prompt",
			QJsonObject{
				{"type", "object"},
				{"properties", QJsonObject{
					{"prompt", QJsonObject{{"type", "string"}, {"description", "System prompt the chatbot answers with."}}}
				}},
				{"required", QJsonArray{"prompt"}}
			}
		},
		Tool{
			"get_chatbot_stats",
			"Get chatbot usage statistics",
			QJsonObject{{"type", "object"}, {"properties", QJsonObject{}}}
		},
		Tool{
			"train_chatbot",
			"Add training data to chatbot",
			QJsonObject{
				{"type", "object"},
				{"properties", QJsonObject{
					{"training_data", QJsonObject{{"type", "array"}, {"description", "Array of {input, output} examples"}}},
					{"category", QJsonObject{{"type", "string"}, {"description", "Restrict results to one marketplace category; omit for all categories."}}},
					{"test_after_train", QJsonObject{{"type", "boolean"}, {"description", "Run a test exchange once training finishes (default false)."}}},
				}},
				{"required", QJsonArray{"training_data"}}
			}
		},

		// AI Voice (TTS) - 5 tools
		Tool{
			"configure_voice_persona",
			"Configure AI voice settings for TTS",
			QJsonObject{
				{"type", "object"},
				{"properties", QJsonObject{
					{"name", QJsonObject{{"type", "string"}, {"description", "Persona name"}}},
					{"voice_id", QJsonObject{{"type", "string"}, {"description", "Voice ID"}}},
					{"pitch", QJsonObject{{"type", "number"}, {"description", "Voice pitch adjustment; 0 leaves it unmodified."}}},
					{"speed", QJsonObject{{"type", "number"}, {"description", "Speech rate multiplier; 1.0 is normal speed."}}},
				}},
				{"required", QJsonArray{"name", "voice_id"}}
			}
		},
		Tool{
			"generate_voice_message",
			"Generate a voice message from text",
			QJsonObject{
				{"type", "object"},
				{"properties", QJsonObject{
					{"text", QJsonObject{{"type", "string"}, {"description", "Text to speak"}}},
					{"pitch", QJsonObject{{"type", "number"}, {"description", "Voice pitch adjustment; 0 leaves it unmodified."}}},
					{"speed", QJsonObject{{"type", "number"}, {"description", "Speech rate multiplier; 1.0 is normal speed."}}},
					{"voice", QJsonObject{{"type", "string"}, {"description", "Voice to speak with."}}},
				}},
				{"required", QJsonArray{"text"}}
			}
		},
		Tool{
			"send_voice_reply",
			"Generate and send a voice reply",
			QJsonObject{
				{"type", "object"},
				{"properties", QJsonObject{
					{"chat_id", QJsonObject{{"type", "integer"}, {"description", "Chat ID"}}},
					{"text", QJsonObject{{"type", "string"}, {"description", "Text to speak"}}},
					{"persona", QJsonObject{{"type", "string"}, {"description", "Named voice persona to speak with."}}},
				}},
				{"required", QJsonArray{"chat_id", "text"}}
			}
		},
		Tool{
			"list_voice_presets",
			"List available voice presets",
			QJsonObject{{"type", "object"}, {"properties", QJsonObject{}}}
		},
		Tool{
			"clone_voice",
			"Clone a voice from audio sample",
			QJsonObject{
				{"type", "object"},
				{"properties", QJsonObject{
					{"name", QJsonObject{{"type", "string"}, {"description", "Clone name"}}},
					{"audio_path", QJsonObject{{"type", "string"}, {"description", "Path to audio sample"}}},
					{"audio_sample", QJsonObject{{"type", "string"}, {"description", "Path to an audio file to clone the voice from."}}},
				}},
				{"required", QJsonArray{"name", "audio_path"}}
			}
		},

		// AI Video Circles (TTV) - 5 tools
		Tool{
			"configure_video_avatar",
			"Configure AI video avatar settings",
			QJsonObject{
				{"type", "object"},
				{"properties", QJsonObject{
					{"name", QJsonObject{{"type", "string"}, {"description", "Avatar name"}}},
					{"file_path", QJsonObject{{"type", "string"}, {"description", "Avatar image/video path"}}},
				}},
				{"required", QJsonArray{"name", "file_path"}}
			}
		},
		Tool{
			"generate_video_circle",
			"Generate a video circle from text",
			QJsonObject{
				{"type", "object"},
				{"properties", QJsonObject{
					{"text", QJsonObject{{"type", "string"}, {"description", "Text to speak"}}},
					{"preset", QJsonObject{{"type", "string"}, {"description", "Avatar preset name"}}},
					{"speed", QJsonObject{{"type", "number"}, {"description", "Speech rate multiplier; 1.0 is normal speed."}}},
					{"voice", QJsonObject{{"type", "string"}, {"description", "Voice to speak with."}}},
				}},
				{"required", QJsonArray{"text"}}
			}
		},
		Tool{
			"send_video_reply",
			"Generate and send a video circle reply",
			QJsonObject{
				{"type", "object"},
				{"properties", QJsonObject{
					{"chat_id", QJsonObject{{"type", "integer"}, {"description", "Chat ID"}}},
					{"text", QJsonObject{{"type", "string"}, {"description", "Text to speak"}}},
					{"preset", QJsonObject{{"type", "string"}, {"description", "Named preset controlling the output style (default default)."}}},
					{"speed", QJsonObject{{"type", "number"}, {"description", "Speech rate multiplier; 1.0 is normal speed."}}},
					{"voice", QJsonObject{{"type", "string"}, {"description", "Voice to speak with."}}},
				}},
				{"required", QJsonArray{"chat_id", "text"}}
			}
		},
		Tool{
			"upload_avatar_source",
			"Upload a new avatar source image/video",
			QJsonObject{
				{"type", "object"},
				{"properties", QJsonObject{
					{"name", QJsonObject{{"type", "string"}, {"description", "Avatar name"}}},
					{"file_path", QJsonObject{{"type", "string"}, {"description", "Path to source file"}}}
				}},
				{"required", QJsonArray{"name", "file_path"}}
			}
		},
		Tool{
			"list_avatar_presets",
			"List available avatar presets",
			QJsonObject{{"type", "object"}, {"properties", QJsonObject{}}}
		},

		// ===== WALLET FEATURES (32 tools) =====

		// Balance & Analytics - 4 tools
		Tool{
			"get_wallet_balance",
			"Get current Stars/TON wallet balance",
			QJsonObject{{"type", "object"}, {"properties", QJsonObject{}}}
		},
		Tool{
			"get_balance_history",
			"Get balance history over time",
			QJsonObject{
				{"type", "object"},
				{"properties", QJsonObject{
					{"days", QJsonObject{{"type", "integer"}, {"default", 30}, {"description", "How many days back to cover."}}}
				}}
			}
		},
		Tool{
			"get_spending_analytics",
			"Get spending analytics breakdown",
			QJsonObject{
				{"type", "object"},
				{"properties", QJsonObject{
					{"period", QJsonObject{{"type", "string"}, {"description", "day, week, month, year"}}}
				}}
			}
		},
		Tool{
			"get_income_analytics",
			"Get income analytics breakdown",
			QJsonObject{
				{"type", "object"},
				{"properties", QJsonObject{
					{"period", QJsonObject{{"type", "string"}, {"description", "day, week, month, year"}}}
				}}
			}
		},

		// Transactions - 4 tools
		Tool{
			"get_transactions",
			"Get transaction history",
			QJsonObject{
				{"type", "object"},
				{"properties", QJsonObject{
					{"limit", QJsonObject{{"type", "integer"}, {"default", 50}, {"description", "Maximum number of results to return."}}},
					{"type", QJsonObject{{"type", "string"}, {"description", "Filter by type"}}}
				}}
			}
		},
		Tool{
			"get_transaction_details",
			"Get details of a specific transaction",
			QJsonObject{
				{"type", "object"},
				{"properties", QJsonObject{
					{"transaction_id", QJsonObject{{"type", "string"}, {"description", "Transaction ID"}}}
				}},
				{"required", QJsonArray{"transaction_id"}}
			}
		},
		Tool{
			"export_transactions",
			"Export transactions to file",
			QJsonObject{
				{"type", "object"},
				{"properties", QJsonObject{
					{"format", QJsonObject{{"type", "string"}, {"description", "csv, json"}}},
					{"start_date", QJsonObject{{"type", "integer"}, {"description", "Start of the range, as a Unix timestamp."}}},
					{"end_date", QJsonObject{{"type", "integer"}, {"description", "End of the range, as a Unix timestamp."}}}
				}},
				{"required", QJsonArray{"format"}}
			}
		},
		Tool{
			"search_transactions",
			"Search transactions",
			QJsonObject{
				{"type", "object"},
				{"properties", QJsonObject{
					{"query", QJsonObject{{"type", "string"}, {"description", "Search query"}}},
					{"limit", QJsonObject{{"type", "integer"}, {"default", 50}, {"description", "Maximum number of results to return."}}},
					{"category", QJsonObject{{"type", "string"}, {"description", "Restrict results to one marketplace category; omit for all categories."}}},
				}},
				{"required", QJsonArray{"query"}}
			}
		},

		// Gifts - 4 tools
		Tool{
			"list_gifts",
			"List received/sent gifts",
			QJsonObject{
				{"type", "object"},
				{"properties", QJsonObject{
				}}
			}
		},
		Tool{
			"get_gift_details",
			"Get details of a specific gift",
			QJsonObject{
				{"type", "object"},
				{"properties", QJsonObject{
					{"slug", QJsonObject{{"type", "integer"}, {"description", "Gift ID"}}}
				}},
				{"required", QJsonArray{"slug"}}
			}
		},
		Tool{
			"get_gift_analytics",
			"Get reaction analytics (this returns reaction data, not gift data)",
			QJsonObject{{"type", "object"}, {"properties", QJsonObject{
					{"period", QJsonObject{{"type", "string"}, {"description", "Time window to report over, such as week or month."}}},
				}}}
		},
		Tool{
			"send_stars",
			"Send Stars to a user. NOT POSSIBLE: Telegram has no user-to-user star transfer, so this always reports failure. send_gift, which the recipient converts, is the nearest route -- a different operation, not an equivalent transfer.",
			QJsonObject{
				{"type", "object"},
				{"properties", QJsonObject{
					{"recipient_id", QJsonObject{{"type", "integer"}, {"description", "Recipient user ID"}}},
					{"amount", QJsonObject{{"type", "integer"}, {"description", "Number of Stars"}}},
					{"message", QJsonObject{{"type", "string"}, {"description", "Optional message"}}}
				}},
				{"required", QJsonArray{"recipient_id", "amount"}}
			}
		},

		// Subscriptions - 3 tools
		Tool{
			"list_subscriptions",
			"List active subscriptions",
			QJsonObject{{"type", "object"}, {"properties", QJsonObject{}}}
		},
		Tool{
			"get_subscription_alerts",
			"Get subscription renewal alerts",
			QJsonObject{{"type", "object"}, {"properties", QJsonObject{}}}
		},
		Tool{
			"cancel_subscription",
			"Cancel a subscription",
			QJsonObject{
				{"type", "object"},
				{"properties", QJsonObject{
					{"subscription_id", QJsonObject{{"type", "string"}, {"description", "Subscription ID"}}},
					{"channel_id", QJsonObject{{"type", "integer"}, {"description", "Channel this applies to."}}},
				}},
				{"required", QJsonArray{"subscription_id"}}
			}
		},

		// Monetization - 5 tools
		Tool{
			"get_channel_earnings",
			"Get earnings for a channel",
			QJsonObject{
				{"type", "object"},
				{"properties", QJsonObject{
					{"channel_id", QJsonObject{{"type", "integer"}, {"description", "Channel ID"}}}
				}},
				{"required", QJsonArray{"channel_id"}}
			}
		},
		Tool{
			"get_all_channels_earnings",
			"Get earnings for all channels",
			QJsonObject{{"type", "object"}, {"properties", QJsonObject{}}}
		},
		Tool{
			"get_earnings_chart",
			"Get earnings chart data",
			QJsonObject{
				{"type", "object"},
				{"properties", QJsonObject{
					{"days", QJsonObject{{"type", "integer"}, {"description", "How many days back to cover (default 30)."}}},
				}}
			}
		},
		Tool{
			"get_reaction_stats",
			"Get star reaction statistics",
			QJsonObject{
				{"type", "object"},
				{"properties", QJsonObject{
					{"period", QJsonObject{{"type", "string"}, {"description", "Time window to report over, such as week or month."}}},
				}}
			}
		},
		Tool{
			"get_paid_content_earnings",
			"Get paid content earnings",
			QJsonObject{
				{"type", "object"},
				{"properties", QJsonObject{
				}}
			}
		},

		// Giveaways - 3 tools
		Tool{
			"get_giveaway_options",
			"Get giveaway configuration options",
			QJsonObject{{"type", "object"}, {"properties", QJsonObject{}}}
		},
		Tool{
			"list_giveaways",
			"List active and past giveaways",
			QJsonObject{
				{"type", "object"},
				{"properties", QJsonObject{
					{"status", QJsonObject{{"type", "string"}, {"description", "active, completed, all"}}},
					{"limit", QJsonObject{{"type", "integer"}, {"description", "Maximum number of results to return (default 50)."}}},
				}}
			}
		},
		Tool{
			"get_giveaway_stats",
			"Get giveaway statistics",
			QJsonObject{
				{"type", "object"},
				{"properties", QJsonObject{
					{"giveaway_id", QJsonObject{{"type", "integer"}, {"description", "Giveaway ID"}}}
				}},
				{"required", QJsonArray{"giveaway_id"}}
			}
		},

		// Advanced Wallet - 4 tools
		Tool{
			"get_topup_options",
			"Get available top-up options",
			QJsonObject{{"type", "object"}, {"properties", QJsonObject{}}}
		},
		Tool{
			"get_star_rating",
			"Get user's star rating/level",
			QJsonObject{{"type", "object"}, {"properties", QJsonObject{}}}
		},
		Tool{
			"get_withdrawal_status",
			"Get withdrawal status and options",
			QJsonObject{{"type", "object"}, {"properties", QJsonObject{}}}
		},
		Tool{
			"create_crypto_payment",
			"Create a crypto payment request",
			QJsonObject{
				{"type", "object"},
				{"properties", QJsonObject{
					{"amount", QJsonObject{{"type", "number"}, {"description", "Amount"}}},
					{"currency", QJsonObject{{"type", "string"}, {"description", "Currency (TON, etc)"}}},
					{"action", QJsonObject{{"type", "string"}, {"description", "Operation to perform (default send)."}}},
					{"comment", QJsonObject{{"type", "string"}, {"description", "Comment attached to the transfer."}}},
					{"limit", QJsonObject{{"type", "integer"}, {"description", "Maximum number of results to return (default 20)."}}},
					{"mnemonics", QJsonObject{{"type", "string"}, {"description", "Wallet seed phrase used to sign the transfer."}}},
					{"recipient", QJsonObject{{"type", "string"}, {"description", "Destination wallet address."}}},
				}},
				{"required", QJsonArray{"amount", "currency"}}
			}
		},

		// Budget & Reporting - 5 tools
		Tool{
			"set_wallet_budget",
			"Set spending budget for a category",
			QJsonObject{
				{"type", "object"},
				{"properties", QJsonObject{
					{"daily_limit", QJsonObject{{"type", "number"}, {"description", "Daily spending cap; 0 means no limit."}}},
					{"monthly_limit", QJsonObject{{"type", "number"}, {"description", "Monthly spending cap; 0 means no limit."}}},
					{"weekly_limit", QJsonObject{{"type", "number"}, {"description", "Weekly spending cap; 0 means no limit."}}},
				}},
				{"required", QJsonArray{}}
			}
		},
		Tool{
			"get_budget_status",
			"Get budget status for a category",
			QJsonObject{
				{"type", "object"},
				{"properties", QJsonObject{
					{"category", QJsonObject{{"type", "string"}, {"description", "Category name"}}}
				}},
				{"required", QJsonArray{"category"}}
			}
		},
		Tool{
			"configure_wallet_alerts",
			"Configure wallet spending alerts",
			QJsonObject{
				{"type", "object"},
				{"properties", QJsonObject{
					{"threshold", QJsonObject{{"type", "number"}, {"description", "Alert threshold, interpreted according to type."}}},
					{"type", QJsonObject{{"type", "string"}, {"description", "How threshold is read: percentage or absolute."}}},
				}}
			}
		},
		Tool{
			"generate_financial_report",
			"Generate a financial report",
			QJsonObject{
				{"type", "object"},
				{"properties", QJsonObject{
					{"period", QJsonObject{{"type", "string"}, {"description", "Time window to report over, such as day, week or month (default month)."}}},
				}},
				{"required", QJsonArray{}}
			}
		},
		Tool{
			"get_tax_summary",
			"Get tax summary for earnings",
			QJsonObject{
				{"type", "object"},
				{"properties", QJsonObject{
					{"year", QJsonObject{{"type", "integer"}, {"description", "Tax year"}}}
				}},
				{"required", QJsonArray{"year"}}
			}
		},

		// ===== STARS FEATURES (45 tools) =====

		// Star Gifts Management - 8 tools
		Tool{
			"list_star_gifts",
			"List available star gifts",
			QJsonObject{
				{"type", "object"},
				{"properties", QJsonObject{
				}}
			}
		},
		Tool{
			"get_star_gift_details",
			"Get recorded price history for a gift type",
			QJsonObject{
				{"type", "object"},
				{"properties", QJsonObject{
					{"gift_type", QJsonObject{{"type", "integer"}, {"description", "Gift ID"}}},
					{"days", QJsonObject{{"type", "integer"}, {"description", "How many days back to cover."}}},
				}},
				{"required", QJsonArray{"gift_type"}}
			}
		},
		Tool{
			"get_unique_gift_analytics",
			"Get analytics for unique/collectible gifts",
			QJsonObject{
				{"type", "object"},
				{"properties", QJsonObject{
					{"gift_type", QJsonObject{{"type", "integer"}, {"description", "Gift ID"}}}
				}},
				{"required", QJsonArray{"gift_type"}}
			}
		},
		Tool{
			"get_collectibles_portfolio",
			"Get user's collectibles portfolio",
			QJsonObject{{"type", "object"}, {"properties", QJsonObject{}}}
		},
		Tool{
			"send_star_gift",
			"Send a star gift to a user",
			QJsonObject{
				{"type", "object"},
				{"properties", QJsonObject{
					{"recipient_id", QJsonObject{{"type", "integer"}, {"description", "Recipient user ID"}}},
					{"gift_id", QJsonObject{{"type", "integer"}, {"description", "Gift ID"}}},
					{"message", QJsonObject{{"type", "string"}, {"description", "Optional message"}}},
					{"anonymous", QJsonObject{{"type", "boolean"}, {"description", "Hide the sender's identity from the recipient."}}},
					{"stars_amount", QJsonObject{{"type", "integer"}, {"description", "Amount in Telegram Stars."}}},
				}},
				{"required", QJsonArray{"recipient_id", "gift_id"}}
			}
		},
		Tool{
			"get_gift_transfer_history",
			"Get transfer history for a gift",
			QJsonObject{
				{"type", "object"},
				{"properties", QJsonObject{
					{"gift_id", QJsonObject{{"type", "integer"}, {"description", "Gift ID"}}},
					{"limit", QJsonObject{{"type", "integer"}, {"description", "Maximum number of results to return (default 50)."}}},
				}},
				{"required", QJsonArray{}}
			}
		},
		Tool{
			"get_upgrade_options",
			"Get upgrade options for a gift",
			QJsonObject{
				{"type", "object"},
				{"properties", QJsonObject{
					{"gift_id", QJsonObject{{"type", "integer"}, {"description", "Gift ID"}}}
				}},
				{"required", QJsonArray{"gift_id"}}
			}
		},
		Tool{
			"transfer_gift",
			"Transfer a gift to another user",
			QJsonObject{
				{"type", "object"},
				{"properties", QJsonObject{
					{"gift_id", QJsonObject{{"type", "integer"}, {"description", "Gift ID"}}},
					{"recipient_id", QJsonObject{{"type", "integer"}, {"description", "Recipient user ID"}}}
				}},
				{"required", QJsonArray{"gift_id", "recipient_id"}}
			}
		},

		// Gift Collections - 3 tools
		Tool{
			"list_gift_collections",
			"List available gift collections",
			QJsonObject{{"type", "object"}, {"properties", QJsonObject{}}}
		},
		Tool{
			"get_collection_details",
			"Get details of a collection",
			QJsonObject{
				{"type", "object"},
				{"properties", QJsonObject{
					{"collection_id", QJsonObject{{"type", "integer"}, {"description", "Collection ID"}}}
				}},
				{"required", QJsonArray{"collection_id"}}
			}
		},
		Tool{
			"get_collection_completion",
			"Get collection completion status",
			QJsonObject{
				{"type", "object"},
				{"properties", QJsonObject{
					{"collection_id", QJsonObject{{"type", "integer"}, {"description", "Collection ID"}}}
				}},
				{"required", QJsonArray{"collection_id"}}
			}
		},

		// Auctions - 5 tools
		//
		// The first three are pass-throughs to list_auctions,
		// get_auction_status and place_bid. They are kept so existing callers
		// keep working; prefer the names they delegate to.
		Tool{
			"list_active_auctions",
			"List the star-gift auctions Telegram currently has running. Alias for list_auctions.",
			QJsonObject{
				{"type", "object"},
				{"properties", QJsonObject{}}
			}
		},
		Tool{
			"get_auction_details",
			"Get the live state of one star-gift auction. Alias for get_auction_status.",
			QJsonObject{
				{"type", "object"},
				{"properties", QJsonObject{
					{"gift_id", QJsonObject{{"type", "integer"}, {"description", "ID of the gift being auctioned -- an auction is identified by its gift."}}}
				}},
				{"required", QJsonArray{"gift_id"}}
			}
		},
		Tool{
			"get_auction_alerts",
			"Get the auction alerts configured on this client. These are local reminders only -- Telegram does not know about them.",
			QJsonObject{{"type", "object"}, {"properties", QJsonObject{}}}
		},
		Tool{
			"place_auction_bid",
			"Place a bid on a star-gift auction. NOT IMPLEMENTED -- alias for place_bid, which always reports failure.",
			QJsonObject{
				{"type", "object"},
				{"properties", QJsonObject{
					{"gift_id", QJsonObject{{"type", "integer"}, {"description", "ID of the gift being auctioned -- an auction is identified by its gift."}}},
					{"bid_amount", QJsonObject{{"type", "integer"}, {"description", "Bid amount in stars."}}}
				}},
				{"required", QJsonArray{"gift_id", "bid_amount"}}
			}
		},
		Tool{
			"get_auction_history",
			"List the gifts a finished star-gift auction handed out, with the winning bid, round and position for each.",
			QJsonObject{{"type", "object"}, {"properties", QJsonObject{
					{"gift_id", QJsonObject{{"type", "integer"}, {"description", "ID of the auctioned gift whose results to fetch."}}},
				}},
				{"required", QJsonArray{"gift_id"}}}
		},

		// Marketplace - 5 tools
		Tool{
			"browse_gift_marketplace",
			"Browse the copies of one star gift currently offered for resale. Alias for list_marketplace.",
			QJsonObject{
				{"type", "object"},
				{"properties", QJsonObject{
					{"gift_id", QJsonObject{{"type", "integer"}, {"description", "Gift type whose resale listings to browse."}}},
					{"sort_by", QJsonObject{{"type", "string"}, {"description", "Order results by asking price (\"price\") or issue number (\"num\"); anything else leaves Telegram's default order."}}},
					{"offset", QJsonObject{{"type", "string"}, {"description", "Opaque paging cursor from a previous call's next_offset; omit for the first page."}}},
					{"limit", QJsonObject{{"type", "integer"}, {"default", 50}, {"description", "Maximum number of results to return."}}}
				}},
				{"required", QJsonArray{"gift_id"}}
			}
		},
		Tool{
			"get_market_trends",
			"Get marketplace trends",
			QJsonObject{
				{"type", "object"},
				{"properties", QJsonObject{
					{"days", QJsonObject{{"type", "integer"}, {"description", "How many days back to cover (default 7)."}}},
					{"gift_type", QJsonObject{{"type", "string"}, {"description", "Restrict to one kind of gift."}}},
				}}
			}
		},
		Tool{
			"list_gift_for_sale",
			"Offer one of your star gifts for resale at a given price. Identify the gift by slug or by msg_id -- one of the two is required.",
			QJsonObject{
				{"type", "object"},
				{"properties", QJsonObject{
					{"slug", QJsonObject{{"type", "string"}, {"description", "Slug of the unique gift being offered. Use this or msg_id."}}},
					{"msg_id", QJsonObject{{"type", "integer"}, {"description", "Message ID of the saved gift in your profile. Use this or slug."}}},
					{"price", QJsonObject{{"type", "integer"}, {"description", "Asking price in stars. Zero takes it off sale, the same as delist_gift."}}},
				}},
				{"required", QJsonArray{"price"}}
			}
		},
		Tool{
			"update_listing",
			"Update a marketplace listing",
			QJsonObject{
				{"type", "object"},
				{"properties", QJsonObject{
					{"slug", QJsonObject{{"type", "integer"}, {"description", "Listing ID"}}},
					{"price", QJsonObject{{"type", "number"}, {"description", "New price"}}},
					{"msg_id", QJsonObject{{"type", "integer"}, {"description", "Message id of the saved gift being relisted; an alternative to slug."}}},
				}},
				{"required", QJsonArray{"slug", "price"}}
			}
		},
		Tool{
			"cancel_listing",
			"Take one of your star gifts off resale. Alias for delist_gift.",
			QJsonObject{
				{"type", "object"},
				{"properties", QJsonObject{
					{"slug", QJsonObject{{"type", "string"}, {"description", "Slug of the unique gift to take off sale. Use this or msg_id."}}},
					{"msg_id", QJsonObject{{"type", "integer"}, {"description", "Message ID of the saved gift in your profile. Use this or slug."}}}
				}}
			}
		},

		// Star Reactions - 3 tools
		Tool{
			"get_star_reactions_received",
			"Get star reactions received",
			QJsonObject{
				{"type", "object"},
				{"properties", QJsonObject{
				}}
			}
		},
		Tool{
			"get_star_reactions_sent",
			"Get star reactions sent",
			QJsonObject{
				{"type", "object"},
				{"properties", QJsonObject{
				}}
			}
		},
		Tool{
			"get_top_supporters",
			"Get top supporters by star reactions",
			QJsonObject{
				{"type", "object"},
				{"properties", QJsonObject{
					{"limit", QJsonObject{{"type", "integer"}, {"default", 10}, {"description", "Maximum number of results to return."}}}
				}}
			}
		},

		// Paid Content - 4 tools
		Tool{
			"get_paid_messages_stats",
			"Get paid messages statistics",
			QJsonObject{{"type", "object"}, {"properties", QJsonObject{}}}
		},
		Tool{
			"configure_paid_messages",
			"Configure paid message settings",
			QJsonObject{
				{"type", "object"},
				{"properties", QJsonObject{
					{"enabled", QJsonObject{{"type", "boolean"}, {"description", "Turn paid messages on or off for the chat."}}},
					{"chat_id", QJsonObject{{"type", "integer"}, {"description", "Chat this applies to."}}},
					{"price", QJsonObject{{"type", "integer"}, {"description", "Price in Telegram Stars."}}},
				}}
			}
		},
		Tool{
			"get_paid_media_stats",
			"Get paid media statistics",
			QJsonObject{{"type", "object"}, {"properties", QJsonObject{}}}
		},
		Tool{
			"get_unlocked_content",
			"Get list of unlocked paid content",
			QJsonObject{
				{"type", "object"},
				{"properties", QJsonObject{
				}}
			}
		},

		// Mini Apps - 3 tools
		Tool{
			"get_miniapp_spending",
			"Get spending in mini apps",
			QJsonObject{
				{"type", "object"},
				{"properties", QJsonObject{
					{"miniapp_id", QJsonObject{{"type", "string"}, {"description", "Restrict results to one mini app; omit for all of them."}}},
				}}
			}
		},
		Tool{
			"get_miniapp_history",
			"Get mini app transaction history",
			QJsonObject{
				{"type", "object"},
				{"properties", QJsonObject{
				}}
			}
		},
		Tool{
			"set_miniapp_budget",
			"Approve a mini app spend (does not set a standing budget -- use set_spending_budget for that)",
			QJsonObject{
				{"type", "object"},
				{"properties", QJsonObject{
					{"miniapp_id", QJsonObject{{"type", "string"}, {"description", "App ID"}}},
					{"amount", QJsonObject{{"type", "number"}, {"description", "Amount in Telegram Stars."}}},
				}},
				{"required", QJsonArray{"miniapp_id"}}
			}
		},

		// Star Rating - 3 tools
		Tool{
			"get_star_rating_details",
			"Get detailed star rating breakdown",
			QJsonObject{{"type", "object"}, {"properties", QJsonObject{}}}
		},
		Tool{
			"get_rating_history",
			"Get rating history over time",
			QJsonObject{
				{"type", "object"},
				{"properties", QJsonObject{
					{"days", QJsonObject{{"type", "integer"}, {"default", 30}, {"description", "How many days back to cover."}}}
				}}
			}
		},
		Tool{
			"simulate_rating_change",
			"Simulate how actions affect rating",
			QJsonObject{
				{"type", "object"},
				{"properties", QJsonObject{
					{"additional_reactions", QJsonObject{{"type", "integer"}, {"description", "Hypothetical extra reactions to simulate (default 1)."}}},
					{"additional_stars", QJsonObject{{"type", "integer"}, {"description", "Hypothetical extra stars to simulate."}}},
				}},
				{"required", QJsonArray{}}
			}
		},

		// Profile Display - 4 tools
		Tool{
			"get_profile_gifts",
			"Get gifts displayed on profile",
			QJsonObject{{"type", "object"}, {"properties", QJsonObject{}}}
		},
		Tool{
			"update_gift_display",
			"Update gift display settings",
			QJsonObject{
				{"type", "object"},
				{"properties", QJsonObject{
					{"msg_ids", QJsonObject{{"type", "integer"}, {"description", "Message ids of the gifts whose display is being changed."}}},
					{"chat_id", QJsonObject{{"type", "integer"}, {"description", "Chat this applies to."}}},
				}},
				{"required", QJsonArray{"msg_ids"}}
			}
		},
		Tool{
			"reorder_profile_gifts",
			"Reorder gifts on profile",
			QJsonObject{
				{"type", "object"},
				{"properties", QJsonObject{
					{"collection_ids", QJsonObject{{"type", "array"}, {"items", QJsonObject{{"type", "integer"}}}, {"description", "Ordered list of gift IDs"}}},
					{"chat_id", QJsonObject{{"type", "integer"}, {"description", "Chat this applies to."}}},
				}},
				{"required", QJsonArray{"collection_ids"}}
			}
		},
		Tool{
			"toggle_gift_notifications",
			"Toggle gift notifications",
			QJsonObject{
				{"type", "object"},
				{"properties", QJsonObject{
					{"enabled", QJsonObject{{"type", "boolean"}, {"description", "Turn gift notifications on or off."}}},
					{"chat_id", QJsonObject{{"type", "integer"}, {"description", "Chat this applies to."}}},
				}},
				{"required", QJsonArray{"enabled"}}
			}
		},

		// AI & Analytics - 7 tools
		Tool{
			"get_gift_investment_advice",
			"Get AI investment advice for gifts",
			QJsonObject{
				{"type", "object"},
				{"properties", QJsonObject{
				}}
			}
		},
		Tool{
			"backtest_strategy",
			"Backtest a gift investment strategy",
			QJsonObject{
				{"type", "object"},
				{"properties", QJsonObject{
					{"strategy", QJsonObject{{"type", "object"}, {"description", "Strategy parameters"}}},
					{"days", QJsonObject{{"type", "integer"}, {"description", "How many days back to cover (default 30)."}}},
					{"gift_type", QJsonObject{{"type", "string"}, {"description", "Restrict to one kind of gift."}}},
					{"initial_investment", QJsonObject{{"type", "number"}, {"description", "Starting capital for the simulation, in stars (default 1000)."}}},
				}},
				{"required", QJsonArray{"strategy"}}
			}
		},
		Tool{
			"get_portfolio_performance",
			"Get portfolio performance metrics",
			QJsonObject{{"type", "object"}, {"properties", QJsonObject{}}}
		},
		Tool{
			"create_price_alert",
			"Create a price alert for a gift",
			QJsonObject{
				{"type", "object"},
				{"properties", QJsonObject{
					{"gift_type", QJsonObject{{"type", "integer"}, {"description", "Gift ID"}}},
					{"target_price", QJsonObject{{"type", "number"}, {"description", "Target price"}}},
					{"direction", QJsonObject{{"type", "string"}, {"description", "above or below"}}}
				}},
				{"required", QJsonArray{"gift_type", "target_price"}}
			}
		},
		Tool{
			"create_auction_alert",
			"Create an auction alert",
			QJsonObject{
				{"type", "object"},
				{"properties", QJsonObject{
					{"auction_id", QJsonObject{{"type", "integer"}, {"description", "Gift ID"}}},
					{"price_threshold", QJsonObject{{"type", "number"}, {"description", "Maximum bid"}}},
				}},
				{"required", QJsonArray{"auction_id", "price_threshold"}}
			}
		},
		Tool{
			"get_fragment_listings",
			"Browse the copies of one star gift currently offered for resale. Alias for list_marketplace -- these are Telegram's own resale listings, not Fragment's.",
			QJsonObject{
				{"type", "object"},
				{"properties", QJsonObject{
					{"gift_id", QJsonObject{{"type", "integer"}, {"description", "Gift type whose resale listings to browse."}}},
					{"limit", QJsonObject{{"type", "integer"}, {"default", 50}, {"description", "Maximum number of results to return."}}},
					{"offset", QJsonObject{{"type", "string"}, {"description", "Opaque paging cursor from a previous call's next_offset; omit for the first page."}}},
					{"sort_by", QJsonObject{{"type", "string"}, {"description", "Order results by asking price (\"price\") or issue number (\"num\"); anything else leaves Telegram's default order."}}},
				}},
				{"required", QJsonArray{"gift_id"}}
			}
		},
		Tool{
			"export_portfolio_report",
			"Export portfolio report",
			QJsonObject{
				{"type", "object"},
				{"properties", QJsonObject{
					{"format", QJsonObject{{"type", "string"}, {"description", "pdf, csv, json"}}}
				}},
				{"required", QJsonArray{"format"}}
			}
		},

		// ===== GRADUAL EXPORT TOOLS (9) =====
		Tool{
			"start_gradual_export",
			"Start gradual/covert export of a chat with natural timing patterns to avoid detection",
			QJsonObject{
				{"type", "object"},
				{"properties", QJsonObject{
					{"chat_id", QJsonObject{{"type", "integer"}, {"description", "Chat ID to export"}}},
					{"min_delay_ms", QJsonObject{{"type", "integer"}, {"description", "Min delay between batches (ms)"}, {"default", 3000}}},
					{"max_delay_ms", QJsonObject{{"type", "integer"}, {"description", "Max delay between batches (ms)"}, {"default", 15000}}},
					{"min_batch_size", QJsonObject{{"type", "integer"}, {"description", "Min messages per batch"}, {"default", 10}}},
					{"max_batch_size", QJsonObject{{"type", "integer"}, {"description", "Max messages per batch"}, {"default", 50}}},
					{"export_format", QJsonObject{{"type", "string"}, {"description", "html, markdown, or both"}, {"default", "html"}}},
					{"export_path", QJsonObject{{"type", "string"}, {"description", "Output directory path"}}}
				}},
				{"required", QJsonArray{"chat_id"}}
			}
		},
		Tool{
			"get_gradual_export_status",
			"Get status of current gradual export operation",
			QJsonObject{{"type", "object"}, {"properties", QJsonObject{}}}
		},
		Tool{
			"pause_gradual_export",
			"Pause the current gradual export",
			QJsonObject{{"type", "object"}, {"properties", QJsonObject{}}}
		},
		Tool{
			"resume_gradual_export",
			"Resume a paused gradual export",
			QJsonObject{{"type", "object"}, {"properties", QJsonObject{}}}
		},
		Tool{
			"cancel_gradual_export",
			"Cancel the current gradual export",
			QJsonObject{{"type", "object"}, {"properties", QJsonObject{}}}
		},
		Tool{
			"get_gradual_export_config",
			"Get current gradual export configuration",
			QJsonObject{{"type", "object"}, {"properties", QJsonObject{}}}
		},
		Tool{
			"set_gradual_export_config",
			"Set gradual export configuration parameters",
			QJsonObject{
				{"type", "object"},
				{"properties", QJsonObject{
				}}
			}
		},
		Tool{
			"queue_gradual_export",
			"Add a chat to the gradual export queue",
			QJsonObject{
				{"type", "object"},
				{"properties", QJsonObject{
					{"chat_id", QJsonObject{{"type", "integer"}, {"description", "Chat ID to queue"}}},
				}},
				{"required", QJsonArray{"chat_id"}}
			}
		},
		Tool{
			"get_gradual_export_queue",
			"Get list of chats in the gradual export queue",
			QJsonObject{{"type", "object"}, {"properties", QJsonObject{}}}
		},


	};

	// ============================================================
	// DELETED ACCOUNT ARCHIVING TOOLS
	// ============================================================

	{
		Tool tool;
		tool.name = "list_deleted_accounts";
		tool.description = "Scan and list all chats with deleted Telegram accounts";

		// No parameters. An `include_archive_folder` toggle used to be
		// advertised here, but toolListDeletedAccounts never reads its
		// arguments and GradualArchiver::scanDeletedAccounts() takes none --
		// the scan scope is fixed, so the toggle could not have had any
		// effect whichever way it was set.
		QJsonObject properties;

		QJsonObject schema;
		schema["type"] = "object";
		schema["properties"] = properties;
		tool.inputSchema = schema;
		_tools.append(tool);
	}

	{
		Tool tool;
		tool.name = "archive_deleted_accounts";
		tool.description = "Archive all deleted account chats to a group with date headers. Uses human simulation (random delays, active hours, rate limits) to avoid detection.";

		QJsonObject groupTitleProp;
		groupTitleProp["type"] = "string";
		groupTitleProp["description"] = "Name for the archive group (default: 'Deleted Accounts Archive')";

		QJsonObject targetGroupIdProp;
		targetGroupIdProp["type"] = "integer";
		targetGroupIdProp["description"] = "Existing group peer ID to archive into (0 = auto-create new group)";

		QJsonObject minDelayProp;
		minDelayProp["type"] = "integer";
		minDelayProp["description"] = "Minimum delay between batches in ms (default: 3000)";

		QJsonObject maxDelayProp;
		maxDelayProp["type"] = "integer";
		maxDelayProp["description"] = "Maximum delay between batches in ms (default: 15000)";

		QJsonObject dateFormatProp;
		dateFormatProp["type"] = "string";
		dateFormatProp["description"] = "Date header format, %1 = YYYY-MM-DD (default: '# %1')";

		QJsonObject addDateHeadersProp;
		addDateHeadersProp["type"] = "boolean";
		addDateHeadersProp["description"] = "Insert date headers between messages (default: true)";

		QJsonObject addChatSepProp;
		addChatSepProp["type"] = "boolean";
		addChatSepProp["description"] = "Insert separator between different chat archives (default: true)";

		QJsonObject properties;
		properties["group_title"] = groupTitleProp;
		properties["target_group_id"] = targetGroupIdProp;
		properties["min_delay_ms"] = minDelayProp;
		properties["max_delay_ms"] = maxDelayProp;
		properties["date_format"] = dateFormatProp;
		properties["add_date_headers"] = addDateHeadersProp;
		properties["add_chat_separators"] = addChatSepProp;

		QJsonObject peerIdProp;
		peerIdProp["type"] = "integer";
		peerIdProp["description"] = "Archive a single specific peer ID";
		properties["peer_id"] = peerIdProp;

		QJsonObject peerIdsProp;
		peerIdsProp["type"] = "array";
		QJsonObject peerIdItemSchema;
		peerIdItemSchema["type"] = "integer";
		peerIdsProp["items"] = peerIdItemSchema;
		peerIdsProp["description"] = "Archive specific peer IDs (array)";
		properties["peer_ids"] = peerIdsProp;

		QJsonObject schema;
		schema["type"] = "object";
		schema["properties"] = properties;
		tool.inputSchema = schema;
		_tools.append(tool);
	}

	{
		Tool tool;
		tool.name = "get_deleted_archive_status";
		tool.description = "Get current status of deleted account archiving operation";

		QJsonObject schema;
		schema["type"] = "object";
		schema["properties"] = QJsonObject();
		tool.inputSchema = schema;
		_tools.append(tool);
	}

	{
		Tool tool;
		tool.name = "pause_deleted_archive";
		tool.description = "Pause the deleted account archiving operation";

		QJsonObject schema;
		schema["type"] = "object";
		schema["properties"] = QJsonObject();
		tool.inputSchema = schema;
		_tools.append(tool);
	}

	{
		Tool tool;
		tool.name = "resume_deleted_archive";
		tool.description = "Resume a paused deleted account archiving operation";

		QJsonObject schema;
		schema["type"] = "object";
		schema["properties"] = QJsonObject();
		tool.inputSchema = schema;
		_tools.append(tool);
	}

	{
		Tool tool;
		tool.name = "cancel_deleted_archive";
		tool.description = "Cancel the deleted account archiving operation";

		QJsonObject schema;
		schema["type"] = "object";
		schema["properties"] = QJsonObject();
		tool.inputSchema = schema;
		_tools.append(tool);
	}

	{
		Tool tool;
		tool.name = "list_deleted_channels";
		tool.description = "List deleted/deactivated/forbidden channels and groups with their IDs and last 5 messages for identification";

		QJsonObject schema;
		schema["type"] = "object";
		schema["properties"] = QJsonObject();
		tool.inputSchema = schema;
		_tools.append(tool);
	}

	// Continue with remaining tools in initializer list style
	const std::vector<Tool> moreTools = {
		// ===== VOICE & TRANSLATION TOOLS =====
		Tool{
			"get_voice_transcription",
			"Get transcription of a voice message",
			QJsonObject{{"type", "object"}, {"properties", QJsonObject{
				{"message_id", QJsonObject{{"type", "integer"}, {"description", "Voice message ID to transcribe"}}},
			}}, {"required", QJsonArray{"message_id", "message_id"}}}
		},
		Tool{
			"translate_message",
			"Translate a single message to a target language",
			QJsonObject{{"type", "object"}, {"properties", QJsonObject{
				{"message_id", QJsonObject{{"type", "integer"}, {"description", "Message ID to translate"}}},
				{"chat_id", QJsonObject{{"type", "integer"}, {"description", "Chat containing the message"}}},
				{"target_language", QJsonObject{{"type", "string"}, {"description", "Target language code (e.g. en, ru, es)"}}},
					{"source_language", QJsonObject{{"type", "string"}, {"description", "Language of the source text; auto detects it (default auto)."}}},
				}}, {"required", QJsonArray{"message_id", "chat_id", "target_language"}}}
		},
		Tool{
			"list_voice_personas",
			"List available TTS voice personas",
			QJsonObject{{"type", "object"}, {"properties", QJsonObject{}}}
		},

		// ===== BUSINESS TOOLS =====
		Tool{
			"set_away_message",
			"Set an away message for the account",
			QJsonObject{{"type", "object"}, {"properties", QJsonObject{
				{"message", QJsonObject{{"type", "string"}, {"description", "Away message text"}}},
				{"enabled", QJsonObject{{"type", "boolean"}, {"description", "Enable or disable away message"}}},
					{"end_time", QJsonObject{{"type", "string"}, {"description", "Time of day the away message stops applying, HH:MM."}}},
					{"start_time", QJsonObject{{"type", "string"}, {"description", "Time of day the away message starts applying, HH:MM."}}},
				}}, {"required", QJsonArray{"message"}}}
		},
		Tool{
			"check_business_status",
			"Check current business feature status and availability",
			QJsonObject{{"type", "object"}, {"properties", QJsonObject{}}}
		},

		// ===== TASK TOOLS =====
		Tool{
			"create_task_from_message",
			"Create a task from an existing message",
			QJsonObject{{"type", "object"}, {"properties", QJsonObject{
				{"message_id", QJsonObject{{"type", "integer"}, {"description", "Message ID to create task from"}}},
				{"chat_id", QJsonObject{{"type", "integer"}, {"description", "Chat containing the message"}}},
					{"due_date", QJsonObject{{"type", "string"}, {"description", "Due date, as an ISO 8601 string."}}},
					{"priority", QJsonObject{{"type", "integer"}, {"description", "Priority; higher is more urgent (default 2)."}}},
					{"title", QJsonObject{{"type", "string"}, {"description", "Title text."}}},
				}}, {"required", QJsonArray{"message_id", "chat_id"}}}
		},

		// ===== PROFILE SETTINGS TOOLS =====
		Tool{
			"get_profile_settings",
			"Get current user profile settings (name, bio, username, phone, birthday)",
			QJsonObject{{"type", "object"}, {"properties", QJsonObject{}}}
		},
		Tool{
			"update_profile_name",
			"Update user first and/or last name",
			QJsonObject{{"type", "object"}, {"properties", QJsonObject{
				{"first_name", QJsonObject{{"type", "string"}, {"description", "New first name"}}},
				{"last_name", QJsonObject{{"type", "string"}, {"description", "New last name"}}}
			}}}
		},
		Tool{
			"update_profile_bio",
			"Update user bio/about text",
			QJsonObject{{"type", "object"}, {"properties", QJsonObject{
				{"bio", QJsonObject{{"type", "string"}, {"description", "New bio text (max 70 chars)"}}}
			}}, {"required", QJsonArray{"bio"}}}
		},
		Tool{
			"update_profile_username",
			"Update user public username",
			QJsonObject{{"type", "object"}, {"properties", QJsonObject{
				{"username", QJsonObject{{"type", "string"}, {"description", "New username"}}}
			}}, {"required", QJsonArray{"username"}}}
		},
		Tool{
			"update_profile_phone",
			"Initiate phone number change (requires SMS verification)",
			QJsonObject{{"type", "object"}, {"properties", QJsonObject{
				{"phone", QJsonObject{{"type", "string"}, {"description", "New phone number"}}},
					{"code", QJsonObject{{"type", "string"}, {"description", "Confirmation code received by SMS."}}},
					{"phone_code_hash", QJsonObject{{"type", "string"}, {"description", "Hash returned by the request that sent the code."}}},
				}}, {"required", QJsonArray{"phone"}}}
		},

		// ===== PRIVACY SETTINGS TOOLS =====
		Tool{
			"get_privacy_settings",
			"Get all privacy settings (last seen, profile photo, phone, forwards, birthday, about)",
			QJsonObject{{"type", "object"}, {"properties", QJsonObject{}}}
		},
		Tool{
			"update_last_seen_privacy",
			"Set who can see your last seen time",
			QJsonObject{{"type", "object"}, {"properties", QJsonObject{
				{"rule", QJsonObject{{"type", "string"}, {"description", "everybody, contacts, close_friends, or nobody"}}}
			}}, {"required", QJsonArray{"rule"}}}
		},
		Tool{
			"update_profile_photo_privacy",
			"Set who can see your profile photo",
			QJsonObject{{"type", "object"}, {"properties", QJsonObject{
				{"rule", QJsonObject{{"type", "string"}, {"description", "everybody, contacts, close_friends, or nobody"}}}
			}}, {"required", QJsonArray{"rule"}}}
		},
		Tool{
			"update_phone_number_privacy",
			"Set who can see your phone number",
			QJsonObject{{"type", "object"}, {"properties", QJsonObject{
				{"rule", QJsonObject{{"type", "string"}, {"description", "everybody, contacts, close_friends, or nobody"}}}
			}}, {"required", QJsonArray{"rule"}}}
		},
		Tool{
			"update_forwards_privacy",
			"Set who can link to your account when forwarding messages",
			QJsonObject{{"type", "object"}, {"properties", QJsonObject{
				{"rule", QJsonObject{{"type", "string"}, {"description", "everybody, contacts, close_friends, or nobody"}}}
			}}, {"required", QJsonArray{"rule"}}}
		},
		Tool{
			"update_birthday_privacy",
			"Set who can see your birthday",
			QJsonObject{{"type", "object"}, {"properties", QJsonObject{
				{"rule", QJsonObject{{"type", "string"}, {"description", "everybody, contacts, close_friends, or nobody"}}}
			}}, {"required", QJsonArray{"rule"}}}
		},
		Tool{
			"update_about_privacy",
			"Set who can see your bio/about",
			QJsonObject{{"type", "object"}, {"properties", QJsonObject{
				{"rule", QJsonObject{{"type", "string"}, {"description", "everybody, contacts, close_friends, or nobody"}}}
			}}, {"required", QJsonArray{"rule"}}}
		},
		Tool{
			"get_blocked_users",
			"Get list of blocked users",
			QJsonObject{{"type", "object"}, {"properties", QJsonObject{}}}
		},

		// ===== SECURITY SETTINGS TOOLS =====
		Tool{
			"get_security_settings",
			"Get security settings including auto-delete period",
			QJsonObject{{"type", "object"}, {"properties", QJsonObject{}}}
		},
		Tool{
			"get_active_sessions",
			"Get list of all active Telegram sessions/devices",
			QJsonObject{{"type", "object"}, {"properties", QJsonObject{}}}
		},
		Tool{
			"terminate_session",
			"Terminate a specific active session by hash",
			QJsonObject{{"type", "object"}, {"properties", QJsonObject{
				{"hash", QJsonObject{{"type", "string"}, {"description", "Session hash to terminate"}}}
			}}, {"required", QJsonArray{"hash"}}}
		},
		Tool{
			"block_user",
			"Block a user by their ID",
			QJsonObject{{"type", "object"}, {"properties", QJsonObject{
				{"user_id", QJsonObject{{"type", "integer"}, {"description", "User ID to block"}}}
			}}, {"required", QJsonArray{"user_id"}}}
		},
		Tool{
			"unblock_user",
			"Unblock a previously blocked user",
			QJsonObject{{"type", "object"}, {"properties", QJsonObject{
				{"user_id", QJsonObject{{"type", "integer"}, {"description", "User ID to unblock"}}}
			}}, {"required", QJsonArray{"user_id"}}}
		},
		Tool{
			"update_auto_delete_period",
			"Set default auto-delete period for new chats (0=off, 86400=1day, 604800=1week, 2592000=1month)",
			QJsonObject{{"type", "object"}, {"properties", QJsonObject{
				{"period", QJsonObject{{"type", "integer"}, {"description", "Auto-delete period in seconds (0, 86400, 604800, 2592000)"}}}
			}}, {"required", QJsonArray{"period"}}}
		},

		// ===== PREVIOUSLY UNREGISTERED TOOLS =====

		// --- Message Tags ---
		Tool{"add_message_tag", "Add a tag to a message", QJsonObject{{"type", "object"}, {"properties", QJsonObject{
			{"chat_id", QJsonObject{{"type", "integer"}, {"description", "Chat ID"}}},
			{"message_id", QJsonObject{{"type", "integer"}, {"description", "Message ID"}}},
			{"tag", QJsonObject{{"type", "string"}, {"description", "Tag name"}}},
					{"color", QJsonObject{{"type", "string"}, {"description", "Tag colour as a hex string (default #3390ec)."}}},
				}}, {"required", QJsonArray{"chat_id", "message_id", "tag"}}}},
		Tool{"remove_message_tag", "Remove a tag from a message", QJsonObject{{"type", "object"}, {"properties", QJsonObject{
			{"chat_id", QJsonObject{{"type", "integer"}, {"description", "Chat ID"}}},
			{"message_id", QJsonObject{{"type", "integer"}, {"description", "Message ID"}}},
			{"tag", QJsonObject{{"type", "string"}, {"description", "Tag name"}}}
		}}, {"required", QJsonArray{"chat_id", "message_id", "tag"}}}},
		Tool{"get_message_tags", "Get all tags on a message", QJsonObject{{"type", "object"}, {"properties", QJsonObject{
			{"chat_id", QJsonObject{{"type", "integer"}, {"description", "Chat ID"}}},
			{"message_id", QJsonObject{{"type", "integer"}, {"description", "Message ID"}}}
		}}, {"required", QJsonArray{"chat_id", "message_id"}}}},
		Tool{"search_by_tag", "Search messages by tag", QJsonObject{{"type", "object"}, {"properties", QJsonObject{
			{"tag", QJsonObject{{"type", "string"}, {"description", "Tag to search for"}}},
			{"chat_id", QJsonObject{{"type", "integer"}, {"description", "Optional chat filter"}}},
					{"limit", QJsonObject{{"type", "integer"}, {"description", "Maximum number of results to return (default 50)."}}},
				}}, {"required", QJsonArray{"tag"}}}},
		Tool{"get_tag_suggestions", "Get tag suggestions for a message", QJsonObject{{"type", "object"}, {"properties", QJsonObject{
			{"text", QJsonObject{{"type", "string"}, {"description", "Message text to analyze"}}},
					{"limit", QJsonObject{{"type", "integer"}, {"description", "Maximum number of results to return (default 5)."}}},
				}}, {"required", QJsonArray{"text"}}}},

		// --- Translation ---
		Tool{"get_translation_history", "Get recent translations", QJsonObject{{"type", "object"}, {"properties", QJsonObject{
			{"limit", QJsonObject{{"type", "integer"}, {"description", "Max results"}, {"default", 50}}},
					{"target_language", QJsonObject{{"type", "string"}, {"description", "Language to translate into."}}},
				}}}},

		// --- Ad Filter ---
		Tool{"get_ad_filter_stats", "Get ad filtering statistics", QJsonObject{{"type", "object"}, {"properties", QJsonObject{}}}},

		// --- Chat Rules ---
		Tool{"get_chat_rules", "Get chat automation rules", QJsonObject{{"type", "object"}, {"properties", QJsonObject{
			{"chat_id", QJsonObject{{"type", "integer"}, {"description", "Chat ID"}}}
		}}}},
		Tool{"set_chat_rules", "Set chat automation rules", QJsonObject{{"type", "object"}, {"properties", QJsonObject{
			{"chat_id", QJsonObject{{"type", "integer"}, {"description", "Chat ID"}}},
					{"actions", QJsonObject{{"type", "object"}, {"description", "Actions the rule performs when it matches, as an object."}}},
					{"conditions", QJsonObject{{"type", "object"}, {"description", "Conditions the rule tests, as an object."}}},
					{"rule_name", QJsonObject{{"type", "string"}, {"description", "Name identifying the rule."}}},
					{"rule_type", QJsonObject{{"type", "string"}, {"description", "Kind of rule to create."}}},
				}}, {"required", QJsonArray{"chat_id"}}}},
		Tool{"test_chat_rules", "Test chat rules against sample text", QJsonObject{{"type", "object"}, {"properties", QJsonObject{
			{"chat_id", QJsonObject{{"type", "integer"}, {"description", "Chat ID"}}},
			{"test_message", QJsonObject{{"type", "string"}, {"description", "Test message"}}}
		}}, {"required", QJsonArray{"chat_id", "test_message"}}}},

		// --- Quick Replies ---
		Tool{"update_quick_reply", "Update an existing quick reply", QJsonObject{{"type", "object"}, {"properties", QJsonObject{
			{"id", QJsonObject{{"type", "integer"}, {"description", "Quick reply ID"}}},
			{"text", QJsonObject{{"type", "string"}, {"description", "New reply text"}}},
					{"category", QJsonObject{{"type", "string"}, {"description", "Restrict results to one marketplace category; omit for all categories."}}},
					{"shortcut", QJsonObject{{"type", "string"}, {"description", "Quick-reply shortcut name."}}},
				}}, {"required", QJsonArray{"id", "text"}}}},
		Tool{"use_quick_reply", "Send a quick reply to a chat", QJsonObject{{"type", "object"}, {"properties", QJsonObject{
			{"shortcut", QJsonObject{{"type", "string"}, {"description", "Quick reply shortcut"}}},
			{"chat_id", QJsonObject{{"type", "integer"}, {"description", "Chat to send to"}}}
		}}, {"required", QJsonArray{"shortcut", "chat_id"}}}},

		// --- Greeting/Away ---
		Tool{"set_greeting_message", "Configure greeting message", QJsonObject{{"type", "object"}, {"properties", QJsonObject{
			{"message", QJsonObject{{"type", "string"}, {"description", "Greeting text"}}},
			{"enabled", QJsonObject{{"type", "boolean"}, {"description", "Enable/disable"}}},
					{"delay_seconds", QJsonObject{{"type", "integer"}, {"description", "How long to wait before sending (default 0)."}}},
					{"trigger_chats", QJsonObject{{"type", "array"}, {"description", "Chat ids this applies to, as an array."}}},
				}}, {"required", QJsonArray{"message"}}}},
		Tool{"get_greeting_message", "Get current greeting configuration", QJsonObject{{"type", "object"}, {"properties", QJsonObject{}}}},
		Tool{"disable_greeting", "Disable greeting message", QJsonObject{{"type", "object"}, {"properties", QJsonObject{}}}},
		Tool{"test_away", "Test away message configuration", QJsonObject{{"type", "object"}, {"properties", QJsonObject{
			{"chat_id", QJsonObject{{"type", "integer"}, {"description", "Chat ID to test with"}}}
		}}, {"required", QJsonArray{"chat_id"}}}},
		Tool{"get_away_message", "Get current away message configuration", QJsonObject{{"type", "object"}, {"properties", QJsonObject{}}}},
		Tool{"get_next_available_slot", "Get next available business hours slot", QJsonObject{{"type", "object"}, {"properties", QJsonObject{}}}},

		// --- AI Chatbot ---
		Tool{"configure_chatbot", "Configure AI chatbot settings", QJsonObject{{"type", "object"}, {"properties", QJsonObject{
			{"name", QJsonObject{{"type", "string"}, {"description", "Chatbot name"}}},
			{"personality", QJsonObject{{"type", "string"}, {"description", "Personality style"}}},
					{"response_style", QJsonObject{{"type", "string"}, {"description", "Reply length and tone (default concise)."}}},
					{"trigger_keywords", QJsonObject{{"type", "array"}, {"description", "Keywords that trigger a reply, as an array of strings."}}},
				}}, {"required", QJsonArray{"name"}}}},
		Tool{"test_chatbot", "Test chatbot with sample input", QJsonObject{{"type", "object"}, {"properties", QJsonObject{
			{"input", QJsonObject{{"type", "string"}, {"description", "Test message"}}}
		}}, {"required", QJsonArray{"input"}}}},
		Tool{"get_chatbot_analytics", "Get chatbot usage analytics", QJsonObject{{"type", "object"}, {"properties", QJsonObject{}}}},

		// --- TTS/Video ---
		Tool{"text_to_speech", "Convert text to speech audio", QJsonObject{{"type", "object"}, {"properties", QJsonObject{
			{"text", QJsonObject{{"type", "string"}, {"description", "Text to convert"}}},
			{"voice", QJsonObject{{"type", "string"}, {"description", "Voice preset name"}}},
					{"pitch", QJsonObject{{"type", "number"}, {"description", "Voice pitch adjustment; 0 leaves it unmodified."}}},
					{"speed", QJsonObject{{"type", "number"}, {"description", "Speech rate multiplier; 1.0 is normal speed."}}},
				}}, {"required", QJsonArray{"text"}}}},
		Tool{"text_to_video", "Generate video circle from text", QJsonObject{{"type", "object"}, {"properties", QJsonObject{
			{"text", QJsonObject{{"type", "string"}, {"description", "Text content"}}},
					{"preset", QJsonObject{{"type", "string"}, {"description", "Named preset controlling the output style (default default)."}}},
					{"speed", QJsonObject{{"type", "number"}, {"description", "Speech rate multiplier; 1.0 is normal speed."}}},
					{"voice", QJsonObject{{"type", "string"}, {"description", "Voice to speak with."}}},
				}}, {"required", QJsonArray{"text"}}}},

		// --- Auto Reply ---
		Tool{"create_auto_reply_rule", "Create auto-reply rule", QJsonObject{{"type", "object"}, {"properties", QJsonObject{
			{"triggers", QJsonObject{{"type", "string"}, {"description", "Trigger keyword or pattern"}}},
			{"response", QJsonObject{{"type", "string"}, {"description", "Auto-reply text"}}},
					{"name", QJsonObject{{"type", "string"}, {"description", "Human-readable name."}}},
					{"priority", QJsonObject{{"type", "integer"}, {"description", "Priority; higher is more urgent (default 5)."}}},
				}}, {"required", QJsonArray{"triggers", "response"}}}},
		Tool{"list_auto_reply_rules", "List all auto-reply rules", QJsonObject{{"type", "object"}, {"properties", QJsonObject{}}}},
		Tool{"update_auto_reply_rule", "Update an auto-reply rule", QJsonObject{{"type", "object"}, {"properties", QJsonObject{
			{"rule_id", QJsonObject{{"type", "integer"}, {"description", "Rule ID"}}},
			{"response", QJsonObject{{"type", "string"}, {"description", "New response text"}}},
					{"enabled", QJsonObject{{"type", "boolean"}, {"description", "Turn the feature on or off (default true)."}}},
					{"name", QJsonObject{{"type", "string"}, {"description", "Human-readable name."}}},
					{"triggers", QJsonObject{{"type", "object"}, {"description", "Trigger configuration for the rule, as an object."}}},
				}}, {"required", QJsonArray{"rule_id"}}}},
		Tool{"delete_auto_reply_rule", "Delete an auto-reply rule", QJsonObject{{"type", "object"}, {"properties", QJsonObject{
			{"rule_id", QJsonObject{{"type", "integer"}, {"description", "Rule ID"}}}
		}}, {"required", QJsonArray{"rule_id"}}}},
		Tool{"test_auto_reply_rule", "Test auto-reply rule against text", QJsonObject{{"type", "object"}, {"properties", QJsonObject{
			{"message", QJsonObject{{"type", "string"}, {"description", "Test message"}}}
		}}, {"required", QJsonArray{"message"}}}},
		Tool{"get_auto_reply_stats", "Get auto-reply usage statistics", QJsonObject{{"type", "object"}, {"properties", QJsonObject{}}}},

		// --- Gift Collections ---
		Tool{"create_gift_collection", "Create a collection around star gifts you already own. Telegram has no empty collections and no collection description or visibility flag -- a collection has a title and its gifts.", QJsonObject{{"type", "object"}, {"properties", QJsonObject{
			{"title", QJsonObject{{"type", "string"}, {"description", "Collection title."}}},
			{"msg_ids", QJsonObject{{"type", "array"}, {"items", QJsonObject{{"type", "integer"}}}, {"description", "Message IDs of the saved gifts to put in it -- at least one, since a collection cannot be created empty."}}},
				}}, {"required", QJsonArray{"title", "msg_ids"}}}},
		// share_collection used to be advertised here. Gift collections have
		// no share, publish or link method and no visibility flag; it only
		// ever set a boolean on a local row, so it was removed.
		Tool{"delete_gift_collection", "Delete one of your star gift collections. The gifts themselves are unaffected.", QJsonObject{{"type", "object"}, {"properties", QJsonObject{
			{"collection_id", QJsonObject{{"type", "integer"}, {"description", "Collection to delete, as returned by list_gift_collections."}}}
		}}, {"required", QJsonArray{"collection_id"}}}},
		Tool{"add_to_collection", "Add one of your star gifts to a collection.", QJsonObject{{"type", "object"}, {"properties", QJsonObject{
			{"collection_id", QJsonObject{{"type", "integer"}, {"description", "Collection to add to, as returned by list_gift_collections."}}},
			{"msg_id", QJsonObject{{"type", "integer"}, {"description", "Message ID of the saved gift in your profile -- how Telegram identifies a gift you own."}}}
		}}, {"required", QJsonArray{"collection_id", "msg_id"}}}},
		Tool{"remove_from_collection", "Remove one of your star gifts from a collection.", QJsonObject{{"type", "object"}, {"properties", QJsonObject{
			{"collection_id", QJsonObject{{"type", "integer"}, {"description", "Collection to remove from, as returned by list_gift_collections."}}},
			{"msg_id", QJsonObject{{"type", "integer"}, {"description", "Message ID of the saved gift in your profile -- how Telegram identifies a gift you own."}}}
		}}, {"required", QJsonArray{"collection_id", "msg_id"}}}},

		// --- Auctions ---
		//
		// Telegram opens and closes star-gift auctions itself, and identifies
		// each one by the gift being auctioned -- there is no client-side
		// create, no cancel, and no separate auction id. `create_gift_auction`
		// and `cancel_auction` used to be advertised here; both were backed by
		// a local table rather than by any API, so they were removed.
		Tool{"place_bid", "Place a bid on a star-gift auction. NOT IMPLEMENTED: bidding spends stars through the payment-form flow, and nothing is recorded locally, so this always reports failure.", QJsonObject{{"type", "object"}, {"properties", QJsonObject{
			{"gift_id", QJsonObject{{"type", "integer"}, {"description", "ID of the gift being auctioned -- an auction is identified by its gift."}}},
			{"bid_amount", QJsonObject{{"type", "integer"}, {"description", "Bid amount in stars."}}}
		}}, {"required", QJsonArray{"gift_id", "bid_amount"}}}},
		Tool{"list_auctions", "List the star-gift auctions Telegram currently has running, with each one's round state and this account's own position in it.", QJsonObject{{"type", "object"}, {"properties", QJsonObject{
				}}}},
		Tool{"get_auction_status", "Get the live state of one star-gift auction: round progress, bid levels, gifts left, and this account's own bid if it has placed one.", QJsonObject{{"type", "object"}, {"properties", QJsonObject{
			{"gift_id", QJsonObject{{"type", "integer"}, {"description", "ID of the gift being auctioned -- an auction is identified by its gift."}}}
		}}, {"required", QJsonArray{"gift_id"}}}},

		// --- Marketplace ---
		//
		// Telegram resells unique gifts by gift type, so you browse one gift's
		// listings rather than a global feed. There is no category and no
		// listing id: a copy on sale is identified by its slug, and your own
		// gift by the message id it occupies in your profile.
		Tool{"list_marketplace", "Browse the copies of one star gift currently offered for resale, each with its asking price and issue number.", QJsonObject{{"type", "object"}, {"properties", QJsonObject{
			{"gift_id", QJsonObject{{"type", "integer"}, {"description", "Gift type whose resale listings to browse."}}},
			{"sort_by", QJsonObject{{"type", "string"}, {"description", "Order results by asking price (\"price\") or issue number (\"num\"); anything else leaves Telegram's default order."}, {"default", "price"}}},
			{"offset", QJsonObject{{"type", "string"}, {"description", "Opaque paging cursor from a previous call's next_offset; omit for the first page."}}},
					{"limit", QJsonObject{{"type", "integer"}, {"description", "Maximum number of results to return (default 50)."}}},
				}}, {"required", QJsonArray{"gift_id"}}}},
		Tool{"buy_gift", "Buy a resold star gift. NOT IMPLEMENTED: this spends stars through the payment-form flow, and nothing is recorded locally, so it always reports failure.", QJsonObject{{"type", "object"}, {"properties", QJsonObject{
			{"slug", QJsonObject{{"type", "string"}, {"description", "Slug of the specific gift copy being bought, from list_marketplace."}}}
		}}, {"required", QJsonArray{"slug"}}}},
		Tool{"delist_gift", "Take one of your star gifts off resale by clearing its asking price. Identify the gift by slug or by msg_id.", QJsonObject{{"type", "object"}, {"properties", QJsonObject{
			{"slug", QJsonObject{{"type", "string"}, {"description", "Slug of the unique gift to take off sale. Use this or msg_id."}}},
			{"msg_id", QJsonObject{{"type", "integer"}, {"description", "Message ID of the saved gift in your profile. Use this or slug."}}}
		}}}},

		// --- Wallet/Gifts ---
		Tool{"send_gift", "Send a gift to a user", QJsonObject{{"type", "object"}, {"properties", QJsonObject{
			{"recipient_id", QJsonObject{{"type", "integer"}, {"description", "Recipient user ID"}}},
			{"stars_amount", QJsonObject{{"type", "integer"}, {"description", "Amount in stars"}}},
					{"anonymous", QJsonObject{{"type", "boolean"}, {"description", "Hide the sender's identity from the recipient (default false)."}}},
					{"gift_id", QJsonObject{{"type", "integer"}, {"description", "Gift to send."}}},
					{"message", QJsonObject{{"type", "string"}, {"description", "Message text sent with the gift."}}},
				}}, {"required", QJsonArray{"recipient_id", "stars_amount"}}}},
		Tool{"get_gift_history", "Get gift sending/receiving history", QJsonObject{{"type", "object"}, {"properties", QJsonObject{
			{"direction", QJsonObject{{"type", "string"}, {"description", "Filter: sent/received/both"}, {"default", "both"}}},
					{"limit", QJsonObject{{"type", "integer"}, {"description", "Maximum number of results to return (default 50)."}}},
				}}}},
		Tool{"list_available_gifts", "List available gift types", QJsonObject{{"type", "object"}, {"properties", QJsonObject{}}}},
		Tool{"get_gift_suggestions", "Get gift suggestions for a user", QJsonObject{{"type", "object"}, {"properties", QJsonObject{
			{"recipient_id", QJsonObject{{"type", "integer"}, {"description", "Recipient user ID"}}}
		}}, {"required", QJsonArray{"recipient_id"}}}},
		Tool{"get_gift_price_history", "Get price history for a gift type", QJsonObject{{"type", "object"}, {"properties", QJsonObject{
			{"gift_type", QJsonObject{{"type", "string"}, {"description", "Gift type"}}},
					{"days", QJsonObject{{"type", "integer"}, {"description", "How many days back to cover (default 30)."}}},
				}}, {"required", QJsonArray{"gift_type"}}}},

		// --- Subscriptions ---
		Tool{"subscribe_to_channel", "Subscribe to a channel", QJsonObject{{"type", "object"}, {"properties", QJsonObject{
			{"channel_id", QJsonObject{{"type", "integer"}, {"description", "Channel ID"}}},
					{"tier", QJsonObject{{"type", "string"}, {"description", "Subscription tier (default basic)."}}},
				}}, {"required", QJsonArray{"channel_id"}}}},
		Tool{"unsubscribe_from_channel", "Unsubscribe from a channel", QJsonObject{{"type", "object"}, {"properties", QJsonObject{
			{"channel_id", QJsonObject{{"type", "integer"}, {"description", "Channel ID"}}},
					{"subscription_id", QJsonObject{{"type", "string"}, {"description", "Subscription to act on."}}},
				}}, {"required", QJsonArray{"channel_id"}}}},
		Tool{"get_subscription_stats", "Get subscription spending statistics", QJsonObject{{"type", "object"}, {"properties", QJsonObject{}}}},

		// --- Monetization ---
		Tool{"get_earnings", "Get creator earnings data", QJsonObject{{"type", "object"}, {"properties", QJsonObject{
					{"channel_id", QJsonObject{{"type", "integer"}, {"description", "Channel this applies to."}}},
				}}}},
		Tool{"withdraw_earnings", "Withdraw earnings to wallet", QJsonObject{{"type", "object"}, {"properties", QJsonObject{
			{"amount", QJsonObject{{"type", "number"}, {"description", "Amount to withdraw"}}},
			{"method", QJsonObject{{"type", "string"}, {"description", "Withdrawal method (ton/fragment)"}, {"default", "ton"}}},
					{"channel_id", QJsonObject{{"type", "integer"}, {"description", "Channel this applies to."}}},
				}}, {"required", QJsonArray{"amount"}}}},
		Tool{"set_monetization_rules", "Configure monetization rules", QJsonObject{{"type", "object"}, {"properties", QJsonObject{
			{"rules", QJsonObject{{"type", "object"}, {"description", "Monetization rules configuration"}}}
		}}, {"required", QJsonArray{"rules"}}}},
		Tool{"get_monetization_analytics", "Get monetization analytics", QJsonObject{{"type", "object"}, {"properties", QJsonObject{
					{"chat_id", QJsonObject{{"type", "integer"}, {"description", "Chat this applies to."}}},
				}}}},

		// --- Budget ---
		Tool{"set_spending_budget", "Set spending budget limits", QJsonObject{{"type", "object"}, {"properties", QJsonObject{
			{"daily_limit", QJsonObject{{"type", "number"}, {"description", "Daily spending limit"}}},
			{"monthly_limit", QJsonObject{{"type", "number"}, {"description", "Monthly spending limit"}}},
					{"weekly_limit", QJsonObject{{"type", "number"}, {"description", "Weekly spending cap; 0 means no limit (default 0)."}}},
				}}}},
		Tool{"set_budget_alert", "Set budget alert threshold", QJsonObject{{"type", "object"}, {"properties", QJsonObject{
			{"threshold", QJsonObject{{"type", "number"}, {"description", "Alert threshold amount"}}},
					{"type", QJsonObject{{"type", "string"}, {"description", "How threshold is read: percentage or absolute (default percentage)."}}},
				}}, {"required", QJsonArray{"threshold"}}}},

		// --- Stars ---
		Tool{"request_stars", "Request stars from a user", QJsonObject{{"type", "object"}, {"properties", QJsonObject{
			{"from_user_id", QJsonObject{{"type", "integer"}, {"description", "User to request from"}}},
			{"amount", QJsonObject{{"type", "integer"}, {"description", "Stars amount"}}},
					{"reason", QJsonObject{{"type", "string"}, {"description", "Reason recorded with the request."}}},
				}}, {"required", QJsonArray{"from_user_id", "amount"}}}},
		Tool{"get_stars_leaderboard", "Get stars leaderboard", QJsonObject{{"type", "object"}, {"properties", QJsonObject{
		}}}},
		Tool{"get_stars_history", "Get stars transaction history", QJsonObject{{"type", "object"}, {"properties", QJsonObject{
			{"limit", QJsonObject{{"type", "integer"}, {"description", "Max entries"}, {"default", 50}}},
					{"direction", QJsonObject{{"type", "string"}, {"description", "Which transactions to include: all, inbound, or outbound (default all)."}}},
				}}}},
		Tool{"get_stars_rate", "Get current stars exchange rate", QJsonObject{{"type", "object"}, {"properties", QJsonObject{}}}},
		Tool{"convert_stars", "Convert stars to/from other currencies", QJsonObject{{"type", "object"}, {"properties", QJsonObject{
			{"stars_amount", QJsonObject{{"type", "integer"}, {"description", "Amount to convert"}}},
					{"target", QJsonObject{{"type", "string"}, {"description", "Currency to convert into (default usd)."}}},
				}}, {"required", QJsonArray{"stars_amount"}}}},
		Tool{"categorize_transaction", "Categorize a transaction", QJsonObject{{"type", "object"}, {"properties", QJsonObject{
			{"transaction_id", QJsonObject{{"type", "integer"}, {"description", "Transaction ID"}}},
			{"category", QJsonObject{{"type", "string"}, {"description", "Category name"}}}
		}}, {"required", QJsonArray{"transaction_id", "category"}}}},
		Tool{"send_star_reaction", "Send a star reaction to a message", QJsonObject{{"type", "object"}, {"properties", QJsonObject{
			{"chat_id", QJsonObject{{"type", "integer"}, {"description", "Chat ID"}}},
			{"message_id", QJsonObject{{"type", "integer"}, {"description", "Message ID"}}},
			{"stars_count", QJsonObject{{"type", "integer"}, {"description", "Stars count"}}}
		}}, {"required", QJsonArray{"chat_id", "message_id", "stars_count"}}}},
		Tool{"get_star_reactions", "Get star reactions for messages", QJsonObject{{"type", "object"}, {"properties", QJsonObject{
			{"chat_id", QJsonObject{{"type", "integer"}, {"description", "Chat ID"}}},
					{"message_id", QJsonObject{{"type", "integer"}, {"description", "Message this applies to."}}},
				}}}},
		Tool{"get_reaction_analytics", "Get reaction analytics", QJsonObject{{"type", "object"}, {"properties", QJsonObject{
					{"period", QJsonObject{{"type", "string"}, {"description", "Time window to report over, such as day, week or month (default week)."}}},
				}}}},
		Tool{"get_top_reacted", "Get most reacted messages", QJsonObject{{"type", "object"}, {"properties", QJsonObject{
			{"limit", QJsonObject{{"type", "integer"}, {"description", "Max entries"}, {"default", 10}}}
		}}}},
		Tool{"set_reaction_price", "Set custom star reaction price", QJsonObject{{"type", "object"}, {"properties", QJsonObject{
			{"min_stars", QJsonObject{{"type", "integer"}, {"description", "Price in stars"}}},
					{"chat_id", QJsonObject{{"type", "integer"}, {"description", "Chat this applies to."}}},
				}}, {"required", QJsonArray{"min_stars"}}}},

		// --- Portfolio ---
		Tool{"get_portfolio", "Get gift portfolio holdings", QJsonObject{{"type", "object"}, {"properties", QJsonObject{}}}},
		Tool{"get_portfolio_value", "Get total portfolio value", QJsonObject{{"type", "object"}, {"properties", QJsonObject{}}}},
		Tool{"get_portfolio_history", "Get portfolio value history", QJsonObject{{"type", "object"}, {"properties", QJsonObject{
			{"days", QJsonObject{{"type", "integer"}, {"description", "Number of days"}, {"default", 30}}}
		}}}},
		Tool{"set_price_alert", "Set price alert for a gift type", QJsonObject{{"type", "object"}, {"properties", QJsonObject{
			{"gift_type", QJsonObject{{"type", "string"}, {"description", "Gift type"}}},
			{"target_price", QJsonObject{{"type", "number"}, {"description", "Target price"}}},
			{"direction", QJsonObject{{"type", "string"}, {"description", "above or below"}, {"default", "above"}}}
		}}, {"required", QJsonArray{"gift_type", "target_price"}}}},
		Tool{"get_price_predictions", "Get price predictions for a gift type", QJsonObject{{"type", "object"}, {"properties", QJsonObject{
			{"gift_type", QJsonObject{{"type", "string"}, {"description", "Gift type to predict"}}}
		}}, {"required", QJsonArray{"gift_type"}}}},

		// --- Achievements ---
		Tool{"list_achievements", "List all available achievements", QJsonObject{{"type", "object"}, {"properties", QJsonObject{}}}},
		Tool{"get_achievement_progress", "Get progress on an achievement", QJsonObject{{"type", "object"}, {"properties", QJsonObject{
			{"achievement_id", QJsonObject{{"type", "string"}, {"description", "Achievement ID"}}}
		}}, {"required", QJsonArray{"achievement_id"}}}},
		Tool{"claim_achievement_reward", "Claim reward for completed achievement", QJsonObject{{"type", "object"}, {"properties", QJsonObject{
			{"achievement_id", QJsonObject{{"type", "string"}, {"description", "Achievement ID"}}}
		}}, {"required", QJsonArray{"achievement_id"}}}},
		Tool{"share_achievement", "Share an achievement to a chat", QJsonObject{{"type", "object"}, {"properties", QJsonObject{
			{"achievement_id", QJsonObject{{"type", "string"}, {"description", "Achievement ID"}}},
			{"chat_id", QJsonObject{{"type", "integer"}, {"description", "Chat to share to"}}}
		}}, {"required", QJsonArray{"achievement_id"}}}},
		Tool{"get_achievement_suggestions", "Get suggested achievements close to completion", QJsonObject{{"type", "object"}, {"properties", QJsonObject{}}}},
		Tool{"get_leaderboard", "Get leaderboard by stars, gifts, or portfolio", QJsonObject{{"type", "object"}, {"properties", QJsonObject{
			{"type", QJsonObject{{"type", "string"}, {"description", "Leaderboard type (stars/gifts/portfolio)"}, {"default", "stars"}}},
			{"limit", QJsonObject{{"type", "integer"}, {"description", "Max entries"}, {"default", 10}}}
		}}}},

		// --- Paid Content ---
		Tool{"create_paid_post", "Create paid content post", QJsonObject{{"type", "object"}, {"properties", QJsonObject{
			{"content", QJsonObject{{"type", "string"}, {"description", "Content text"}}},
			{"price", QJsonObject{{"type", "integer"}, {"description", "Price in stars"}}},
					{"chat_id", QJsonObject{{"type", "integer"}, {"description", "Chat this applies to."}}},
					{"preview", QJsonObject{{"type", "string"}, {"description", "Public preview text shown before purchase."}}},
				}}, {"required", QJsonArray{"content", "price"}}}},
		Tool{"set_content_price", "Set price for content", QJsonObject{{"type", "object"}, {"properties", QJsonObject{
			{"content_id", QJsonObject{{"type", "integer"}, {"description", "Content ID"}}},
			{"price", QJsonObject{{"type", "integer"}, {"description", "New price in stars"}}}
		}}, {"required", QJsonArray{"content_id", "price"}}}},
		Tool{"get_paid_content_stats", "Get paid content statistics", QJsonObject{{"type", "object"}, {"properties", QJsonObject{}}}},
		Tool{"unlock_content", "Unlock paid content", QJsonObject{{"type", "object"}, {"properties", QJsonObject{
			{"content_id", QJsonObject{{"type", "integer"}, {"description", "Content ID to unlock"}}}
		}}, {"required", QJsonArray{"content_id"}}}},
		Tool{"list_purchased_content", "List purchased content", QJsonObject{{"type", "object"}, {"properties", QJsonObject{}}}},
		Tool{"refund_content", "Request refund for content", QJsonObject{{"type", "object"}, {"properties", QJsonObject{
			{"content_id", QJsonObject{{"type", "integer"}, {"description", "Content ID"}}},
					{"reason", QJsonObject{{"type", "string"}, {"description", "Reason recorded with the request."}}},
				}}, {"required", QJsonArray{"content_id"}}}},

		// --- Creator ---
		Tool{"create_exclusive_content", "Create exclusive content for subscribers", QJsonObject{{"type", "object"}, {"properties", QJsonObject{
			{"content", QJsonObject{{"type", "string"}, {"description", "Content text"}}},
			{"tier", QJsonObject{{"type", "string"}, {"description", "Subscriber tier"}, {"default", "all"}}},
					{"price", QJsonObject{{"type", "integer"}, {"description", "Price in Telegram Stars (default 0)."}}},
				}}, {"required", QJsonArray{"content"}}}},
		Tool{"get_subscriber_analytics", "Get subscriber analytics", QJsonObject{{"type", "object"}, {"properties", QJsonObject{}}}},
		Tool{"send_subscriber_message", "Send message to subscribers", QJsonObject{{"type", "object"}, {"properties", QJsonObject{
			{"message", QJsonObject{{"type", "string"}, {"description", "Message text"}}},
			{"tier", QJsonObject{{"type", "string"}, {"description", "Target tier"}, {"default", "all"}}},
					{"channel_id", QJsonObject{{"type", "integer"}, {"description", "Channel this applies to."}}},
				}}, {"required", QJsonArray{"message"}}}},
		Tool{"set_subscriber_tiers", "Configure subscriber tiers", QJsonObject{{"type", "object"}, {"properties", QJsonObject{
			{"tiers", QJsonObject{{"type", "array"}, {"description", "Tier configurations"}}}
		}}, {"required", QJsonArray{"tiers"}}}},
		Tool{"get_creator_dashboard", "Get creator dashboard overview", QJsonObject{{"type", "object"}, {"properties", QJsonObject{}}}},

		// --- Mini Apps ---
		Tool{"list_miniapp_permissions", "List mini app permissions", QJsonObject{{"type", "object"}, {"properties", QJsonObject{}}}},
		Tool{"approve_miniapp_spend", "Approve mini app spending", QJsonObject{{"type", "object"}, {"properties", QJsonObject{
			{"miniapp_id", QJsonObject{{"type", "string"}, {"description", "Mini app ID"}}},
			{"amount", QJsonObject{{"type", "number"}, {"description", "Amount to approve"}}}
		}}, {"required", QJsonArray{"miniapp_id", "amount"}}}},
		Tool{"revoke_miniapp_permission", "Revoke mini app permission", QJsonObject{{"type", "object"}, {"properties", QJsonObject{
			{"miniapp_id", QJsonObject{{"type", "string"}, {"description", "Mini app ID"}}}
		}}, {"required", QJsonArray{"miniapp_id"}}}},

		// --- Semantic Search ---
		Tool{"semantic_index_messages", "Index messages for semantic search (alias for index_messages)", QJsonObject{{"type", "object"}, {"properties", QJsonObject{
			{"chat_id", QJsonObject{{"type", "integer"}, {"description", "Chat ID to index"}}},
			{"limit", QJsonObject{{"type", "integer"}, {"description", "Max messages to index"}, {"default", 1000}}},
					{"rebuild", QJsonObject{{"type", "boolean"}, {"description", "Rebuild the index from scratch instead of updating it (default false)."}}},
				}}, {"required", QJsonArray{"chat_id"}}}},

		// --- Tasks ---
		Tool{"update_task", "Update a task's status or details", QJsonObject{{"type", "object"}, {"properties", QJsonObject{
			{"task_id", QJsonObject{{"type", "integer"}, {"description", "Task ID"}}},
			{"status", QJsonObject{{"type", "string"}, {"description", "New status (pending/in_progress/completed)"}}},
					{"priority", QJsonObject{{"type", "integer"}, {"description", "Priority; higher is more urgent (default -1)."}}},
					{"title", QJsonObject{{"type", "string"}, {"description", "Title text."}}},
				}}, {"required", QJsonArray{"task_id"}}}},

		// ===== V6.5.1 FEATURE TOOLS (10) =====

		Tool{"request_message_summary", "Request an AI-generated summary of a message using Telegram's server-side AI", QJsonObject{{"type", "object"}, {"properties", QJsonObject{
			{"chat_id", QJsonObject{{"type", "integer"}, {"description", "Chat ID containing the message"}}},
			{"message_id", QJsonObject{{"type", "integer"}, {"description", "Message ID to summarize"}}},
			{"language", QJsonObject{{"type", "string"}, {"description", "Language code for the summary (e.g. 'en', 'ru', 'es')"}, {"default", "en"}}}
		}}, {"required", QJsonArray{"chat_id", "message_id"}}}},

		Tool{"list_folders", "List all chat folders/filters with their settings and included chat counts", QJsonObject{{"type", "object"}, {"properties", QJsonObject{}}}},

		Tool{"create_folder", "Create a new chat folder with inclusion/exclusion rules", QJsonObject{{"type", "object"}, {"properties", QJsonObject{
			{"title", QJsonObject{{"type", "string"}, {"description", "Folder title"}}},
			{"icon_emoji", QJsonObject{{"type", "string"}, {"description", "Folder icon emoji"}}},
			{"include_contacts", QJsonObject{{"type", "boolean"}, {"description", "Include contact chats"}, {"default", false}}},
			{"include_non_contacts", QJsonObject{{"type", "boolean"}, {"description", "Include non-contact chats"}, {"default", false}}},
			{"include_groups", QJsonObject{{"type", "boolean"}, {"description", "Include group chats"}, {"default", false}}},
			{"include_channels", QJsonObject{{"type", "boolean"}, {"description", "Include channels"}, {"default", false}}},
			{"include_bots", QJsonObject{{"type", "boolean"}, {"description", "Include bot chats"}, {"default", false}}},
			{"exclude_muted", QJsonObject{{"type", "boolean"}, {"description", "Exclude muted chats"}, {"default", false}}},
			{"exclude_read", QJsonObject{{"type", "boolean"}, {"description", "Exclude read chats"}, {"default", false}}},
			{"exclude_archived", QJsonObject{{"type", "boolean"}, {"description", "Exclude archived chats"}, {"default", false}}},
			{"include_chat_ids", QJsonObject{{"type", "array"}, {"description", "Specific chat IDs to always include"}, {"items", QJsonObject{{"type", "integer"}}}}},
			{"exclude_chat_ids", QJsonObject{{"type", "array"}, {"description", "Specific chat IDs to always exclude"}, {"items", QJsonObject{{"type", "integer"}}}}}
		}}, {"required", QJsonArray{"title"}}}},

		Tool{"update_folder", "Update an existing chat folder's settings", QJsonObject{{"type", "object"}, {"properties", QJsonObject{
			{"folder_id", QJsonObject{{"type", "integer"}, {"description", "Folder ID to update"}}},
			{"title", QJsonObject{{"type", "string"}, {"description", "New folder title"}}},
			{"icon_emoji", QJsonObject{{"type", "string"}, {"description", "New folder icon emoji"}}},
			{"include_contacts", QJsonObject{{"type", "boolean"}, {"description", "Include contact chats"}}},
			{"include_non_contacts", QJsonObject{{"type", "boolean"}, {"description", "Include non-contact chats"}}},
			{"include_groups", QJsonObject{{"type", "boolean"}, {"description", "Include group chats"}}},
			{"include_channels", QJsonObject{{"type", "boolean"}, {"description", "Include channels"}}},
			{"include_bots", QJsonObject{{"type", "boolean"}, {"description", "Include bot chats"}}},
			{"exclude_muted", QJsonObject{{"type", "boolean"}, {"description", "Exclude muted chats"}}},
			{"exclude_read", QJsonObject{{"type", "boolean"}, {"description", "Exclude read chats"}}},
			{"exclude_archived", QJsonObject{{"type", "boolean"}, {"description", "Exclude archived chats"}}},
		}}, {"required", QJsonArray{"folder_id"}}}},

		Tool{"delete_folder", "Delete a chat folder (does not delete the chats inside it)", QJsonObject{{"type", "object"}, {"properties", QJsonObject{
			{"folder_id", QJsonObject{{"type", "integer"}, {"description", "Folder ID to delete"}}}
		}}, {"required", QJsonArray{"folder_id"}}}},

		Tool{"reorder_folders", "Reorder chat folders by specifying the new order of folder IDs", QJsonObject{{"type", "object"}, {"properties", QJsonObject{
			{"folder_ids", QJsonObject{{"type", "array"}, {"description", "Array of folder IDs in desired display order"}, {"items", QJsonObject{{"type", "integer"}}}}}
		}}, {"required", QJsonArray{"folder_ids"}}}},

		Tool{"transfer_group_ownership", "Transfer ownership of a group or channel to another user (requires 2FA)", QJsonObject{{"type", "object"}, {"properties", QJsonObject{
			{"chat_id", QJsonObject{{"type", "integer"}, {"description", "Group or channel ID"}}},
			{"new_owner_id", QJsonObject{{"type", "integer"}, {"description", "User ID of the new owner"}}}
		}}, {"required", QJsonArray{"chat_id", "new_owner_id"}}}},

		Tool{"craft_star_gift", "Craft a unique gift by combining multiple saved star gifts", QJsonObject{{"type", "object"}, {"properties", QJsonObject{
			{"gift_ids", QJsonObject{{"type", "array"}, {"description", "Array of saved gift identifiers to combine"}, {"items", QJsonObject{{"type", "string"}}}}}
		}}, {"required", QJsonArray{"gift_ids"}}}},

		Tool{"get_craft_options", "Get available crafting options and recipes for star gifts", QJsonObject{{"type", "object"}, {"properties", QJsonObject{}}}},

		Tool{"export_topic", "Export messages from a forum topic to JSON or text format", QJsonObject{{"type", "object"}, {"properties", QJsonObject{
			{"chat_id", QJsonObject{{"type", "integer"}, {"description", "Forum supergroup chat ID"}}},
			{"topic_id", QJsonObject{{"type", "integer"}, {"description", "Forum topic ID (message ID of the topic creation message)"}}},
			{"limit", QJsonObject{{"type", "integer"}, {"description", "Maximum number of messages to export"}, {"default", 100}}},
			{"format", QJsonObject{{"type", "string"}, {"description", "Export format: 'json' or 'text'"}, {"default", "json"}, {"enum", QJsonArray{"json", "text"}}}}
		}}, {"required", QJsonArray{"chat_id", "topic_id"}}}},
	};
	for (const auto &t : moreTools) {
		_tools.append(t);
	}

	// These were callable but never advertised, so no client could discover
	// them and tools/list understated the surface by eight. Schemas below are
	// taken from what each implementation actually reads, not from what its
	// name suggests.
	const QVector<Tool> unlistedTools = {
		Tool{"create_channel", "Create a channel or supergroup and return its chat_id", QJsonObject{{"type", "object"}, {"properties", QJsonObject{
			{"title", QJsonObject{{"type", "string"}, {"description", "Channel title"}}},
			{"about", QJsonObject{{"type", "string"}, {"description", "Channel description"}}},
			{"megagroup", QJsonObject{{"type", "boolean"}, {"description", "Create a supergroup instead of a broadcast channel"}, {"default", false}}}
		}}, {"required", QJsonArray{"title"}}}},

		Tool{"delete_channel", "Delete a channel or supergroup (creator only)", QJsonObject{{"type", "object"}, {"properties", QJsonObject{
			{"chat_id", QJsonObject{{"type", "integer"}, {"description", "Channel to delete"}}}
		}}, {"required", QJsonArray{"chat_id"}}}},

		Tool{"set_channel_username", "Set or clear a channel's public username (pass an empty string to clear)", QJsonObject{{"type", "object"}, {"properties", QJsonObject{
			{"chat_id", QJsonObject{{"type", "integer"}, {"description", "Channel to update"}}},
			{"username", QJsonObject{{"type", "string"}, {"description", "Public username, without the @"}}}
		}}, {"required", QJsonArray{"chat_id", "username"}}}},

		Tool{"check_channel_username", "Check whether a public username is available for a channel", QJsonObject{{"type", "object"}, {"properties", QJsonObject{
			{"chat_id", QJsonObject{{"type", "integer"}, {"description", "Channel that would take the username"}}},
			{"username", QJsonObject{{"type", "string"}, {"description", "Username to test, without the @"}}}
		}}, {"required", QJsonArray{"chat_id", "username"}}}},

		// COMMUNITY TOOLS -- a community is a channel that groups other chats
		// under one dialogs row. `chat_id` always names the community itself;
		// `member_chat_id` names a chat inside it.
		Tool{"list_communities", "List the communities this account has joined", QJsonObject{{"type", "object"}, {"properties", QJsonObject{}}}},

		Tool{"get_community", "Get a community's details and the chats linked into it", QJsonObject{{"type", "object"}, {"properties", QJsonObject{
			{"chat_id", QJsonObject{{"type", "integer"}, {"description", "The community channel"}}}
		}}, {"required", QJsonArray{"chat_id"}}}},

		Tool{"create_community", "Create a community around a first chat and return its chat_id", QJsonObject{{"type", "object"}, {"properties", QJsonObject{
			{"title", QJsonObject{{"type", "string"}, {"description", "Community title"}}},
			{"first_chat_id", QJsonObject{{"type", "integer"}, {"description", "Chat to link in at creation; Telegram requires one"}}},
			{"about", QJsonObject{{"type", "string"}, {"description", "Community description"}}},
			{"hidden", QJsonObject{{"type", "boolean"}, {"description", "Create it hidden rather than publicly listed"}, {"default", false}}}
		}}, {"required", QJsonArray{"title", "first_chat_id"}}}},

		Tool{"add_chat_to_community", "Link a chat into a community. Returns pending_approval when the community's admins have to approve the link", QJsonObject{{"type", "object"}, {"properties", QJsonObject{
			{"chat_id", QJsonObject{{"type", "integer"}, {"description", "The community channel"}}},
			{"member_chat_id", QJsonObject{{"type", "integer"}, {"description", "Chat to link in"}}},
			{"visible", QJsonObject{{"type", "boolean"}, {"description", "Show the chat to everyone rather than only to members"}, {"default", true}}}
		}}, {"required", QJsonArray{"chat_id", "member_chat_id"}}}},

		Tool{"remove_chat_from_community", "Unlink a chat from a community", QJsonObject{{"type", "object"}, {"properties", QJsonObject{
			{"chat_id", QJsonObject{{"type", "integer"}, {"description", "The community channel"}}},
			{"member_chat_id", QJsonObject{{"type", "integer"}, {"description", "Chat to unlink"}}}
		}}, {"required", QJsonArray{"chat_id", "member_chat_id"}}}},

		Tool{"set_community_collapsed", "Collapse or expand a community's row in the chats list", QJsonObject{{"type", "object"}, {"properties", QJsonObject{
			{"chat_id", QJsonObject{{"type", "integer"}, {"description", "The community channel"}}},
			{"collapsed", QJsonObject{{"type", "boolean"}, {"description", "True to collapse the group of chats into one row"}}}
		}}, {"required", QJsonArray{"chat_id", "collapsed"}}}},

		Tool{"list_community_join_requests", "List chats waiting for approval to join a community", QJsonObject{{"type", "object"}, {"properties", QJsonObject{
			{"chat_id", QJsonObject{{"type", "integer"}, {"description", "The community channel"}}},
			{"limit", QJsonObject{{"type", "integer"}, {"description", "How many requests to return"}, {"default", 50}}},
			{"offset", QJsonObject{{"type", "string"}, {"description", "next_offset from a previous call, to page through"}}}
		}}, {"required", QJsonArray{"chat_id"}}}},

		Tool{"review_community_join_request", "Approve or reject a pending community join request, or every pending one at once", QJsonObject{{"type", "object"}, {"properties", QJsonObject{
			{"chat_id", QJsonObject{{"type", "integer"}, {"description", "The community channel"}}},
			{"approve", QJsonObject{{"type", "boolean"}, {"description", "True to approve, false to reject"}}},
			{"member_chat_id", QJsonObject{{"type", "integer"}, {"description", "Chat whose request to decide; ignored when all is true"}}},
			{"all", QJsonObject{{"type", "boolean"}, {"description", "Decide every pending request at once"}, {"default", false}}}
		}}, {"required", QJsonArray{"chat_id", "approve"}}}},

		// DOWNLOAD TOOLS
		Tool{"list_downloads", "List what this account is downloading and what it has downloaded", QJsonObject{{"type", "object"}, {"properties", QJsonObject{}}}},

		Tool{"clear_finished_downloads", "Remove finished downloads from the downloads list, leaving the files on disk", QJsonObject{{"type", "object"}, {"properties", QJsonObject{}}}},

		Tool{"delete_downloaded_files", "Delete downloaded files from disk and clear the downloads list", QJsonObject{{"type", "object"}, {"properties", QJsonObject{}}}},

		Tool{"get_auto_download_settings", "Read the auto-download limits for private chats, groups and channels", QJsonObject{{"type", "object"}, {"properties", QJsonObject{}}}},

		Tool{"set_auto_download_settings", "Turn auto-download on or off for a source, either wholesale or for one media type", QJsonObject{{"type", "object"}, {"properties", QJsonObject{
			{"source", QJsonObject{{"type", "string"}, {"description", "private_chats, groups or channels"}}},
			{"enabled", QJsonObject{{"type", "boolean"}, {"description", "True to enable, false to disable"}}},
			{"type", QJsonObject{{"type", "string"}, {"description", "photo, video, voice_message, video_message, music, gif or file. Omit to set the whole source at once"}}},
			{"bytes_limit", QJsonObject{{"type", "integer"}, {"description", "Largest file to fetch automatically, in bytes. Only with type; defaults to no limit"}}}
		}}, {"required", QJsonArray{"source", "enabled"}}}},

		// RICH MESSAGE TOOLS
		Tool{"list_rich_messages", "Find messages in a chat that carry a rich page rather than plain text", QJsonObject{{"type", "object"}, {"properties", QJsonObject{
			{"chat_id", QJsonObject{{"type", "integer"}, {"description", "Chat to scan"}}},
			{"limit", QJsonObject{{"type", "integer"}, {"description", "How many to return"}, {"default", 50}}}
		}}, {"required", QJsonArray{"chat_id"}}}},

		Tool{"save_rich_message_html", "Write a rich message out as a self-contained HTML folder. Starts the work and returns; media is fetched in the background", QJsonObject{{"type", "object"}, {"properties", QJsonObject{
			{"chat_id", QJsonObject{{"type", "integer"}, {"description", "Chat holding the message"}}},
			{"message_id", QJsonObject{{"type", "integer"}, {"description", "The rich message"}}},
			{"path", QJsonObject{{"type", "string"}, {"description", "Directory to write into; defaults to the export directory"}}}
		}}, {"required", QJsonArray{"chat_id", "message_id"}}}},

		// FORUM TOPIC TOOLS
		Tool{"list_topics", "List a forum's topics with their unread counts", QJsonObject{{"type", "object"}, {"properties", QJsonObject{
			{"chat_id", QJsonObject{{"type", "integer"}, {"description", "The forum supergroup"}}},
			{"unread_only", QJsonObject{{"type", "boolean"}, {"description", "Return only topics with something unread"}, {"default", false}}},
			{"limit", QJsonObject{{"type", "integer"}, {"description", "How many topics to return"}, {"default", 100}}}
		}}, {"required", QJsonArray{"chat_id"}}}},

		Tool{"create_giveaway", "Create a Telegram Stars giveaway in a channel", QJsonObject{{"type", "object"}, {"properties", QJsonObject{
			{"channel_id", QJsonObject{{"type", "integer"}, {"description", "Channel to host the giveaway"}}},
			{"stars_amount", QJsonObject{{"type", "integer"}, {"description", "Total stars to give away"}}},
			{"winners_count", QJsonObject{{"type", "integer"}, {"description", "Number of winners"}}},
			{"prize", QJsonObject{{"type", "string"}, {"description", "Prize description"}}},
			{"end_date", QJsonObject{{"type", "integer"}, {"description", "Unix timestamp when the giveaway ends"}}}
		}}, {"required", QJsonArray{"channel_id"}}}},
	};
	for (const auto &t : unlistedTools) {
		_tools.append(t);
	}
}

} // namespace MCP

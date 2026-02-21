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
						{"default", 50}
					}}
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
			"Export chat history to JSON/JSONL/CSV format",
			QJsonObject{
				{"type", "object"},
				{"properties", QJsonObject{
					{"chat_id", QJsonObject{
						{"type", "integer"},
						{"description", "Chat ID"}
					}},
					{"format", QJsonObject{
						{"type", "string"},
						{"enum", QJsonArray{"json", "jsonl", "csv"}},
						{"description", "Export format"}
					}},
					{"output_path", QJsonObject{
						{"type", "string"},
						{"description", "Output directory path (optional - uses UI export settings if not specified)"}
					}}
				}},
				{"required", QJsonArray{"chat_id", "format"}},
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
						{"default", 50}
					}}
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
						{"default", "all"}
					}}
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
						{"default", "daily"}
					}}
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
						{"default", 10}
					}}
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
						{"default", 20}
					}}
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
					}}
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
					}}
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
						{"default", 10}
					}},
					{"min_similarity", QJsonObject{
						{"type", "number"},
						{"default", 0.7}
					}}
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
					}}
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
						{"default", 5}
					}}
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
					}}
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
						{"default", false}
					}}
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
					}}
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
					}}
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
					}}
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
						{"default", 50}
					}},
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
					{"job_id", QJsonObject{{"type", "string"}, {"description", "Transcription job ID"}}}
				}},
				{"required", QJsonArray{"job_id"}}
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
					{"message_ids", QJsonObject{{"type", "array"}, {"items", QJsonObject{{"type", "integer"}}}, {"description", "Message IDs to translate"}}},
					{"target_language", QJsonObject{{"type", "string"}, {"description", "Target language code"}}}
				}},
				{"required", QJsonArray{"chat_id", "message_ids", "target_language"}}
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
					{"tags", QJsonObject{{"type", "array"}, {"items", QJsonObject{{"type", "string"}}}, {"description", "Tags to add"}}}
				}},
				{"required", QJsonArray{"chat_id", "message_id", "tags"}}
			}
		},
		Tool{
			"get_tagged_messages",
			"Get messages with specific tags",
			QJsonObject{
				{"type", "object"},
				{"properties", QJsonObject{
					{"tags", QJsonObject{{"type", "array"}, {"items", QJsonObject{{"type", "string"}}}, {"description", "Tags to filter by"}}},
					{"limit", QJsonObject{{"type", "integer"}, {"default", 50}}}
				}},
				{"required", QJsonArray{"tags"}}
			}
		},
		Tool{
			"list_tags",
			"List all tags with usage counts",
			QJsonObject{{"type", "object"}, {"properties", QJsonObject{}}}
		},
		Tool{
			"delete_tag",
			"Delete a tag from all messages",
			QJsonObject{
				{"type", "object"},
				{"properties", QJsonObject{
					{"tag", QJsonObject{{"type", "string"}, {"description", "Tag to delete"}}}
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
					{"hide_sponsored", QJsonObject{{"type", "boolean"}, {"default", true}}},
					{"hide_promoted", QJsonObject{{"type", "boolean"}, {"default", true}}}
				}}
			}
		},
		Tool{
			"get_filtered_ads",
			"Get log of filtered ads",
			QJsonObject{
				{"type", "object"},
				{"properties", QJsonObject{
					{"limit", QJsonObject{{"type", "integer"}, {"default", 100}}}
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
					{"name", QJsonObject{{"type", "string"}, {"description", "Rule name"}}},
					{"conditions", QJsonObject{{"type", "object"}, {"description", "Conditions JSON"}}},
					{"actions", QJsonObject{{"type", "object"}, {"description", "Actions JSON"}}}
				}},
				{"required", QJsonArray{"name", "conditions", "actions"}}
			}
		},
		Tool{
			"list_chat_rules",
			"List all chat management rules",
			QJsonObject{{"type", "object"}, {"properties", QJsonObject{}}}
		},
		Tool{
			"execute_chat_rules",
			"Manually execute chat rules",
			QJsonObject{{"type", "object"}, {"properties", QJsonObject{}}}
		},
		Tool{
			"delete_chat_rule",
			"Delete a chat rule",
			QJsonObject{
				{"type", "object"},
				{"properties", QJsonObject{
					{"rule_id", QJsonObject{{"type", "integer"}, {"description", "Rule ID"}}}
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
					{"due_date", QJsonObject{{"type", "integer"}, {"description", "Due date (Unix timestamp)"}}}
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
					{"chat_id", QJsonObject{{"type", "integer"}, {"description", "Filter by chat"}}}
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
					{"category", QJsonObject{{"type", "string"}, {"description", "Filter by category"}}}
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
					{"shortcut", QJsonObject{{"type", "string"}}},
					{"text", QJsonObject{{"type", "string"}}},
					{"category", QJsonObject{{"type", "string"}}}
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
					{"enabled", QJsonObject{{"type", "boolean"}}},
					{"message", QJsonObject{{"type", "string"}, {"description", "Greeting message text"}}},
					{"delay_seconds", QJsonObject{{"type", "integer"}, {"default", 0}}},
					{"only_first_message", QJsonObject{{"type", "boolean"}, {"default", true}}}
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
			QJsonObject{{"type", "object"}, {"properties", QJsonObject{}}}
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
					{"enabled", QJsonObject{{"type", "boolean"}}},
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
					{"duration_hours", QJsonObject{{"type", "integer"}, {"description", "Duration in hours"}}}
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
					{"timezone", QJsonObject{{"type", "string"}, {"default", "UTC"}}}
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
					{"latitude", QJsonObject{{"type", "number"}}},
					{"longitude", QJsonObject{{"type", "number"}}}
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
					{"enabled", QJsonObject{{"type", "boolean"}}},
					{"system_prompt", QJsonObject{{"type", "string"}, {"description", "System prompt for AI"}}},
					{"model", QJsonObject{{"type", "string"}, {"default", "claude"}}},
					{"max_tokens", QJsonObject{{"type", "integer"}, {"default", 1000}}}
				}},
				{"required", QJsonArray{"enabled"}}
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
					{"system_prompt", QJsonObject{{"type", "string"}}}
				}},
				{"required", QJsonArray{"system_prompt"}}
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
					{"examples", QJsonObject{{"type", "array"}, {"description", "Array of {input, output} examples"}}}
				}},
				{"required", QJsonArray{"examples"}}
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
					{"provider", QJsonObject{{"type", "string"}, {"description", "TTS provider (elevenlabs, coqui)"}}},
					{"voice_id", QJsonObject{{"type", "string"}, {"description", "Voice ID"}}},
					{"settings", QJsonObject{{"type", "object"}, {"description", "Voice settings"}}}
				}},
				{"required", QJsonArray{"name", "provider", "voice_id"}}
			}
		},
		Tool{
			"generate_voice_message",
			"Generate a voice message from text",
			QJsonObject{
				{"type", "object"},
				{"properties", QJsonObject{
					{"text", QJsonObject{{"type", "string"}, {"description", "Text to speak"}}},
					{"preset", QJsonObject{{"type", "string"}, {"description", "Voice preset name"}}}
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
					{"text", QJsonObject{{"type", "string"}, {"description", "Text to speak"}}}
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
					{"audio_path", QJsonObject{{"type", "string"}, {"description", "Path to audio sample"}}}
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
					{"provider", QJsonObject{{"type", "string"}, {"description", "TTV provider (heygen, d-id)"}}},
					{"avatar_path", QJsonObject{{"type", "string"}, {"description", "Avatar image/video path"}}},
					{"settings", QJsonObject{{"type", "object"}, {"description", "Avatar settings"}}}
				}},
				{"required", QJsonArray{"name", "provider", "avatar_path"}}
			}
		},
		Tool{
			"generate_video_circle",
			"Generate a video circle from text",
			QJsonObject{
				{"type", "object"},
				{"properties", QJsonObject{
					{"text", QJsonObject{{"type", "string"}, {"description", "Text to speak"}}},
					{"preset", QJsonObject{{"type", "string"}, {"description", "Avatar preset name"}}}
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
					{"text", QJsonObject{{"type", "string"}, {"description", "Text to speak"}}}
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
					{"days", QJsonObject{{"type", "integer"}, {"default", 30}}}
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
					{"limit", QJsonObject{{"type", "integer"}, {"default", 50}}},
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
					{"start_date", QJsonObject{{"type", "integer"}}},
					{"end_date", QJsonObject{{"type", "integer"}}}
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
					{"limit", QJsonObject{{"type", "integer"}, {"default", 50}}}
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
					{"direction", QJsonObject{{"type", "string"}, {"description", "received or sent"}}},
					{"limit", QJsonObject{{"type", "integer"}, {"default", 50}}}
				}}
			}
		},
		Tool{
			"get_gift_details",
			"Get details of a specific gift",
			QJsonObject{
				{"type", "object"},
				{"properties", QJsonObject{
					{"gift_id", QJsonObject{{"type", "integer"}, {"description", "Gift ID"}}}
				}},
				{"required", QJsonArray{"gift_id"}}
			}
		},
		Tool{
			"get_gift_analytics",
			"Get gift giving/receiving analytics",
			QJsonObject{{"type", "object"}, {"properties", QJsonObject{}}}
		},
		Tool{
			"send_stars",
			"Send Stars to a user",
			QJsonObject{
				{"type", "object"},
				{"properties", QJsonObject{
					{"user_id", QJsonObject{{"type", "integer"}, {"description", "Recipient user ID"}}},
					{"amount", QJsonObject{{"type", "integer"}, {"description", "Number of Stars"}}},
					{"message", QJsonObject{{"type", "string"}, {"description", "Optional message"}}}
				}},
				{"required", QJsonArray{"user_id", "amount"}}
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
					{"subscription_id", QJsonObject{{"type", "string"}, {"description", "Subscription ID"}}}
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
					{"channel_id", QJsonObject{{"type", "integer"}}},
					{"period", QJsonObject{{"type", "string"}, {"description", "week, month, year"}}}
				}}
			}
		},
		Tool{
			"get_reaction_stats",
			"Get star reaction statistics",
			QJsonObject{
				{"type", "object"},
				{"properties", QJsonObject{
					{"channel_id", QJsonObject{{"type", "integer"}}}
				}}
			}
		},
		Tool{
			"get_paid_content_earnings",
			"Get paid content earnings",
			QJsonObject{
				{"type", "object"},
				{"properties", QJsonObject{
					{"channel_id", QJsonObject{{"type", "integer"}}}
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
					{"status", QJsonObject{{"type", "string"}, {"description", "active, completed, all"}}}
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
					{"currency", QJsonObject{{"type", "string"}, {"description", "Currency (TON, etc)"}}}
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
					{"category", QJsonObject{{"type", "string"}, {"description", "Category name"}}},
					{"amount", QJsonObject{{"type", "number"}, {"description", "Budget amount"}}},
					{"period", QJsonObject{{"type", "string"}, {"description", "daily, weekly, monthly"}}}
				}},
				{"required", QJsonArray{"category", "amount"}}
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
					{"threshold_percentage", QJsonObject{{"type", "number"}, {"description", "Alert at this % of budget"}}},
					{"enabled", QJsonObject{{"type", "boolean"}}}
				}}
			}
		},
		Tool{
			"generate_financial_report",
			"Generate a financial report",
			QJsonObject{
				{"type", "object"},
				{"properties", QJsonObject{
					{"start_date", QJsonObject{{"type", "integer"}}},
					{"end_date", QJsonObject{{"type", "integer"}}},
					{"format", QJsonObject{{"type", "string"}, {"description", "pdf, csv, json"}}}
				}},
				{"required", QJsonArray{"start_date", "end_date"}}
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
					{"type", QJsonObject{{"type", "string"}, {"description", "regular, unique, limited"}}},
					{"limit", QJsonObject{{"type", "integer"}, {"default", 50}}}
				}}
			}
		},
		Tool{
			"get_star_gift_details",
			"Get details of a specific star gift",
			QJsonObject{
				{"type", "object"},
				{"properties", QJsonObject{
					{"gift_id", QJsonObject{{"type", "integer"}, {"description", "Gift ID"}}}
				}},
				{"required", QJsonArray{"gift_id"}}
			}
		},
		Tool{
			"get_unique_gift_analytics",
			"Get analytics for unique/collectible gifts",
			QJsonObject{
				{"type", "object"},
				{"properties", QJsonObject{
					{"gift_id", QJsonObject{{"type", "integer"}, {"description", "Gift ID"}}}
				}},
				{"required", QJsonArray{"gift_id"}}
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
					{"user_id", QJsonObject{{"type", "integer"}, {"description", "Recipient user ID"}}},
					{"gift_id", QJsonObject{{"type", "integer"}, {"description", "Gift ID"}}},
					{"message", QJsonObject{{"type", "string"}, {"description", "Optional message"}}}
				}},
				{"required", QJsonArray{"user_id", "gift_id"}}
			}
		},
		Tool{
			"get_gift_transfer_history",
			"Get transfer history for a gift",
			QJsonObject{
				{"type", "object"},
				{"properties", QJsonObject{
					{"gift_id", QJsonObject{{"type", "integer"}, {"description", "Gift ID"}}}
				}},
				{"required", QJsonArray{"gift_id"}}
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
					{"to_user_id", QJsonObject{{"type", "integer"}, {"description", "Recipient user ID"}}}
				}},
				{"required", QJsonArray{"gift_id", "to_user_id"}}
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
		Tool{
			"list_active_auctions",
			"List active gift auctions",
			QJsonObject{
				{"type", "object"},
				{"properties", QJsonObject{
					{"limit", QJsonObject{{"type", "integer"}, {"default", 50}}}
				}}
			}
		},
		Tool{
			"get_auction_details",
			"Get details of an auction",
			QJsonObject{
				{"type", "object"},
				{"properties", QJsonObject{
					{"auction_id", QJsonObject{{"type", "integer"}, {"description", "Auction ID"}}}
				}},
				{"required", QJsonArray{"auction_id"}}
			}
		},
		Tool{
			"get_auction_alerts",
			"Get configured auction alerts",
			QJsonObject{{"type", "object"}, {"properties", QJsonObject{}}}
		},
		Tool{
			"place_auction_bid",
			"Place a bid on an auction",
			QJsonObject{
				{"type", "object"},
				{"properties", QJsonObject{
					{"auction_id", QJsonObject{{"type", "integer"}, {"description", "Auction ID"}}},
					{"amount", QJsonObject{{"type", "number"}, {"description", "Bid amount"}}}
				}},
				{"required", QJsonArray{"auction_id", "amount"}}
			}
		},
		Tool{
			"get_auction_history",
			"Get user's auction history",
			QJsonObject{{"type", "object"}, {"properties", QJsonObject{}}}
		},

		// Marketplace - 5 tools
		Tool{
			"browse_gift_marketplace",
			"Browse the gift marketplace",
			QJsonObject{
				{"type", "object"},
				{"properties", QJsonObject{
					{"category", QJsonObject{{"type", "string"}}},
					{"sort_by", QJsonObject{{"type", "string"}, {"description", "price, rarity, date"}}},
					{"limit", QJsonObject{{"type", "integer"}, {"default", 50}}}
				}}
			}
		},
		Tool{
			"get_market_trends",
			"Get marketplace trends",
			QJsonObject{
				{"type", "object"},
				{"properties", QJsonObject{
					{"period", QJsonObject{{"type", "string"}, {"description", "day, week, month"}}}
				}}
			}
		},
		Tool{
			"list_gift_for_sale",
			"List a gift for sale",
			QJsonObject{
				{"type", "object"},
				{"properties", QJsonObject{
					{"gift_id", QJsonObject{{"type", "integer"}, {"description", "Gift ID"}}},
					{"price", QJsonObject{{"type", "number"}, {"description", "Sale price"}}}
				}},
				{"required", QJsonArray{"gift_id", "price"}}
			}
		},
		Tool{
			"update_listing",
			"Update a marketplace listing",
			QJsonObject{
				{"type", "object"},
				{"properties", QJsonObject{
					{"listing_id", QJsonObject{{"type", "integer"}, {"description", "Listing ID"}}},
					{"price", QJsonObject{{"type", "number"}, {"description", "New price"}}}
				}},
				{"required", QJsonArray{"listing_id", "price"}}
			}
		},
		Tool{
			"cancel_listing",
			"Cancel a marketplace listing",
			QJsonObject{
				{"type", "object"},
				{"properties", QJsonObject{
					{"listing_id", QJsonObject{{"type", "integer"}, {"description", "Listing ID"}}}
				}},
				{"required", QJsonArray{"listing_id"}}
			}
		},

		// Star Reactions - 3 tools
		Tool{
			"get_star_reactions_received",
			"Get star reactions received",
			QJsonObject{
				{"type", "object"},
				{"properties", QJsonObject{
					{"limit", QJsonObject{{"type", "integer"}, {"default", 50}}}
				}}
			}
		},
		Tool{
			"get_star_reactions_sent",
			"Get star reactions sent",
			QJsonObject{
				{"type", "object"},
				{"properties", QJsonObject{
					{"limit", QJsonObject{{"type", "integer"}, {"default", 50}}}
				}}
			}
		},
		Tool{
			"get_top_supporters",
			"Get top supporters by star reactions",
			QJsonObject{
				{"type", "object"},
				{"properties", QJsonObject{
					{"limit", QJsonObject{{"type", "integer"}, {"default", 10}}}
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
					{"enabled", QJsonObject{{"type", "boolean"}}},
					{"min_stars", QJsonObject{{"type", "integer"}, {"description", "Minimum stars required"}}}
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
					{"limit", QJsonObject{{"type", "integer"}, {"default", 50}}}
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
					{"app_id", QJsonObject{{"type", "string"}, {"description", "App ID (optional)"}}}
				}}
			}
		},
		Tool{
			"get_miniapp_history",
			"Get mini app transaction history",
			QJsonObject{
				{"type", "object"},
				{"properties", QJsonObject{
					{"app_id", QJsonObject{{"type", "string"}}},
					{"limit", QJsonObject{{"type", "integer"}, {"default", 50}}}
				}}
			}
		},
		Tool{
			"set_miniapp_budget",
			"Set spending budget for a mini app",
			QJsonObject{
				{"type", "object"},
				{"properties", QJsonObject{
					{"app_id", QJsonObject{{"type", "string"}, {"description", "App ID"}}},
					{"daily_limit", QJsonObject{{"type", "number"}}},
					{"monthly_limit", QJsonObject{{"type", "number"}}}
				}},
				{"required", QJsonArray{"app_id"}}
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
					{"days", QJsonObject{{"type", "integer"}, {"default", 30}}}
				}}
			}
		},
		Tool{
			"simulate_rating_change",
			"Simulate how actions affect rating",
			QJsonObject{
				{"type", "object"},
				{"properties", QJsonObject{
					{"action", QJsonObject{{"type", "string"}, {"description", "Action type"}}},
					{"amount", QJsonObject{{"type", "number"}}}
				}},
				{"required", QJsonArray{"action"}}
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
					{"gift_id", QJsonObject{{"type", "integer"}}},
					{"visible", QJsonObject{{"type", "boolean"}}}
				}},
				{"required", QJsonArray{"gift_id", "visible"}}
			}
		},
		Tool{
			"reorder_profile_gifts",
			"Reorder gifts on profile",
			QJsonObject{
				{"type", "object"},
				{"properties", QJsonObject{
					{"gift_ids", QJsonObject{{"type", "array"}, {"items", QJsonObject{{"type", "integer"}}}, {"description", "Ordered list of gift IDs"}}}
				}},
				{"required", QJsonArray{"gift_ids"}}
			}
		},
		Tool{
			"toggle_gift_notifications",
			"Toggle gift notifications",
			QJsonObject{
				{"type", "object"},
				{"properties", QJsonObject{
					{"enabled", QJsonObject{{"type", "boolean"}}}
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
					{"budget", QJsonObject{{"type", "number"}, {"description", "Available budget"}}},
					{"risk_level", QJsonObject{{"type", "string"}, {"description", "low, medium, high"}}}
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
					{"start_date", QJsonObject{{"type", "integer"}}},
					{"end_date", QJsonObject{{"type", "integer"}}}
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
					{"gift_id", QJsonObject{{"type", "integer"}, {"description", "Gift ID"}}},
					{"target_price", QJsonObject{{"type", "number"}, {"description", "Target price"}}},
					{"direction", QJsonObject{{"type", "string"}, {"description", "above or below"}}}
				}},
				{"required", QJsonArray{"gift_id", "target_price"}}
			}
		},
		Tool{
			"create_auction_alert",
			"Create an auction alert",
			QJsonObject{
				{"type", "object"},
				{"properties", QJsonObject{
					{"gift_id", QJsonObject{{"type", "integer"}, {"description", "Gift ID"}}},
					{"max_bid", QJsonObject{{"type", "number"}, {"description", "Maximum bid"}}},
					{"minutes_before", QJsonObject{{"type", "integer"}, {"default", 5}}}
				}},
				{"required", QJsonArray{"gift_id", "max_bid"}}
			}
		},
		Tool{
			"get_fragment_listings",
			"Get listings from Fragment marketplace",
			QJsonObject{
				{"type", "object"},
				{"properties", QJsonObject{
					{"type", QJsonObject{{"type", "string"}, {"description", "usernames, numbers, gifts"}}},
					{"limit", QJsonObject{{"type", "integer"}, {"default", 50}}}
				}}
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
					{"min_delay_ms", QJsonObject{{"type", "integer"}, {"description", "Min delay between batches (ms)"}}},
					{"max_delay_ms", QJsonObject{{"type", "integer"}, {"description", "Max delay between batches (ms)"}}},
					{"burst_pause_ms", QJsonObject{{"type", "integer"}, {"description", "Pause after burst (ms)"}}},
					{"long_pause_ms", QJsonObject{{"type", "integer"}, {"description", "Occasional long pause (ms)"}}},
					{"min_batch_size", QJsonObject{{"type", "integer"}, {"description", "Min messages per batch"}}},
					{"max_batch_size", QJsonObject{{"type", "integer"}, {"description", "Max messages per batch"}}},
					{"batches_before_pause", QJsonObject{{"type", "integer"}, {"description", "Batches before burst pause"}}},
					{"max_messages_per_day", QJsonObject{{"type", "integer"}, {"description", "Daily limit"}}},
					{"max_messages_per_hour", QJsonObject{{"type", "integer"}, {"description", "Hourly limit"}}},
					{"respect_active_hours", QJsonObject{{"type", "boolean"}, {"description", "Only run during typical hours"}}},
					{"active_hour_start", QJsonObject{{"type", "integer"}, {"description", "Start hour (0-23)"}}},
					{"active_hour_end", QJsonObject{{"type", "integer"}, {"description", "End hour (0-23)"}}},
					{"export_format", QJsonObject{{"type", "string"}, {"description", "html, markdown, or both"}}}
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
					{"priority", QJsonObject{{"type", "integer"}, {"description", "Queue priority (lower = higher)"}, {"default", 0}}}
				}},
				{"required", QJsonArray{"chat_id"}}
			}
		},
		Tool{
			"get_gradual_export_queue",
			"Get list of chats in the gradual export queue",
			QJsonObject{{"type", "object"}, {"properties", QJsonObject{}}}
		},

		// ===== GRADUAL EXPORT TOOLS (5) - Bypass Takeout Detection =====
		Tool{
			"start_gradual_export",
			"Start gradual export for a chat - uses direct API calls to bypass takeout detection",
			QJsonObject{
				{"type", "object"},
				{"properties", QJsonObject{
					{"chat_id", QJsonObject{
						{"type", "integer"},
						{"description", "Chat ID to export"}
					}},
					{"min_delay_ms", QJsonObject{
						{"type", "integer"},
						{"description", "Minimum delay between requests in ms"},
						{"default", 2000}
					}},
					{"max_delay_ms", QJsonObject{
						{"type", "integer"},
						{"description", "Maximum delay between requests in ms"},
						{"default", 5000}
					}},
					{"min_batch_size", QJsonObject{
						{"type", "integer"},
						{"description", "Minimum messages per batch"},
						{"default", 10}
					}},
					{"max_batch_size", QJsonObject{
						{"type", "integer"},
						{"description", "Maximum messages per batch"},
						{"default", 50}
					}},
					{"export_format", QJsonObject{
						{"type", "string"},
						{"description", "Export format: json, html, markdown, or all"},
						{"default", "json"}
					}},
					{"export_path", QJsonObject{
						{"type", "string"},
						{"description", "Custom export path (optional)"}
					}}
				}},
				{"required", QJsonArray{"chat_id"}}
			}
		},
		// ===== VOICE & TRANSLATION TOOLS =====
		Tool{
			"get_voice_transcription",
			"Get transcription of a voice message",
			QJsonObject{{"type", "object"}, {"properties", QJsonObject{
				{"message_id", QJsonObject{{"type", "integer"}, {"description", "Voice message ID to transcribe"}}},
				{"chat_id", QJsonObject{{"type", "integer"}, {"description", "Chat containing the voice message"}}}
			}}, {"required", QJsonArray{"message_id", "chat_id"}}}
		},
		Tool{
			"translate_message",
			"Translate a single message to a target language",
			QJsonObject{{"type", "object"}, {"properties", QJsonObject{
				{"message_id", QJsonObject{{"type", "integer"}, {"description", "Message ID to translate"}}},
				{"chat_id", QJsonObject{{"type", "integer"}, {"description", "Chat containing the message"}}},
				{"target_language", QJsonObject{{"type", "string"}, {"description", "Target language code (e.g. en, ru, es)"}}}
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
				{"enabled", QJsonObject{{"type", "boolean"}, {"description", "Enable or disable away message"}}}
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
				{"chat_id", QJsonObject{{"type", "integer"}, {"description", "Chat containing the message"}}}
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
				{"phone", QJsonObject{{"type", "string"}, {"description", "New phone number"}}}
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
				{"option", QJsonObject{{"type", "string"}, {"description", "everybody, contacts, close_friends, or nobody"}}}
			}}, {"required", QJsonArray{"option"}}}
		},
		Tool{
			"update_profile_photo_privacy",
			"Set who can see your profile photo",
			QJsonObject{{"type", "object"}, {"properties", QJsonObject{
				{"option", QJsonObject{{"type", "string"}, {"description", "everybody, contacts, close_friends, or nobody"}}}
			}}, {"required", QJsonArray{"option"}}}
		},
		Tool{
			"update_phone_number_privacy",
			"Set who can see your phone number",
			QJsonObject{{"type", "object"}, {"properties", QJsonObject{
				{"option", QJsonObject{{"type", "string"}, {"description", "everybody, contacts, close_friends, or nobody"}}}
			}}, {"required", QJsonArray{"option"}}}
		},
		Tool{
			"update_forwards_privacy",
			"Set who can link to your account when forwarding messages",
			QJsonObject{{"type", "object"}, {"properties", QJsonObject{
				{"option", QJsonObject{{"type", "string"}, {"description", "everybody, contacts, close_friends, or nobody"}}}
			}}, {"required", QJsonArray{"option"}}}
		},
		Tool{
			"update_birthday_privacy",
			"Set who can see your birthday",
			QJsonObject{{"type", "object"}, {"properties", QJsonObject{
				{"option", QJsonObject{{"type", "string"}, {"description", "everybody, contacts, close_friends, or nobody"}}}
			}}, {"required", QJsonArray{"option"}}}
		},
		Tool{
			"update_about_privacy",
			"Set who can see your bio/about",
			QJsonObject{{"type", "object"}, {"properties", QJsonObject{
				{"option", QJsonObject{{"type", "string"}, {"description", "everybody, contacts, close_friends, or nobody"}}}
			}}, {"required", QJsonArray{"option"}}}
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
			{"tag_name", QJsonObject{{"type", "string"}, {"description", "Tag name"}}}
		}}, {"required", QJsonArray{"chat_id", "message_id", "tag_name"}}}},
		Tool{"remove_message_tag", "Remove a tag from a message", QJsonObject{{"type", "object"}, {"properties", QJsonObject{
			{"chat_id", QJsonObject{{"type", "integer"}, {"description", "Chat ID"}}},
			{"message_id", QJsonObject{{"type", "integer"}, {"description", "Message ID"}}},
			{"tag_name", QJsonObject{{"type", "string"}, {"description", "Tag name"}}}
		}}, {"required", QJsonArray{"chat_id", "message_id", "tag_name"}}}},
		Tool{"get_message_tags", "Get all tags on a message", QJsonObject{{"type", "object"}, {"properties", QJsonObject{
			{"chat_id", QJsonObject{{"type", "integer"}, {"description", "Chat ID"}}},
			{"message_id", QJsonObject{{"type", "integer"}, {"description", "Message ID"}}}
		}}, {"required", QJsonArray{"chat_id", "message_id"}}}},
		Tool{"search_by_tag", "Search messages by tag", QJsonObject{{"type", "object"}, {"properties", QJsonObject{
			{"tag_name", QJsonObject{{"type", "string"}, {"description", "Tag to search for"}}},
			{"chat_id", QJsonObject{{"type", "integer"}, {"description", "Optional chat filter"}}}
		}}, {"required", QJsonArray{"tag_name"}}}},
		Tool{"get_tag_suggestions", "Get tag suggestions for a message", QJsonObject{{"type", "object"}, {"properties", QJsonObject{
			{"text", QJsonObject{{"type", "string"}, {"description", "Message text to analyze"}}}
		}}, {"required", QJsonArray{"text"}}}},

		// --- Translation ---
		Tool{"get_translation_history", "Get recent translations", QJsonObject{{"type", "object"}, {"properties", QJsonObject{
			{"limit", QJsonObject{{"type", "integer"}, {"description", "Max results"}, {"default", 50}}}
		}}}},

		// --- Ad Filter ---
		Tool{"get_ad_filter_stats", "Get ad filtering statistics", QJsonObject{{"type", "object"}, {"properties", QJsonObject{}}}},

		// --- Chat Rules ---
		Tool{"get_chat_rules", "Get chat automation rules", QJsonObject{{"type", "object"}, {"properties", QJsonObject{
			{"chat_id", QJsonObject{{"type", "integer"}, {"description", "Chat ID"}}}
		}}}},
		Tool{"set_chat_rules", "Set chat automation rules", QJsonObject{{"type", "object"}, {"properties", QJsonObject{
			{"chat_id", QJsonObject{{"type", "integer"}, {"description", "Chat ID"}}},
			{"rules", QJsonObject{{"type", "object"}, {"description", "Rules configuration"}}}
		}}, {"required", QJsonArray{"chat_id", "rules"}}}},
		Tool{"test_chat_rules", "Test chat rules against sample text", QJsonObject{{"type", "object"}, {"properties", QJsonObject{
			{"chat_id", QJsonObject{{"type", "integer"}, {"description", "Chat ID"}}},
			{"text", QJsonObject{{"type", "string"}, {"description", "Test message"}}}
		}}, {"required", QJsonArray{"chat_id", "text"}}}},

		// --- Quick Replies ---
		Tool{"update_quick_reply", "Update an existing quick reply", QJsonObject{{"type", "object"}, {"properties", QJsonObject{
			{"id", QJsonObject{{"type", "integer"}, {"description", "Quick reply ID"}}},
			{"text", QJsonObject{{"type", "string"}, {"description", "New reply text"}}}
		}}, {"required", QJsonArray{"id", "text"}}}},
		Tool{"use_quick_reply", "Send a quick reply to a chat", QJsonObject{{"type", "object"}, {"properties", QJsonObject{
			{"shortcut", QJsonObject{{"type", "string"}, {"description", "Quick reply shortcut"}}},
			{"chat_id", QJsonObject{{"type", "integer"}, {"description", "Chat to send to"}}}
		}}, {"required", QJsonArray{"shortcut", "chat_id"}}}},

		// --- Greeting/Away ---
		Tool{"set_greeting_message", "Configure greeting message", QJsonObject{{"type", "object"}, {"properties", QJsonObject{
			{"message", QJsonObject{{"type", "string"}, {"description", "Greeting text"}}},
			{"enabled", QJsonObject{{"type", "boolean"}, {"description", "Enable/disable"}}}
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
			{"personality", QJsonObject{{"type", "string"}, {"description", "Personality style"}}}
		}}, {"required", QJsonArray{"name"}}}},
		Tool{"test_chatbot", "Test chatbot with sample input", QJsonObject{{"type", "object"}, {"properties", QJsonObject{
			{"input", QJsonObject{{"type", "string"}, {"description", "Test message"}}}
		}}, {"required", QJsonArray{"input"}}}},
		Tool{"get_chatbot_analytics", "Get chatbot usage analytics", QJsonObject{{"type", "object"}, {"properties", QJsonObject{}}}},

		// --- TTS/Video ---
		Tool{"text_to_speech", "Convert text to speech audio", QJsonObject{{"type", "object"}, {"properties", QJsonObject{
			{"text", QJsonObject{{"type", "string"}, {"description", "Text to convert"}}},
			{"voice", QJsonObject{{"type", "string"}, {"description", "Voice preset name"}}}
		}}, {"required", QJsonArray{"text"}}}},
		Tool{"text_to_video", "Generate video circle from text", QJsonObject{{"type", "object"}, {"properties", QJsonObject{
			{"text", QJsonObject{{"type", "string"}, {"description", "Text content"}}},
			{"avatar", QJsonObject{{"type", "string"}, {"description", "Avatar preset"}}}
		}}, {"required", QJsonArray{"text"}}}},

		// --- Auto Reply ---
		Tool{"create_auto_reply_rule", "Create auto-reply rule", QJsonObject{{"type", "object"}, {"properties", QJsonObject{
			{"trigger", QJsonObject{{"type", "string"}, {"description", "Trigger keyword or pattern"}}},
			{"response", QJsonObject{{"type", "string"}, {"description", "Auto-reply text"}}}
		}}, {"required", QJsonArray{"trigger", "response"}}}},
		Tool{"list_auto_reply_rules", "List all auto-reply rules", QJsonObject{{"type", "object"}, {"properties", QJsonObject{}}}},
		Tool{"update_auto_reply_rule", "Update an auto-reply rule", QJsonObject{{"type", "object"}, {"properties", QJsonObject{
			{"id", QJsonObject{{"type", "integer"}, {"description", "Rule ID"}}},
			{"response", QJsonObject{{"type", "string"}, {"description", "New response text"}}}
		}}, {"required", QJsonArray{"id"}}}},
		Tool{"delete_auto_reply_rule", "Delete an auto-reply rule", QJsonObject{{"type", "object"}, {"properties", QJsonObject{
			{"id", QJsonObject{{"type", "integer"}, {"description", "Rule ID"}}}
		}}, {"required", QJsonArray{"id"}}}},
		Tool{"test_auto_reply_rule", "Test auto-reply rule against text", QJsonObject{{"type", "object"}, {"properties", QJsonObject{
			{"text", QJsonObject{{"type", "string"}, {"description", "Test message"}}}
		}}, {"required", QJsonArray{"text"}}}},
		Tool{"get_auto_reply_stats", "Get auto-reply usage statistics", QJsonObject{{"type", "object"}, {"properties", QJsonObject{}}}},

		// --- Gift Collections ---
		Tool{"create_gift_collection", "Create a gift collection", QJsonObject{{"type", "object"}, {"properties", QJsonObject{
			{"name", QJsonObject{{"type", "string"}, {"description", "Collection name"}}},
			{"description", QJsonObject{{"type", "string"}, {"description", "Collection description"}}}
		}}, {"required", QJsonArray{"name"}}}},
		Tool{"add_to_collection", "Add a gift to a collection", QJsonObject{{"type", "object"}, {"properties", QJsonObject{
			{"collection_id", QJsonObject{{"type", "integer"}, {"description", "Collection ID"}}},
			{"gift_id", QJsonObject{{"type", "string"}, {"description", "Gift identifier"}}}
		}}, {"required", QJsonArray{"collection_id", "gift_id"}}}},
		Tool{"remove_from_collection", "Remove a gift from a collection", QJsonObject{{"type", "object"}, {"properties", QJsonObject{
			{"collection_id", QJsonObject{{"type", "integer"}, {"description", "Collection ID"}}},
			{"gift_id", QJsonObject{{"type", "string"}, {"description", "Gift identifier"}}}
		}}, {"required", QJsonArray{"collection_id", "gift_id"}}}},
		Tool{"share_collection", "Share a collection with a user", QJsonObject{{"type", "object"}, {"properties", QJsonObject{
			{"collection_id", QJsonObject{{"type", "integer"}, {"description", "Collection ID"}}},
			{"with_user_id", QJsonObject{{"type", "integer"}, {"description", "User ID to share with"}}}
		}}, {"required", QJsonArray{"collection_id"}}}},

		// --- Auctions ---
		Tool{"create_gift_auction", "Create an auction for a gift", QJsonObject{{"type", "object"}, {"properties", QJsonObject{
			{"gift_id", QJsonObject{{"type", "string"}, {"description", "Gift to auction"}}},
			{"starting_bid", QJsonObject{{"type", "integer"}, {"description", "Starting bid in stars"}}}
		}}, {"required", QJsonArray{"gift_id", "starting_bid"}}}},
		Tool{"place_bid", "Place a bid on an auction", QJsonObject{{"type", "object"}, {"properties", QJsonObject{
			{"auction_id", QJsonObject{{"type", "string"}, {"description", "Auction ID"}}},
			{"bid_amount", QJsonObject{{"type", "integer"}, {"description", "Bid amount in stars"}}}
		}}, {"required", QJsonArray{"auction_id", "bid_amount"}}}},
		Tool{"list_auctions", "List active auctions", QJsonObject{{"type", "object"}, {"properties", QJsonObject{
			{"status", QJsonObject{{"type", "string"}, {"description", "Filter by status (active/ended/cancelled)"}, {"default", "active"}}}
		}}}},
		Tool{"get_auction_status", "Get auction details and status", QJsonObject{{"type", "object"}, {"properties", QJsonObject{
			{"auction_id", QJsonObject{{"type", "string"}, {"description", "Auction ID"}}}
		}}, {"required", QJsonArray{"auction_id"}}}},
		Tool{"cancel_auction", "Cancel an active auction", QJsonObject{{"type", "object"}, {"properties", QJsonObject{
			{"auction_id", QJsonObject{{"type", "string"}, {"description", "Auction ID"}}}
		}}, {"required", QJsonArray{"auction_id"}}}},

		// --- Marketplace ---
		Tool{"list_marketplace", "Browse gift marketplace listings", QJsonObject{{"type", "object"}, {"properties", QJsonObject{
			{"category", QJsonObject{{"type", "string"}, {"description", "Filter by category"}}},
			{"sort_by", QJsonObject{{"type", "string"}, {"description", "Sort order (recent/price_asc/price_desc)"}, {"default", "recent"}}}
		}}}},
		Tool{"buy_gift", "Purchase a gift from marketplace", QJsonObject{{"type", "object"}, {"properties", QJsonObject{
			{"listing_id", QJsonObject{{"type", "string"}, {"description", "Marketplace listing ID"}}}
		}}, {"required", QJsonArray{"listing_id"}}}},
		Tool{"delist_gift", "Remove a gift listing from marketplace", QJsonObject{{"type", "object"}, {"properties", QJsonObject{
			{"listing_id", QJsonObject{{"type", "string"}, {"description", "Listing ID"}}}
		}}, {"required", QJsonArray{"listing_id"}}}},

		// --- Wallet/Gifts ---
		Tool{"send_gift", "Send a gift to a user", QJsonObject{{"type", "object"}, {"properties", QJsonObject{
			{"recipient_id", QJsonObject{{"type", "integer"}, {"description", "Recipient user ID"}}},
			{"gift_type", QJsonObject{{"type", "string"}, {"description", "Type of gift"}}},
			{"amount", QJsonObject{{"type", "integer"}, {"description", "Amount in stars"}}}
		}}, {"required", QJsonArray{"recipient_id", "amount"}}}},
		Tool{"get_gift_history", "Get gift sending/receiving history", QJsonObject{{"type", "object"}, {"properties", QJsonObject{
			{"direction", QJsonObject{{"type", "string"}, {"description", "Filter: sent/received/both"}, {"default", "both"}}}
		}}}},
		Tool{"list_available_gifts", "List available gift types", QJsonObject{{"type", "object"}, {"properties", QJsonObject{}}}},
		Tool{"get_gift_suggestions", "Get gift suggestions for a user", QJsonObject{{"type", "object"}, {"properties", QJsonObject{
			{"recipient_id", QJsonObject{{"type", "integer"}, {"description", "Recipient user ID"}}}
		}}, {"required", QJsonArray{"recipient_id"}}}},
		Tool{"get_gift_price_history", "Get price history for a gift type", QJsonObject{{"type", "object"}, {"properties", QJsonObject{
			{"gift_type", QJsonObject{{"type", "string"}, {"description", "Gift type"}}}
		}}, {"required", QJsonArray{"gift_type"}}}},

		// --- Subscriptions ---
		Tool{"subscribe_to_channel", "Subscribe to a channel", QJsonObject{{"type", "object"}, {"properties", QJsonObject{
			{"channel_id", QJsonObject{{"type", "integer"}, {"description", "Channel ID"}}}
		}}, {"required", QJsonArray{"channel_id"}}}},
		Tool{"unsubscribe_from_channel", "Unsubscribe from a channel", QJsonObject{{"type", "object"}, {"properties", QJsonObject{
			{"channel_id", QJsonObject{{"type", "integer"}, {"description", "Channel ID"}}}
		}}, {"required", QJsonArray{"channel_id"}}}},
		Tool{"get_subscription_stats", "Get subscription spending statistics", QJsonObject{{"type", "object"}, {"properties", QJsonObject{}}}},

		// --- Monetization ---
		Tool{"get_earnings", "Get creator earnings data", QJsonObject{{"type", "object"}, {"properties", QJsonObject{
			{"period", QJsonObject{{"type", "string"}, {"description", "Time period (day/week/month)"}, {"default", "month"}}}
		}}}},
		Tool{"withdraw_earnings", "Withdraw earnings to wallet", QJsonObject{{"type", "object"}, {"properties", QJsonObject{
			{"amount", QJsonObject{{"type", "number"}, {"description", "Amount to withdraw"}}},
			{"method", QJsonObject{{"type", "string"}, {"description", "Withdrawal method (ton/fragment)"}, {"default", "ton"}}}
		}}, {"required", QJsonArray{"amount"}}}},
		Tool{"set_monetization_rules", "Configure monetization rules", QJsonObject{{"type", "object"}, {"properties", QJsonObject{
			{"rules", QJsonObject{{"type", "object"}, {"description", "Monetization rules configuration"}}}
		}}, {"required", QJsonArray{"rules"}}}},
		Tool{"get_monetization_analytics", "Get monetization analytics", QJsonObject{{"type", "object"}, {"properties", QJsonObject{}}}},

		// --- Budget ---
		Tool{"set_spending_budget", "Set spending budget limits", QJsonObject{{"type", "object"}, {"properties", QJsonObject{
			{"daily_limit", QJsonObject{{"type", "number"}, {"description", "Daily spending limit"}}},
			{"monthly_limit", QJsonObject{{"type", "number"}, {"description", "Monthly spending limit"}}}
		}}}},
		Tool{"set_budget_alert", "Set budget alert threshold", QJsonObject{{"type", "object"}, {"properties", QJsonObject{
			{"threshold", QJsonObject{{"type", "number"}, {"description", "Alert threshold amount"}}}
		}}, {"required", QJsonArray{"threshold"}}}},

		// --- Stars ---
		Tool{"request_stars", "Request stars from a user", QJsonObject{{"type", "object"}, {"properties", QJsonObject{
			{"user_id", QJsonObject{{"type", "integer"}, {"description", "User to request from"}}},
			{"amount", QJsonObject{{"type", "integer"}, {"description", "Stars amount"}}}
		}}, {"required", QJsonArray{"user_id", "amount"}}}},
		Tool{"get_stars_leaderboard", "Get stars leaderboard", QJsonObject{{"type", "object"}, {"properties", QJsonObject{
			{"limit", QJsonObject{{"type", "integer"}, {"description", "Max entries"}, {"default", 10}}}
		}}}},
		Tool{"get_stars_history", "Get stars transaction history", QJsonObject{{"type", "object"}, {"properties", QJsonObject{
			{"limit", QJsonObject{{"type", "integer"}, {"description", "Max entries"}, {"default", 50}}}
		}}}},
		Tool{"get_stars_rate", "Get current stars exchange rate", QJsonObject{{"type", "object"}, {"properties", QJsonObject{}}}},
		Tool{"convert_stars", "Convert stars to/from other currencies", QJsonObject{{"type", "object"}, {"properties", QJsonObject{
			{"amount", QJsonObject{{"type", "integer"}, {"description", "Amount to convert"}}},
			{"direction", QJsonObject{{"type", "string"}, {"description", "Conversion direction"}}}
		}}, {"required", QJsonArray{"amount"}}}},
		Tool{"categorize_transaction", "Categorize a transaction", QJsonObject{{"type", "object"}, {"properties", QJsonObject{
			{"transaction_id", QJsonObject{{"type", "integer"}, {"description", "Transaction ID"}}},
			{"category", QJsonObject{{"type", "string"}, {"description", "Category name"}}}
		}}, {"required", QJsonArray{"transaction_id", "category"}}}},
		Tool{"send_star_reaction", "Send a star reaction to a message", QJsonObject{{"type", "object"}, {"properties", QJsonObject{
			{"chat_id", QJsonObject{{"type", "integer"}, {"description", "Chat ID"}}},
			{"message_id", QJsonObject{{"type", "integer"}, {"description", "Message ID"}}},
			{"stars", QJsonObject{{"type", "integer"}, {"description", "Stars count"}}}
		}}, {"required", QJsonArray{"chat_id", "message_id", "stars"}}}},
		Tool{"get_star_reactions", "Get star reactions for messages", QJsonObject{{"type", "object"}, {"properties", QJsonObject{
			{"chat_id", QJsonObject{{"type", "integer"}, {"description", "Chat ID"}}}
		}}}},
		Tool{"get_reaction_analytics", "Get reaction analytics", QJsonObject{{"type", "object"}, {"properties", QJsonObject{}}}},
		Tool{"get_top_reacted", "Get most reacted messages", QJsonObject{{"type", "object"}, {"properties", QJsonObject{
			{"limit", QJsonObject{{"type", "integer"}, {"description", "Max entries"}, {"default", 10}}}
		}}}},
		Tool{"set_reaction_price", "Set custom star reaction price", QJsonObject{{"type", "object"}, {"properties", QJsonObject{
			{"price", QJsonObject{{"type", "integer"}, {"description", "Price in stars"}}}
		}}, {"required", QJsonArray{"price"}}}},

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
			{"price", QJsonObject{{"type", "integer"}, {"description", "Price in stars"}}}
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
			{"content_id", QJsonObject{{"type", "integer"}, {"description", "Content ID"}}}
		}}, {"required", QJsonArray{"content_id"}}}},

		// --- Creator ---
		Tool{"create_exclusive_content", "Create exclusive content for subscribers", QJsonObject{{"type", "object"}, {"properties", QJsonObject{
			{"content", QJsonObject{{"type", "string"}, {"description", "Content text"}}},
			{"tier", QJsonObject{{"type", "string"}, {"description", "Subscriber tier"}, {"default", "all"}}}
		}}, {"required", QJsonArray{"content"}}}},
		Tool{"get_subscriber_analytics", "Get subscriber analytics", QJsonObject{{"type", "object"}, {"properties", QJsonObject{}}}},
		Tool{"send_subscriber_message", "Send message to subscribers", QJsonObject{{"type", "object"}, {"properties", QJsonObject{
			{"message", QJsonObject{{"type", "string"}, {"description", "Message text"}}},
			{"tier", QJsonObject{{"type", "string"}, {"description", "Target tier"}, {"default", "all"}}}
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
			{"limit", QJsonObject{{"type", "integer"}, {"description", "Max messages to index"}, {"default", 1000}}}
		}}, {"required", QJsonArray{"chat_id"}}}},

		// --- Tasks ---
		Tool{"update_task", "Update a task's status or details", QJsonObject{{"type", "object"}, {"properties", QJsonObject{
			{"task_id", QJsonObject{{"type", "integer"}, {"description", "Task ID"}}},
			{"status", QJsonObject{{"type", "string"}, {"description", "New status (pending/in_progress/completed)"}}}
		}}, {"required", QJsonArray{"task_id"}}}},
	};
}

} // namespace MCP

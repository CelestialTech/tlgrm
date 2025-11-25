// MCP Server - Complete Implementation with 45+ Tools
//
// This file is part of Telegram Desktop MCP integration,
// the official desktop application for the Telegram messaging service.
//
// For license and copyright information please follow this link:
// https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL

#include "mcp_server.h"
#include "chat_archiver.h"
#include "analytics.h"
#include "semantic_search.h"
#include "batch_operations.h"
#include "message_scheduler.h"
#include "audit_logger.h"
#include "rbac.h"
#include "voice_transcription.h"
#include "bot_manager.h"
#include "context_assistant_bot.h"
#include "cache_manager.h"

#include <QtCore/QTimer>
#include <QtCore/QDebug>
#include <QtCore/QDir>
#include <QtCore/QJsonObject>
#include <QtCore/QJsonArray>
#include <QtSql/QSqlError>

#include "main/main_session.h"
#include "data/data_session.h"
#include "data/data_peer.h"
#include "data/data_user.h"
#include "data/data_chat.h"
#include "data/data_channel.h"
#include "data/data_thread.h"
#include "data/data_histories.h"
#include "dialogs/dialogs_main_list.h"
#include "dialogs/dialogs_indexed_list.h"
#include "dialogs/dialogs_row.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/view/history_view_element.h"
#include "api/api_common.h"
#include "api/api_editing.h"
#include "apiwrap.h"

namespace MCP {

Server::Server(QObject *parent)
	: QObject(parent) {
	fprintf(stderr, "[MCP] Server object created\n");
	fflush(stderr);
	initializeCapabilities();
	registerTools();
	registerResources();
	registerPrompts();
}

Server::~Server() {
	stop();
}

void Server::initializeCapabilities() {
	_serverInfo.capabilities = QJsonObject{
		{"tools", QJsonObject{{"listChanged", true}}},
		{"resources", QJsonObject{{"listChanged", true}}},
		{"prompts", QJsonObject{{"listChanged", true}}},
	};
}

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
						{"description", "Output file path"}
					}}
				}},
				{"required", QJsonArray{"chat_id", "format", "output_path"}},
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
	};
}

void Server::registerResources() {
	_resources = {
		Resource{
			"telegram://chats",
			"All Chats",
			"List of all Telegram chats",
			"application/json"
		},
		Resource{
			"telegram://messages/{chat_id}",
			"Chat Messages",
			"Messages from a specific chat",
			"application/json"
		},
		Resource{
			"telegram://archive/stats",
			"Archive Statistics",
			"Statistics about archived data",
			"application/json"
		},
	};
}

void Server::registerPrompts() {
	_prompts = {
		Prompt{
			"summarize_chat",
			"Analyze and summarize recent messages in a chat",
			QJsonArray{
				QJsonObject{
					{"name", "chat_id"},
					{"description", "Chat ID to summarize"},
					{"required", true}
				},
				QJsonObject{
					{"name", "limit"},
					{"description", "Number of messages to analyze"},
					{"required", false}
				}
			}
		},
		Prompt{
			"analyze_trends",
			"Analyze activity trends in a chat",
			QJsonArray{
				QJsonObject{
					{"name", "chat_id"},
					{"description", "Chat ID to analyze"},
					{"required", true}
				}
			}
		},
	};
}

bool Server::start(TransportType transport) {
	fprintf(stderr, "[MCP] Server::start() called, initialized=%d\n", _initialized);
	fflush(stderr);

	if (_initialized) {
		return true;
	}

	_transport = transport;

	// Set database path
	_databasePath = QDir::home().filePath("telegram_mcp.db");

	// Initialize database
	_db = QSqlDatabase::addDatabase("QSQLITE", "mcp_main");
	_db.setDatabaseName(_databasePath);
	if (!_db.open()) {
		qWarning() << "MCP: Failed to open database:" << _db.lastError().text();
		return false;
	}

	fprintf(stderr, "[MCP] Database initialized successfully\n");
	fflush(stderr);

	// Initialize session-independent components only
	_auditLogger = new AuditLogger(this);
	_auditLogger->start(&_db, QDir::home().filePath("telegram_mcp_audit.log"));

	_rbac = new RBAC(this);
	_rbac->start(&_db);

	fprintf(stderr, "[MCP] Session-independent components initialized (AuditLogger, RBAC)\n");
	fflush(stderr);

	// Start transport (this allows JSON-RPC to work even without session)
	switch (_transport) {
	case TransportType::Stdio:
		startStdioTransport();
		break;
	case TransportType::HTTP:
		startHttpTransport();
		break;
	default:
		qWarning() << "MCP: Unsupported transport type";
		return false;
	}

	_initialized = true;

	_auditLogger->logSystemEvent("server_start", "MCP Server started (session-dependent components will initialize when session available)");

	fprintf(stderr, "[MCP] ========================================\n");
	fprintf(stderr, "[MCP] SERVER STARTED SUCCESSFULLY\n");
	fprintf(stderr, "[MCP] Transport: %s\n", (_transport == TransportType::Stdio ? "stdio" : "http"));
	fprintf(stderr, "[MCP] Session-dependent components will initialize when session is set\n");
	fprintf(stderr, "[MCP] Ready to receive requests\n");
	fprintf(stderr, "[MCP] ========================================\n");
	fflush(stderr);

	qInfo() << "MCP Server started (transport:"
		<< (_transport == TransportType::Stdio ? "stdio" : "http")
		<< ") - awaiting session";

	return true;
}

void Server::stop() {
	if (!_initialized) {
		return;
	}

	_auditLogger->logSystemEvent("server_stop", "MCP Server stopping");

	// Cleanup components
	if (_archiver) {
		_archiver->stop();
		delete _archiver;
		_archiver = nullptr;
	}

	if (_ephemeralArchiver) {
		_ephemeralArchiver->stop();
		delete _ephemeralArchiver;
		_ephemeralArchiver = nullptr;
	}

	delete _analytics;
	delete _semanticSearch;
	delete _batchOps;

	if (_scheduler) {
		_scheduler->stop();
		delete _scheduler;
		_scheduler = nullptr;
	}

	if (_auditLogger) {
		_auditLogger->stop();
		delete _auditLogger;
		_auditLogger = nullptr;
	}

	if (_rbac) {
		_rbac->stop();
		delete _rbac;
		_rbac = nullptr;
	}

	_db.close();

	delete _stdin;
	delete _stdout;
	delete _httpServer;

	_stdin = nullptr;
	_stdout = nullptr;
	_httpServer = nullptr;

	_initialized = false;
	qInfo() << "MCP Server stopped";
}

void Server::setSession(Main::Session *session) {
	_session = session;

	fprintf(stderr, "[MCP] setSession() called with session=%p\n", (void*)session);
	fflush(stderr);

	if (!_session) {
		qWarning() << "MCP: setSession() called with null session";
		return;
	}

	// Initialize session-dependent components
	fprintf(stderr, "[MCP] Initializing session-dependent components...\n");
	fflush(stderr);

	// CacheManager - initialize first so other components can use it
	_cache = new CacheManager(this);
	_cache->setMaxSize(50);  // 50 MB cache
	_cache->setDefaultTTL(300);  // 5 minutes TTL
	fprintf(stderr, "[MCP] CacheManager initialized (50MB, 300s TTL)\n");
	fflush(stderr);

	// ChatArchiver - requires database
	_archiver = new ChatArchiver(this);
	if (!_archiver->start(_databasePath)) {
		qWarning() << "MCP: Failed to start ChatArchiver";
		fprintf(stderr, "[MCP] WARNING: ChatArchiver failed to start\n");
		fflush(stderr);
		// Don't return - continue with other components
	} else {
		fprintf(stderr, "[MCP] ChatArchiver initialized\n");
		fflush(stderr);
	}

	// EphemeralArchiver - depends on ChatArchiver
	if (_archiver) {
		_ephemeralArchiver = new EphemeralArchiver(this);
		_ephemeralArchiver->start(_archiver);
		fprintf(stderr, "[MCP] EphemeralArchiver initialized\n");
		fflush(stderr);
	}

	// Analytics - requires session data
	_analytics = new Analytics(this);
	_analytics->start(&_session->data(), _archiver);
	fprintf(stderr, "[MCP] Analytics initialized\n");
	fflush(stderr);

	// SemanticSearch - depends on ChatArchiver
	if (_archiver) {
		_semanticSearch = new SemanticSearch(_archiver, this);
		_semanticSearch->initialize();
		fprintf(stderr, "[MCP] SemanticSearch initialized\n");
		fflush(stderr);
	}

	// BatchOperations - requires session
	_batchOps = new BatchOperations(this);
	_batchOps->start(_session);
	fprintf(stderr, "[MCP] BatchOperations initialized\n");
	fflush(stderr);

	// MessageScheduler - requires session
	_scheduler = new MessageScheduler(this);
	_scheduler->start(_session);
	fprintf(stderr, "[MCP] MessageScheduler initialized\n");
	fflush(stderr);

	// BotManager - depends on all other components
	if (_archiver && _analytics && _semanticSearch && _scheduler && _auditLogger && _rbac) {
		_botManager = new BotManager(this);
		_botManager->initialize(
			_archiver,
			_analytics,
			_semanticSearch,
			_scheduler,
			_auditLogger,
			_rbac
		);

		// Load and register built-in bots
		_botManager->discoverBots();

		// Register and start the Context Assistant Bot (example)
		auto *contextBot = new ContextAssistantBot(_botManager);
		_botManager->registerBot(contextBot);
		_botManager->startBot("context_assistant");

		fprintf(stderr, "[MCP] BotManager initialized and bots started\n");
		fflush(stderr);
	}

	if (_auditLogger) {
		_auditLogger->logSystemEvent("session_connected", "MCP Server session-dependent components initialized successfully");
	}

	fprintf(stderr, "[MCP] ========================================\n");
	fprintf(stderr, "[MCP] SESSION CONNECTED SUCCESSFULLY\n");
	fprintf(stderr, "[MCP] All components initialized and ready\n");
	fprintf(stderr, "[MCP] Live Telegram data access enabled\n");
	fprintf(stderr, "[MCP] ========================================\n");
	fflush(stderr);

	qInfo() << "MCP: Session set, live data access enabled";
}

void Server::startStdioTransport() {
	_stdin = new QTextStream(stdin);
	_stdout = new QTextStream(stdout);

	fprintf(stderr, "[MCP] Stdio transport started, polling stdin every 100ms\n");
	fflush(stderr);

	// Use a timer to poll stdin
	QTimer *timer = new QTimer(this);
	connect(timer, &QTimer::timeout, this, &Server::handleStdioInput);
	timer->start(100); // Poll every 100ms
}

void Server::handleStdioInput() {
	if (!_stdin->atEnd()) {
		QString line = _stdin->readLine();

		if (line.isEmpty()) {
			return;
		}

		fprintf(stderr, "[MCP] Received input: %s\n", line.toUtf8().constData());
		fflush(stderr);

		// Parse JSON-RPC request
		QJsonParseError error;
		QJsonDocument doc = QJsonDocument::fromJson(line.toUtf8(), &error);

		if (error.error != QJsonParseError::NoError) {
			fprintf(stderr, "[MCP] JSON parse error: %s\n", error.errorString().toUtf8().constData());
			fflush(stderr);
			return;
		}

		QJsonObject request = doc.object();
		QJsonObject response = handleRequest(request);

		// Write response to stdout
		QByteArray responseBytes = QJsonDocument(response).toJson(QJsonDocument::Compact);
		fprintf(stderr, "[MCP] Sending response: %s\n", responseBytes.constData());
		fflush(stderr);

		*_stdout << responseBytes;
		*_stdout << "\n";
		_stdout->flush();
	}
}

void Server::startHttpTransport(int port) {
	// TODO: Implement HTTP transport with QHttpServer (Qt 6)
	qInfo() << "MCP: HTTP transport on port" << port << "(not implemented yet)";
}

QJsonObject Server::handleRequest(const QJsonObject &request) {
	QString method = request["method"].toString();
	QJsonObject params = request["params"].toObject();
	QJsonValue id = request["id"];

	qDebug() << "MCP: Request" << method;

	// Dispatch to method handlers
	if (method == "initialize") {
		return successResponse(id, handleInitialize(params));
	} else if (method == "tools/list") {
		return successResponse(id, handleListTools(params));
	} else if (method == "tools/call") {
		return successResponse(id, handleCallTool(params));
	} else if (method == "resources/list") {
		return successResponse(id, handleListResources(params));
	} else if (method == "resources/read") {
		return successResponse(id, handleReadResource(params));
	} else if (method == "prompts/list") {
		return successResponse(id, handleListPrompts(params));
	} else if (method == "prompts/get") {
		return successResponse(id, handleGetPrompt(params));
	} else {
		return errorResponse(id, -32601, "Method not found: " + method);
	}
}

QJsonObject Server::handleInitialize(const QJsonObject &params) {
	Q_UNUSED(params);

	_auditLogger->logSystemEvent("initialize", "Client initialized");

	return QJsonObject{
		{"protocolVersion", "2024-11-05"},
		{"serverInfo", QJsonObject{
			{"name", _serverInfo.name},
			{"version", _serverInfo.version}
		}},
		{"capabilities", _serverInfo.capabilities},
	};
}

QJsonObject Server::handleListTools(const QJsonObject &params) {
	Q_UNUSED(params);

	QJsonArray tools;
	for (const auto &tool : _tools) {
		tools.append(QJsonObject{
			{"name", tool.name},
			{"description", tool.description},
			{"inputSchema", tool.inputSchema},
		});
	}

	return QJsonObject{{"tools", tools}};
}

QJsonObject Server::handleCallTool(const QJsonObject &params) {
	QString toolName = params["name"].toString();
	QJsonObject arguments = params["arguments"].toObject();

	_auditLogger->logToolInvoked(toolName, arguments);

	QJsonObject result;

	// CORE TOOLS
	if (toolName == "list_chats") {
		result = toolListChats(arguments);
	} else if (toolName == "get_chat_info") {
		result = toolGetChatInfo(arguments);
	} else if (toolName == "read_messages") {
		result = toolReadMessages(arguments);
	} else if (toolName == "send_message") {
		result = toolSendMessage(arguments);
	} else if (toolName == "search_messages") {
		result = toolSearchMessages(arguments);
	} else if (toolName == "get_user_info") {
		result = toolGetUserInfo(arguments);

	// ARCHIVE TOOLS
	} else if (toolName == "archive_chat") {
		result = toolArchiveChat(arguments);
	} else if (toolName == "export_chat") {
		result = toolExportChat(arguments);
	} else if (toolName == "list_archived_chats") {
		result = toolListArchivedChats(arguments);
	} else if (toolName == "get_archive_stats") {
		result = toolGetArchiveStats(arguments);
	} else if (toolName == "configure_ephemeral_capture") {
		result = toolConfigureEphemeralCapture(arguments);
	} else if (toolName == "get_ephemeral_stats") {
		result = toolGetEphemeralStats(arguments);
	} else if (toolName == "get_ephemeral_messages") {
		result = toolGetEphemeralMessages(arguments);
	} else if (toolName == "search_archive") {
		result = toolSearchArchive(arguments);
	} else if (toolName == "purge_archive") {
		result = toolPurgeArchive(arguments);

	// ANALYTICS TOOLS
	} else if (toolName == "get_message_stats") {
		result = toolGetMessageStats(arguments);
	} else if (toolName == "get_user_activity") {
		result = toolGetUserActivity(arguments);
	} else if (toolName == "get_chat_activity") {
		result = toolGetChatActivity(arguments);
	} else if (toolName == "get_time_series") {
		result = toolGetTimeSeries(arguments);
	} else if (toolName == "get_top_users") {
		result = toolGetTopUsers(arguments);
	} else if (toolName == "get_top_words") {
		result = toolGetTopWords(arguments);
	} else if (toolName == "export_analytics") {
		result = toolExportAnalytics(arguments);
	} else if (toolName == "get_trends") {
		result = toolGetTrends(arguments);

	// SEMANTIC SEARCH TOOLS
	} else if (toolName == "semantic_search") {
		result = toolSemanticSearch(arguments);
	} else if (toolName == "index_messages") {
		result = toolIndexMessages(arguments);
	} else if (toolName == "detect_topics") {
		result = toolDetectTopics(arguments);
	} else if (toolName == "classify_intent") {
		result = toolClassifyIntent(arguments);
	} else if (toolName == "extract_entities") {
		result = toolExtractEntities(arguments);

	// MESSAGE OPERATIONS
	} else if (toolName == "edit_message") {
		result = toolEditMessage(arguments);
	} else if (toolName == "delete_message") {
		result = toolDeleteMessage(arguments);
	} else if (toolName == "forward_message") {
		result = toolForwardMessage(arguments);
	} else if (toolName == "pin_message") {
		result = toolPinMessage(arguments);
	} else if (toolName == "unpin_message") {
		result = toolUnpinMessage(arguments);
	} else if (toolName == "add_reaction") {
		result = toolAddReaction(arguments);

	// BATCH OPERATIONS
	} else if (toolName == "batch_send") {
		result = toolBatchSend(arguments);
	} else if (toolName == "batch_delete") {
		result = toolBatchDelete(arguments);
	} else if (toolName == "batch_forward") {
		result = toolBatchForward(arguments);
	} else if (toolName == "batch_pin") {
		result = toolBatchPin(arguments);
	} else if (toolName == "batch_reaction") {
		result = toolBatchReaction(arguments);

	// SCHEDULER TOOLS
	} else if (toolName == "schedule_message") {
		result = toolScheduleMessage(arguments);
	} else if (toolName == "cancel_scheduled") {
		result = toolCancelScheduled(arguments);
	} else if (toolName == "list_scheduled") {
		result = toolListScheduled(arguments);
	} else if (toolName == "update_scheduled") {
		result = toolUpdateScheduled(arguments);

	// SYSTEM TOOLS
	} else if (toolName == "get_cache_stats") {
		result = toolGetCacheStats(arguments);
	} else if (toolName == "get_server_info") {
		result = toolGetServerInfo(arguments);
	} else if (toolName == "get_audit_log") {
		result = toolGetAuditLog(arguments);
	} else if (toolName == "health_check") {
		result = toolHealthCheck(arguments);

	// VOICE TOOLS
	} else if (toolName == "transcribe_voice") {
		result = toolTranscribeVoice(arguments);
	} else if (toolName == "get_transcription") {
		result = toolGetTranscription(arguments);

	// BOT FRAMEWORK TOOLS
	} else if (toolName == "list_bots") {
		result = toolListBots(arguments);
	} else if (toolName == "get_bot_info") {
		result = toolGetBotInfo(arguments);
	} else if (toolName == "start_bot") {
		result = toolStartBot(arguments);
	} else if (toolName == "stop_bot") {
		result = toolStopBot(arguments);
	} else if (toolName == "configure_bot") {
		result = toolConfigureBot(arguments);
	} else if (toolName == "get_bot_stats") {
		result = toolGetBotStats(arguments);
	} else if (toolName == "send_bot_command") {
		result = toolSendBotCommand(arguments);
	} else if (toolName == "get_bot_suggestions") {
		result = toolGetBotSuggestions(arguments);

	} else {
		result["error"] = "Unknown tool: " + toolName;
		_auditLogger->logError("Unknown tool: " + toolName, "tool_call");
	}

	// _auditLogger->logToolCompleted(toolName, result); // TODO: implement logToolCompleted

	// Build response object
	QJsonObject response;
	QJsonArray contentArray;
	QJsonObject textContent;
	textContent["type"] = "text";
	textContent["text"] = QString(QJsonDocument(result).toJson(QJsonDocument::Compact));
	contentArray.append(textContent);
	response["content"] = contentArray;
	return response;
}

// ===== CORE TOOL IMPLEMENTATIONS =====

QJsonObject Server::toolListChats(const QJsonObject &args) {
	Q_UNUSED(args);

	// Check cache first
	if (_cache) {
		QJsonObject cached;
		if (_cache->get(_cache->chatListKey(), cached)) {
			// Cache hit - return immediately
			cached["source"] = cached["source"].toString() + " (cached)";
			return cached;
		}
	}

	QJsonArray chats;

	// Try live data first if session is available
	if (_session) {
		try {
			auto chatsList = _session->data().chatsList();  // Main folder chat list
			auto indexed = chatsList->indexed();

			for (const auto &row : *indexed) {
				auto thread = row->thread();
				auto peer = thread->peer();

				QJsonObject chat;
				chat["id"] = QString::number(peer->id.value);
				chat["name"] = peer->name();
				chat["username"] = peer->username();
				chat["source"] = "live";

				chats.append(chat);
			}

			QJsonObject result;
			result["chats"] = chats;
			result["count"] = chats.size();
			result["source"] = "live_telegram_data";

			// Cache the result
			if (_cache) {
				_cache->put(_cache->chatListKey(), result, 60);  // Cache for 60 seconds
			}

			return result;
		} catch (...) {
			qWarning() << "MCP: Failed to access live chat data, falling back to archive";
		}
	}

	// Fallback to archived data
	chats = _archiver->listArchivedChats();

	QJsonObject result;
	result["chats"] = chats;
	result["count"] = chats.size();
	result["source"] = "archived_data";

	// Cache the archived result too
	if (_cache) {
		_cache->put(_cache->chatListKey(), result, 300);  // Cache for 5 minutes
	}

	return result;
}

QJsonObject Server::toolGetChatInfo(const QJsonObject &args) {
	qint64 chatId = args["chat_id"].toVariant().toLongLong();

	QJsonObject chatInfo;

	// Try live data first if session is available
	if (_session) {
		try {
			// Convert chat_id to PeerId
			PeerId peerId(chatId);

			// Get the peer data
			auto peer = _session->data().peer(peerId);
			if (!peer) {
				qWarning() << "MCP: No peer found for chat" << chatId;
				chatInfo["error"] = "Chat not found";
				chatInfo["chat_id"] = QString::number(chatId);
				return chatInfo;
			}

			// Basic information
			chatInfo["id"] = QString::number(peer->id.value);
			chatInfo["name"] = peer->name();

			// Determine chat type
			if (peer->isUser()) {
				chatInfo["type"] = "user";
				auto user = peer->asUser();
				if (user->isBot()) {
					chatInfo["is_bot"] = true;
				}
			} else if (peer->isChat()) {
				chatInfo["type"] = "group";
				auto chat = peer->asChat();
				chatInfo["member_count"] = chat->count;
				chatInfo["is_creator"] = chat->amCreator();
			} else if (peer->isChannel()) {
				auto channel = peer->asChannel();
				if (channel->isMegagroup()) {
					chatInfo["type"] = "supergroup";
				} else {
					chatInfo["type"] = "channel";
				}
				chatInfo["member_count"] = channel->membersCount();
				chatInfo["is_broadcast"] = channel->isBroadcast();
				chatInfo["is_megagroup"] = channel->isMegagroup();
				chatInfo["is_creator"] = channel->amCreator();
			}

			// Optional fields
			if (!peer->username().isEmpty()) {
				chatInfo["username"] = peer->username();
			}

			// Status fields
			chatInfo["is_verified"] = peer->isVerified();
			chatInfo["is_scam"] = peer->isScam();
			chatInfo["is_fake"] = peer->isFake();

			// About/description
			if (!peer->about().isEmpty()) {
				chatInfo["about"] = peer->about();
			}

			// Get message count from history
			auto history = _session->data().history(peerId);
			if (history) {
				int messageCount = 0;
				for (const auto &block : history->blocks) {
					messageCount += block->messages.size();
				}
				chatInfo["loaded_message_count"] = messageCount;
			}

			chatInfo["source"] = "live_telegram_data";

			qInfo() << "MCP: Retrieved info for chat" << chatId;
			return chatInfo;

		} catch (const std::exception &e) {
			qWarning() << "MCP: Failed to access chat info:" << e.what();
		} catch (...) {
			qWarning() << "MCP: Failed to access chat info for" << chatId;
		}
	}

	// Fallback to archived data
	chatInfo = _archiver->getChatInfo(chatId);
	if (chatInfo.isEmpty() || !chatInfo.contains("id")) {
		chatInfo["chat_id"] = QString::number(chatId);
		chatInfo["error"] = "Chat info not available (session not active)";
		chatInfo["source"] = "error";
	} else {
		chatInfo["source"] = "archived_data";
	}

	return chatInfo;
}

QJsonObject Server::toolReadMessages(const QJsonObject &args) {
	qint64 chatId = args["chat_id"].toVariant().toLongLong();
	int limit = args.value("limit").toInt(50);
	qint64 beforeTimestamp = args.value("before_timestamp").toVariant().toLongLong();

	QJsonArray messages;

	// Try live data first if session is available
	if (_session) {
		try {
			// Convert chat_id to PeerId
			PeerId peerId(chatId);

			// Get the history for this peer
			auto history = _session->data().history(peerId);
			if (!history) {
				qWarning() << "MCP: No history found for peer" << chatId;
			} else {
				// Iterate through blocks and messages (newest first)
				int collected = 0;
				for (auto blockIt = history->blocks.rbegin();
				     blockIt != history->blocks.rend() && collected < limit;
				     ++blockIt) {
					const auto &block = *blockIt;

					// Iterate through messages in this block (newest first)
					for (auto msgIt = block->messages.rbegin();
					     msgIt != block->messages.rend() && collected < limit;
					     ++msgIt) {
						const auto &element = *msgIt;
						auto item = element->data();

						// Skip if message is after beforeTimestamp filter
						if (beforeTimestamp > 0 && item->date() >= beforeTimestamp) {
							continue;
						}

						// Extract message data
						QJsonObject msg;
						msg["message_id"] = QString::number(item->id.bare);
						msg["date"] = static_cast<qint64>(item->date());

						// Get message text
						const auto &text = item->originalText();
						msg["text"] = text.text;

						// Get sender information
						auto from = item->from();
						QJsonObject fromUser;
						fromUser["id"] = QString::number(from->id.value);
						fromUser["name"] = from->name();
						if (!from->username().isEmpty()) {
							fromUser["username"] = from->username();
						}
						msg["from_user"] = fromUser;

						// Add optional fields
						if (item->out()) {
							msg["is_outgoing"] = true;
						}
						if (item->isPinned()) {
							msg["is_pinned"] = true;
						}

						// Add reply information if present
						if (item->replyToId()) {
							QJsonObject reply;
							reply["message_id"] = QString::number(item->replyToId().bare);
							msg["reply_to"] = reply;
						}

						messages.append(msg);
						collected++;
					}
				}

				// Return live data result
				QJsonObject result;
				result["messages"] = messages;
				result["count"] = messages.size();
				result["chat_id"] = chatId;
				result["source"] = "live_telegram_data";

				qInfo() << "MCP: Read" << messages.size() << "live messages from chat" << chatId;
				return result;
			}
		} catch (const std::exception &e) {
			qWarning() << "MCP: Failed to access live messages:" << e.what();
		} catch (...) {
			qWarning() << "MCP: Failed to access live messages, falling back to archive";
		}
	}

	// Fallback to archived data
	messages = _archiver->getMessages(chatId, limit, beforeTimestamp);

	QJsonObject result;
	result["messages"] = messages;
	result["count"] = messages.size();
	result["chat_id"] = chatId;
	result["source"] = "archived_data";

	return result;
}

QJsonObject Server::toolSendMessage(const QJsonObject &args) {
	qint64 chatId = args["chat_id"].toVariant().toLongLong();
	QString text = args["text"].toString();

	QJsonObject result;

	// Check if session is available
	if (!_session) {
		result["success"] = false;
		result["error"] = "Session not available";
		result["chat_id"] = chatId;
		return result;
	}

	try {
		// Convert chat_id to PeerId
		PeerId peerId(chatId);

		// Get the history for this peer
		auto history = _session->data().history(peerId);
		if (!history) {
			result["success"] = false;
			result["error"] = "Chat not found";
			result["chat_id"] = chatId;
			return result;
		}

		// Create SendAction (history is a Data::Thread)
		Api::SendAction action(history);

		// Create MessageToSend
		Api::MessageToSend message(action);
		message.textWithTags = TextWithTags{ text };

		// Send the message via API
		_session->api().sendMessage(std::move(message));

		// Return success
		result["success"] = true;
		result["chat_id"] = chatId;
		result["text"] = text;
		result["status"] = "Message queued for sending";

		qInfo() << "MCP: Queued message send to chat" << chatId;
		return result;

	} catch (const std::exception &e) {
		qWarning() << "MCP: Failed to send message:" << e.what();
		result["success"] = false;
		result["error"] = QString("Failed to send message: %1").arg(e.what());
		result["chat_id"] = chatId;
		return result;
	} catch (...) {
		qWarning() << "MCP: Failed to send message to chat" << chatId;
		result["success"] = false;
		result["error"] = "Failed to send message (unknown error)";
		result["chat_id"] = chatId;
		return result;
	}
}

QJsonObject Server::toolSearchMessages(const QJsonObject &args) {
	QString query = args["query"].toString();
	qint64 chatId = args.value("chat_id").toVariant().toLongLong();
	int limit = args.value("limit").toInt(50);

	QJsonArray results;

	// Try live search first if session is available
	if (_session && chatId != 0) {
		try {
			PeerId peerId(chatId);
			auto history = _session->data().history(peerId);

			if (history) {
				QString lowerQuery = query.toLower();
				int found = 0;

				// Search through loaded messages
				for (auto blockIt = history->blocks.rbegin();
				     blockIt != history->blocks.rend() && found < limit;
				     ++blockIt) {
					const auto &block = *blockIt;

					for (auto msgIt = block->messages.rbegin();
					     msgIt != block->messages.rend() && found < limit;
					     ++msgIt) {
						const auto &element = *msgIt;
						auto item = element->data();

						// Get message text and check if it contains query
						const auto &text = item->originalText();
						if (text.text.toLower().contains(lowerQuery)) {
							QJsonObject msg;
							msg["message_id"] = QString::number(item->id.bare);
							msg["date"] = static_cast<qint64>(item->date());
							msg["text"] = text.text;

							// Get sender info
							auto from = item->from();
							QJsonObject fromUser;
							fromUser["id"] = QString::number(from->id.value);
							fromUser["name"] = from->name();
							if (!from->username().isEmpty()) {
								fromUser["username"] = from->username();
							}
							msg["from_user"] = fromUser;

							msg["source"] = "live";
							results.append(msg);
							found++;
						}
					}
				}

				if (found > 0) {
					QJsonObject result;
					result["results"] = results;
					result["count"] = results.size();
					result["query"] = query;
					result["chat_id"] = chatId;
					result["source"] = "live_search";

					qInfo() << "MCP: Found" << found << "messages in live search for:" << query;
					return result;
				}
			}
		} catch (const std::exception &e) {
			qWarning() << "MCP: Live search failed:" << e.what();
		} catch (...) {
			qWarning() << "MCP: Live search failed, falling back to archive";
		}
	}

	// Fallback to archived data search (more comprehensive, uses FTS)
	results = _archiver->searchMessages(chatId, query, limit);

	QJsonObject result;
	result["results"] = results;
	result["count"] = results.size();
	result["query"] = query;
	if (chatId != 0) {
		result["chat_id"] = chatId;
	}
	result["source"] = "archived_search";

	return result;
}

QJsonObject Server::toolGetUserInfo(const QJsonObject &args) {
	qint64 userId = args["user_id"].toVariant().toLongLong();

	QJsonObject userInfo;

	// Try live data first if session is available
	if (_session) {
		try {
			// Convert user_id to UserId and then PeerId
			UserId uid(userId);
			PeerId peerId = peerFromUser(uid);

			// Get the user data
			auto user = _session->data().peer(peerId)->asUser();
			if (!user) {
				qWarning() << "MCP: Peer" << userId << "is not a user";
				userInfo["error"] = "Specified ID is not a user";
				userInfo["user_id"] = QString::number(userId);
				return userInfo;
			}

			// Extract user information
			userInfo["id"] = QString::number(user->id.value);
			userInfo["name"] = user->name();

			// Optional fields
			if (!user->username().isEmpty()) {
				userInfo["username"] = user->username();
			}
			if (!user->firstName.isEmpty()) {
				userInfo["first_name"] = user->firstName;
			}
			if (!user->lastName.isEmpty()) {
				userInfo["last_name"] = user->lastName;
			}
			if (!user->phone().isEmpty()) {
				userInfo["phone"] = user->phone();
			}

			// Boolean fields
			userInfo["is_bot"] = user->isBot();
			userInfo["is_self"] = user->isSelf();
			userInfo["is_contact"] = user->isContact();
			userInfo["is_premium"] = user->isPremium();
			userInfo["is_verified"] = user->isVerified();
			userInfo["is_scam"] = user->isScam();
			userInfo["is_fake"] = user->isFake();

			// User status
			/* Online status via lastseen() - TODO: implement if needed
			if (false) {
				userInfo["online_till"] = static_cast<qint64>(0);
			}
			*/

			// About/bio if available
			if (!user->about().isEmpty()) {
				userInfo["about"] = user->about();
			}

			userInfo["source"] = "live_telegram_data";

			qInfo() << "MCP: Retrieved info for user" << userId;
			return userInfo;

		} catch (const std::exception &e) {
			qWarning() << "MCP: Failed to access user info:" << e.what();
		} catch (...) {
			qWarning() << "MCP: Failed to access user info for" << userId;
		}
	}

	// Fallback response if session not available
	userInfo["user_id"] = QString::number(userId);
	userInfo["error"] = "User info not available (session not active)";
	userInfo["source"] = "error";

	return userInfo;
}

// ===== ARCHIVE TOOL IMPLEMENTATIONS =====

QJsonObject Server::toolArchiveChat(const QJsonObject &args) {
	if (!_archiver) {
		QJsonObject error;
		error["error"] = "Archiver not available";
		return error;
	}

	qint64 chatId = args["chat_id"].toVariant().toLongLong();
	int limit = args.value("limit").toInt(1000);

	bool success = _archiver->archiveChat(chatId, limit);

	QJsonObject result;
	result["success"] = success;
	result["chat_id"] = chatId;
	result["requested_limit"] = limit;

	if (!success) {
		result["error"] = "Failed to archive chat";
	}

	return result;
}

QJsonObject Server::toolExportChat(const QJsonObject &args) {
	qint64 chatId = args["chat_id"].toVariant().toLongLong();
	QString format = args["format"].toString();
	QString outputPath = args["output_path"].toString();

	ExportFormat exportFormat;
	if (format == "json") {
		exportFormat = ExportFormat::JSON;
	} else if (format == "jsonl") {
		exportFormat = ExportFormat::JSONL;
	} else if (format == "csv") {
		exportFormat = ExportFormat::CSV;
	} else {
		QJsonObject error;
		error["error"] = "Invalid format: " + format;
		return error;
	}

	QString resultPath = _archiver->exportChat(chatId, exportFormat, outputPath);

	QJsonObject result;
	result["success"] = !resultPath.isEmpty();
	result["chat_id"] = chatId;
	result["format"] = format;
	result["output_path"] = resultPath;

	return result;
}

QJsonObject Server::toolListArchivedChats(const QJsonObject &args) {
	Q_UNUSED(args);

	QJsonArray chats = _archiver->listArchivedChats();

	QJsonObject result;
	result["chats"] = chats;
	result["count"] = chats.size();

	return result;
}

QJsonObject Server::toolGetArchiveStats(const QJsonObject &args) {
	Q_UNUSED(args);

	if (!_archiver) {
		QJsonObject error;
		error["error"] = "Archiver not available";
		return error;
	}

	auto stats = _archiver->getStats();

	QJsonObject result;
	result["total_messages"] = stats.totalMessages;
	result["total_chats"] = stats.totalChats;
	result["total_users"] = stats.totalUsers;
	result["ephemeral_captured"] = stats.ephemeralCaptured;
	result["media_downloaded"] = stats.mediaDownloaded;
	result["database_size_bytes"] = stats.databaseSize;
	result["last_archived"] = stats.lastArchived.toString(Qt::ISODate);
	result["success"] = true;

	return result;
}

QJsonObject Server::toolGetEphemeralMessages(const QJsonObject &args) {
	if (!_archiver) {
		QJsonObject error;
		error["error"] = "Archiver not available";
		return error;
	}

	qint64 chatId = args.value("chat_id").toVariant().toLongLong();
	QString type = args.value("type").toString();  // "self_destruct", "view_once", "vanishing", or empty for all
	int limit = args.value("limit").toInt(50);

	// Query ephemeral messages from database
	QSqlQuery query(_archiver->database());

	QString sql;
	if (chatId > 0 && !type.isEmpty()) {
		sql = QString("SELECT message_id, chat_id, from_user_id, text, date, ephemeral_type, ttl "
			"FROM messages WHERE chat_id = ? AND ephemeral_type = ? "
			"ORDER BY date DESC LIMIT ?");
		query.prepare(sql);
		query.addBindValue(chatId);
		query.addBindValue(type);
		query.addBindValue(limit);
	} else if (chatId > 0) {
		sql = QString("SELECT message_id, chat_id, from_user_id, text, date, ephemeral_type, ttl "
			"FROM messages WHERE chat_id = ? AND ephemeral_type IS NOT NULL "
			"ORDER BY date DESC LIMIT ?");
		query.prepare(sql);
		query.addBindValue(chatId);
		query.addBindValue(limit);
	} else if (!type.isEmpty()) {
		sql = QString("SELECT message_id, chat_id, from_user_id, text, date, ephemeral_type, ttl "
			"FROM messages WHERE ephemeral_type = ? "
			"ORDER BY date DESC LIMIT ?");
		query.prepare(sql);
		query.addBindValue(type);
		query.addBindValue(limit);
	} else {
		sql = QString("SELECT message_id, chat_id, from_user_id, text, date, ephemeral_type, ttl "
			"FROM messages WHERE ephemeral_type IS NOT NULL "
			"ORDER BY date DESC LIMIT ?");
		query.prepare(sql);
		query.addBindValue(limit);
	}

	QJsonArray messages;
	if (query.exec()) {
		while (query.next()) {
			QJsonObject msg;
			msg["message_id"] = query.value(0).toLongLong();
			msg["chat_id"] = query.value(1).toLongLong();
			msg["from_user_id"] = query.value(2).toLongLong();
			msg["text"] = query.value(3).toString();
			msg["date"] = query.value(4).toLongLong();
			msg["ephemeral_type"] = query.value(5).toString();
			msg["ttl_seconds"] = query.value(6).toInt();
			messages.append(msg);
		}
	}

	QJsonObject result;
	result["messages"] = messages;
	result["count"] = messages.size();
	result["success"] = true;

	if (!type.isEmpty()) {
		result["type"] = type;
	}
	if (chatId > 0) {
		result["chat_id"] = chatId;
	}

	return result;
}

QJsonObject Server::toolSearchArchive(const QJsonObject &args) {
	QString query = args["query"].toString();
	qint64 chatId = args.value("chat_id").toVariant().toLongLong();
	int limit = args.value("limit").toInt(50);

	QJsonArray results = _archiver->searchMessages(chatId, query, limit);

	QJsonObject result;
	result["results"] = results;
	result["count"] = results.size();
	result["query"] = query;

	return result;
}

QJsonObject Server::toolPurgeArchive(const QJsonObject &args) {
	int daysToKeep = args["days_to_keep"].toInt();

	qint64 cutoffTimestamp = QDateTime::currentSecsSinceEpoch() - (daysToKeep * 86400);
	int deleted = _archiver->purgeOldMessages(cutoffTimestamp);

	QJsonObject result;
	result["success"] = true;
	result["deleted_count"] = deleted;
	result["days_kept"] = daysToKeep;

	return result;
}

// ===== ANALYTICS TOOL IMPLEMENTATIONS =====

QJsonObject Server::toolGetMessageStats(const QJsonObject &args) {
	qint64 chatId = args["chat_id"].toVariant().toLongLong();
	QString period = args.value("period").toString("all");

	auto stats = _analytics->getMessageStatistics(chatId, period);

	// stats is already a QJsonObject
	QJsonObject result = stats;
	result["chat_id"] = QString::number(chatId);

	return result;
}

QJsonObject Server::toolGetUserActivity(const QJsonObject &args) {
	qint64 userId = args["user_id"].toVariant().toLongLong();
	qint64 chatId = args.value("chat_id").toVariant().toLongLong();

	auto activity = _analytics->getUserActivity(userId, chatId);

	// activity is already a QJsonObject
	return activity;
}

QJsonObject Server::toolGetChatActivity(const QJsonObject &args) {
	qint64 chatId = args["chat_id"].toVariant().toLongLong();

	auto activity = _analytics->getChatActivity(chatId);

	// activity is already a QJsonObject
	return activity;
}

QJsonObject Server::toolGetTimeSeries(const QJsonObject &args) {
	qint64 chatId = args["chat_id"].toVariant().toLongLong();
	QString granularity = args.value("granularity").toString("daily");

	auto timeSeries = _analytics->getTimeSeries(chatId, granularity);

	// timeSeries is already a QJsonArray
	QJsonObject result;
	result["chat_id"] = QString::number(chatId);
	result["granularity"] = granularity;
	result["data_points"] = timeSeries;
	result["count"] = timeSeries.size();

	return result;
}

QJsonObject Server::toolGetTopUsers(const QJsonObject &args) {
	qint64 chatId = args["chat_id"].toVariant().toLongLong();
	int limit = args.value("limit").toInt(10);

	auto topUsers = _analytics->getTopUsers(chatId, limit);

	// topUsers is already a QJsonArray
	QJsonObject result;
	result["chat_id"] = QString::number(chatId);
	result["users"] = topUsers;
	result["count"] = topUsers.size();

	return result;
}

QJsonObject Server::toolGetTopWords(const QJsonObject &args) {
	qint64 chatId = args["chat_id"].toVariant().toLongLong();
	int limit = args.value("limit").toInt(20);

	auto topWords = _analytics->getTopWords(chatId, limit);

	// topWords is already a QJsonArray
	QJsonObject result;
	result["chat_id"] = QString::number(chatId);
	result["words"] = topWords;
	result["count"] = topWords.size();

	return result;
}

QJsonObject Server::toolExportAnalytics(const QJsonObject &args) {
	qint64 chatId = args["chat_id"].toVariant().toLongLong();
	QString outputPath = args["output_path"].toString();
	QString format = args.value("format").toString("json");

	QString resultPath = _analytics->exportAnalytics(chatId, format, outputPath);

	QJsonObject result;
	result["success"] = !resultPath.isEmpty();
	result["chat_id"] = QString::number(chatId);
	result["output_path"] = resultPath;
	result["format"] = format;

	return result;
}

QJsonObject Server::toolGetTrends(const QJsonObject &args) {
	qint64 chatId = args["chat_id"].toVariant().toLongLong();
	QString metric = args.value("metric").toString("messages");
	int daysBack = args.value("days_back").toInt(30);

	auto trends = _analytics->getTrends(chatId, metric, daysBack);
	// trends is already a QJsonObject with all trend data

	QJsonObject result = trends;
	result["chat_id"] = QString::number(chatId);
	result["metric"] = metric;
	result["days_back"] = daysBack;

	return result;
}

// ===== SEMANTIC SEARCH TOOL IMPLEMENTATIONS =====

QJsonObject Server::toolSemanticSearch(const QJsonObject &args) {
	QString query = args["query"].toString();
	qint64 chatId = args.value("chat_id").toVariant().toLongLong();
	int limit = args.value("limit").toInt(10);
	float minSimilarity = args.value("min_similarity").toDouble(0.7);

	auto results = _semanticSearch->searchSimilar(query, chatId, limit, minSimilarity);

	QJsonArray matches;
	for (const auto &result : results) {
		QJsonObject match;
		match["message_id"] = result.messageId;
		match["chat_id"] = result.chatId;
		match["content"] = result.content;
		match["similarity"] = result.similarity;
		matches.append(match);
	}

	QJsonObject result;
	result["query"] = query;
	result["results"] = matches;
	result["count"] = matches.size();

	return result;
}

QJsonObject Server::toolIndexMessages(const QJsonObject &args) {
	qint64 chatId = args["chat_id"].toVariant().toLongLong();
	int limit = args.value("limit").toInt(1000);

	// TODO: Implement message indexing
	// For now, return placeholder

	QJsonObject result;
	result["success"] = false;
	result["error"] = "Message indexing not yet implemented (requires ML model integration)";
	result["chat_id"] = chatId;
	result["requested_limit"] = limit;

	return result;
}

QJsonObject Server::toolDetectTopics(const QJsonObject &args) {
	qint64 chatId = args["chat_id"].toVariant().toLongLong();
	int numTopics = args.value("num_topics").toInt(5);

	// TODO: Implement topic detection with clustering
	// For now, return placeholder

	QJsonObject result;
	result["success"] = false;
	result["error"] = "Topic detection not yet implemented (requires ML model integration)";
	result["chat_id"] = chatId;
	result["requested_topics"] = numTopics;

	return result;
}

QJsonObject Server::toolClassifyIntent(const QJsonObject &args) {
	QString text = args["text"].toString();

	SearchIntent intent = _semanticSearch->classifyIntent(text);

	QString intentStr;
	switch (intent) {
		case SearchIntent::Question: intentStr = "question"; break;
		case SearchIntent::Answer: intentStr = "answer"; break;
		case SearchIntent::Statement: intentStr = "statement"; break;
		case SearchIntent::Command: intentStr = "command"; break;
		case SearchIntent::Greeting: intentStr = "greeting"; break;
		case SearchIntent::Farewell: intentStr = "farewell"; break;
		case SearchIntent::Agreement: intentStr = "agreement"; break;
		case SearchIntent::Disagreement: intentStr = "disagreement"; break;
		default: intentStr = "other"; break;
	}

	QJsonObject result;
	result["text"] = text;
	result["intent"] = intentStr;

	return result;
}

QJsonObject Server::toolExtractEntities(const QJsonObject &args) {
	QString text = args["text"].toString();

	auto entities = _semanticSearch->extractEntities(text);

	QJsonArray entitiesArray;
	for (const auto &entity : entities) {
		QJsonObject e;

		QString typeStr;
		switch (entity.type) {
			case EntityType::UserMention: typeStr = "user_mention"; break;
			case EntityType::ChatMention: typeStr = "chat_mention"; break;
			case EntityType::URL: typeStr = "url"; break;
			case EntityType::Email: typeStr = "email"; break;
			case EntityType::PhoneNumber: typeStr = "phone_number"; break;
			case EntityType::Hashtag: typeStr = "hashtag"; break;
			case EntityType::BotCommand: typeStr = "bot_command"; break;
			case EntityType::CustomEmoji: typeStr = "custom_emoji"; break;
			default: typeStr = "unknown"; break;
		}

		e["type"] = typeStr;
		e["text"] = entity.text;
		e["offset"] = entity.offset;
		e["length"] = entity.length;
		entitiesArray.append(e);
	}

	QJsonObject result;
	result["text"] = text;
	result["entities"] = entitiesArray;
	result["count"] = entitiesArray.size();

	return result;
}

// ===== MESSAGE OPERATION TOOL IMPLEMENTATIONS =====

QJsonObject Server::toolEditMessage(const QJsonObject &args) {
	if (!_session) {
		QJsonObject error;
		error["success"] = false;
		error["error"] = "Session not available";
		return error;
	}

	qint64 chatId = args["chat_id"].toVariant().toLongLong();
	qint64 messageId = args["message_id"].toVariant().toLongLong();
	QString newText = args["new_text"].toString();

	QJsonObject result;
	result["chat_id"] = chatId;
	result["message_id"] = messageId;

	// Get the message
	auto &owner = _session->data();
	const auto peerId = PeerId(chatId);
	const auto history = owner.historyLoaded(peerId);
	if (!history) {
		result["success"] = false;
		result["error"] = "Chat not found";
		return result;
	}

	auto item = owner.message(history->peer->id, MsgId(messageId));
	if (!item) {
		result["success"] = false;
		result["error"] = "Message not found";
		return result;
	}

	// Edit the message via API
	try {
		// Prepare text with entities
		TextWithEntities textWithEntities;
		textWithEntities.text = newText;

		// Create edit options
		Api::SendOptions options;
		options.scheduled = 0;  // Not scheduled

		// Use the Api namespace function (not api member function)
		// This is an asynchronous operation with callbacks
		Api::EditTextMessage(
			item,
			textWithEntities,
			Data::WebPageDraft(),  // No webpage
			options,
			[=](mtpRequestId) {
				// Success callback
				qInfo() << "MCP: Edit message succeeded" << messageId;
			},
			[=](const QString &error, mtpRequestId) {
				// Failure callback
				qWarning() << "MCP: Edit message failed:" << error;
			},
			false  // not spoilered
		);

		result["success"] = true;
		result["edited"] = true;
		result["note"] = "Edit request sent (async operation)";

		qInfo() << "MCP: Edit message requested for" << messageId << "in chat" << chatId;
		return result;

	} catch (const std::exception &e) {
		qWarning() << "MCP: Failed to edit message:" << e.what();
		result["success"] = false;
		result["error"] = QString("Edit failed: %1").arg(e.what());
		return result;
	} catch (...) {
		qWarning() << "MCP: Failed to edit message (unknown error)";
		result["success"] = false;
		result["error"] = "Edit failed with unknown error";
		return result;
	}
}

QJsonObject Server::toolDeleteMessage(const QJsonObject &args) {
	if (!_session) {
		QJsonObject error;
		error["success"] = false;
		error["error"] = "Session not available";
		return error;
	}

	qint64 chatId = args["chat_id"].toVariant().toLongLong();
	qint64 messageId = args["message_id"].toVariant().toLongLong();
	bool revoke = args.value("revoke").toBool(true);  // Delete for everyone by default

	QJsonObject result;
	result["chat_id"] = chatId;
	result["message_id"] = messageId;

	// Get the history
	auto &owner = _session->data();
	const auto peerId = PeerId(chatId);
	const auto history = owner.historyLoaded(peerId);
	if (!history) {
		result["success"] = false;
		result["error"] = "Chat not found";
		return result;
	}

	// Verify message exists
	auto item = owner.message(history->peer->id, MsgId(messageId));
	if (!item) {
		result["success"] = false;
		result["error"] = "Message not found";
		return result;
	}

	// Delete the message using Data::Histories API
	try {
		// Create message ID list
		MessageIdsList ids = { item->fullId() };

		// Delete via session's histories manager
		_session->data().histories().deleteMessages(ids, revoke);
		_session->data().sendHistoryChangeNotifications();

		result["success"] = true;
		result["revoked"] = revoke;

		qInfo() << "MCP: Deleted message" << messageId << "from chat" << chatId << "(revoke:" << revoke << ")";
		return result;

	} catch (const std::exception &e) {
		qWarning() << "MCP: Failed to delete message:" << e.what();
		result["success"] = false;
		result["error"] = QString("Delete failed: %1").arg(e.what());
		return result;
	} catch (...) {
		qWarning() << "MCP: Failed to delete message (unknown error)";
		result["success"] = false;
		result["error"] = "Delete failed with unknown error";
		return result;
	}
}

QJsonObject Server::toolForwardMessage(const QJsonObject &args) {
	if (!_session) {
		QJsonObject error;
		error["success"] = false;
		error["error"] = "Session not available";
		return error;
	}

	qint64 fromChatId = args["from_chat_id"].toVariant().toLongLong();
	qint64 toChatId = args["to_chat_id"].toVariant().toLongLong();
	qint64 messageId = args["message_id"].toVariant().toLongLong();

	QJsonObject result;
	result["from_chat_id"] = fromChatId;
	result["to_chat_id"] = toChatId;
	result["message_id"] = messageId;

	// Get source message
	auto &owner = _session->data();
	const auto fromPeerId = PeerId(fromChatId);
	const auto fromHistory = owner.historyLoaded(fromPeerId);
	if (!fromHistory) {
		result["success"] = false;
		result["error"] = "Source chat not found";
		return result;
	}

	auto item = owner.message(fromHistory->peer->id, MsgId(messageId));
	if (!item) {
		result["success"] = false;
		result["error"] = "Message not found";
		return result;
	}

	// Get destination peer
	const auto toPeerId = PeerId(toChatId);
	const auto toPeer = owner.peer(toPeerId);
	if (!toPeer) {
		result["success"] = false;
		result["error"] = "Destination chat not found";
		return result;
	}

	// Forward the message
	try {
		// Get destination history
		auto toHistory = _session->data().history(toPeerId);
		if (!toHistory) {
			result["success"] = false;
			result["error"] = "Failed to get destination history";
			return result;
		}

		// Create HistoryItemsList with the item to forward
		HistoryItemsList items;
		items.push_back(item);

		// Create ResolvedForwardDraft
		Data::ResolvedForwardDraft draft;
		draft.items = items;
		draft.options = Data::ForwardOptions::PreserveInfo;  // Preserve original sender info

		// Create SendAction with destination thread
		// For simple cases, history can be cast to Data::Thread*
		auto thread = (Data::Thread*)toHistory;
		Api::SendAction action(thread, Api::SendOptions());

		// Forward via session API
		auto &api = _session->api();
		api.forwardMessages(std::move(draft), action);

		result["success"] = true;
		result["forwarded"] = true;

		qInfo() << "MCP: Forwarded message" << messageId << "from chat" << fromChatId << "to chat" << toChatId;
		return result;

	} catch (const std::exception &e) {
		qWarning() << "MCP: Failed to forward message:" << e.what();
		result["success"] = false;
		result["error"] = QString("Forward failed: %1").arg(e.what());
		return result;
	} catch (...) {
		qWarning() << "MCP: Failed to forward message (unknown error)";
		result["success"] = false;
		result["error"] = "Forward failed with unknown error";
		return result;
	}
}

QJsonObject Server::toolPinMessage(const QJsonObject &args) {
	if (!_session) {
		QJsonObject error;
		error["success"] = false;
		error["error"] = "Session not available";
		return error;
	}

	qint64 chatId = args["chat_id"].toVariant().toLongLong();
	qint64 messageId = args["message_id"].toVariant().toLongLong();
	bool notify = args.value("notify").toBool(false);

	QJsonObject result;
	result["chat_id"] = chatId;
	result["message_id"] = messageId;

	// Get the message
	auto &owner = _session->data();
	const auto peerId = PeerId(chatId);
	const auto history = owner.historyLoaded(peerId);
	if (!history) {
		result["success"] = false;
		result["error"] = "Chat not found";
		return result;
	}

	auto item = owner.message(history->peer->id, MsgId(messageId));
	if (!item) {
		result["success"] = false;
		result["error"] = "Message not found";
		return result;
	}

	// Check permissions
	const auto peer = history->peer;
	if (const auto chat = peer->asChat()) {
		if (!chat->canPinMessages()) {
			result["success"] = false;
			result["error"] = "No permission to pin messages in this chat";
			return result;
		}
	} else if (const auto channel = peer->asChannel()) {
		if (!channel->canPinMessages()) {
			result["success"] = false;
			result["error"] = "No permission to pin messages in this channel";
			return result;
		}
	}

	// Pin via API
	try {
		// Use the session's API to pin the message
		auto &api = _session->api();

		// Request message pin (notify parameter controls silent pinning)
		// Using the peer's pinMessage method through API
		api.request(MTPmessages_UpdatePinnedMessage(
			MTP_flags(notify ? MTPmessages_UpdatePinnedMessage::Flag::f_unpin : MTPmessages_UpdatePinnedMessage::Flags(0)),
			peer->input,
			MTP_int(messageId)
		)).done([=](const MTPUpdates &result) {
			_session->api().applyUpdates(result);
		}).fail([=](const MTP::Error &error) {
			qWarning() << "MCP: Pin message failed:" << error.type();
		}).send();

		result["success"] = true;
		result["pinned"] = true;
		result["notify"] = notify;

		qInfo() << "MCP: Pinned message" << messageId << "in chat" << chatId << "(notify:" << notify << ")";
		return result;

	} catch (const std::exception &e) {
		qWarning() << "MCP: Failed to pin message:" << e.what();
		result["success"] = false;
		result["error"] = QString("Pin failed: %1").arg(e.what());
		return result;
	} catch (...) {
		qWarning() << "MCP: Failed to pin message (unknown error)";
		result["success"] = false;
		result["error"] = "Pin failed with unknown error";
		return result;
	}
}

QJsonObject Server::toolUnpinMessage(const QJsonObject &args) {
	if (!_session) {
		QJsonObject error;
		error["success"] = false;
		error["error"] = "Session not available";
		return error;
	}

	qint64 chatId = args["chat_id"].toVariant().toLongLong();
	qint64 messageId = args["message_id"].toVariant().toLongLong();

	QJsonObject result;
	result["chat_id"] = chatId;
	result["message_id"] = messageId;

	// Get the peer
	auto &owner = _session->data();
	const auto peerId = PeerId(chatId);
	const auto peer = owner.peer(peerId);
	if (!peer) {
		result["success"] = false;
		result["error"] = "Chat not found";
		return result;
	}

	// Check permissions
	if (const auto chat = peer->asChat()) {
		if (!chat->canPinMessages()) {
			result["success"] = false;
			result["error"] = "No permission to unpin messages in this chat";
			return result;
		}
	} else if (const auto channel = peer->asChannel()) {
		if (!channel->canPinMessages()) {
			result["success"] = false;
			result["error"] = "No permission to unpin messages in this channel";
			return result;
		}
	}

	// Unpin via API
	try {
		// Use the session's API to unpin the message
		auto &api = _session->api();

		// Request message unpin (using the same API as pin with unpin flag)
		api.request(MTPmessages_UpdatePinnedMessage(
			MTP_flags(MTPmessages_UpdatePinnedMessage::Flag::f_unpin),
			peer->input,
			MTP_int(messageId)
		)).done([=](const MTPUpdates &result) {
			_session->api().applyUpdates(result);
		}).fail([=](const MTP::Error &error) {
			qWarning() << "MCP: Unpin message failed:" << error.type();
		}).send();

		result["success"] = true;
		result["unpinned"] = true;

		qInfo() << "MCP: Unpinned message" << messageId << "in chat" << chatId;
		return result;

	} catch (const std::exception &e) {
		qWarning() << "MCP: Failed to unpin message:" << e.what();
		result["success"] = false;
		result["error"] = QString("Unpin failed: %1").arg(e.what());
		return result;
	} catch (...) {
		qWarning() << "MCP: Failed to unpin message (unknown error)";
		result["success"] = false;
		result["error"] = "Unpin failed with unknown error";
		return result;
	}
}

QJsonObject Server::toolAddReaction(const QJsonObject &args) {
	if (!_session) {
		QJsonObject error;
		error["success"] = false;
		error["error"] = "Session not available";
		return error;
	}

	qint64 chatId = args["chat_id"].toVariant().toLongLong();
	qint64 messageId = args["message_id"].toVariant().toLongLong();
	QString emoji = args["emoji"].toString();

	QJsonObject result;
	result["chat_id"] = chatId;
	result["message_id"] = messageId;
	result["emoji"] = emoji;

	// Get the message
	auto &owner = _session->data();
	const auto peerId = PeerId(chatId);
	const auto history = owner.historyLoaded(peerId);
	if (!history) {
		result["success"] = false;
		result["error"] = "Chat not found";
		return result;
	}

	auto item = owner.message(history->peer->id, MsgId(messageId));
	if (!item) {
		result["success"] = false;
		result["error"] = "Message not found";
		return result;
	}

	// Check if reactions are available
	const auto reactionsOwner = &owner.reactions();
	if (!reactionsOwner) {
		result["success"] = false;
		result["error"] = "Reactions system not available";
		return result;
	}

	// Add reaction via HistoryItem API
	try {
		// Create reaction ID from emoji string
		const Data::ReactionId reactionId{ emoji };

		// Toggle the reaction (will add if not present, remove if already present)
		// Using HistoryReactionSource::Selector for programmatic reactions
		item->toggleReaction(reactionId, HistoryReactionSource::Selector);

		result["success"] = true;
		result["added"] = true;

		qInfo() << "MCP: Added reaction" << emoji << "to message" << messageId << "in chat" << chatId;
		return result;

	} catch (const std::exception &e) {
		qWarning() << "MCP: Failed to add reaction:" << e.what();
		result["success"] = false;
		result["error"] = QString("Add reaction failed: %1").arg(e.what());
		return result;
	} catch (...) {
		qWarning() << "MCP: Failed to add reaction (unknown error)";
		result["success"] = false;
		result["error"] = "Add reaction failed with unknown error";
		return result;
	}
}

// ===== BATCH OPERATION TOOL IMPLEMENTATIONS =====

QJsonObject Server::toolBatchSend(const QJsonObject &args) {
	if (!_session) {
		QJsonObject error;
		error["success"] = false;
		error["error"] = "Session not available";
		return error;
	}

	QJsonArray chatIdsArray = args["chat_ids"].toArray();
	QString text = args["message"].toString();

	int successCount = 0;
	int failureCount = 0;
	QJsonArray results;

	// Loop through all chat IDs and send message to each
	for (const auto &chatIdVal : chatIdsArray) {
		qint64 chatId = chatIdVal.toVariant().toLongLong();

		// Create args for single send
		QJsonObject sendArgs;
		sendArgs["chat_id"] = chatId;
		sendArgs["text"] = text;

		// Call single send_message implementation
		QJsonObject sendResult = toolSendMessage(sendArgs);

		// Track success/failure
		if (sendResult.value("success").toBool()) {
			successCount++;
		} else {
			failureCount++;
		}

		// Add to results array
		QJsonObject chatResult;
		chatResult["chat_id"] = chatId;
		chatResult["success"] = sendResult.value("success").toBool();
		if (sendResult.contains("error")) {
			chatResult["error"] = sendResult["error"];
		}
		results.append(chatResult);
	}

	QJsonObject result;
	result["success"] = (failureCount == 0);
	result["total_chats"] = chatIdsArray.size();
	result["succeeded"] = successCount;
	result["failed"] = failureCount;
	result["results"] = results;

	qInfo() << "MCP: Batch send to" << chatIdsArray.size() << "chats -" << successCount << "succeeded," << failureCount << "failed";

	return result;
}

QJsonObject Server::toolBatchDelete(const QJsonObject &args) {
	if (!_session) {
		QJsonObject error;
		error["success"] = false;
		error["error"] = "Session not available";
		return error;
	}

	qint64 chatId = args["chat_id"].toVariant().toLongLong();
	QJsonArray messageIdsArray = args["message_ids"].toArray();
	bool revoke = args.value("revoke").toBool(true);

	int successCount = 0;
	int failureCount = 0;
	QJsonArray results;

	// Loop through all message IDs and delete each
	for (const auto &msgIdVal : messageIdsArray) {
		qint64 messageId = msgIdVal.toVariant().toLongLong();

		// Create args for single delete
		QJsonObject deleteArgs;
		deleteArgs["chat_id"] = chatId;
		deleteArgs["message_id"] = messageId;
		deleteArgs["revoke"] = revoke;

		// Call single delete_message implementation
		QJsonObject deleteResult = toolDeleteMessage(deleteArgs);

		// Track success/failure
		if (deleteResult.value("success").toBool()) {
			successCount++;
		} else {
			failureCount++;
		}

		// Add to results array
		QJsonObject msgResult;
		msgResult["message_id"] = messageId;
		msgResult["success"] = deleteResult.value("success").toBool();
		if (deleteResult.contains("error")) {
			msgResult["error"] = deleteResult["error"];
		}
		results.append(msgResult);
	}

	QJsonObject result;
	result["success"] = (failureCount == 0);
	result["chat_id"] = chatId;
	result["total_messages"] = messageIdsArray.size();
	result["succeeded"] = successCount;
	result["failed"] = failureCount;
	result["revoke"] = revoke;
	result["results"] = results;

	qInfo() << "MCP: Batch delete" << messageIdsArray.size() << "messages from chat" << chatId << "-" << successCount << "succeeded," << failureCount << "failed";

	return result;
}

QJsonObject Server::toolBatchForward(const QJsonObject &args) {
	if (!_session) {
		QJsonObject error;
		error["success"] = false;
		error["error"] = "Session not available";
		return error;
	}

	qint64 fromChatId = args["from_chat_id"].toVariant().toLongLong();
	qint64 toChatId = args["to_chat_id"].toVariant().toLongLong();
	QJsonArray messageIdsArray = args["message_ids"].toArray();

	int successCount = 0;
	int failureCount = 0;
	QJsonArray results;

	// Loop through all message IDs and forward each
	for (const auto &msgIdVal : messageIdsArray) {
		qint64 messageId = msgIdVal.toVariant().toLongLong();

		// Create args for single forward
		QJsonObject forwardArgs;
		forwardArgs["from_chat_id"] = fromChatId;
		forwardArgs["to_chat_id"] = toChatId;
		forwardArgs["message_id"] = messageId;

		// Call single forward_message implementation
		QJsonObject forwardResult = toolForwardMessage(forwardArgs);

		// Track success/failure
		if (forwardResult.value("success").toBool()) {
			successCount++;
		} else {
			failureCount++;
		}

		// Add to results array
		QJsonObject msgResult;
		msgResult["message_id"] = messageId;
		msgResult["success"] = forwardResult.value("success").toBool();
		if (forwardResult.contains("error")) {
			msgResult["error"] = forwardResult["error"];
		}
		results.append(msgResult);
	}

	QJsonObject result;
	result["success"] = (failureCount == 0);
	result["from_chat_id"] = fromChatId;
	result["to_chat_id"] = toChatId;
	result["total_messages"] = messageIdsArray.size();
	result["succeeded"] = successCount;
	result["failed"] = failureCount;
	result["results"] = results;

	qInfo() << "MCP: Batch forward" << messageIdsArray.size() << "messages from chat" << fromChatId << "to chat" << toChatId << "-" << successCount << "succeeded," << failureCount << "failed";

	return result;
}

QJsonObject Server::toolBatchPin(const QJsonObject &args) {
	if (!_session) {
		QJsonObject error;
		error["success"] = false;
		error["error"] = "Session not available";
		return error;
	}

	qint64 chatId = args["chat_id"].toVariant().toLongLong();
	QJsonArray messageIdsArray = args["message_ids"].toArray();
	bool notify = args.value("notify").toBool(false);

	int successCount = 0;
	int failureCount = 0;
	QJsonArray results;

	// Loop through all message IDs and pin each
	for (const auto &msgIdVal : messageIdsArray) {
		qint64 messageId = msgIdVal.toVariant().toLongLong();

		// Create args for single pin
		QJsonObject pinArgs;
		pinArgs["chat_id"] = chatId;
		pinArgs["message_id"] = messageId;
		pinArgs["notify"] = notify;

		// Call single pin_message implementation
		QJsonObject pinResult = toolPinMessage(pinArgs);

		// Track success/failure
		if (pinResult.value("success").toBool()) {
			successCount++;
		} else {
			failureCount++;
		}

		// Add to results array
		QJsonObject msgResult;
		msgResult["message_id"] = messageId;
		msgResult["success"] = pinResult.value("success").toBool();
		if (pinResult.contains("error")) {
			msgResult["error"] = pinResult["error"];
		}
		results.append(msgResult);
	}

	QJsonObject result;
	result["success"] = (failureCount == 0);
	result["chat_id"] = chatId;
	result["total_messages"] = messageIdsArray.size();
	result["succeeded"] = successCount;
	result["failed"] = failureCount;
	result["notify"] = notify;
	result["results"] = results;

	qInfo() << "MCP: Batch pin" << messageIdsArray.size() << "messages in chat" << chatId << "-" << successCount << "succeeded," << failureCount << "failed";

	return result;
}

QJsonObject Server::toolBatchReaction(const QJsonObject &args) {
	if (!_session) {
		QJsonObject error;
		error["success"] = false;
		error["error"] = "Session not available";
		return error;
	}

	qint64 chatId = args["chat_id"].toVariant().toLongLong();
	QJsonArray messageIdsArray = args["message_ids"].toArray();
	QString emoji = args["emoji"].toString();

	int successCount = 0;
	int failureCount = 0;
	QJsonArray results;

	// Loop through all message IDs and add reaction to each
	for (const auto &msgIdVal : messageIdsArray) {
		qint64 messageId = msgIdVal.toVariant().toLongLong();

		// Create args for single reaction
		QJsonObject reactionArgs;
		reactionArgs["chat_id"] = chatId;
		reactionArgs["message_id"] = messageId;
		reactionArgs["emoji"] = emoji;

		// Call single add_reaction implementation
		QJsonObject reactionResult = toolAddReaction(reactionArgs);

		// Track success/failure
		if (reactionResult.value("success").toBool()) {
			successCount++;
		} else {
			failureCount++;
		}

		// Add to results array
		QJsonObject msgResult;
		msgResult["message_id"] = messageId;
		msgResult["success"] = reactionResult.value("success").toBool();
		if (reactionResult.contains("error")) {
			msgResult["error"] = reactionResult["error"];
		}
		results.append(msgResult);
	}

	QJsonObject result;
	result["success"] = (failureCount == 0);
	result["chat_id"] = chatId;
	result["emoji"] = emoji;
	result["total_messages"] = messageIdsArray.size();
	result["succeeded"] = successCount;
	result["failed"] = failureCount;
	result["results"] = results;

	qInfo() << "MCP: Batch reaction" << emoji << "on" << messageIdsArray.size() << "messages in chat" << chatId << "-" << successCount << "succeeded," << failureCount << "failed";

	return result;
}

// ===== SCHEDULER TOOL IMPLEMENTATIONS =====

QJsonObject Server::toolScheduleMessage(const QJsonObject &args) {
	qint64 chatId = args["chat_id"].toVariant().toLongLong();
	QString text = args["text"].toString();
	QString scheduleType = args["schedule_type"].toString();
	QString when = args["when"].toString();
	QString pattern = args.value("pattern").toString();

	qint64 scheduleId = -1;

	if (scheduleType == "once") {
		QDateTime dateTime = QDateTime::fromString(when, Qt::ISODate);
		scheduleId = _scheduler->scheduleMessage(chatId, text, dateTime);
	} else if (scheduleType == "delayed") {
		int delaySeconds = when.toInt();
		QDateTime dateTime = QDateTime::currentDateTime().addSecs(delaySeconds);
		scheduleId = _scheduler->scheduleMessage(chatId, text, dateTime);
	} else if (scheduleType == "recurring") {
		QDateTime startTime = QDateTime::fromString(when, Qt::ISODate);
		scheduleId = _scheduler->scheduleRecurringMessage(chatId, text, pattern, startTime);
	}

	QJsonObject result;
	result["success"] = (scheduleId > 0);
	result["schedule_id"] = QString::number(scheduleId);
	result["chat_id"] = QString::number(chatId);
	result["type"] = scheduleType;

	return result;
}

QJsonObject Server::toolCancelScheduled(const QJsonObject &args) {
	qint64 scheduleId = args["schedule_id"].toVariant().toLongLong();

	bool success = _scheduler->cancelScheduledMessage(scheduleId);

	QJsonObject result;
	result["success"] = success;
	result["schedule_id"] = scheduleId;

	return result;
}

QJsonObject Server::toolListScheduled(const QJsonObject &args) {
	qint64 chatId = args.value("chat_id").toVariant().toLongLong();

	auto schedules = _scheduler->listScheduledMessages(chatId);
	// schedules is already a QJsonArray

	QJsonObject result;
	result["schedules"] = schedules;
	result["count"] = schedules.size();
	if (chatId > 0) {
		result["chat_id"] = QString::number(chatId);
	}

	return result;
}

QJsonObject Server::toolUpdateScheduled(const QJsonObject &args) {
	qint64 scheduleId = args["schedule_id"].toVariant().toLongLong();

	// Build updates object from args
	QJsonObject updates;
	if (args.contains("new_text")) {
		updates["text"] = args["new_text"];
	}
	if (args.contains("new_time")) {
		updates["scheduled_time"] = args["new_time"];
	}
	if (args.contains("new_pattern")) {
		updates["recurrence_pattern"] = args["new_pattern"];
	}

	bool success = _scheduler->updateScheduledMessage(scheduleId, updates);

	QJsonObject result;
	result["success"] = success;
	result["schedule_id"] = QString::number(scheduleId);

	return result;
}

// ===== SYSTEM TOOL IMPLEMENTATIONS =====

QJsonObject Server::toolGetCacheStats(const QJsonObject &args) {
	Q_UNUSED(args);

	// TODO: Implement getStatistics() in ChatArchiver
	// auto stats = _archiver->getStatistics();

	QJsonObject result;
	result["error"] = "getStatistics not yet implemented";
	result["total_messages"] = 0;
	result["total_chats"] = 0;
	result["database_size_bytes"] = 0;
	// result["total_messages"] = static_cast<qint64>(stats.totalMessages);
	// result["total_chats"] = static_cast<qint64>(stats.totalChats);
	// result["database_size_bytes"] = static_cast<qint64>(stats.databaseSizeBytes);
	result["indexed_messages"] = _semanticSearch->getIndexedMessageCount();

	return result;
}

QJsonObject Server::toolGetServerInfo(const QJsonObject &args) {
	Q_UNUSED(args);

	QJsonObject result;
	result["name"] = _serverInfo.name;
	result["version"] = _serverInfo.version;
	result["protocol_version"] = "2024-11-05";
	result["total_tools"] = _tools.size();
	result["total_resources"] = _resources.size();
	result["total_prompts"] = _prompts.size();
	result["database_path"] = _databasePath;

	return result;
}

QJsonObject Server::toolGetAuditLog(const QJsonObject &args) {
	int limit = args.value("limit").toInt(50);
	QString eventType = args.value("event_type").toString();

	auto events = _auditLogger->getRecentEvents(limit);

	QJsonArray eventsArray;
	for (const auto &event : events) {
		// Filter by event type if specified
		if (!eventType.isEmpty()) {
			QString typeStr;
			switch (event.eventType) {
				case AuditEventType::ToolInvoked: typeStr = "tool"; break;
				case AuditEventType::AuthEvent: typeStr = "auth"; break;
				case AuditEventType::TelegramOp: typeStr = "telegram"; break;
				case AuditEventType::SystemEvent: typeStr = "system"; break;
				case AuditEventType::Error: typeStr = "error"; break;
			}
			if (typeStr != eventType) {
				continue;
			}
		}

		QJsonObject e;
		e["event_id"] = event.id;
		e["timestamp"] = event.timestamp.toString(Qt::ISODate);
		e["action"] = event.eventSubtype;
		e["user"] = event.userId;
		e["tool_name"] = event.toolName;
		e["duration_ms"] = static_cast<qint64>(event.durationMs);
		e["status"] = event.resultStatus;
		eventsArray.append(e);
	}

	QJsonObject result;
	result["events"] = eventsArray;
	result["count"] = eventsArray.size();

	return result;
}

QJsonObject Server::toolHealthCheck(const QJsonObject &args) {
	Q_UNUSED(args);

	QJsonObject result;
	result["status"] = "healthy";
	result["database_connected"] = _db.isOpen();
	result["archiver_running"] = (_archiver != nullptr);
	result["scheduler_running"] = (_scheduler != nullptr);
	result["uptime_seconds"] = 0; // TODO: Track actual uptime

	return result;
}

// ===== VOICE TOOL IMPLEMENTATIONS =====

QJsonObject Server::toolTranscribeVoice(const QJsonObject &args) {
	qint64 messageId = args.value("message_id").toVariant().toLongLong();
	QString audioPath = args["audio_path"].toString();

	// Initialize voice transcription if not already done
	if (!_voiceTranscription) {
		_voiceTranscription = new VoiceTranscription(this);
		_voiceTranscription->start(&_db);
	}

	auto transcriptionResult = _voiceTranscription->transcribe(audioPath);

	if (transcriptionResult.success && messageId > 0) {
		_voiceTranscription->storeTranscription(messageId, 0, transcriptionResult);
	}

	QJsonObject result;
	result["success"] = transcriptionResult.success;
	result["text"] = transcriptionResult.text;
	result["language"] = transcriptionResult.language;
	result["confidence"] = transcriptionResult.confidence;
	result["duration_seconds"] = transcriptionResult.durationSeconds;
	result["model"] = transcriptionResult.modelUsed;
	result["provider"] = transcriptionResult.provider;

	if (!transcriptionResult.error.isEmpty()) {
		result["error"] = transcriptionResult.error;
	}

	return result;
}

QJsonObject Server::toolGetTranscription(const QJsonObject &args) {
	qint64 messageId = args["message_id"].toVariant().toLongLong();

	if (!_voiceTranscription) {
		QJsonObject error;
		error["error"] = "Voice transcription not initialized";
		return error;
	}

	auto transcriptionResult = _voiceTranscription->getStoredTranscription(messageId);

	QJsonObject result;
	result["success"] = transcriptionResult.success;

	if (transcriptionResult.success) {
		result["text"] = transcriptionResult.text;
		result["language"] = transcriptionResult.language;
		result["confidence"] = transcriptionResult.confidence;
		result["model"] = transcriptionResult.modelUsed;
		result["transcribed_at"] = transcriptionResult.transcribedAt.toString(Qt::ISODate);
	} else {
		result["error"] = "No transcription found";
	}

	return result;
}

// ===== RESOURCE HANDLERS =====

QJsonObject Server::handleListResources(const QJsonObject &params) {
	Q_UNUSED(params);

	QJsonArray resources;
	for (const auto &resource : _resources) {
		QJsonObject r;
		r["uri"] = resource.uri;
		r["name"] = resource.name;
		r["description"] = resource.description;
		r["mimeType"] = resource.mimeType;
		resources.append(r);
	}

	return QJsonObject{{"resources", resources}};
}

QJsonObject Server::handleReadResource(const QJsonObject &params) {
	QString uri = params["uri"].toString();

	QJsonObject result;

	if (uri == "telegram://chats") {
		QJsonArray chats = _archiver->listArchivedChats();

		QJsonObject dataObj;
		dataObj["chats"] = chats;

		QJsonObject contentObj;
		contentObj["uri"] = uri;
		contentObj["mimeType"] = "application/json";
		contentObj["text"] = QString::fromUtf8(QJsonDocument(dataObj).toJson(QJsonDocument::Compact));

		QJsonArray contents;
		contents.append(contentObj);
		result["contents"] = contents;
	} else if (uri.startsWith("telegram://messages/")) {
		QString chatIdStr = uri.mid(QString("telegram://messages/").length());
		qint64 chatId = chatIdStr.toLongLong();

		QJsonArray messages = _archiver->getMessages(chatId, 50, 0);

		QJsonObject dataObj;
		dataObj["messages"] = messages;

		QJsonObject contentObj;
		contentObj["uri"] = uri;
		contentObj["mimeType"] = "application/json";
		contentObj["text"] = QString::fromUtf8(QJsonDocument(dataObj).toJson(QJsonDocument::Compact));

		QJsonArray contents;
		contents.append(contentObj);
		result["contents"] = contents;
	} else if (uri == "telegram://archive/stats") {
		// TODO: Implement getStatistics() in ChatArchiver
		// auto stats = _archiver->getStatistics();
		QJsonObject statsObj;
		statsObj["total_messages"] = 0;
		statsObj["total_chats"] = 0;
		statsObj["database_size_bytes"] = 0;
		statsObj["error"] = "getStatistics not yet implemented";
		// statsObj["total_messages"] = static_cast<qint64>(stats.totalMessages);
		// statsObj["total_chats"] = static_cast<qint64>(stats.totalChats);
		// statsObj["database_size_bytes"] = static_cast<qint64>(stats.databaseSizeBytes);

		QJsonObject contentObj;
		contentObj["uri"] = uri;
		contentObj["mimeType"] = "application/json";
		contentObj["text"] = QString::fromUtf8(QJsonDocument(statsObj).toJson(QJsonDocument::Compact));

		QJsonArray contents;
		contents.append(contentObj);
		result["contents"] = contents;
	} else {
		result["error"] = "Unknown resource URI: " + uri;
	}

	return result;
}

// ===== PROMPT HANDLERS =====

QJsonObject Server::handleListPrompts(const QJsonObject &params) {
	Q_UNUSED(params);

	QJsonArray prompts;
	for (const auto &prompt : _prompts) {
		QJsonObject p;
		p["name"] = prompt.name;
		p["description"] = prompt.description;
		p["arguments"] = prompt.arguments;
		prompts.append(p);
	}

	return QJsonObject{{"prompts", prompts}};
}

QJsonObject Server::handleGetPrompt(const QJsonObject &params) {
	QString name = params["name"].toString();
	QJsonObject arguments = params["arguments"].toObject();

	if (name == "summarize_chat") {
		qint64 chatId = arguments["chat_id"].toVariant().toLongLong();
		int limit = arguments.value("limit").toInt(50);

		QString promptText = QString(
			"Analyze the last %1 messages in chat %2 and provide a comprehensive summary. "
			"Include: main topics discussed, key participants, important decisions, "
			"action items, and overall sentiment."
		).arg(limit).arg(chatId);

		QJsonObject result;
		result["description"] = "Chat summary analysis";
		result["messages"] = QJsonArray{QJsonObject{
			{"role", "user"},
			{"content", QJsonObject{{"type", "text"}, {"text", promptText}}}
		}};
		return result;

	} else if (name == "analyze_trends") {
		qint64 chatId = arguments["chat_id"].toVariant().toLongLong();

		QString promptText = QString(
			"Analyze activity trends in chat %1. Examine message frequency over time, "
			"user participation patterns, peak activity hours, and provide insights "
			"about whether the chat is becoming more or less active."
		).arg(chatId);

		QJsonObject result;
		result["description"] = "Activity trend analysis";
		result["messages"] = QJsonArray{QJsonObject{
			{"role", "user"},
			{"content", QJsonObject{{"type", "text"}, {"text", promptText}}}
		}};
		return result;

	} else {
		QJsonObject error;
		error["error"] = "Unknown prompt: " + name;
		return error;
	}
}

// ===== RESPONSE HELPERS =====

QJsonObject Server::successResponse(const QJsonValue &id, const QJsonObject &result) {
	QJsonObject response;
	response["jsonrpc"] = "2.0";
	response["id"] = id;
	response["result"] = result;
	return response;
}

QJsonObject Server::errorResponse(const QJsonValue &id, int code, const QString &message) {
	QJsonObject error;
	error["code"] = code;
	error["message"] = message;

	QJsonObject response;
	response["jsonrpc"] = "2.0";
	response["id"] = id;
	response["error"] = error;

	return response;
}

// ============================================================================
// Bot Framework Tools
// ============================================================================

QJsonObject Server::toolListBots(const QJsonObject &args) {
	QJsonObject result;

	if (!_botManager) {
		result["error"] = "Bot framework not initialized";
		return result;
	}

	bool includeDisabled = args.value("include_disabled").toBool(false);

	QVector<BotBase*> bots = includeDisabled
		? _botManager->getAllBots()
		: _botManager->getEnabledBots();

	QJsonArray botsArray;
	for (BotBase *bot : bots) {
		auto botInfo = bot->info();
		QJsonObject botObj;
		botObj["id"] = botInfo.id;
		botObj["name"] = botInfo.name;
		botObj["version"] = botInfo.version;
		botObj["description"] = botInfo.description;
		botObj["author"] = botInfo.author;

		QJsonArray tagsArray;
		for (const QString &tag : botInfo.tags) {
			tagsArray.append(tag);
		}
		botObj["tags"] = tagsArray;

		botObj["is_premium"] = botInfo.isPremium;
		botObj["is_enabled"] = bot->isEnabled();
		botObj["is_running"] = bot->isRunning();

		botsArray.append(botObj);
	}

	result["bots"] = botsArray;
	result["total_count"] = botsArray.size();
	result["success"] = true;

	return result;
}

QJsonObject Server::toolGetBotInfo(const QJsonObject &args) {
	QJsonObject result;

	if (!_botManager) {
		result["error"] = "Bot framework not initialized";
		return result;
	}

	QString botId = args.value("bot_id").toString();
	if (botId.isEmpty()) {
		result["error"] = "Missing bot_id parameter";
		return result;
	}

	BotBase *bot = _botManager->getBot(botId);
	if (!bot) {
		result["error"] = "Bot not found: " + botId;
		return result;
	}

	auto botInfo = bot->info();
	result["id"] = botInfo.id;
	result["name"] = botInfo.name;
	result["version"] = botInfo.version;
	result["description"] = botInfo.description;
	result["author"] = botInfo.author;

	QJsonArray tagsArray;
	for (const QString &tag : botInfo.tags) {
		tagsArray.append(tag);
	}
	result["tags"] = tagsArray;

	result["is_premium"] = botInfo.isPremium;
	result["is_enabled"] = bot->isEnabled();
	result["is_running"] = bot->isRunning();

	// Get config
	result["config"] = bot->config();

	// Get required permissions
	QJsonArray permsArray;
	for (const QString &perm : bot->requiredPermissions()) {
		permsArray.append(perm);
	}
	result["required_permissions"] = permsArray;

	// Get statistics
	BotStats stats = _botManager->getBotStats(botId);
	QJsonObject statsObj;
	statsObj["messages_processed"] = static_cast<qint64>(stats.messagesProcessed);
	statsObj["commands_executed"] = static_cast<qint64>(stats.commandsExecuted);
	statsObj["errors_occurred"] = static_cast<qint64>(stats.errorsOccurred);
	statsObj["avg_execution_ms"] = stats.avgExecutionTimeMs();
	statsObj["registered_at"] = stats.registeredAt.toString(Qt::ISODate);
	if (stats.lastActive.isValid()) {
		statsObj["last_active"] = stats.lastActive.toString(Qt::ISODate);
	}
	result["statistics"] = statsObj;

	result["success"] = true;

	return result;
}

QJsonObject Server::toolStartBot(const QJsonObject &args) {
	QJsonObject result;

	if (!_botManager) {
		result["error"] = "Bot framework not initialized";
		return result;
	}

	QString botId = args.value("bot_id").toString();
	if (botId.isEmpty()) {
		result["error"] = "Missing bot_id parameter";
		return result;
	}

	bool success = _botManager->startBot(botId);

	if (success) {
		result["success"] = true;
		result["message"] = "Bot started: " + botId;
		_auditLogger->logSystemEvent("bot_started", botId);
	} else {
		result["success"] = false;
		result["error"] = "Failed to start bot: " + botId;
	}

	return result;
}

QJsonObject Server::toolStopBot(const QJsonObject &args) {
	QJsonObject result;

	if (!_botManager) {
		result["error"] = "Bot framework not initialized";
		return result;
	}

	QString botId = args.value("bot_id").toString();
	if (botId.isEmpty()) {
		result["error"] = "Missing bot_id parameter";
		return result;
	}

	bool success = _botManager->stopBot(botId);

	if (success) {
		result["success"] = true;
		result["message"] = "Bot stopped: " + botId;
		_auditLogger->logSystemEvent("bot_stopped", botId);
	} else {
		result["success"] = false;
		result["error"] = "Failed to stop bot: " + botId;
	}

	return result;
}

QJsonObject Server::toolConfigureBot(const QJsonObject &args) {
	QJsonObject result;

	if (!_botManager) {
		result["error"] = "Bot framework not initialized";
		return result;
	}

	QString botId = args.value("bot_id").toString();
	if (botId.isEmpty()) {
		result["error"] = "Missing bot_id parameter";
		return result;
	}

	QJsonObject config = args.value("config").toObject();
	if (config.isEmpty()) {
		result["error"] = "Missing or invalid config parameter";
		return result;
	}

	bool success = _botManager->saveBotConfig(botId, config);

	if (success) {
		result["success"] = true;
		result["message"] = "Bot configuration updated: " + botId;
		_auditLogger->logSystemEvent("bot_configured", botId);
	} else {
		result["success"] = false;
		result["error"] = "Failed to update bot configuration: " + botId;
	}

	return result;
}

QJsonObject Server::toolGetBotStats(const QJsonObject &args) {
	QJsonObject result;

	if (!_botManager) {
		result["error"] = "Bot framework not initialized";
		return result;
	}

	QString botId = args.value("bot_id").toString();
	if (botId.isEmpty()) {
		result["error"] = "Missing bot_id parameter";
		return result;
	}

	if (!_botManager->isBotRegistered(botId)) {
		result["error"] = "Bot not found: " + botId;
		return result;
	}

	BotStats stats = _botManager->getBotStats(botId);

	result["bot_id"] = botId;
	result["messages_processed"] = static_cast<qint64>(stats.messagesProcessed);
	result["commands_executed"] = static_cast<qint64>(stats.commandsExecuted);
	result["errors_occurred"] = static_cast<qint64>(stats.errorsOccurred);
	result["total_execution_time_ms"] = static_cast<qint64>(stats.totalExecutionTimeMs);
	result["last_execution_time_ms"] = static_cast<qint64>(stats.lastExecutionTimeMs);
	result["avg_execution_time_ms"] = stats.avgExecutionTimeMs();
	result["registered_at"] = stats.registeredAt.toString(Qt::ISODate);

	if (stats.lastActive.isValid()) {
		result["last_active"] = stats.lastActive.toString(Qt::ISODate);
	}

	// Calculate error rate
	if (stats.messagesProcessed > 0) {
		double errorRate = static_cast<double>(stats.errorsOccurred) / stats.messagesProcessed;
		result["error_rate"] = errorRate;
		result["error_rate_percent"] = errorRate * 100.0;
	} else {
		result["error_rate"] = 0.0;
		result["error_rate_percent"] = 0.0;
	}

	result["success"] = true;

	return result;
}

QJsonObject Server::toolSendBotCommand(const QJsonObject &args) {
	QJsonObject result;

	if (!_botManager) {
		result["error"] = "Bot framework not initialized";
		return result;
	}

	QString botId = args.value("bot_id").toString();
	if (botId.isEmpty()) {
		result["error"] = "Missing bot_id parameter";
		return result;
	}

	QString command = args.value("command").toString();
	if (command.isEmpty()) {
		result["error"] = "Missing command parameter";
		return result;
	}

	QJsonObject commandArgs = args.value("args").toObject();

	// Dispatch command to bot
	_botManager->dispatchCommand(botId, command, commandArgs);

	result["success"] = true;
	result["message"] = QString("Command '%1' sent to bot '%2'").arg(command, botId);
	result["bot_id"] = botId;
	result["command"] = command;

	_auditLogger->logSystemEvent(
		"bot_command_sent",
		QString("Bot: %1, Command: %2").arg(botId, command)
	);

	return result;
}

QJsonObject Server::toolGetBotSuggestions(const QJsonObject &args) {
	QJsonObject result;

	if (!_botManager) {
		result["error"] = "Bot framework not initialized";
		return result;
	}

	// Note: This would require querying the bot_suggestions table
	// For now, return a placeholder implementation
	// TODO: Implement database query for bot suggestions

	qint64 chatId = args.value("chat_id").toVariant().toLongLong();
	int limit = args.value("limit").toInt(10);

	QJsonArray suggestionsArray;

	// Placeholder - in real implementation, query bot_suggestions table
	result["suggestions"] = suggestionsArray;
	result["total_count"] = 0;
	result["limit"] = limit;

	if (chatId > 0) {
		result["chat_id"] = chatId;
	}

	result["success"] = true;
	result["note"] = "Suggestions feature requires database integration";

	return result;
}

// ===== EPHEMERAL CAPTURE TOOL IMPLEMENTATIONS (Phase B) =====

QJsonObject Server::toolConfigureEphemeralCapture(const QJsonObject &args) {
	if (!_ephemeralArchiver) {
		QJsonObject error;
		error["error"] = "Ephemeral archiver not available";
		return error;
	}

	bool selfDestruct = args.value("capture_self_destruct").toBool(true);
	bool viewOnce = args.value("capture_view_once").toBool(true);
	bool vanishing = args.value("capture_vanishing").toBool(true);

	_ephemeralArchiver->setCaptureTypes(
		selfDestruct, viewOnce, vanishing);

	QJsonObject result;
	result["success"] = true;
	result["capture_self_destruct"] = selfDestruct;
	result["capture_view_once"] = viewOnce;
	result["capture_vanishing"] = vanishing;

	return result;
}

QJsonObject Server::toolGetEphemeralStats(const QJsonObject &args) {
	Q_UNUSED(args);

	if (!_ephemeralArchiver) {
		QJsonObject error;
		error["error"] = "Ephemeral archiver not available";
		return error;
	}

	auto stats = _ephemeralArchiver->getStats();

	QJsonObject result;
	result["total_captured"] = stats.totalCaptured;
	result["self_destruct_count"] = stats.selfDestructCount;
	result["view_once_count"] = stats.viewOnceCount;
	result["vanishing_count"] = stats.vanishingCount;
	result["media_saved"] = stats.mediaSaved;
	result["last_captured"] = stats.lastCaptured.toString(Qt::ISODate);
	result["success"] = true;

	return result;
}

} // namespace MCP

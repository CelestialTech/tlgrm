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

#include <QtCore/QTimer>
#include <QtCore/QDebug>
#include <QtCore/QDir>

namespace MCP {

Server::Server(QObject *parent)
	: QObject(parent) {
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
			"get_ephemeral_messages",
			"Get captured ephemeral messages (self-destruct, view-once)",
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

	// Initialize all components
	_archiver = new ChatArchiver(this);
	if (!_archiver->start(_databasePath)) {
		qWarning() << "MCP: Failed to start ChatArchiver";
		return false;
	}

	_ephemeralArchiver = new EphemeralArchiver(this);
	_ephemeralArchiver->start(_archiver);

	_analytics = new Analytics(_archiver, this);
	_semanticSearch = new SemanticSearch(_archiver, this);
	_semanticSearch->initialize();

	_batchOps = new BatchOperations(this);

	_scheduler = new MessageScheduler(this);
	_scheduler->start(&_db);

	_auditLogger = new AuditLogger(this);
	_auditLogger->start(&_db, QDir::home().filePath("telegram_mcp_audit.log"));

	_rbac = new RBAC(this);
	_rbac->start(&_db);

	// Start transport
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

	_auditLogger->logSystemEvent("server_start", "MCP Server started successfully");

	qInfo() << "MCP Server started (transport:"
		<< (_transport == TransportType::Stdio ? "stdio" : "http")
		<< ")";

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

void Server::startStdioTransport() {
	_stdin = new QTextStream(stdin);
	_stdout = new QTextStream(stdout);

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

		// Parse JSON-RPC request
		QJsonParseError error;
		QJsonDocument doc = QJsonDocument::fromJson(line.toUtf8(), &error);

		if (error.error != QJsonParseError::NoError) {
			qWarning() << "MCP: JSON parse error:" << error.errorString();
			return;
		}

		QJsonObject request = doc.object();
		QJsonObject response = handleRequest(request);

		// Write response to stdout
		*_stdout << QJsonDocument(response).toJson(QJsonDocument::Compact);
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

	} else {
		result["error"] = "Unknown tool: " + toolName;
		_auditLogger->logError("tool_call", "Unknown tool: " + toolName);
	}

	_auditLogger->logToolCompleted(toolName, result);
	return QJsonObject{{"content", QJsonArray{QJsonObject{{"type", "text"}, {"text", QJsonDocument(result).toJson(QJsonDocument::Compact)}}}}};
}

// ===== CORE TOOL IMPLEMENTATIONS =====

QJsonObject Server::toolListChats(const QJsonObject &args) {
	Q_UNUSED(args);

	QJsonArray chats = _archiver->getArchivedChats();

	QJsonObject result;
	result["chats"] = chats;
	result["count"] = chats.size();

	return result;
}

QJsonObject Server::toolGetChatInfo(const QJsonObject &args) {
	qint64 chatId = args["chat_id"].toVariant().toLongLong();

	QJsonObject chatInfo = _archiver->getChatInfo(chatId);

	return chatInfo;
}

QJsonObject Server::toolReadMessages(const QJsonObject &args) {
	qint64 chatId = args["chat_id"].toVariant().toLongLong();
	int limit = args.value("limit").toInt(50);
	qint64 beforeTimestamp = args.value("before_timestamp").toVariant().toLongLong();

	QJsonArray messages = _archiver->getMessages(chatId, limit, beforeTimestamp);

	QJsonObject result;
	result["messages"] = messages;
	result["count"] = messages.size();
	result["chat_id"] = chatId;

	return result;
}

QJsonObject Server::toolSendMessage(const QJsonObject &args) {
	qint64 chatId = args["chat_id"].toVariant().toLongLong();
	QString text = args["text"].toString();

	// TODO: Implement actual message sending via tdesktop API
	// For now, return placeholder

	QJsonObject result;
	result["success"] = false;
	result["error"] = "Message sending not yet implemented (requires tdesktop API integration)";
	result["chat_id"] = chatId;
	result["text"] = text;

	return result;
}

QJsonObject Server::toolSearchMessages(const QJsonObject &args) {
	QString query = args["query"].toString();
	qint64 chatId = args.value("chat_id").toVariant().toLongLong();
	int limit = args.value("limit").toInt(50);

	QJsonArray results = _archiver->searchMessages(query, chatId, limit);

	QJsonObject result;
	result["results"] = results;
	result["count"] = results.size();
	result["query"] = query;

	return result;
}

QJsonObject Server::toolGetUserInfo(const QJsonObject &args) {
	qint64 userId = args["user_id"].toVariant().toLongLong();

	QJsonObject userInfo = _archiver->getUserInfo(userId);

	return userInfo;
}

// ===== ARCHIVE TOOL IMPLEMENTATIONS =====

QJsonObject Server::toolArchiveChat(const QJsonObject &args) {
	qint64 chatId = args["chat_id"].toVariant().toLongLong();
	int limit = args.value("limit").toInt(1000);

	// TODO: Implement actual archiving from tdesktop
	// For now, return placeholder

	QJsonObject result;
	result["success"] = false;
	result["error"] = "Chat archiving not yet implemented (requires tdesktop API integration)";
	result["chat_id"] = chatId;
	result["requested_limit"] = limit;

	return result;
}

QJsonObject Server::toolExportChat(const QJsonObject &args) {
	qint64 chatId = args["chat_id"].toVariant().toLongLong();
	QString format = args["format"].toString();
	QString outputPath = args["output_path"].toString();

	ChatArchiver::ExportFormat exportFormat;
	if (format == "json") {
		exportFormat = ChatArchiver::ExportFormat::JSON;
	} else if (format == "jsonl") {
		exportFormat = ChatArchiver::ExportFormat::JSONL;
	} else if (format == "csv") {
		exportFormat = ChatArchiver::ExportFormat::CSV;
	} else {
		QJsonObject error;
		error["error"] = "Invalid format: " + format;
		return error;
	}

	bool success = _archiver->exportChat(chatId, outputPath, exportFormat);

	QJsonObject result;
	result["success"] = success;
	result["chat_id"] = chatId;
	result["format"] = format;
	result["output_path"] = outputPath;

	return result;
}

QJsonObject Server::toolListArchivedChats(const QJsonObject &args) {
	Q_UNUSED(args);

	QJsonArray chats = _archiver->getArchivedChats();

	QJsonObject result;
	result["chats"] = chats;
	result["count"] = chats.size();

	return result;
}

QJsonObject Server::toolGetArchiveStats(const QJsonObject &args) {
	Q_UNUSED(args);

	auto stats = _archiver->getStatistics();

	QJsonObject result;
	result["total_messages"] = static_cast<qint64>(stats.totalMessages);
	result["total_chats"] = static_cast<qint64>(stats.totalChats);
	result["total_users"] = static_cast<qint64>(stats.totalUsers);
	result["ephemeral_messages"] = static_cast<qint64>(stats.ephemeralMessages);
	result["database_size_bytes"] = static_cast<qint64>(stats.databaseSizeBytes);
	result["oldest_message_timestamp"] = static_cast<qint64>(stats.oldestMessageTimestamp);
	result["newest_message_timestamp"] = static_cast<qint64>(stats.newestMessageTimestamp);

	return result;
}

QJsonObject Server::toolGetEphemeralMessages(const QJsonObject &args) {
	qint64 chatId = args.value("chat_id").toVariant().toLongLong();

	QJsonArray ephemeral;
	if (chatId > 0) {
		ephemeral = _ephemeralArchiver->getEphemeralMessages(chatId);
	} else {
		ephemeral = _ephemeralArchiver->getAllEphemeralMessages();
	}

	QJsonObject result;
	result["messages"] = ephemeral;
	result["count"] = ephemeral.size();

	return result;
}

QJsonObject Server::toolSearchArchive(const QJsonObject &args) {
	QString query = args["query"].toString();
	qint64 chatId = args.value("chat_id").toVariant().toLongLong();
	int limit = args.value("limit").toInt(50);

	QJsonArray results = _archiver->searchMessages(query, chatId, limit);

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

	auto stats = _analytics->getMessageStatistics(chatId);

	QJsonObject result;
	result["chat_id"] = chatId;
	result["message_count"] = static_cast<qint64>(stats.messageCount);
	result["total_words"] = static_cast<qint64>(stats.totalWords);
	result["total_characters"] = static_cast<qint64>(stats.totalCharacters);
	result["average_message_length"] = stats.averageMessageLength;
	result["messages_per_day"] = stats.messagesPerDay;
	result["first_message_timestamp"] = static_cast<qint64>(stats.firstMessageTimestamp);
	result["last_message_timestamp"] = static_cast<qint64>(stats.lastMessageTimestamp);

	return result;
}

QJsonObject Server::toolGetUserActivity(const QJsonObject &args) {
	qint64 userId = args["user_id"].toVariant().toLongLong();
	qint64 chatId = args.value("chat_id").toVariant().toLongLong();

	auto activity = _analytics->getUserActivityAnalysis(userId, chatId);

	QJsonObject result;
	result["user_id"] = userId;
	result["total_messages"] = static_cast<qint64>(activity.totalMessages);
	result["total_words"] = static_cast<qint64>(activity.totalWords);
	result["average_message_length"] = activity.averageMessageLength;
	result["most_active_hour"] = activity.mostActiveHour;
	result["days_active"] = activity.daysActive;
	result["first_seen"] = static_cast<qint64>(activity.firstSeen);
	result["last_seen"] = static_cast<qint64>(activity.lastSeen);

	return result;
}

QJsonObject Server::toolGetChatActivity(const QJsonObject &args) {
	qint64 chatId = args["chat_id"].toVariant().toLongLong();

	auto activity = _analytics->getChatActivityAnalysis(chatId);

	QJsonObject result;
	result["chat_id"] = chatId;
	result["total_messages"] = static_cast<qint64>(activity.totalMessages);
	result["unique_users"] = static_cast<qint64>(activity.uniqueUsers);
	result["messages_per_day"] = activity.messagesPerDay;
	result["most_active_hour"] = activity.mostActiveHour;

	QString trendStr;
	switch (activity.trend) {
		case Analytics::ActivityTrend::Increasing: trendStr = "increasing"; break;
		case Analytics::ActivityTrend::Decreasing: trendStr = "decreasing"; break;
		case Analytics::ActivityTrend::Stable: trendStr = "stable"; break;
		default: trendStr = "unknown"; break;
	}
	result["trend"] = trendStr;

	result["first_message"] = static_cast<qint64>(activity.firstMessage);
	result["last_message"] = static_cast<qint64>(activity.lastMessage);

	return result;
}

QJsonObject Server::toolGetTimeSeries(const QJsonObject &args) {
	qint64 chatId = args["chat_id"].toVariant().toLongLong();
	QString granularity = args.value("granularity").toString("daily");

	Analytics::TimeSeriesGranularity gran;
	if (granularity == "hourly") {
		gran = Analytics::TimeSeriesGranularity::Hourly;
	} else if (granularity == "daily") {
		gran = Analytics::TimeSeriesGranularity::Daily;
	} else if (granularity == "weekly") {
		gran = Analytics::TimeSeriesGranularity::Weekly;
	} else if (granularity == "monthly") {
		gran = Analytics::TimeSeriesGranularity::Monthly;
	} else {
		gran = Analytics::TimeSeriesGranularity::Daily;
	}

	auto timeSeries = _analytics->getTimeSeries(chatId, gran);

	QJsonArray dataPoints;
	for (const auto &point : timeSeries) {
		QJsonObject dp;
		dp["timestamp"] = static_cast<qint64>(point.timestamp);
		dp["message_count"] = static_cast<qint64>(point.messageCount);
		dp["unique_users"] = static_cast<qint64>(point.uniqueUsers);
		dataPoints.append(dp);
	}

	QJsonObject result;
	result["chat_id"] = chatId;
	result["granularity"] = granularity;
	result["data_points"] = dataPoints;
	result["count"] = dataPoints.size();

	return result;
}

QJsonObject Server::toolGetTopUsers(const QJsonObject &args) {
	qint64 chatId = args["chat_id"].toVariant().toLongLong();
	int limit = args.value("limit").toInt(10);

	auto topUsers = _analytics->getTopUsers(chatId, limit);

	QJsonArray users;
	for (const auto &user : topUsers) {
		QJsonObject u;
		u["user_id"] = user.userId;
		u["username"] = user.username;
		u["message_count"] = static_cast<qint64>(user.messageCount);
		u["total_words"] = static_cast<qint64>(user.totalWords);
		users.append(u);
	}

	QJsonObject result;
	result["chat_id"] = chatId;
	result["users"] = users;
	result["count"] = users.size();

	return result;
}

QJsonObject Server::toolGetTopWords(const QJsonObject &args) {
	qint64 chatId = args["chat_id"].toVariant().toLongLong();
	int limit = args.value("limit").toInt(20);

	auto topWords = _analytics->getTopWords(chatId, limit);

	QJsonArray words;
	for (const auto &word : topWords) {
		QJsonObject w;
		w["word"] = word.word;
		w["count"] = static_cast<qint64>(word.count);
		w["frequency"] = word.frequency;
		words.append(w);
	}

	QJsonObject result;
	result["chat_id"] = chatId;
	result["words"] = words;
	result["count"] = words.size();

	return result;
}

QJsonObject Server::toolExportAnalytics(const QJsonObject &args) {
	qint64 chatId = args["chat_id"].toVariant().toLongLong();
	QString outputPath = args["output_path"].toString();

	bool success = _analytics->exportToCSV(chatId, outputPath);

	QJsonObject result;
	result["success"] = success;
	result["chat_id"] = chatId;
	result["output_path"] = outputPath;

	return result;
}

QJsonObject Server::toolGetTrends(const QJsonObject &args) {
	qint64 chatId = args["chat_id"].toVariant().toLongLong();

	Analytics::ActivityTrend trend = _analytics->detectActivityTrend(chatId);

	QString trendStr;
	switch (trend) {
		case Analytics::ActivityTrend::Increasing: trendStr = "increasing"; break;
		case Analytics::ActivityTrend::Decreasing: trendStr = "decreasing"; break;
		case Analytics::ActivityTrend::Stable: trendStr = "stable"; break;
		default: trendStr = "unknown"; break;
	}

	QJsonObject result;
	result["chat_id"] = chatId;
	result["trend"] = trendStr;

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

	MessageIntent intent = _semanticSearch->classifyIntent(text);

	QString intentStr;
	switch (intent) {
		case MessageIntent::Question: intentStr = "question"; break;
		case MessageIntent::Answer: intentStr = "answer"; break;
		case MessageIntent::Command: intentStr = "command"; break;
		case MessageIntent::Statement: intentStr = "statement"; break;
		case MessageIntent::Greeting: intentStr = "greeting"; break;
		case MessageIntent::Farewell: intentStr = "farewell"; break;
		case MessageIntent::Agreement: intentStr = "agreement"; break;
		case MessageIntent::Disagreement: intentStr = "disagreement"; break;
		default: intentStr = "unknown"; break;
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
			case EntityType::URL: typeStr = "url"; break;
			case EntityType::Hashtag: typeStr = "hashtag"; break;
			case EntityType::BotCommand: typeStr = "bot_command"; break;
			case EntityType::Email: typeStr = "email"; break;
			case EntityType::PhoneNumber: typeStr = "phone_number"; break;
			case EntityType::CashTag: typeStr = "cash_tag"; break;
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
	qint64 chatId = args["chat_id"].toVariant().toLongLong();
	qint64 messageId = args["message_id"].toVariant().toLongLong();
	QString newText = args["new_text"].toString();

	// TODO: Implement via tdesktop API

	QJsonObject result;
	result["success"] = false;
	result["error"] = "Message editing not yet implemented (requires tdesktop API integration)";
	result["chat_id"] = chatId;
	result["message_id"] = messageId;

	return result;
}

QJsonObject Server::toolDeleteMessage(const QJsonObject &args) {
	qint64 chatId = args["chat_id"].toVariant().toLongLong();
	qint64 messageId = args["message_id"].toVariant().toLongLong();

	// TODO: Implement via tdesktop API

	QJsonObject result;
	result["success"] = false;
	result["error"] = "Message deletion not yet implemented (requires tdesktop API integration)";
	result["chat_id"] = chatId;
	result["message_id"] = messageId;

	return result;
}

QJsonObject Server::toolForwardMessage(const QJsonObject &args) {
	qint64 fromChatId = args["from_chat_id"].toVariant().toLongLong();
	qint64 toChatId = args["to_chat_id"].toVariant().toLongLong();
	qint64 messageId = args["message_id"].toVariant().toLongLong();

	// TODO: Implement via tdesktop API

	QJsonObject result;
	result["success"] = false;
	result["error"] = "Message forwarding not yet implemented (requires tdesktop API integration)";
	result["from_chat_id"] = fromChatId;
	result["to_chat_id"] = toChatId;
	result["message_id"] = messageId;

	return result;
}

QJsonObject Server::toolPinMessage(const QJsonObject &args) {
	qint64 chatId = args["chat_id"].toVariant().toLongLong();
	qint64 messageId = args["message_id"].toVariant().toLongLong();
	bool notify = args.value("notify").toBool(false);

	// TODO: Implement via tdesktop API

	QJsonObject result;
	result["success"] = false;
	result["error"] = "Message pinning not yet implemented (requires tdesktop API integration)";
	result["chat_id"] = chatId;
	result["message_id"] = messageId;

	return result;
}

QJsonObject Server::toolUnpinMessage(const QJsonObject &args) {
	qint64 chatId = args["chat_id"].toVariant().toLongLong();
	qint64 messageId = args["message_id"].toVariant().toLongLong();

	// TODO: Implement via tdesktop API

	QJsonObject result;
	result["success"] = false;
	result["error"] = "Message unpinning not yet implemented (requires tdesktop API integration)";
	result["chat_id"] = chatId;
	result["message_id"] = messageId;

	return result;
}

QJsonObject Server::toolAddReaction(const QJsonObject &args) {
	qint64 chatId = args["chat_id"].toVariant().toLongLong();
	qint64 messageId = args["message_id"].toVariant().toLongLong();
	QString emoji = args["emoji"].toString();

	// TODO: Implement via tdesktop API

	QJsonObject result;
	result["success"] = false;
	result["error"] = "Reactions not yet implemented (requires tdesktop API integration)";
	result["chat_id"] = chatId;
	result["message_id"] = messageId;
	result["emoji"] = emoji;

	return result;
}

// ===== BATCH OPERATION TOOL IMPLEMENTATIONS =====

QJsonObject Server::toolBatchSend(const QJsonObject &args) {
	QJsonArray chatIdsArray = args["chat_ids"].toArray();
	QString message = args["message"].toString();

	QVector<qint64> chatIds;
	for (const auto &id : chatIdsArray) {
		chatIds.append(id.toVariant().toLongLong());
	}

	// TODO: Implement via BatchOperations component

	QJsonObject result;
	result["success"] = false;
	result["error"] = "Batch send not yet implemented (requires tdesktop API integration)";
	result["chat_count"] = chatIds.size();
	result["message"] = message;

	return result;
}

QJsonObject Server::toolBatchDelete(const QJsonObject &args) {
	qint64 chatId = args["chat_id"].toVariant().toLongLong();
	QJsonArray messageIdsArray = args["message_ids"].toArray();

	QVector<qint64> messageIds;
	for (const auto &id : messageIdsArray) {
		messageIds.append(id.toVariant().toLongLong());
	}

	// TODO: Implement via BatchOperations component

	QJsonObject result;
	result["success"] = false;
	result["error"] = "Batch delete not yet implemented (requires tdesktop API integration)";
	result["chat_id"] = chatId;
	result["message_count"] = messageIds.size();

	return result;
}

QJsonObject Server::toolBatchForward(const QJsonObject &args) {
	qint64 fromChatId = args["from_chat_id"].toVariant().toLongLong();
	qint64 toChatId = args["to_chat_id"].toVariant().toLongLong();
	QJsonArray messageIdsArray = args["message_ids"].toArray();

	QVector<qint64> messageIds;
	for (const auto &id : messageIdsArray) {
		messageIds.append(id.toVariant().toLongLong());
	}

	// TODO: Implement via BatchOperations component

	QJsonObject result;
	result["success"] = false;
	result["error"] = "Batch forward not yet implemented (requires tdesktop API integration)";
	result["from_chat_id"] = fromChatId;
	result["to_chat_id"] = toChatId;
	result["message_count"] = messageIds.size();

	return result;
}

QJsonObject Server::toolBatchPin(const QJsonObject &args) {
	qint64 chatId = args["chat_id"].toVariant().toLongLong();
	QJsonArray messageIdsArray = args["message_ids"].toArray();

	QVector<qint64> messageIds;
	for (const auto &id : messageIdsArray) {
		messageIds.append(id.toVariant().toLongLong());
	}

	// TODO: Implement via BatchOperations component

	QJsonObject result;
	result["success"] = false;
	result["error"] = "Batch pin not yet implemented (requires tdesktop API integration)";
	result["chat_id"] = chatId;
	result["message_count"] = messageIds.size();

	return result;
}

QJsonObject Server::toolBatchReaction(const QJsonObject &args) {
	qint64 chatId = args["chat_id"].toVariant().toLongLong();
	QJsonArray messageIdsArray = args["message_ids"].toArray();
	QString emoji = args["emoji"].toString();

	QVector<qint64> messageIds;
	for (const auto &id : messageIdsArray) {
		messageIds.append(id.toVariant().toLongLong());
	}

	// TODO: Implement via BatchOperations component

	QJsonObject result;
	result["success"] = false;
	result["error"] = "Batch reactions not yet implemented (requires tdesktop API integration)";
	result["chat_id"] = chatId;
	result["message_count"] = messageIds.size();
	result["emoji"] = emoji;

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
		scheduleId = _scheduler->scheduleOnce(chatId, text, dateTime);
	} else if (scheduleType == "delayed") {
		int delaySeconds = when.toInt();
		scheduleId = _scheduler->scheduleDelayed(chatId, text, delaySeconds);
	} else if (scheduleType == "recurring") {
		QDateTime startTime = QDateTime::fromString(when, Qt::ISODate);

		MessageScheduler::RecurrencePattern recurrence;
		if (pattern == "hourly") {
			recurrence = MessageScheduler::RecurrencePattern::Hourly;
		} else if (pattern == "daily") {
			recurrence = MessageScheduler::RecurrencePattern::Daily;
		} else if (pattern == "weekly") {
			recurrence = MessageScheduler::RecurrencePattern::Weekly;
		} else if (pattern == "monthly") {
			recurrence = MessageScheduler::RecurrencePattern::Monthly;
		} else {
			recurrence = MessageScheduler::RecurrencePattern::Daily;
		}

		scheduleId = _scheduler->scheduleRecurring(chatId, text, startTime, recurrence);
	}

	QJsonObject result;
	result["success"] = (scheduleId > 0);
	result["schedule_id"] = scheduleId;
	result["chat_id"] = chatId;
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

	auto scheduled = _scheduler->getScheduledMessages(chatId);

	QJsonArray schedules;
	for (const auto &msg : scheduled) {
		QJsonObject s;
		s["schedule_id"] = msg.id;
		s["chat_id"] = msg.chatId;
		s["text"] = msg.text;
		s["start_time"] = msg.startTime.toString(Qt::ISODate);
		s["status"] = static_cast<int>(msg.status);
		s["occurrences"] = static_cast<qint64>(msg.occurrences);
		schedules.append(s);
	}

	QJsonObject result;
	result["schedules"] = schedules;
	result["count"] = schedules.size();

	return result;
}

QJsonObject Server::toolUpdateScheduled(const QJsonObject &args) {
	qint64 scheduleId = args["schedule_id"].toVariant().toLongLong();
	QString newText = args["new_text"].toString();

	bool success = _scheduler->updateScheduledMessage(scheduleId, newText);

	QJsonObject result;
	result["success"] = success;
	result["schedule_id"] = scheduleId;

	return result;
}

// ===== SYSTEM TOOL IMPLEMENTATIONS =====

QJsonObject Server::toolGetCacheStats(const QJsonObject &args) {
	Q_UNUSED(args);

	auto stats = _archiver->getStatistics();

	QJsonObject result;
	result["total_messages"] = static_cast<qint64>(stats.totalMessages);
	result["total_chats"] = static_cast<qint64>(stats.totalChats);
	result["database_size_bytes"] = static_cast<qint64>(stats.databaseSizeBytes);
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
			switch (event.type) {
				case AuditLogger::EventType::ToolInvoked: typeStr = "tool"; break;
				case AuditLogger::EventType::AuthEvent: typeStr = "auth"; break;
				case AuditLogger::EventType::TelegramOp: typeStr = "telegram"; break;
				case AuditLogger::EventType::SystemEvent: typeStr = "system"; break;
				case AuditLogger::EventType::Error: typeStr = "error"; break;
			}
			if (typeStr != eventType) {
				continue;
			}
		}

		QJsonObject e;
		e["event_id"] = event.id;
		e["timestamp"] = event.timestamp.toString(Qt::ISODate);
		e["action"] = event.action;
		e["user"] = event.user;
		e["duration_ms"] = static_cast<qint64>(event.durationMs);
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
		QJsonArray chats = _archiver->getArchivedChats();
		result["contents"] = QJsonArray{QJsonObject{
			{"uri", uri},
			{"mimeType", "application/json"},
			{"text", QJsonDocument(QJsonObject{{"chats", chats}}).toJson(QJsonDocument::Compact)}
		}};
	} else if (uri.startsWith("telegram://messages/")) {
		QString chatIdStr = uri.mid(QString("telegram://messages/").length());
		qint64 chatId = chatIdStr.toLongLong();

		QJsonArray messages = _archiver->getMessages(chatId, 50, 0);
		result["contents"] = QJsonArray{QJsonObject{
			{"uri", uri},
			{"mimeType", "application/json"},
			{"text", QJsonDocument(QJsonObject{{"messages", messages}}).toJson(QJsonDocument::Compact)}
		}};
	} else if (uri == "telegram://archive/stats") {
		auto stats = _archiver->getStatistics();
		QJsonObject statsObj;
		statsObj["total_messages"] = static_cast<qint64>(stats.totalMessages);
		statsObj["total_chats"] = static_cast<qint64>(stats.totalChats);
		statsObj["database_size_bytes"] = static_cast<qint64>(stats.databaseSizeBytes);

		result["contents"] = QJsonArray{QJsonObject{
			{"uri", uri},
			{"mimeType", "application/json"},
			{"text", QJsonDocument(statsObj).toJson(QJsonDocument::Compact)}
		}};
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

} // namespace MCP

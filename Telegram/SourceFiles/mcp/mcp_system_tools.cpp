// This file is part of Telegram Desktop MCP integration.
// Extracted from mcp_server_complete.cpp for modular organization.
//
// For license and copyright information please follow this link:
// https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL

#include "mcp_server_includes.h"

namespace MCP {
// ===== SYSTEM TOOL IMPLEMENTATIONS =====

QJsonObject Server::toolGetCacheStats(const QJsonObject &args) {
	Q_UNUSED(args);

	QJsonObject result;

	// Get database file size
	QFileInfo dbInfo(_databasePath);
	qint64 dbSize = dbInfo.exists() ? dbInfo.size() : 0;

	// Count messages from local DB
	QSqlQuery msgQuery(_db);
	int totalMessages = 0;
	int totalChats = 0;
	if (msgQuery.exec("SELECT COUNT(*) FROM message_tags")) {
		if (msgQuery.next()) totalMessages = msgQuery.value(0).toInt();
	}

	// Count unique chats from various tables
	QSqlQuery chatQuery(_db);
	if (chatQuery.exec("SELECT COUNT(DISTINCT chat_id) FROM message_tags")) {
		if (chatQuery.next()) totalChats = chatQuery.value(0).toInt();
	}

	// Get archiver stats if available
	if (_archiver) {
		QJsonArray archivedChats = _archiver->listArchivedChats();
		if (archivedChats.size() > totalChats) {
			totalChats = archivedChats.size();
		}
	}

	result["total_messages_tracked"] = totalMessages;
	result["total_chats_tracked"] = totalChats;
	result["database_size_bytes"] = dbSize;
	result["database_path"] = _databasePath;
	result["indexed_messages"] = _semanticSearch ? _semanticSearch->getIndexedMessageCount() : 0;
	result["archiver_active"] = (_archiver != nullptr);

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
	result["uptime_seconds"] = _startTime.secsTo(QDateTime::currentDateTime());

	return result;
}

// ===== VOICE TOOL IMPLEMENTATIONS =====

QJsonObject Server::toolTranscribeVoice(const QJsonObject &args) {
	qint64 messageId = args.value("message_id").toVariant().toLongLong();
	QString audioPath = args["audio_path"].toString();

	// Initialize voice transcription if not already done
	if (!_voiceTranscription) {
		_voiceTranscription.reset(new VoiceTranscription(this));
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


} // namespace MCP

// This file is part of Telegram Desktop MCP integration.
// Extracted from mcp_server_complete.cpp for modular organization.
//
// For license and copyright information please follow this link:
// https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL

#include "mcp_server_includes.h"

namespace MCP {
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
	// Non-blocking export: starts export and returns immediately.
	// Use get_export_status to poll for completion.

	qint64 chatId = args["chat_id"].toVariant().toLongLong();
	QString outputPath = args["output_path"].toString();

	// Check session
	if (!_session) {
		QJsonObject error;
		error["error"] = "No active session";
		return error;
	}

	// Check if another export is already running
	if (_activeExport && !_activeExport->finished) {
		QJsonObject error;
		error["error"] = "Another export is already in progress";
		error["chat_id"] = _activeExport->chatId;
		error["chat_name"] = _activeExport->chatName;
		return error;
	}

	// Get peer and history
	PeerId peerId(chatId);
	auto peer = _session->data().peerLoaded(peerId);
	if (!peer) {
		auto history = _session->data().history(peerId);
		if (history) {
			peer = history->peer;
		}
	}
	if (!peer) {
		QJsonObject error;
		error["error"] = "Chat not found: " + QString::number(chatId);
		return error;
	}

	qWarning() << "MCP: toolExportChat starting export for peer:" << peer->name();

	// Build MTPInputPeer from the peer
	MTPInputPeer inputPeer = peer->input;

	// Load saved export settings from UI (same settings as export dialog uses)
	Export::Settings settings = _session->local().readExportSettings();

	// Override output path if specified in MCP args
	if (!outputPath.isEmpty()) {
		settings.path = outputPath;
	}

	// Set single peer mode for this chat
	settings.singlePeer = inputPeer;

	// Enable all export types for single peer export (messages, media, etc.)
	settings.types = Export::Settings::Type::AnyChatsMask;
	settings.fullChats = Export::Settings::Type::AnyChatsMask;

	// Gradual mode is always true in this fork
	settings.gradualMode = true;

	// Resolve settings (sets path, peer name, peer type)
	Export::View::ResolveSettings(_session, settings);

	// MCP exports always create a named subdirectory (Type-Name-DDMMYYYY-HHMMSS)
	// even when exporting to an empty directory
	settings.forceSubPath = true;

	// Prepare localized environment strings (same as UI export)
	Export::Environment environment = Export::View::PrepareEnvironment(_session);

	qWarning() << "MCP: Export settings resolved - path:" << settings.path
	           << "peerName:" << settings.singlePeerName
	           << "peerType:" << settings.singlePeerType;

	// Set up tracking state
	_activeExport = std::make_unique<ActiveExport>();
	_activeExport->chatId = chatId;
	_activeExport->chatName = settings.singlePeerName;
	_activeExport->chatType = settings.singlePeerType;
	_activeExport->outputPath = settings.path;
	_activeExport->startTime = QDateTime::currentDateTime();

	// Clean up any previous controller
	_exportController.reset();

	// Create the export controller
	_exportController = std::make_unique<Export::Controller>(
		&_session->mtp(),
		inputPeer);

	// Subscribe to state changes (update _activeExport members)
	_exportController->state(
	) | rpl::start_with_next([this](Export::State state) {
		if (!_activeExport) return;

		if (auto *processing = std::get_if<Export::ProcessingState>(&state)) {
			_activeExport->currentStep = static_cast<int>(processing->step);
			qWarning() << "MCP: Export processing step:" << _activeExport->currentStep;
		} else if (auto *finished = std::get_if<Export::FinishedState>(&state)) {
			_activeExport->finishedPath = finished->path;
			_activeExport->filesCount = finished->filesCount;
			_activeExport->bytesCount = finished->bytesCount;
			_activeExport->success = true;
			_activeExport->finished = true;
			qWarning() << "MCP: Export finished - path:" << finished->path
			           << "files:" << finished->filesCount << "bytes:" << finished->bytesCount;
		} else if (auto *apiError = std::get_if<Export::ApiErrorState>(&state)) {
			_activeExport->errorMessage = QString("API Error: %1 - %2")
				.arg(apiError->data.type())
				.arg(apiError->data.description());
			_activeExport->finished = true;
			qWarning() << "MCP: Export API error:" << _activeExport->errorMessage;
		} else if (auto *outputError = std::get_if<Export::OutputErrorState>(&state)) {
			_activeExport->errorMessage = "Output error at path: " + outputError->path;
			_activeExport->finished = true;
			qWarning() << "MCP: Export output error:" << _activeExport->errorMessage;
		} else if (std::get_if<Export::CancelledState>(&state)) {
			_activeExport->errorMessage = "Export was cancelled";
			_activeExport->finished = true;
			qWarning() << "MCP: Export cancelled";
		}
	}, _exportController->lifetime());

	// Start the export (non-blocking - runs on crl::object_on_queue)
	_exportController->startExport(settings, environment);

	// Return immediately with "started" status
	QJsonObject result;
	result["success"] = true;
	result["status"] = "started";
	result["chat_id"] = chatId;
	result["chat_name"] = settings.singlePeerName;
	result["chat_type"] = settings.singlePeerType;
	result["output_path"] = settings.path;
	result["message"] = "Export started. Use get_export_status to poll for completion.";

	return result;
}

QJsonObject Server::toolGetExportStatus(const QJsonObject &args) {
	Q_UNUSED(args);

	if (!_activeExport) {
		QJsonObject result;
		result["success"] = true;
		result["state"] = "idle";
		result["message"] = "No export in progress";
		return result;
	}

	QJsonObject result;
	result["success"] = true;
	result["chat_id"] = _activeExport->chatId;
	result["chat_name"] = _activeExport->chatName;
	result["chat_type"] = _activeExport->chatType;
	result["output_path"] = _activeExport->outputPath;

	if (_activeExport->finished) {
		result["state"] = _activeExport->success ? "completed" : "failed";
		result["export_success"] = _activeExport->success;

		if (_activeExport->success) {
			result["output_directory"] = _activeExport->finishedPath;
			result["files_count"] = _activeExport->filesCount;
			result["bytes_count"] = _activeExport->bytesCount;
		} else {
			result["error"] = _activeExport->errorMessage.isEmpty()
				? "Export failed" : _activeExport->errorMessage;
		}

		// Calculate duration
		qint64 durationSecs = _activeExport->startTime.secsTo(QDateTime::currentDateTime());
		result["duration_seconds"] = durationSecs;
	} else {
		result["state"] = "in_progress";
		result["current_step"] = _activeExport->currentStep;

		// Elapsed time
		qint64 elapsedSecs = _activeExport->startTime.secsTo(QDateTime::currentDateTime());
		result["elapsed_seconds"] = elapsedSecs;
	}

	return result;
}

QJsonObject Server::toolListArchivedChats(const QJsonObject &args) {
	Q_UNUSED(args);

	if (!_archiver) {
		QJsonObject error;
		error["error"] = "Archiver not available";
		return error;
	}

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
	if (!_archiver) {
		QJsonObject error;
		error["error"] = "Archiver not available";
		return error;
	}

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
	if (!_archiver) {
		QJsonObject error;
		error["error"] = "Archiver not available";
		return error;
	}

	int daysToKeep = args["days_to_keep"].toInt();

	qint64 cutoffTimestamp = QDateTime::currentSecsSinceEpoch() - (daysToKeep * 86400);
	int deleted = _archiver->purgeOldMessages(cutoffTimestamp);

	QJsonObject result;
	result["success"] = true;
	result["deleted_count"] = deleted;
	result["days_kept"] = daysToKeep;

	return result;
}


} // namespace MCP

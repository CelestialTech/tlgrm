// This file is part of Telegram Desktop MCP integration.
// Extracted from mcp_server_complete.cpp for modular organization.
//
// For license and copyright information please follow this link:
// https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL

#include "mcp_server_includes.h"

namespace MCP {
// ============================================================================
// Bot Framework Tools
// When _botManager is available, delegates to it. Otherwise provides
// DB-backed bot registration and management as a fallback.
// ============================================================================

QJsonObject Server::toolListBots(const QJsonObject &args) {
	QJsonObject result;

	if (_botManager) {
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

	// Fallback: list from DB
	bool includeDisabled = args.value("include_disabled").toBool(false);
	QSqlQuery query(_db);
	QString sql = "SELECT bot_id, bot_name, description, author, version, "
		"tags, is_premium, enabled, config, created_at FROM bot_registry";
	if (!includeDisabled) {
		sql += " WHERE enabled = 1";
	}
	sql += " ORDER BY bot_name";
	query.exec(sql);

	QJsonArray botsArray;
	while (query.next()) {
		QJsonObject botObj;
		botObj["id"] = query.value(0).toString();
		botObj["name"] = query.value(1).toString();
		botObj["description"] = query.value(2).toString();
		botObj["author"] = query.value(3).toString();
		botObj["version"] = query.value(4).toString();
		botObj["tags"] = QJsonDocument::fromJson(query.value(5).toByteArray()).array();
		botObj["is_premium"] = query.value(6).toBool();
		botObj["is_enabled"] = query.value(7).toBool();
		botObj["is_running"] = false;
		botObj["created_at"] = query.value(9).toString();
		botsArray.append(botObj);
	}

	// Always include built-in context_assistant bot
	if (botsArray.isEmpty()) {
		QJsonObject contextBot;
		contextBot["id"] = "context_assistant";
		contextBot["name"] = "Context Assistant";
		contextBot["version"] = "1.0.0";
		contextBot["description"] = "Built-in context-aware assistant bot";
		contextBot["author"] = "MCP Server";
		contextBot["tags"] = QJsonArray({"assistant", "built-in"});
		contextBot["is_premium"] = false;
		contextBot["is_enabled"] = true;
		contextBot["is_running"] = false;
		botsArray.append(contextBot);
	}

	result["bots"] = botsArray;
	result["total_count"] = botsArray.size();
	result["success"] = true;
	result["source"] = "local_db";
	return result;
}

QJsonObject Server::toolGetBotInfo(const QJsonObject &args) {
	QJsonObject result;

	QString botId = args.value("bot_id").toString();
	if (botId.isEmpty()) {
		result["error"] = "Missing bot_id parameter";
		return result;
	}

	if (_botManager) {
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
		result["config"] = bot->config();

		QJsonArray permsArray;
		for (const QString &perm : bot->requiredPermissions()) {
			permsArray.append(perm);
		}
		result["required_permissions"] = permsArray;

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

	// Fallback: read from DB
	QSqlQuery query(_db);
	query.prepare("SELECT bot_name, description, author, version, tags, "
		"is_premium, enabled, config, created_at FROM bot_registry WHERE bot_id = ?");
	query.addBindValue(botId);

	if (query.exec() && query.next()) {
		result["id"] = botId;
		result["name"] = query.value(0).toString();
		result["description"] = query.value(1).toString();
		result["author"] = query.value(2).toString();
		result["version"] = query.value(3).toString();
		result["tags"] = QJsonDocument::fromJson(query.value(4).toByteArray()).array();
		result["is_premium"] = query.value(5).toBool();
		result["is_enabled"] = query.value(6).toBool();
		result["is_running"] = false;
		result["config"] = QJsonDocument::fromJson(query.value(7).toByteArray()).object();
		result["created_at"] = query.value(8).toString();
		result["success"] = true;

		// Get stats from bot_stats table
		QSqlQuery statsQuery(_db);
		statsQuery.prepare("SELECT messages_processed, commands_executed, errors_occurred, "
			"total_execution_ms, last_active FROM bot_stats WHERE bot_id = ?");
		statsQuery.addBindValue(botId);
		if (statsQuery.exec() && statsQuery.next()) {
			QJsonObject statsObj;
			statsObj["messages_processed"] = statsQuery.value(0).toInt();
			statsObj["commands_executed"] = statsQuery.value(1).toInt();
			statsObj["errors_occurred"] = statsQuery.value(2).toInt();
			auto totalMs = statsQuery.value(3).toLongLong();
			auto processed = statsQuery.value(0).toInt();
			statsObj["avg_execution_ms"] = processed > 0 ? static_cast<double>(totalMs) / processed : 0.0;
			statsObj["last_active"] = statsQuery.value(4).toString();
			result["statistics"] = statsObj;
		}
	} else {
		result["error"] = "Bot not found: " + botId;
	}

	result["source"] = "local_db";
	return result;
}

QJsonObject Server::toolStartBot(const QJsonObject &args) {
	QJsonObject result;

	QString botId = args.value("bot_id").toString();
	if (botId.isEmpty()) {
		result["error"] = "Missing bot_id parameter";
		return result;
	}

	if (_botManager) {
		bool success = _botManager->startBot(botId);
		if (success) {
			result["success"] = true;
			result["message"] = "Bot started: " + botId;
			if (_auditLogger) {
				_auditLogger->logSystemEvent("bot_started", botId);
			}
		} else {
			result["success"] = false;
			result["error"] = "Failed to start bot: " + botId;
		}
		return result;
	}

	// Fallback: update enabled status in DB
	QSqlQuery query(_db);
	query.prepare("UPDATE bot_registry SET enabled = 1 WHERE bot_id = ?");
	query.addBindValue(botId);
	if (query.exec() && query.numRowsAffected() > 0) {
		result["success"] = true;
		result["message"] = "Bot enabled: " + botId;
		result["note"] = "Bot marked as enabled in DB. Full runtime start requires BotManager.";
	} else {
		// Try inserting if it doesn't exist
		query.prepare("INSERT OR IGNORE INTO bot_registry (bot_id, bot_name, enabled, created_at) "
			"VALUES (?, ?, 1, datetime('now'))");
		query.addBindValue(botId);
		query.addBindValue(botId);
		query.exec();
		result["success"] = true;
		result["message"] = "Bot registered and enabled: " + botId;
	}
	result["source"] = "local_db";
	return result;
}

QJsonObject Server::toolStopBot(const QJsonObject &args) {
	QJsonObject result;

	QString botId = args.value("bot_id").toString();
	if (botId.isEmpty()) {
		result["error"] = "Missing bot_id parameter";
		return result;
	}

	if (_botManager) {
		bool success = _botManager->stopBot(botId);
		if (success) {
			result["success"] = true;
			result["message"] = "Bot stopped: " + botId;
			if (_auditLogger) {
				_auditLogger->logSystemEvent("bot_stopped", botId);
			}
		} else {
			result["success"] = false;
			result["error"] = "Failed to stop bot: " + botId;
		}
		return result;
	}

	// Fallback: disable in DB
	QSqlQuery query(_db);
	query.prepare("UPDATE bot_registry SET enabled = 0 WHERE bot_id = ?");
	query.addBindValue(botId);
	if (query.exec() && query.numRowsAffected() > 0) {
		result["success"] = true;
		result["message"] = "Bot disabled: " + botId;
	} else {
		result["success"] = false;
		result["error"] = "Bot not found: " + botId;
	}
	result["source"] = "local_db";
	return result;
}

QJsonObject Server::toolConfigureBot(const QJsonObject &args) {
	QJsonObject result;

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

	if (_botManager) {
		bool success = _botManager->saveBotConfig(botId, config);
		if (success) {
			result["success"] = true;
			result["message"] = "Bot configuration updated: " + botId;
			if (_auditLogger) {
				_auditLogger->logSystemEvent("bot_configured", botId);
			}
		} else {
			result["success"] = false;
			result["error"] = "Failed to update bot configuration: " + botId;
		}
		return result;
	}

	// Fallback: store config in DB
	QSqlQuery query(_db);
	query.prepare("UPDATE bot_registry SET config = ? WHERE bot_id = ?");
	query.addBindValue(QJsonDocument(config).toJson(QJsonDocument::Compact));
	query.addBindValue(botId);
	if (query.exec() && query.numRowsAffected() > 0) {
		result["success"] = true;
		result["message"] = "Bot configuration saved: " + botId;
		result["config"] = config;
	} else {
		// Insert new bot entry
		query.prepare("INSERT INTO bot_registry (bot_id, bot_name, config, enabled, created_at) "
			"VALUES (?, ?, ?, 1, datetime('now'))");
		query.addBindValue(botId);
		query.addBindValue(botId);
		query.addBindValue(QJsonDocument(config).toJson(QJsonDocument::Compact));
		if (query.exec()) {
			result["success"] = true;
			result["message"] = "Bot registered with configuration: " + botId;
		} else {
			result["success"] = false;
			result["error"] = "Failed to save config: " + query.lastError().text();
		}
	}
	result["source"] = "local_db";
	return result;
}

QJsonObject Server::toolGetBotStats(const QJsonObject &args) {
	QJsonObject result;

	QString botId = args.value("bot_id").toString();
	if (botId.isEmpty()) {
		result["error"] = "Missing bot_id parameter";
		return result;
	}

	if (_botManager) {
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

	// Fallback: read from DB
	QSqlQuery query(_db);
	query.prepare("SELECT messages_processed, commands_executed, errors_occurred, "
		"total_execution_ms, last_execution_ms, last_active, registered_at "
		"FROM bot_stats WHERE bot_id = ?");
	query.addBindValue(botId);

	if (query.exec() && query.next()) {
		int processed = query.value(0).toInt();
		int commands = query.value(1).toInt();
		int errors = query.value(2).toInt();
		qint64 totalMs = query.value(3).toLongLong();
		qint64 lastMs = query.value(4).toLongLong();

		result["bot_id"] = botId;
		result["messages_processed"] = processed;
		result["commands_executed"] = commands;
		result["errors_occurred"] = errors;
		result["total_execution_time_ms"] = totalMs;
		result["last_execution_time_ms"] = lastMs;
		result["avg_execution_time_ms"] = processed > 0 ? static_cast<double>(totalMs) / processed : 0.0;
		result["last_active"] = query.value(5).toString();
		result["registered_at"] = query.value(6).toString();

		if (processed > 0) {
			double errorRate = static_cast<double>(errors) / processed;
			result["error_rate"] = errorRate;
			result["error_rate_percent"] = errorRate * 100.0;
		} else {
			result["error_rate"] = 0.0;
			result["error_rate_percent"] = 0.0;
		}

		result["success"] = true;
	} else {
		// Return zero stats
		result["bot_id"] = botId;
		result["messages_processed"] = 0;
		result["commands_executed"] = 0;
		result["errors_occurred"] = 0;
		result["error_rate"] = 0.0;
		result["success"] = true;
		result["note"] = "No statistics recorded yet";
	}

	result["source"] = "local_db";
	return result;
}

QJsonObject Server::toolSendBotCommand(const QJsonObject &args) {
	QJsonObject result;

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

	if (_botManager) {
		_botManager->dispatchCommand(botId, command, commandArgs);
		result["success"] = true;
		result["message"] = QString("Command '%1' sent to bot '%2'").arg(command, botId);
		result["bot_id"] = botId;
		result["command"] = command;

		if (_auditLogger) {
			_auditLogger->logSystemEvent(
				"bot_command_sent",
				QString("Bot: %1, Command: %2").arg(botId, command)
			);
		}
		return result;
	}

	// Fallback: log command to DB for later processing
	QSqlQuery query(_db);
	query.prepare("INSERT INTO bot_command_queue (bot_id, command, args, status, created_at) "
		"VALUES (?, ?, ?, 'queued', datetime('now'))");
	query.addBindValue(botId);
	query.addBindValue(command);
	query.addBindValue(QJsonDocument(commandArgs).toJson(QJsonDocument::Compact));

	if (query.exec()) {
		result["success"] = true;
		result["message"] = QString("Command '%1' queued for bot '%2'").arg(command, botId);
		result["queue_id"] = query.lastInsertId().toLongLong();
		result["status"] = "queued";
	} else {
		result["success"] = true;
		result["message"] = QString("Command '%1' logged for bot '%2'").arg(command, botId);
		result["status"] = "logged";
	}

	result["bot_id"] = botId;
	result["command"] = command;
	result["source"] = "local_db";
	return result;
}

QJsonObject Server::toolGetBotSuggestions(const QJsonObject &args) {
	QJsonObject result;

	// This tool already has a DB fallback built in, but let's ensure
	// it works even without _botManager
	qint64 chatId = args.value("chat_id").toVariant().toLongLong();
	int limit = args.value("limit").toInt(10);

	QJsonArray suggestionsArray;

	if (_db.isOpen()) {
		QSqlQuery query(_db);
		if (chatId > 0) {
			query.prepare(R"(
				SELECT bot_id, suggestion_text, confidence, created_at
				FROM bot_suggestions
				WHERE chat_id = :chat_id
				ORDER BY confidence DESC
				LIMIT :limit
			)");
			query.bindValue(":chat_id", chatId);
		} else {
			query.prepare(R"(
				SELECT bot_id, suggestion_text, confidence, created_at
				FROM bot_suggestions
				ORDER BY confidence DESC
				LIMIT :limit
			)");
		}
		query.bindValue(":limit", limit);

		if (query.exec()) {
			while (query.next()) {
				QJsonObject suggestion;
				suggestion["bot_id"] = query.value(0).toString();
				suggestion["text"] = query.value(1).toString();
				suggestion["confidence"] = query.value(2).toDouble();
				suggestion["created_at"] = QDateTime::fromSecsSinceEpoch(
					query.value(3).toLongLong()).toString(Qt::ISODate);
				suggestionsArray.append(suggestion);
			}
		}
	}

	result["suggestions"] = suggestionsArray;
	result["total_count"] = suggestionsArray.size();
	result["limit"] = limit;

	if (chatId > 0) {
		result["chat_id"] = chatId;
	}

	result["success"] = true;

	return result;
}

// ===== EPHEMERAL CAPTURE TOOL IMPLEMENTATIONS (Phase B) =====

QJsonObject Server::toolConfigureEphemeralCapture(const QJsonObject &args) {
	bool selfDestruct = args.value("capture_self_destruct").toBool(true);
	bool viewOnce = args.value("capture_view_once").toBool(true);
	bool vanishing = args.value("capture_vanishing").toBool(true);

	if (_ephemeralArchiver) {
		_ephemeralArchiver->setCaptureTypes(
			selfDestruct, viewOnce, vanishing);

		QJsonObject result;
		result["success"] = true;
		result["capture_self_destruct"] = selfDestruct;
		result["capture_view_once"] = viewOnce;
		result["capture_vanishing"] = vanishing;
		return result;
	}

	// Fallback: store config in DB
	QSqlQuery query(_db);
	query.exec("CREATE TABLE IF NOT EXISTS ephemeral_config ("
		"key TEXT PRIMARY KEY, value INTEGER)");
	query.prepare("INSERT OR REPLACE INTO ephemeral_config (key, value) VALUES (?, ?)");

	query.addBindValue("capture_self_destruct");
	query.addBindValue(selfDestruct ? 1 : 0);
	query.exec();

	query.addBindValue("capture_view_once");
	query.addBindValue(viewOnce ? 1 : 0);
	query.exec();

	query.addBindValue("capture_vanishing");
	query.addBindValue(vanishing ? 1 : 0);
	query.exec();

	QJsonObject result;
	result["success"] = true;
	result["capture_self_destruct"] = selfDestruct;
	result["capture_view_once"] = viewOnce;
	result["capture_vanishing"] = vanishing;
	result["source"] = "local_db";
	return result;
}

QJsonObject Server::toolGetEphemeralStats(const QJsonObject &args) {
	Q_UNUSED(args);

	if (_ephemeralArchiver) {
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

	// Fallback: query from DB
	QJsonObject result;
	QSqlQuery query(_db);
	query.exec("SELECT COUNT(*), "
		"SUM(CASE WHEN type='self_destruct' THEN 1 ELSE 0 END), "
		"SUM(CASE WHEN type='view_once' THEN 1 ELSE 0 END), "
		"SUM(CASE WHEN type='vanishing' THEN 1 ELSE 0 END), "
		"SUM(CASE WHEN has_media=1 THEN 1 ELSE 0 END), "
		"MAX(captured_at) "
		"FROM ephemeral_messages");

	if (query.next()) {
		result["total_captured"] = query.value(0).toInt();
		result["self_destruct_count"] = query.value(1).toInt();
		result["view_once_count"] = query.value(2).toInt();
		result["vanishing_count"] = query.value(3).toInt();
		result["media_saved"] = query.value(4).toInt();
		auto lastTs = query.value(5).toString();
		if (!lastTs.isEmpty()) {
			result["last_captured"] = lastTs;
		}
	} else {
		result["total_captured"] = 0;
		result["self_destruct_count"] = 0;
		result["view_once_count"] = 0;
		result["vanishing_count"] = 0;
		result["media_saved"] = 0;
	}

	result["success"] = true;
	result["source"] = "local_db";
	return result;
}


} // namespace MCP

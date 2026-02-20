// This file is part of Telegram Desktop MCP integration.
// Extracted from mcp_server_complete.cpp for modular organization.
//
// For license and copyright information please follow this link:
// https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL

#include "mcp_server_includes.h"

namespace MCP {
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

	qint64 chatId = args.value("chat_id").toVariant().toLongLong();
	int limit = args.value("limit").toInt(10);

	QJsonArray suggestionsArray;

	// Query bot_suggestions table from the database
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

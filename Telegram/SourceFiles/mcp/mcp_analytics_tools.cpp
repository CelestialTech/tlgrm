// This file is part of Telegram Desktop MCP integration.
// Extracted from mcp_server_complete.cpp for modular organization.
//
// For license and copyright information please follow this link:
// https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL

#include "mcp_server_includes.h"

namespace MCP {
// ===== ANALYTICS TOOL IMPLEMENTATIONS =====

QJsonObject Server::toolGetMessageStats(const QJsonObject &args) {
	qint64 chatId = args["chat_id"].toVariant().toLongLong();
	QString period = args.value("period").toString("all");

	if (!_analytics) {
		QJsonObject result;
		result["error"] = "Analytics not available";
		result["chat_id"] = QString::number(chatId);
		return result;
	}

	auto stats = _analytics->getMessageStatistics(chatId, period);

	// stats is already a QJsonObject
	QJsonObject result = stats;
	result["chat_id"] = QString::number(chatId);

	return result;
}

QJsonObject Server::toolGetUserActivity(const QJsonObject &args) {
	qint64 userId = args["user_id"].toVariant().toLongLong();
	qint64 chatId = args.value("chat_id").toVariant().toLongLong();

	if (!_analytics) {
		QJsonObject result;
		result["error"] = "Analytics not available";
		return result;
	}

	auto activity = _analytics->getUserActivity(userId, chatId);

	// activity is already a QJsonObject
	return activity;
}

QJsonObject Server::toolGetChatActivity(const QJsonObject &args) {
	qint64 chatId = args["chat_id"].toVariant().toLongLong();

	if (!_analytics) {
		QJsonObject result;
		result["error"] = "Analytics not available";
		result["chat_id"] = QString::number(chatId);
		return result;
	}

	auto activity = _analytics->getChatActivity(chatId);

	// activity is already a QJsonObject
	return activity;
}

QJsonObject Server::toolGetTimeSeries(const QJsonObject &args) {
	qint64 chatId = args["chat_id"].toVariant().toLongLong();
	QString granularity = args.value("granularity").toString("daily");

	if (!_analytics) {
		QJsonObject result;
		result["error"] = "Analytics not available";
		result["chat_id"] = QString::number(chatId);
		return result;
	}

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

	if (!_analytics) {
		QJsonObject result;
		result["error"] = "Analytics not available";
		result["chat_id"] = QString::number(chatId);
		return result;
	}

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

	if (!_analytics) {
		QJsonObject result;
		result["error"] = "Analytics not available";
		result["chat_id"] = QString::number(chatId);
		return result;
	}

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

	if (!_analytics) {
		QJsonObject result;
		result["success"] = false;
		result["error"] = "Analytics not available";
		result["chat_id"] = QString::number(chatId);
		return result;
	}

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

	if (!_analytics) {
		QJsonObject result;
		result["error"] = "Analytics not available";
		result["chat_id"] = QString::number(chatId);
		return result;
	}

	auto trends = _analytics->getTrends(chatId, metric, daysBack);
	// trends is already a QJsonObject with all trend data

	QJsonObject result = trends;
	result["chat_id"] = QString::number(chatId);
	result["metric"] = metric;
	result["days_back"] = daysBack;

	return result;
}


} // namespace MCP

// This file is part of Telegram Desktop MCP integration.
// Extracted from mcp_server_complete.cpp for modular organization.
//
// For license and copyright information please follow this link:
// https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL

#include "mcp_server_includes.h"

namespace MCP {
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
		QJsonArray chats;
		if (_archiver) {
			chats = _archiver->listArchivedChats();
		}

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

		QJsonArray messages;
		if (_archiver) {
			messages = _archiver->getMessages(chatId, 50, 0);
		}

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
		// Get real stats from toolGetCacheStats
		QJsonObject statsObj = toolGetCacheStats(QJsonObject());

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


} // namespace MCP

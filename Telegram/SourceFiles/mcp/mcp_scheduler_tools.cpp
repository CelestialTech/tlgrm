// This file is part of Telegram Desktop MCP integration.
// Extracted from mcp_server_complete.cpp for modular organization.
//
// For license and copyright information please follow this link:
// https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL

#include "mcp_server_includes.h"

namespace MCP {
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


} // namespace MCP

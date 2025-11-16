// This file is part of Telegram Desktop MCP Server.
// Licensed under GPLv3 with OpenSSL exception.

#include "audit_logger.h"

#include <QtCore/QFile>
#include <QtCore/QTextStream>
#include <QtCore/QJsonDocument>
#include <QtSql/QSqlQuery>
#include <QtSql/QSqlError>

namespace MCP {

AuditLogger::AuditLogger(QObject *parent)
	: QObject(parent) {
}

AuditLogger::~AuditLogger() {
	stop();
}

bool AuditLogger::start(QSqlDatabase *db, const QString &logFilePath) {
	if (_isRunning) {
		return true;
	}

	_db = db;
	_logFilePath = logFilePath;
	_isRunning = true;

	return true;
}

void AuditLogger::stop() {
	if (!_isRunning) {
		return;
	}

	_db = nullptr;
	_isRunning = false;
}

// Log tool invoked
void AuditLogger::logToolInvoked(
		const QString &toolName,
		const QJsonObject &parameters,
		const QString &userId) {

	AuditEvent event;
	event.id = _nextEventId++;
	event.eventType = AuditEventType::ToolInvoked;
	event.eventSubtype = toolName;
	event.userId = userId;
	event.toolName = toolName;
	event.parameters = parameters;
	event.timestamp = QDateTime::currentDateTime();

	storeEvent(event);
	Q_EMIT eventLogged(event);
}

// Log tool completed
void AuditLogger::logToolCompleted(
		const QString &toolName,
		const QString &status,
		qint64 durationMs,
		const QString &error) {

	AuditEvent event;
	event.id = _nextEventId++;
	event.eventType = AuditEventType::ToolInvoked;
	event.eventSubtype = toolName + "_completed";
	event.toolName = toolName;
	event.resultStatus = status;
	event.durationMs = durationMs;
	event.errorMessage = error;
	event.timestamp = QDateTime::currentDateTime();

	storeEvent(event);
	Q_EMIT eventLogged(event);
}

// Log auth event
void AuditLogger::logAuthEvent(
		const QString &event,
		const QString &userId,
		bool success,
		const QString &details) {

	AuditEvent auditEvent;
	auditEvent.id = _nextEventId++;
	auditEvent.eventType = AuditEventType::AuthEvent;
	auditEvent.eventSubtype = event;
	auditEvent.userId = userId;
	auditEvent.resultStatus = success ? "success" : "failure";
	auditEvent.timestamp = QDateTime::currentDateTime();

	if (!details.isEmpty()) {
		auditEvent.metadata["details"] = details;
	}

	storeEvent(auditEvent);
	Q_EMIT eventLogged(auditEvent);
}

// Log Telegram operation
void AuditLogger::logTelegramOp(
		const QString &operation,
		qint64 chatId,
		qint64 messageId,
		const QString &userId,
		bool success,
		const QString &error) {

	AuditEvent event;
	event.id = _nextEventId++;
	event.eventType = AuditEventType::TelegramOp;
	event.eventSubtype = operation;
	event.userId = userId;
	event.resultStatus = success ? "success" : "failure";
	event.errorMessage = error;
	event.timestamp = QDateTime::currentDateTime();

	event.metadata["chat_id"] = chatId;
	event.metadata["message_id"] = messageId;

	storeEvent(event);
	Q_EMIT eventLogged(event);
}

// Log system event
void AuditLogger::logSystemEvent(
		const QString &event,
		const QString &details,
		const QJsonObject &metadata) {

	AuditEvent auditEvent;
	auditEvent.id = _nextEventId++;
	auditEvent.eventType = AuditEventType::SystemEvent;
	auditEvent.eventSubtype = event;
	auditEvent.metadata = metadata;
	auditEvent.timestamp = QDateTime::currentDateTime();

	if (!details.isEmpty()) {
		auditEvent.metadata["details"] = details;
	}

	storeEvent(auditEvent);
	Q_EMIT eventLogged(auditEvent);
}

// Log error
void AuditLogger::logError(
		const QString &error,
		const QString &context,
		const QJsonObject &metadata) {

	AuditEvent event;
	event.id = _nextEventId++;
	event.eventType = AuditEventType::Error;
	event.eventSubtype = context;
	event.errorMessage = error;
	event.metadata = metadata;
	event.timestamp = QDateTime::currentDateTime();

	storeEvent(event);
	Q_EMIT eventLogged(event);
}

// Query events
QVector<AuditEvent> AuditLogger::queryEvents(
		AuditEventType eventType,
		const QString &userId,
		const QString &toolName,
		const QDateTime &startTime,
		const QDateTime &endTime,
		int limit) {

	QVector<AuditEvent> events;

	if (!_db || !_db->isOpen()) {
		return events;
	}

	QString sql = "SELECT * FROM audit_log WHERE 1=1";
	QStringList conditions;

	if (static_cast<int>(eventType) >= 0) {
		conditions << "event_type = :event_type";
	}
	if (!userId.isEmpty()) {
		conditions << "user_id = :user_id";
	}
	if (!toolName.isEmpty()) {
		conditions << "tool_name = :tool_name";
	}
	if (startTime.isValid()) {
		conditions << "timestamp >= :start_time";
	}
	if (endTime.isValid()) {
		conditions << "timestamp <= :end_time";
	}

	if (!conditions.isEmpty()) {
		sql += " AND " + conditions.join(" AND ");
	}

	sql += " ORDER BY timestamp DESC LIMIT :limit";

	QSqlQuery query(*_db);
	query.prepare(sql);

	if (static_cast<int>(eventType) >= 0) {
		query.bindValue(":event_type", eventTypeToString(eventType));
	}
	if (!userId.isEmpty()) {
		query.bindValue(":user_id", userId);
	}
	if (!toolName.isEmpty()) {
		query.bindValue(":tool_name", toolName);
	}
	if (startTime.isValid()) {
		query.bindValue(":start_time", startTime.toSecsSinceEpoch());
	}
	if (endTime.isValid()) {
		query.bindValue(":end_time", endTime.toSecsSinceEpoch());
	}
	query.bindValue(":limit", limit);

	if (query.exec()) {
		while (query.next()) {
			events.append(loadEventFromQuery(query));
		}
	}

	return events;
}

// Get recent events
QVector<AuditEvent> AuditLogger::getRecentEvents(int limit) {
	// Return from in-memory buffer first if available
	if (_eventBuffer.size() <= limit) {
		return _eventBuffer;
	}

	// Otherwise query database
	return queryEvents(static_cast<AuditEventType>(-1), QString(), QString(),
	                   QDateTime(), QDateTime(), limit);
}

// Get events by user
QVector<AuditEvent> AuditLogger::getEventsByUser(const QString &userId, int limit) {
	return queryEvents(static_cast<AuditEventType>(-1), userId, QString(),
	                   QDateTime(), QDateTime(), limit);
}

// Get events by tool
QVector<AuditEvent> AuditLogger::getEventsByTool(const QString &toolName, int limit) {
	return queryEvents(static_cast<AuditEventType>(-1), QString(), toolName,
	                   QDateTime(), QDateTime(), limit);
}

// Get statistics
AuditLogger::AuditStatistics AuditLogger::getStatistics(
		const QDateTime &start,
		const QDateTime &end) {

	AuditStatistics stats;

	if (!_db || !_db->isOpen()) {
		return stats;
	}

	QString sql = "SELECT COUNT(*) as total, event_type FROM audit_log WHERE 1=1";

	if (start.isValid()) {
		sql += " AND timestamp >= " + QString::number(start.toSecsSinceEpoch());
	}
	if (end.isValid()) {
		sql += " AND timestamp <= " + QString::number(end.toSecsSinceEpoch());
	}

	sql += " GROUP BY event_type";

	QSqlQuery query(*_db);
	if (query.exec(sql)) {
		while (query.next()) {
			int count = query.value("total").toInt();
			QString typeStr = query.value("event_type").toString();
			AuditEventType type = stringToEventType(typeStr);

			stats.totalEvents += count;

			switch (type) {
			case AuditEventType::ToolInvoked:
				stats.toolInvocations += count;
				break;
			case AuditEventType::AuthEvent:
				stats.authEvents += count;
				break;
			case AuditEventType::TelegramOp:
				stats.telegramOps += count;
				break;
			case AuditEventType::SystemEvent:
				stats.systemEvents += count;
				break;
			case AuditEventType::Error:
				stats.errors += count;
				break;
			}
		}
	}

	// Tool counts
	QSqlQuery toolQuery(*_db);
	QString toolSql = "SELECT tool_name, COUNT(*) as count FROM audit_log "
	                  "WHERE tool_name IS NOT NULL";

	if (start.isValid()) {
		toolSql += " AND timestamp >= " + QString::number(start.toSecsSinceEpoch());
	}
	if (end.isValid()) {
		toolSql += " AND timestamp <= " + QString::number(end.toSecsSinceEpoch());
	}

	toolSql += " GROUP BY tool_name";

	if (toolQuery.exec(toolSql)) {
		while (toolQuery.next()) {
			stats.toolCounts[toolQuery.value("tool_name").toString()] =
				toolQuery.value("count").toInt();
		}
	}

	// User counts
	QSqlQuery userQuery(*_db);
	QString userSql = "SELECT user_id, COUNT(*) as count FROM audit_log "
	                  "WHERE user_id IS NOT NULL";

	if (start.isValid()) {
		userSql += " AND timestamp >= " + QString::number(start.toSecsSinceEpoch());
	}
	if (end.isValid()) {
		userSql += " AND timestamp <= " + QString::number(end.toSecsSinceEpoch());
	}

	userSql += " GROUP BY user_id";

	if (userQuery.exec(userSql)) {
		while (userQuery.next()) {
			stats.userCounts[userQuery.value("user_id").toString()] =
				userQuery.value("count").toInt();
		}
	}

	// Average duration
	QSqlQuery durationQuery(*_db);
	QString durationSql = "SELECT AVG(duration_ms) as avg_duration FROM audit_log "
	                      "WHERE duration_ms IS NOT NULL";

	if (start.isValid()) {
		durationSql += " AND timestamp >= " + QString::number(start.toSecsSinceEpoch());
	}
	if (end.isValid()) {
		durationSql += " AND timestamp <= " + QString::number(end.toSecsSinceEpoch());
	}

	if (durationQuery.exec(durationSql) && durationQuery.next()) {
		stats.avgDuration = durationQuery.value("avg_duration").toDouble();
	}

	return stats;
}

// Export functions
QJsonObject AuditLogger::exportEvent(const AuditEvent &event) {
	QJsonObject json;
	json["id"] = event.id;
	json["event_type"] = eventTypeToString(event.eventType);
	json["event_subtype"] = event.eventSubtype;
	json["user_id"] = event.userId;
	json["tool_name"] = event.toolName;
	json["parameters"] = event.parameters;
	json["result_status"] = event.resultStatus;
	json["error_message"] = event.errorMessage;
	json["duration_ms"] = event.durationMs;
	json["timestamp"] = event.timestamp.toString(Qt::ISODate);
	json["metadata"] = event.metadata;

	return json;
}

QJsonArray AuditLogger::exportEvents(const QVector<AuditEvent> &events) {
	QJsonArray array;
	for (const auto &event : events) {
		array.append(exportEvent(event));
	}
	return array;
}

QString AuditLogger::exportEventsToFile(
		const QVector<AuditEvent> &events,
		const QString &outputPath) {

	QFile file(outputPath);
	if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
		return QString();
	}

	QTextStream out(&file);

	for (const auto &event : events) {
		QJsonDocument doc(exportEvent(event));
		out << doc.toJson(QJsonDocument::Compact) << "\n";
	}

	file.close();
	return outputPath;
}

// Maintenance
bool AuditLogger::purgeOldEvents(int daysToKeep) {
	if (!_db || !_db->isOpen()) {
		return false;
	}

	qint64 cutoffTime = QDateTime::currentDateTime().addDays(-daysToKeep).toSecsSinceEpoch();

	QSqlQuery query(*_db);
	query.prepare("DELETE FROM audit_log WHERE timestamp < :cutoff");
	query.bindValue(":cutoff", cutoffTime);

	return query.exec();
}

qint64 AuditLogger::getEventCount() const {
	if (!_db || !_db->isOpen()) {
		return 0;
	}

	QSqlQuery query(*_db);
	query.exec("SELECT COUNT(*) FROM audit_log");

	if (query.next()) {
		return query.value(0).toLongLong();
	}

	return 0;
}

// Private helpers
bool AuditLogger::storeEvent(const AuditEvent &event) {
	// Add to in-memory buffer
	_eventBuffer.append(event);
	if (_eventBuffer.size() > MAX_BUFFER_SIZE) {
		_eventBuffer.removeFirst();
	}

	// Write to file if path is set
	if (!_logFilePath.isEmpty()) {
		writeToLogFile(event);
	}

	// Store in database
	if (!_db || !_db->isOpen()) {
		return false;
	}

	QSqlQuery query(*_db);
	query.prepare(R"(
		INSERT INTO audit_log (
			event_type, event_subtype, user_id, tool_name, parameters,
			result_status, error_message, duration_ms, timestamp, metadata
		) VALUES (
			:event_type, :event_subtype, :user_id, :tool_name, :parameters,
			:result_status, :error_message, :duration_ms, :timestamp, :metadata
		)
	)");

	query.bindValue(":event_type", eventTypeToString(event.eventType));
	query.bindValue(":event_subtype", event.eventSubtype);
	query.bindValue(":user_id", event.userId.isEmpty() ? QVariant() : event.userId);
	query.bindValue(":tool_name", event.toolName.isEmpty() ? QVariant() : event.toolName);
	query.bindValue(":parameters", QJsonDocument(event.parameters).toJson(QJsonDocument::Compact));
	query.bindValue(":result_status", event.resultStatus.isEmpty() ? QVariant() : event.resultStatus);
	query.bindValue(":error_message", event.errorMessage.isEmpty() ? QVariant() : event.errorMessage);
	query.bindValue(":duration_ms", event.durationMs > 0 ? event.durationMs : QVariant());
	query.bindValue(":timestamp", event.timestamp.toSecsSinceEpoch());
	query.bindValue(":metadata", QJsonDocument(event.metadata).toJson(QJsonDocument::Compact));

	return query.exec();
}

AuditEvent AuditLogger::loadEventFromQuery(const QSqlQuery &query) const {
	AuditEvent event;
	event.id = query.value("id").toLongLong();
	event.eventType = stringToEventType(query.value("event_type").toString());
	event.eventSubtype = query.value("event_subtype").toString();
	event.userId = query.value("user_id").toString();
	event.toolName = query.value("tool_name").toString();

	QString paramsJson = query.value("parameters").toString();
	if (!paramsJson.isEmpty()) {
		event.parameters = QJsonDocument::fromJson(paramsJson.toUtf8()).object();
	}

	event.resultStatus = query.value("result_status").toString();
	event.errorMessage = query.value("error_message").toString();
	event.durationMs = query.value("duration_ms").toLongLong();
	event.timestamp = QDateTime::fromSecsSinceEpoch(query.value("timestamp").toLongLong());

	QString metadataJson = query.value("metadata").toString();
	if (!metadataJson.isEmpty()) {
		event.metadata = QJsonDocument::fromJson(metadataJson.toUtf8()).object();
	}

	return event;
}

bool AuditLogger::writeToLogFile(const AuditEvent &event) {
	QFile file(_logFilePath);
	if (!file.open(QIODevice::Append | QIODevice::Text)) {
		return false;
	}

	QTextStream out(&file);
	QJsonDocument doc(exportEvent(event));
	out << doc.toJson(QJsonDocument::Compact) << "\n";

	file.close();
	return true;
}

QString AuditLogger::eventTypeToString(AuditEventType type) const {
	switch (type) {
	case AuditEventType::ToolInvoked: return "tool_invoked";
	case AuditEventType::AuthEvent: return "auth_event";
	case AuditEventType::TelegramOp: return "telegram_op";
	case AuditEventType::SystemEvent: return "system_event";
	case AuditEventType::Error: return "error";
	default: return "unknown";
	}
}

AuditEventType AuditLogger::stringToEventType(const QString &str) const {
	if (str == "tool_invoked") return AuditEventType::ToolInvoked;
	if (str == "auth_event") return AuditEventType::AuthEvent;
	if (str == "telegram_op") return AuditEventType::TelegramOp;
	if (str == "system_event") return AuditEventType::SystemEvent;
	if (str == "error") return AuditEventType::Error;
	return AuditEventType::SystemEvent;
}

} // namespace MCP

// This file is part of Telegram Desktop MCP Server.
// Licensed under GPLv3 with OpenSSL exception.

#pragma once

#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtCore/QDateTime>
#include <QtCore/QJsonObject>
#include <QtCore/QJsonArray>
#include <QtSql/QSqlDatabase>

namespace MCP {

// Event types
enum class AuditEventType {
	ToolInvoked,      // MCP tool called
	AuthEvent,        // Authentication/authorization
	TelegramOp,       // Telegram operation (send, delete, edit)
	SystemEvent,      // Server start/stop, config change
	Error             // Error occurred
};

// Audit event
struct AuditEvent {
	qint64 id;
	AuditEventType eventType;
	QString eventSubtype;  // Specific operation
	QString userId;  // API key or user identifier
	QString toolName;
	QJsonObject parameters;
	QString resultStatus;  // "success", "failure", "partial"
	QString errorMessage;
	qint64 durationMs;
	QDateTime timestamp;
	QJsonObject metadata;
};

// Audit logger
class AuditLogger : public QObject {
	Q_OBJECT

public:
	explicit AuditLogger(QObject *parent = nullptr);
	~AuditLogger();

	// Initialization
	bool start(QSqlDatabase *db, const QString &logFilePath = QString());
	void stop();
	[[nodiscard]] bool isRunning() const { return _isRunning; }

	// Logging
	void logToolInvoked(
		const QString &toolName,
		const QJsonObject &parameters,
		const QString &userId = QString()
	);

	void logToolCompleted(
		const QString &toolName,
		const QString &status,
		qint64 durationMs,
		const QString &error = QString()
	);

	void logAuthEvent(
		const QString &event,
		const QString &userId,
		bool success,
		const QString &details = QString()
	);

	void logTelegramOp(
		const QString &operation,
		qint64 chatId,
		qint64 messageId,
		const QString &userId,
		bool success,
		const QString &error = QString()
	);

	void logSystemEvent(
		const QString &event,
		const QString &details = QString(),
		const QJsonObject &metadata = QJsonObject()
	);

	void logError(
		const QString &error,
		const QString &context,
		const QJsonObject &metadata = QJsonObject()
	);

	// Queries
	QVector<AuditEvent> queryEvents(
		AuditEventType eventType = static_cast<AuditEventType>(-1),  // All types
		const QString &userId = QString(),
		const QString &toolName = QString(),
		const QDateTime &startTime = QDateTime(),
		const QDateTime &endTime = QDateTime(),
		int limit = 100
	);

	QVector<AuditEvent> getRecentEvents(int limit = 50);
	QVector<AuditEvent> getEventsByUser(const QString &userId, int limit = 100);
	QVector<AuditEvent> getEventsByTool(const QString &toolName, int limit = 100);

	// Statistics
	struct AuditStatistics {
		int totalEvents;
		int toolInvocations;
		int authEvents;
		int telegramOps;
		int systemEvents;
		int errors;
		QMap<QString, int> toolCounts;  // tool -> count
		QMap<QString, int> userCounts;  // user -> count
		double avgDuration;  // Average operation duration
	};

	[[nodiscard]] AuditStatistics getStatistics(
		const QDateTime &start = QDateTime(),
		const QDateTime &end = QDateTime()
	);

	// Export
	QJsonObject exportEvent(const AuditEvent &event);
	QJsonArray exportEvents(const QVector<AuditEvent> &events);
	QString exportEventsToFile(
		const QVector<AuditEvent> &events,
		const QString &outputPath
	);

	// Maintenance
	bool purgeOldEvents(int daysToKeep);
	qint64 getEventCount() const;

Q_SIGNALS:
	void eventLogged(const AuditEvent &event);
	void error(const QString &errorMessage);

private:
	// Database operations
	bool storeEvent(const AuditEvent &event);
	AuditEvent loadEventFromQuery(const QSqlQuery &query) const;

	// File logging
	bool writeToLogFile(const AuditEvent &event);

	// Helpers
	QString eventTypeToString(AuditEventType type) const;
	AuditEventType stringToEventType(const QString &str) const;

	QSqlDatabase *_db = nullptr;
	QString _logFilePath;
	bool _isRunning = false;
	qint64 _nextEventId = 1;

	// In-memory buffer for recent events (performance optimization)
	static const int MAX_BUFFER_SIZE = 1000;
	QVector<AuditEvent> _eventBuffer;
};

} // namespace MCP

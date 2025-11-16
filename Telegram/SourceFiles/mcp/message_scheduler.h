// This file is part of Telegram Desktop MCP Server.
// Licensed under GPLv3 with OpenSSL exception.

#pragma once

#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtCore/QDateTime>
#include <QtCore/QTimer>
#include <QtCore/QJsonObject>
#include <QtSql/QSqlDatabase>

namespace MCP {

// Schedule type
enum class ScheduleType {
	Once,       // Send at specific time
	Recurring,  // Repeat on pattern
	Delayed     // Send after X seconds
};

// Recurrence pattern
enum class RecurrencePattern {
	None,
	Hourly,
	Daily,
	Weekly,
	Monthly,
	Custom  // Cron-like expression
};

// Scheduled message
struct ScheduledMessage {
	int id;
	qint64 chatId;
	QString content;
	ScheduleType scheduleType;
	QDateTime scheduledTime;  // For 'once'
	int delaySeconds;  // For 'delayed'
	RecurrencePattern recurrencePattern;
	QDateTime startTime;  // For recurring
	int maxOccurrences;  // For recurring
	int occurrencesSent;
	QDateTime lastSent;
	QDateTime nextScheduled;
	bool isActive;
	QString createdBy;
	QDateTime createdAt;
};

// Message scheduler
class MessageScheduler : public QObject {
	Q_OBJECT

public:
	explicit MessageScheduler(QObject *parent = nullptr);
	~MessageScheduler();

	// Initialization
	bool start(QSqlDatabase *db);
	void stop();
	[[nodiscard]] bool isRunning() const { return _isRunning; }

	// Schedule messages
	int scheduleOnce(
		qint64 chatId,
		const QString &content,
		const QDateTime &sendTime,
		const QString &createdBy = QString()
	);

	int scheduleRecurring(
		qint64 chatId,
		const QString &content,
		const QDateTime &startTime,
		RecurrencePattern pattern,
		int maxOccurrences = -1,  // -1 = infinite
		const QString &createdBy = QString()
	);

	int scheduleDelayed(
		qint64 chatId,
		const QString &content,
		int delaySeconds,
		const QString &createdBy = QString()
	);

	// Management
	bool cancelScheduledMessage(int scheduleId);
	bool updateScheduledMessage(int scheduleId, const QString &newContent);
	bool pauseScheduledMessage(int scheduleId);
	bool resumeScheduledMessage(int scheduleId);

	// Queries
	QVector<ScheduledMessage> getScheduledMessages(qint64 chatId = 0, bool activeOnly = true);
	ScheduledMessage getScheduledMessage(int scheduleId);
	int getActiveScheduleCount() const;

	// Export
	QJsonObject exportScheduledMessage(const ScheduledMessage &msg);
	QJsonArray exportAllScheduled();

Q_SIGNALS:
	void messageScheduled(int scheduleId, qint64 chatId, const QDateTime &sendTime);
	void messageSent(int scheduleId, qint64 chatId, qint64 messageId);
	void scheduleCancelled(int scheduleId);
	void error(const QString &errorMessage);

private Q_SLOTS:
	void checkScheduledMessages();

private:
	// Execution
	bool sendScheduledMessage(const ScheduledMessage &msg);
	void updateNextScheduledTime(const ScheduledMessage &msg);
	QDateTime calculateNextOccurrence(const ScheduledMessage &msg);

	// Database operations
	bool loadScheduledMessages();
	bool saveScheduledMessage(const ScheduledMessage &msg);
	bool updateScheduledMessageInDB(const ScheduledMessage &msg);
	bool deleteScheduledMessageFromDB(int scheduleId);

	// Helpers
	QString scheduleTypeToString(ScheduleType type) const;
	QString recurrencePatternToString(RecurrencePattern pattern) const;
	ScheduleType stringToScheduleType(const QString &str) const;
	RecurrencePattern stringToRecurrencePattern(const QString &str) const;

	QSqlDatabase *_db = nullptr;
	bool _isRunning = false;
	QTimer *_checkTimer = nullptr;
	QVector<ScheduledMessage> _scheduledMessages;
	int _nextScheduleId = 1;
};

} // namespace MCP

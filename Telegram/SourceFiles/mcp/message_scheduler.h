// This file is part of Telegram Desktop MCP Server.
// Licensed under GPLv3 with OpenSSL exception.

#pragma once

#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtCore/QJsonObject>
#include <QtCore/QJsonArray>
#include <QtCore/QDateTime>
#include <QtCore/QTimer>
#include <QtCore/QHash>
#include <QtCore/QVector>
#include <memory>

namespace Main {
class Session;
} // namespace Main

namespace MCP {

// Scheduled message status
enum class ScheduleStatus {
	Pending,      // Waiting to be sent
	Sending,      // Currently being sent
	Sent,         // Successfully sent
	Failed,       // Failed to send
	Cancelled     // Cancelled by user
};

// Scheduled message data
struct ScheduledMessage {
	qint64 scheduleId = 0;
	qint64 chatId = 0;
	QString chatTitle;
	QString text;
	QJsonObject media;
	QDateTime scheduledTime;
	QDateTime createdTime;
	ScheduleStatus status = ScheduleStatus::Pending;
	QString errorMessage;
	int retryCount = 0;
	bool recurring = false;
	QString recurrencePattern; // "daily", "weekly", "monthly", "custom"
	QJsonObject recurrenceData;
};

// Scheduler statistics
struct SchedulerStats {
	int totalScheduled = 0;
	int pendingCount = 0;
	int sentCount = 0;
	int failedCount = 0;
	int cancelledCount = 0;
	QDateTime lastScheduled;
	QDateTime lastSent;
};

// Message scheduler class
class MessageScheduler : public QObject {
	Q_OBJECT

public:
	explicit MessageScheduler(QObject *parent = nullptr);
	~MessageScheduler();

	// Initialization
	bool start(Main::Session *session);
	void stop();
	[[nodiscard]] bool isRunning() const { return _isRunning; }

	// Scheduling functions
	qint64 scheduleMessage(
		qint64 chatId,
		const QString &text,
		const QDateTime &scheduledTime,
		const QJsonObject &options = QJsonObject()
	);

	qint64 scheduleRecurringMessage(
		qint64 chatId,
		const QString &text,
		const QString &pattern,
		const QDateTime &startTime,
		const QJsonObject &recurrenceData = QJsonObject()
	);

	bool cancelScheduledMessage(qint64 scheduleId);
	bool updateScheduledMessage(qint64 scheduleId, const QJsonObject &updates);
	bool rescheduleMessage(qint64 scheduleId, const QDateTime &newTime);

	// Query functions
	QJsonArray listScheduledMessages(
		qint64 chatId = 0,
		ScheduleStatus status = ScheduleStatus::Pending
	);

	QJsonObject getScheduledMessage(qint64 scheduleId);
	
	QJsonArray getUpcomingMessages(int limit = 10);
	QJsonArray getFailedMessages();

	// Statistics
	SchedulerStats getStats() const;
	QJsonObject getSchedulerActivity();

	// Bulk operations
	int cancelAllScheduled(qint64 chatId);
	int rescheduleAll(qint64 chatId, int delayMinutes);

	// Recurrence patterns
	static bool validateRecurrencePattern(const QString &pattern);
	static QDateTime getNextOccurrence(
		const QDateTime &lastTime,
		const QString &pattern,
		const QJsonObject &data
	);

Q_SIGNALS:
	void messageScheduled(qint64 scheduleId, qint64 chatId);
	void messageSent(qint64 scheduleId, qint64 chatId, qint64 messageId);
	void messageFailed(qint64 scheduleId, const QString &error);
	void messageCancelled(qint64 scheduleId);
	void schedulerError(const QString &errorMessage);

private Q_SLOTS:
	void checkScheduledMessages();
	void handleSendResult(qint64 scheduleId, bool success, const QString &error);

private:
	// Sending
	void sendScheduledMessage(ScheduledMessage &message);
	void retryFailedMessage(ScheduledMessage &message);
	void handleRecurringMessage(ScheduledMessage &message);

	// Persistence
	bool loadScheduledMessages();
	bool saveScheduledMessage(const ScheduledMessage &message);
	bool updateScheduleStatus(qint64 scheduleId, ScheduleStatus status);
	bool deleteScheduledMessage(qint64 scheduleId);

	// Conversion
	QJsonObject scheduledMessageToJson(const ScheduledMessage &message) const;
	ScheduledMessage jsonToScheduledMessage(const QJsonObject &json) const;
	QString scheduleStatusToString(ScheduleStatus status) const;
	ScheduleStatus stringToScheduleStatus(const QString &str) const;

	// Time calculations
	QDateTime parseScheduleTime(const QString &timeStr) const;
	bool isTimeToSend(const QDateTime &scheduledTime) const;
	int getSecondsUntilNext() const;

	// Validation
	bool validateScheduleTime(const QDateTime &time, QString &error) const;
	bool validateChatId(qint64 chatId, QString &error) const;
	bool validateMessageText(const QString &text, QString &error) const;

	Main::Session *_session = nullptr;
	bool _isRunning = false;
	SchedulerStats _stats;

	// Storage
	QHash<qint64, ScheduledMessage> _scheduledMessages;
	qint64 _nextScheduleId = 1;

	// Timer for checking scheduled messages
	QTimer *_checkTimer = nullptr;
	int _checkIntervalSeconds = 60; // Check every minute

	// Retry configuration
	int _maxRetries = 3;
	int _retryDelaySeconds = 300; // 5 minutes

	// Persistence file
	QString _persistenceFilePath;
	bool _persistenceEnabled = true;
};

} // namespace MCP

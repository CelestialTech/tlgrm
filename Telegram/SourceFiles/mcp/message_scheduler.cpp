// This file is part of Telegram Desktop MCP Server.
// Licensed under GPLv3 with OpenSSL exception.

#include "message_scheduler.h"
#include "data/data_session.h"
#include "history/history.h"
#include "main/main_session.h"
#include "apiwrap.h"
#include "api/api_text_entities.h"
#include "data/data_peer.h"
#include "chat_archiver.h"

#include <QtCore/QFile>
#include <QtCore/QDir>
#include <QtCore/QStandardPaths>
#include <QtCore/QJsonDocument>
#include <QtSql/QSqlDatabase>
#include <QtSql/QSqlQuery>
#include <QtSql/QSqlError>

namespace MCP {

MessageScheduler::MessageScheduler(QObject *parent)
: QObject(parent) {
}

MessageScheduler::~MessageScheduler() {
	stop();
}

bool MessageScheduler::start(Main::Session *session) {
	if (_isRunning) {
		return true;
	}

	if (!session) {
		Q_EMIT schedulerError("Invalid session");
		return false;
	}

	_session = session;

	// Set up persistence file path
	QString dataPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
	_persistenceFilePath = dataPath + "/scheduled_messages.json";

	// Load previously scheduled messages
	if (_persistenceEnabled) {
		loadScheduledMessages();
	}

	// Set up timer for checking scheduled messages
	_checkTimer = new QTimer(this);
	connect(_checkTimer, &QTimer::timeout, this, &MessageScheduler::checkScheduledMessages);
	_checkTimer->start(_checkIntervalSeconds * 1000);

	_isRunning = true;

	return true;
}

void MessageScheduler::stop() {
	if (!_isRunning) {
		return;
	}

	if (_checkTimer) {
		_checkTimer->stop();
		delete _checkTimer;
		_checkTimer = nullptr;
	}

	_session = nullptr;
	_isRunning = false;
}

qint64 MessageScheduler::scheduleMessage(
	qint64 chatId,
	const QString &text,
	const QDateTime &scheduledTime,
	const QJsonObject &options
) {
	if (!_isRunning) {
		Q_EMIT schedulerError("Scheduler not running");
		return 0;
	}

	// Validation
	QString error;
	if (!validateScheduleTime(scheduledTime, error)) {
		Q_EMIT schedulerError(error);
		return 0;
	}
	if (!validateChatId(chatId, error)) {
		Q_EMIT schedulerError(error);
		return 0;
	}
	if (!validateMessageText(text, error)) {
		Q_EMIT schedulerError(error);
		return 0;
	}

	// Create scheduled message
	ScheduledMessage message;
	message.scheduleId = _nextScheduleId++;
	message.chatId = chatId;
	message.text = text;
	message.scheduledTime = scheduledTime;
	message.createdTime = QDateTime::currentDateTime();
	message.status = ScheduleStatus::Pending;
	message.recurring = false;

	// Parse options
	if (options.contains("media")) {
		message.media = options["media"].toObject();
	}

	// Store message
	_scheduledMessages[message.scheduleId] = message;

	// Save to disk
	if (_persistenceEnabled) {
		saveScheduledMessage(message);
	}

	// Update stats
	_stats.totalScheduled++;
	_stats.pendingCount++;
	_stats.lastScheduled = QDateTime::currentDateTime();

	Q_EMIT messageScheduled(message.scheduleId, chatId);

	return message.scheduleId;
}

qint64 MessageScheduler::scheduleRecurringMessage(
	qint64 chatId,
	const QString &text,
	const QString &pattern,
	const QDateTime &startTime,
	const QJsonObject &recurrenceData
) {
	if (!_isRunning) {
		Q_EMIT schedulerError("Scheduler not running");
		return 0;
	}

	// Validate recurrence pattern
	if (!validateRecurrencePattern(pattern)) {
		Q_EMIT schedulerError("Invalid recurrence pattern");
		return 0;
	}

	// Create scheduled message
	ScheduledMessage message;
	message.scheduleId = _nextScheduleId++;
	message.chatId = chatId;
	message.text = text;
	message.scheduledTime = startTime;
	message.createdTime = QDateTime::currentDateTime();
	message.status = ScheduleStatus::Pending;
	message.recurring = true;
	message.recurrencePattern = pattern;
	message.recurrenceData = recurrenceData;

	// Store message
	_scheduledMessages[message.scheduleId] = message;

	// Save to disk
	if (_persistenceEnabled) {
		saveScheduledMessage(message);
	}

	// Update stats
	_stats.totalScheduled++;
	_stats.pendingCount++;

	Q_EMIT messageScheduled(message.scheduleId, chatId);

	return message.scheduleId;
}

bool MessageScheduler::cancelScheduledMessage(qint64 scheduleId) {
	if (!_scheduledMessages.contains(scheduleId)) {
		return false;
	}

	auto &message = _scheduledMessages[scheduleId];
	
	// Only cancel if pending
	if (message.status != ScheduleStatus::Pending) {
		return false;
	}

	message.status = ScheduleStatus::Cancelled;
	updateScheduleStatus(scheduleId, ScheduleStatus::Cancelled);

	// Update stats
	_stats.pendingCount--;
	_stats.cancelledCount++;

	Q_EMIT messageCancelled(scheduleId);

	return true;
}

bool MessageScheduler::updateScheduledMessage(qint64 scheduleId, const QJsonObject &updates) {
	if (!_scheduledMessages.contains(scheduleId)) {
		return false;
	}

	auto &message = _scheduledMessages[scheduleId];

	// Only update if pending
	if (message.status != ScheduleStatus::Pending) {
		return false;
	}

	// Update fields
	if (updates.contains("text")) {
		message.text = updates["text"].toString();
	}
	if (updates.contains("scheduledTime")) {
		QString timeStr = updates["scheduledTime"].toString();
		message.scheduledTime = QDateTime::fromString(timeStr, Qt::ISODate);
	}
	if (updates.contains("media")) {
		message.media = updates["media"].toObject();
	}

	// Save changes
	if (_persistenceEnabled) {
		saveScheduledMessage(message);
	}

	return true;
}

bool MessageScheduler::rescheduleMessage(qint64 scheduleId, const QDateTime &newTime) {
	if (!_scheduledMessages.contains(scheduleId)) {
		return false;
	}

	auto &message = _scheduledMessages[scheduleId];

	// Only reschedule if pending or failed
	if (message.status != ScheduleStatus::Pending && message.status != ScheduleStatus::Failed) {
		return false;
	}

	// Validate new time
	QString error;
	if (!validateScheduleTime(newTime, error)) {
		Q_EMIT schedulerError(error);
		return false;
	}

	message.scheduledTime = newTime;
	message.status = ScheduleStatus::Pending;
	message.retryCount = 0;

	// Save changes
	if (_persistenceEnabled) {
		saveScheduledMessage(message);
	}

	return true;
}

QJsonArray MessageScheduler::listScheduledMessages(
	qint64 chatId,
	ScheduleStatus status
) {
	QJsonArray result;

	for (const auto &message : _scheduledMessages) {
		// Filter by chat ID if specified
		if (chatId != 0 && message.chatId != chatId) {
			continue;
		}

		// Filter by status
		if (message.status != status) {
			continue;
		}

		result.append(scheduledMessageToJson(message));
	}

	return result;
}

QJsonObject MessageScheduler::getScheduledMessage(qint64 scheduleId) {
	if (!_scheduledMessages.contains(scheduleId)) {
		QJsonObject error;
		error["error"] = "Schedule ID not found";
		return error;
	}

	return scheduledMessageToJson(_scheduledMessages[scheduleId]);
}

QJsonArray MessageScheduler::getUpcomingMessages(int limit) {
	// Collect all pending messages
	QVector<ScheduledMessage> pending;
	for (const auto &message : _scheduledMessages) {
		if (message.status == ScheduleStatus::Pending) {
			pending.append(message);
		}
	}

	// Sort by scheduled time
	std::sort(pending.begin(), pending.end(), [](const ScheduledMessage &a, const ScheduledMessage &b) {
		return a.scheduledTime < b.scheduledTime;
	});

	// Take first N
	QJsonArray result;
	for (int i = 0; i < qMin(limit, pending.size()); ++i) {
		result.append(scheduledMessageToJson(pending[i]));
	}

	return result;
}

QJsonArray MessageScheduler::getFailedMessages() {
	QJsonArray result;

	for (const auto &message : _scheduledMessages) {
		if (message.status == ScheduleStatus::Failed) {
			result.append(scheduledMessageToJson(message));
		}
	}

	return result;
}

SchedulerStats MessageScheduler::getStats() const {
	return _stats;
}

QJsonObject MessageScheduler::getSchedulerActivity() {
	QJsonObject activity;
	
	activity["totalScheduled"] = _stats.totalScheduled;
	activity["pending"] = _stats.pendingCount;
	activity["sent"] = _stats.sentCount;
	activity["failed"] = _stats.failedCount;
	activity["cancelled"] = _stats.cancelledCount;
	activity["lastScheduled"] = _stats.lastScheduled.toString(Qt::ISODate);
	activity["lastSent"] = _stats.lastSent.toString(Qt::ISODate);

	return activity;
}

int MessageScheduler::cancelAllScheduled(qint64 chatId) {
	int cancelled = 0;

	for (auto &message : _scheduledMessages) {
		if (message.chatId == chatId && message.status == ScheduleStatus::Pending) {
			message.status = ScheduleStatus::Cancelled;
			updateScheduleStatus(message.scheduleId, ScheduleStatus::Cancelled);
			cancelled++;
		}
	}

	_stats.pendingCount -= cancelled;
	_stats.cancelledCount += cancelled;

	return cancelled;
}

int MessageScheduler::rescheduleAll(qint64 chatId, int delayMinutes) {
	int rescheduled = 0;

	for (auto &message : _scheduledMessages) {
		if (message.chatId == chatId && message.status == ScheduleStatus::Pending) {
			message.scheduledTime = message.scheduledTime.addSecs(delayMinutes * 60);
			saveScheduledMessage(message);
			rescheduled++;
		}
	}

	return rescheduled;
}

bool MessageScheduler::validateRecurrencePattern(const QString &pattern) {
	QStringList validPatterns = {"daily", "weekly", "monthly", "yearly", "custom"};
	return validPatterns.contains(pattern.toLower());
}

QDateTime MessageScheduler::getNextOccurrence(
	const QDateTime &lastTime,
	const QString &pattern,
	const QJsonObject &data
) {
	QString p = pattern.toLower();

	if (p == "daily") {
		return lastTime.addDays(1);
	} else if (p == "weekly") {
		return lastTime.addDays(7);
	} else if (p == "monthly") {
		return lastTime.addMonths(1);
	} else if (p == "yearly") {
		return lastTime.addYears(1);
	} else if (p == "custom") {
		// Custom pattern from data
		if (data.contains("intervalDays")) {
			return lastTime.addDays(data["intervalDays"].toInt());
		}
	}

	return QDateTime();
}

// Private slots

void MessageScheduler::checkScheduledMessages() {
	if (!_isRunning || !_session) {
		return;
	}

	QDateTime now = QDateTime::currentDateTime();

	for (auto &message : _scheduledMessages) {
		// Check if it's time to send
		if (message.status == ScheduleStatus::Pending && isTimeToSend(message.scheduledTime)) {
			sendScheduledMessage(message);
		}

		// Check for retries
		if (message.status == ScheduleStatus::Failed && message.retryCount < _maxRetries) {
			QDateTime nextRetry = message.scheduledTime.addSecs(_retryDelaySeconds * (message.retryCount + 1));
			if (now >= nextRetry) {
				retryFailedMessage(message);
			}
		}
	}
}

void MessageScheduler::handleSendResult(qint64 scheduleId, bool success, const QString &error) {
	if (!_scheduledMessages.contains(scheduleId)) {
		return;
	}

	auto &message = _scheduledMessages[scheduleId];

	if (success) {
		message.status = ScheduleStatus::Sent;
		updateScheduleStatus(scheduleId, ScheduleStatus::Sent);

		_stats.pendingCount--;
		_stats.sentCount++;
		_stats.lastSent = QDateTime::currentDateTime();

		Q_EMIT messageSent(scheduleId, message.chatId, 0);

		// Handle recurring
		if (message.recurring) {
			handleRecurringMessage(message);
		}
	} else {
		message.status = ScheduleStatus::Failed;
		message.errorMessage = error;
		updateScheduleStatus(scheduleId, ScheduleStatus::Failed);

		_stats.pendingCount--;
		_stats.failedCount++;

		Q_EMIT messageFailed(scheduleId, error);
	}
}

// Private methods

void MessageScheduler::sendScheduledMessage(ScheduledMessage &message) {
	if (!_session) {
		handleSendResult(message.scheduleId, false, "Session not available");
		return;
	}

	message.status = ScheduleStatus::Sending;

	// Get the history/chat
	auto peer = _session->data().peer(PeerId(message.chatId));
	if (!peer) {
		qWarning() << "MessageScheduler: Invalid peer ID" << message.chatId;
		handleSendResult(message.scheduleId, false, "Invalid chat ID");
		return;
	}

	auto history = _session->data().history(peer);
	if (!history) {
		qWarning() << "MessageScheduler: History not found for peer" << message.chatId;
		handleSendResult(message.scheduleId, false, "Chat history not found");
		return;
	}

	// Use apiwrap to send message
	// Build the message
	Api::SendAction action(history);
	action.replyTo = {}; // No reply
	action.options = {};

	// TODO: Implement message sending with correct tdesktop API
	// MessageToSend requires: SendAction with textWithTags (not TextWithEntities)
	// and webPage draft information
	qWarning() << "TODO: Implement sendMessage with correct tdesktop API";
	qWarning() << "MessageScheduler: Stub - message not actually sent";

	// For now, just mark as failed so we can compile
	handleSendResult(message.scheduleId, false, "Not implemented: sendMessage API");
}

void MessageScheduler::retryFailedMessage(ScheduledMessage &message) {
	message.retryCount++;
	message.status = ScheduleStatus::Pending;
	sendScheduledMessage(message);
}

void MessageScheduler::handleRecurringMessage(ScheduledMessage &message) {
	// Calculate next occurrence
	QDateTime nextTime = getNextOccurrence(
		message.scheduledTime,
		message.recurrencePattern,
		message.recurrenceData
	);

	if (nextTime.isValid()) {
		// Create new scheduled message for next occurrence
		scheduleMessage(message.chatId, message.text, nextTime);
	}
}

bool MessageScheduler::loadScheduledMessages() {
	QFile file(_persistenceFilePath);
	if (!file.exists()) {
		return true; // Not an error if file doesn't exist yet
	}

	if (!file.open(QIODevice::ReadOnly)) {
		return false;
	}

	QByteArray data = file.readAll();
	file.close();

	QJsonDocument doc = QJsonDocument::fromJson(data);
	if (!doc.isArray()) {
		return false;
	}

	QJsonArray array = doc.array();
	for (int i = 0; i < array.size(); ++i) {
		ScheduledMessage message = jsonToScheduledMessage(array[i].toObject());
		_scheduledMessages[message.scheduleId] = message;

		// Update next schedule ID
		if (message.scheduleId >= _nextScheduleId) {
			_nextScheduleId = message.scheduleId + 1;
		}

		// Update stats
		if (message.status == ScheduleStatus::Pending) {
			_stats.pendingCount++;
		} else if (message.status == ScheduleStatus::Sent) {
			_stats.sentCount++;
		} else if (message.status == ScheduleStatus::Failed) {
			_stats.failedCount++;
		} else if (message.status == ScheduleStatus::Cancelled) {
			_stats.cancelledCount++;
		}
	}

	_stats.totalScheduled = _scheduledMessages.size();

	return true;
}

bool MessageScheduler::saveScheduledMessage(const ScheduledMessage &message) {
	// Load all messages, update this one, save back
	QJsonArray array;

	for (const auto &msg : _scheduledMessages) {
		array.append(scheduledMessageToJson(msg));
	}

	QJsonDocument doc(array);

	// Ensure directory exists
	QFileInfo fileInfo(_persistenceFilePath);
	QDir dir = fileInfo.dir();
	if (!dir.exists()) {
		dir.mkpath(".");
	}

	QFile file(_persistenceFilePath);
	if (!file.open(QIODevice::WriteOnly)) {
		return false;
	}

	file.write(doc.toJson());
	file.close();

	return true;
}

bool MessageScheduler::updateScheduleStatus(qint64 scheduleId, ScheduleStatus status) {
	if (!_scheduledMessages.contains(scheduleId)) {
		return false;
	}

	_scheduledMessages[scheduleId].status = status;

	if (_persistenceEnabled) {
		return saveScheduledMessage(_scheduledMessages[scheduleId]);
	}

	return true;
}

bool MessageScheduler::deleteScheduledMessage(qint64 scheduleId) {
	if (!_scheduledMessages.contains(scheduleId)) {
		return false;
	}

	_scheduledMessages.remove(scheduleId);

	// Save updated list
	if (_persistenceEnabled) {
		QJsonArray array;
		for (const auto &msg : _scheduledMessages) {
			array.append(scheduledMessageToJson(msg));
		}

		QJsonDocument doc(array);
		QFile file(_persistenceFilePath);
		if (file.open(QIODevice::WriteOnly)) {
			file.write(doc.toJson());
			file.close();
		}
	}

	return true;
}

QJsonObject MessageScheduler::scheduledMessageToJson(const ScheduledMessage &message) const {
	QJsonObject json;
	json["scheduleId"] = QString::number(message.scheduleId);
	json["chatId"] = QString::number(message.chatId);
	json["chatTitle"] = message.chatTitle;
	json["text"] = message.text;
	json["media"] = message.media;
	json["scheduledTime"] = message.scheduledTime.toString(Qt::ISODate);
	json["createdTime"] = message.createdTime.toString(Qt::ISODate);
	json["status"] = scheduleStatusToString(message.status);
	json["errorMessage"] = message.errorMessage;
	json["retryCount"] = message.retryCount;
	json["recurring"] = message.recurring;
	json["recurrencePattern"] = message.recurrencePattern;
	json["recurrenceData"] = message.recurrenceData;
	return json;
}

ScheduledMessage MessageScheduler::jsonToScheduledMessage(const QJsonObject &json) const {
	ScheduledMessage message;
	message.scheduleId = json["scheduleId"].toString().toLongLong();
	message.chatId = json["chatId"].toString().toLongLong();
	message.chatTitle = json["chatTitle"].toString();
	message.text = json["text"].toString();
	message.media = json["media"].toObject();
	message.scheduledTime = QDateTime::fromString(json["scheduledTime"].toString(), Qt::ISODate);
	message.createdTime = QDateTime::fromString(json["createdTime"].toString(), Qt::ISODate);
	message.status = stringToScheduleStatus(json["status"].toString());
	message.errorMessage = json["errorMessage"].toString();
	message.retryCount = json["retryCount"].toInt();
	message.recurring = json["recurring"].toBool();
	message.recurrencePattern = json["recurrencePattern"].toString();
	message.recurrenceData = json["recurrenceData"].toObject();
	return message;
}

QString MessageScheduler::scheduleStatusToString(ScheduleStatus status) const {
	switch (status) {
		case ScheduleStatus::Pending: return "pending";
		case ScheduleStatus::Sending: return "sending";
		case ScheduleStatus::Sent: return "sent";
		case ScheduleStatus::Failed: return "failed";
		case ScheduleStatus::Cancelled: return "cancelled";
	}
	return "unknown";
}

ScheduleStatus MessageScheduler::stringToScheduleStatus(const QString &str) const {
	if (str == "pending") return ScheduleStatus::Pending;
	if (str == "sending") return ScheduleStatus::Sending;
	if (str == "sent") return ScheduleStatus::Sent;
	if (str == "failed") return ScheduleStatus::Failed;
	if (str == "cancelled") return ScheduleStatus::Cancelled;
	return ScheduleStatus::Pending;
}

QDateTime MessageScheduler::parseScheduleTime(const QString &timeStr) const {
	// Try various formats
	QDateTime dt = QDateTime::fromString(timeStr, Qt::ISODate);
	if (dt.isValid()) return dt;

	dt = QDateTime::fromString(timeStr, "yyyy-MM-dd HH:mm:ss");
	if (dt.isValid()) return dt;

	return QDateTime();
}

bool MessageScheduler::isTimeToSend(const QDateTime &scheduledTime) const {
	QDateTime now = QDateTime::currentDateTime();
	return now >= scheduledTime;
}

int MessageScheduler::getSecondsUntilNext() const {
	QDateTime now = QDateTime::currentDateTime();
	QDateTime nextCheck = now.addSecs(_checkIntervalSeconds);

	qint64 seconds = now.secsTo(nextCheck);
	return static_cast<int>(seconds);
}

bool MessageScheduler::validateScheduleTime(const QDateTime &time, QString &error) const {
	if (!time.isValid()) {
		error = "Invalid date/time";
		return false;
	}

	QDateTime now = QDateTime::currentDateTime();
	if (time <= now) {
		error = "Scheduled time must be in the future";
		return false;
	}

	return true;
}

bool MessageScheduler::validateChatId(qint64 chatId, QString &error) const {
	if (chatId == 0) {
		error = "Invalid chat ID";
		return false;
	}

	// TODO: Check if chat exists in session
	return true;
}

bool MessageScheduler::validateMessageText(const QString &text, QString &error) const {
	if (text.isEmpty()) {
		error = "Message text cannot be empty";
		return false;
	}

	if (text.length() > 4096) {
		error = "Message text too long (max 4096 characters)";
		return false;
	}

	return true;
}

} // namespace MCP

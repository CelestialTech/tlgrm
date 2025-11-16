// This file is part of Telegram Desktop MCP Server.
// Licensed under GPLv3 with OpenSSL exception.

#include "message_scheduler.h"

#include <QtCore/QTimer>
#include <QtCore/QDateTime>
#include <QtSql/QSqlQuery>
#include <QtSql/QSqlError>

namespace MCP {

MessageScheduler::MessageScheduler(QObject *parent)
	: QObject(parent)
	, _checkTimer(new QTimer(this)) {

	connect(_checkTimer, &QTimer::timeout, this, &MessageScheduler::checkScheduledMessages);
}

MessageScheduler::~MessageScheduler() {
	stop();
}

bool MessageScheduler::start(QSqlDatabase *db) {
	if (_isRunning || !db || !db->isOpen()) {
		return false;
	}

	_db = db;

	// Load scheduled messages from database
	if (!loadScheduledMessages()) {
		return false;
	}

	// Start checking every minute
	_checkTimer->start(60000);  // 60 seconds

	_isRunning = true;
	return true;
}

void MessageScheduler::stop() {
	if (!_isRunning) {
		return;
	}

	_checkTimer->stop();
	_db = nullptr;
	_isRunning = false;
}

// Schedule once
int MessageScheduler::scheduleOnce(
		qint64 chatId,
		const QString &content,
		const QDateTime &sendTime,
		const QString &createdBy) {

	ScheduledMessage msg;
	msg.id = _nextScheduleId++;
	msg.chatId = chatId;
	msg.content = content;
	msg.scheduleType = ScheduleType::Once;
	msg.scheduledTime = sendTime;
	msg.nextScheduled = sendTime;
	msg.isActive = true;
	msg.createdBy = createdBy;
	msg.createdAt = QDateTime::currentDateTime();

	if (!saveScheduledMessage(msg)) {
		return -1;
	}

	_scheduledMessages.append(msg);

	Q_EMIT messageScheduled(msg.id, chatId, sendTime);
	return msg.id;
}

// Schedule recurring
int MessageScheduler::scheduleRecurring(
		qint64 chatId,
		const QString &content,
		const QDateTime &startTime,
		RecurrencePattern pattern,
		int maxOccurrences,
		const QString &createdBy) {

	ScheduledMessage msg;
	msg.id = _nextScheduleId++;
	msg.chatId = chatId;
	msg.content = content;
	msg.scheduleType = ScheduleType::Recurring;
	msg.startTime = startTime;
	msg.recurrencePattern = pattern;
	msg.maxOccurrences = maxOccurrences;
	msg.occurrencesSent = 0;
	msg.nextScheduled = startTime;
	msg.isActive = true;
	msg.createdBy = createdBy;
	msg.createdAt = QDateTime::currentDateTime();

	if (!saveScheduledMessage(msg)) {
		return -1;
	}

	_scheduledMessages.append(msg);

	Q_EMIT messageScheduled(msg.id, chatId, startTime);
	return msg.id;
}

// Schedule delayed
int MessageScheduler::scheduleDelayed(
		qint64 chatId,
		const QString &content,
		int delaySeconds,
		const QString &createdBy) {

	QDateTime sendTime = QDateTime::currentDateTime().addSecs(delaySeconds);

	ScheduledMessage msg;
	msg.id = _nextScheduleId++;
	msg.chatId = chatId;
	msg.content = content;
	msg.scheduleType = ScheduleType::Delayed;
	msg.delaySeconds = delaySeconds;
	msg.nextScheduled = sendTime;
	msg.isActive = true;
	msg.createdBy = createdBy;
	msg.createdAt = QDateTime::currentDateTime();

	if (!saveScheduledMessage(msg)) {
		return -1;
	}

	_scheduledMessages.append(msg);

	Q_EMIT messageScheduled(msg.id, chatId, sendTime);
	return msg.id;
}

// Cancel scheduled message
bool MessageScheduler::cancelScheduledMessage(int scheduleId) {
	for (int i = 0; i < _scheduledMessages.size(); ++i) {
		if (_scheduledMessages[i].id == scheduleId) {
			_scheduledMessages[i].isActive = false;
			updateScheduledMessageInDB(_scheduledMessages[i]);
			Q_EMIT scheduleCancelled(scheduleId);
			return true;
		}
	}
	return false;
}

// Update scheduled message
bool MessageScheduler::updateScheduledMessage(int scheduleId, const QString &newContent) {
	for (int i = 0; i < _scheduledMessages.size(); ++i) {
		if (_scheduledMessages[i].id == scheduleId) {
			_scheduledMessages[i].content = newContent;
			updateScheduledMessageInDB(_scheduledMessages[i]);
			return true;
		}
	}
	return false;
}

// Pause scheduled message
bool MessageScheduler::pauseScheduledMessage(int scheduleId) {
	for (int i = 0; i < _scheduledMessages.size(); ++i) {
		if (_scheduledMessages[i].id == scheduleId) {
			_scheduledMessages[i].isActive = false;
			updateScheduledMessageInDB(_scheduledMessages[i]);
			return true;
		}
	}
	return false;
}

// Resume scheduled message
bool MessageScheduler::resumeScheduledMessage(int scheduleId) {
	for (int i = 0; i < _scheduledMessages.size(); ++i) {
		if (_scheduledMessages[i].id == scheduleId) {
			_scheduledMessages[i].isActive = true;
			updateScheduledMessageInDB(_scheduledMessages[i]);
			return true;
		}
	}
	return false;
}

// Get scheduled messages
QVector<ScheduledMessage> MessageScheduler::getScheduledMessages(qint64 chatId, bool activeOnly) {
	QVector<ScheduledMessage> result;

	for (const auto &msg : _scheduledMessages) {
		if ((chatId == 0 || msg.chatId == chatId) &&
		    (!activeOnly || msg.isActive)) {
			result.append(msg);
		}
	}

	return result;
}

// Get specific scheduled message
ScheduledMessage MessageScheduler::getScheduledMessage(int scheduleId) {
	for (const auto &msg : _scheduledMessages) {
		if (msg.id == scheduleId) {
			return msg;
		}
	}
	return ScheduledMessage();
}

// Get active schedule count
int MessageScheduler::getActiveScheduleCount() const {
	int count = 0;
	for (const auto &msg : _scheduledMessages) {
		if (msg.isActive) {
			count++;
		}
	}
	return count;
}

// Export scheduled message
QJsonObject MessageScheduler::exportScheduledMessage(const ScheduledMessage &msg) {
	QJsonObject json;
	json["id"] = msg.id;
	json["chat_id"] = msg.chatId;
	json["content"] = msg.content;
	json["schedule_type"] = scheduleTypeToString(msg.scheduleType);
	json["is_active"] = msg.isActive;
	json["created_by"] = msg.createdBy;
	json["created_at"] = msg.createdAt.toString(Qt::ISODate);

	if (msg.scheduleType == ScheduleType::Once) {
		json["scheduled_time"] = msg.scheduledTime.toString(Qt::ISODate);
	} else if (msg.scheduleType == ScheduleType::Recurring) {
		json["start_time"] = msg.startTime.toString(Qt::ISODate);
		json["recurrence_pattern"] = recurrencePatternToString(msg.recurrencePattern);
		json["max_occurrences"] = msg.maxOccurrences;
		json["occurrences_sent"] = msg.occurrencesSent;
		if (msg.lastSent.isValid()) {
			json["last_sent"] = msg.lastSent.toString(Qt::ISODate);
		}
	} else if (msg.scheduleType == ScheduleType::Delayed) {
		json["delay_seconds"] = msg.delaySeconds;
	}

	if (msg.nextScheduled.isValid()) {
		json["next_scheduled"] = msg.nextScheduled.toString(Qt::ISODate);
	}

	return json;
}

// Export all scheduled
QJsonArray MessageScheduler::exportAllScheduled() {
	QJsonArray array;
	for (const auto &msg : _scheduledMessages) {
		if (msg.isActive) {
			array.append(exportScheduledMessage(msg));
		}
	}
	return array;
}

// Check and send scheduled messages
void MessageScheduler::checkScheduledMessages() {
	QDateTime now = QDateTime::currentDateTime();

	for (int i = 0; i < _scheduledMessages.size(); ++i) {
		ScheduledMessage &msg = _scheduledMessages[i];

		if (!msg.isActive) {
			continue;
		}

		// Check if it's time to send
		if (msg.nextScheduled.isValid() && msg.nextScheduled <= now) {
			if (sendScheduledMessage(msg)) {
				// Update next scheduled time for recurring messages
				if (msg.scheduleType == ScheduleType::Recurring) {
					msg.occurrencesSent++;
					msg.lastSent = now;

					// Check if max occurrences reached
					if (msg.maxOccurrences > 0 && msg.occurrencesSent >= msg.maxOccurrences) {
						msg.isActive = false;
					} else {
						msg.nextScheduled = calculateNextOccurrence(msg);
					}

					updateScheduledMessageInDB(msg);
				} else {
					// One-time or delayed message - mark as inactive
					msg.isActive = false;
					updateScheduledMessageInDB(msg);
				}
			}
		}
	}
}

// Send scheduled message
bool MessageScheduler::sendScheduledMessage(const ScheduledMessage &msg) {
	// TODO: Implement actual message sending via tdesktop API
	// For now, just emit signal

	Q_EMIT messageSent(msg.id, msg.chatId, 0);  // messageId placeholder
	return true;
}

// Calculate next occurrence for recurring messages
QDateTime MessageScheduler::calculateNextOccurrence(const ScheduledMessage &msg) {
	if (msg.scheduleType != ScheduleType::Recurring) {
		return QDateTime();
	}

	QDateTime current = msg.lastSent.isValid() ? msg.lastSent : msg.startTime;

	switch (msg.recurrencePattern) {
	case RecurrencePattern::Hourly:
		return current.addSecs(3600);

	case RecurrencePattern::Daily:
		return current.addDays(1);

	case RecurrencePattern::Weekly:
		return current.addDays(7);

	case RecurrencePattern::Monthly:
		return current.addMonths(1);

	default:
		return QDateTime();
	}
}

// Database operations
bool MessageScheduler::loadScheduledMessages() {
	if (!_db || !_db->isOpen()) {
		return false;
	}

	QSqlQuery query(*_db);
	query.exec("SELECT * FROM scheduled_messages WHERE is_active = 1");

	while (query.next()) {
		ScheduledMessage msg;
		msg.id = query.value("id").toInt();
		msg.chatId = query.value("chat_id").toLongLong();
		msg.content = query.value("content").toString();
		msg.scheduleType = stringToScheduleType(query.value("schedule_type").toString());
		msg.isActive = query.value("is_active").toBool();
		msg.createdBy = query.value("created_by").toString();
		msg.createdAt = QDateTime::fromSecsSinceEpoch(query.value("created_at").toLongLong());

		if (!query.value("scheduled_time").isNull()) {
			msg.scheduledTime = QDateTime::fromSecsSinceEpoch(query.value("scheduled_time").toLongLong());
		}

		if (!query.value("delay_seconds").isNull()) {
			msg.delaySeconds = query.value("delay_seconds").toInt();
		}

		msg.recurrencePattern = stringToRecurrencePattern(query.value("recurrence_pattern").toString());

		if (!query.value("start_time").isNull()) {
			msg.startTime = QDateTime::fromSecsSinceEpoch(query.value("start_time").toLongLong());
		}

		if (!query.value("max_occurrences").isNull()) {
			msg.maxOccurrences = query.value("max_occurrences").toInt();
		}

		msg.occurrencesSent = query.value("occurrences_sent").toInt();

		if (!query.value("last_sent").isNull()) {
			msg.lastSent = QDateTime::fromSecsSinceEpoch(query.value("last_sent").toLongLong());
		}

		if (!query.value("next_scheduled").isNull()) {
			msg.nextScheduled = QDateTime::fromSecsSinceEpoch(query.value("next_scheduled").toLongLong());
		}

		_scheduledMessages.append(msg);

		if (msg.id >= _nextScheduleId) {
			_nextScheduleId = msg.id + 1;
		}
	}

	return true;
}

bool MessageScheduler::saveScheduledMessage(const ScheduledMessage &msg) {
	if (!_db || !_db->isOpen()) {
		return false;
	}

	QSqlQuery query(*_db);
	query.prepare(R"(
		INSERT INTO scheduled_messages (
			id, chat_id, content, schedule_type, scheduled_time, delay_seconds,
			recurrence_pattern, start_time, max_occurrences, occurrences_sent,
			last_sent, next_scheduled, is_active, created_by, created_at
		) VALUES (
			:id, :chat_id, :content, :schedule_type, :scheduled_time, :delay_seconds,
			:recurrence_pattern, :start_time, :max_occurrences, :occurrences_sent,
			:last_sent, :next_scheduled, :is_active, :created_by, :created_at
		)
	)");

	query.bindValue(":id", msg.id);
	query.bindValue(":chat_id", msg.chatId);
	query.bindValue(":content", msg.content);
	query.bindValue(":schedule_type", scheduleTypeToString(msg.scheduleType));
	query.bindValue(":scheduled_time", msg.scheduledTime.isValid() ? msg.scheduledTime.toSecsSinceEpoch() : QVariant());
	query.bindValue(":delay_seconds", msg.delaySeconds > 0 ? msg.delaySeconds : QVariant());
	query.bindValue(":recurrence_pattern", recurrencePatternToString(msg.recurrencePattern));
	query.bindValue(":start_time", msg.startTime.isValid() ? msg.startTime.toSecsSinceEpoch() : QVariant());
	query.bindValue(":max_occurrences", msg.maxOccurrences > 0 ? msg.maxOccurrences : QVariant());
	query.bindValue(":occurrences_sent", msg.occurrencesSent);
	query.bindValue(":last_sent", msg.lastSent.isValid() ? msg.lastSent.toSecsSinceEpoch() : QVariant());
	query.bindValue(":next_scheduled", msg.nextScheduled.isValid() ? msg.nextScheduled.toSecsSinceEpoch() : QVariant());
	query.bindValue(":is_active", msg.isActive);
	query.bindValue(":created_by", msg.createdBy);
	query.bindValue(":created_at", msg.createdAt.toSecsSinceEpoch());

	return query.exec();
}

bool MessageScheduler::updateScheduledMessageInDB(const ScheduledMessage &msg) {
	if (!_db || !_db->isOpen()) {
		return false;
	}

	QSqlQuery query(*_db);
	query.prepare(R"(
		UPDATE scheduled_messages SET
			content = :content,
			occurrences_sent = :occurrences_sent,
			last_sent = :last_sent,
			next_scheduled = :next_scheduled,
			is_active = :is_active,
			updated_at = :updated_at
		WHERE id = :id
	)");

	query.bindValue(":content", msg.content);
	query.bindValue(":occurrences_sent", msg.occurrencesSent);
	query.bindValue(":last_sent", msg.lastSent.isValid() ? msg.lastSent.toSecsSinceEpoch() : QVariant());
	query.bindValue(":next_scheduled", msg.nextScheduled.isValid() ? msg.nextScheduled.toSecsSinceEpoch() : QVariant());
	query.bindValue(":is_active", msg.isActive);
	query.bindValue(":updated_at", QDateTime::currentSecsSinceEpoch());
	query.bindValue(":id", msg.id);

	return query.exec();
}

bool MessageScheduler::deleteScheduledMessageFromDB(int scheduleId) {
	if (!_db || !_db->isOpen()) {
		return false;
	}

	QSqlQuery query(*_db);
	query.prepare("DELETE FROM scheduled_messages WHERE id = :id");
	query.bindValue(":id", scheduleId);

	return query.exec();
}

// Helper conversions
QString MessageScheduler::scheduleTypeToString(ScheduleType type) const {
	switch (type) {
	case ScheduleType::Once: return "once";
	case ScheduleType::Recurring: return "recurring";
	case ScheduleType::Delayed: return "delayed";
	default: return "once";
	}
}

QString MessageScheduler::recurrencePatternToString(RecurrencePattern pattern) const {
	switch (pattern) {
	case RecurrencePattern::Hourly: return "hourly";
	case RecurrencePattern::Daily: return "daily";
	case RecurrencePattern::Weekly: return "weekly";
	case RecurrencePattern::Monthly: return "monthly";
	case RecurrencePattern::Custom: return "custom";
	default: return "none";
	}
}

ScheduleType MessageScheduler::stringToScheduleType(const QString &str) const {
	if (str == "once") return ScheduleType::Once;
	if (str == "recurring") return ScheduleType::Recurring;
	if (str == "delayed") return ScheduleType::Delayed;
	return ScheduleType::Once;
}

RecurrencePattern MessageScheduler::stringToRecurrencePattern(const QString &str) const {
	if (str == "hourly") return RecurrencePattern::Hourly;
	if (str == "daily") return RecurrencePattern::Daily;
	if (str == "weekly") return RecurrencePattern::Weekly;
	if (str == "monthly") return RecurrencePattern::Monthly;
	if (str == "custom") return RecurrencePattern::Custom;
	return RecurrencePattern::None;
}

} // namespace MCP

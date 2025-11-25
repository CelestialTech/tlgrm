// This file is part of Telegram Desktop MCP Server.
// Licensed under GPLv3 with OpenSSL exception.

#include "analytics.h"
#include "chat_archiver.h"
#include "data/data_session.h"
#include "history/history.h"
#include "history/history_item.h"
#include "data/data_peer.h"
#include "data/data_user.h"

#include <QtCore/QFile>
#include <QtCore/QTextStream>
#include <QtCore/QJsonDocument>
#include <QtCore/QRegularExpression>
#include <QtCore/QtMath>
#include <QtSql/QSqlError>
#include <algorithm>

namespace MCP {

Analytics::Analytics(QObject *parent)
: QObject(parent) {
	initializeStopWords();
}

Analytics::~Analytics() {
	stop();
}

bool Analytics::start(Data::Session *session, ChatArchiver *archiver) {
	if (_isRunning) {
		return true;
	}

	if (!session || !archiver) {
		Q_EMIT error("Invalid session or archiver");
		return false;
	}

	_session = session;
	_archiver = archiver;
	_isRunning = true;

	return true;
}

void Analytics::stop() {
	if (!_isRunning) {
		return;
	}

	_session = nullptr;
	_archiver = nullptr;
	_isRunning = false;
	clearCache();
}

QJsonObject Analytics::getMessageStatistics(
	qint64 chatId,
	const QString &period,
	const QDateTime &startDate,
	const QDateTime &endDate
) {
	if (!_isRunning || !_archiver) {
		QJsonObject error;
		error["error"] = "Analytics not running";
		return error;
	}

	// Check cache
	QString cacheKey = getCacheKey(chatId, QString("msgstats_%1").arg(period));
	if (isCacheValid(cacheKey)) {
		return getCacheValue(cacheKey);
	}

	// Parse time range
	auto range = parseTimeRange(period, startDate, endDate);

	// Collect statistics from archiver database
	auto stats = collectMessageStats(chatId, range);

	// Convert to JSON
	QJsonObject result = messageStatsToJson(stats);

	// Cache the result
	setCacheValue(cacheKey, result);

	return result;
}

QJsonObject Analytics::getUserActivity(
	qint64 userId,
	qint64 chatId,
	const QString &period
) {
	if (!_isRunning || !_archiver) {
		QJsonObject error;
		error["error"] = "Analytics not running";
		return error;
	}

	// Check cache
	QString cacheKey = getCacheKey(chatId, QString("useract_%1_%2").arg(userId).arg(period));
	if (isCacheValid(cacheKey)) {
		return getCacheValue(cacheKey);
	}

	// Parse time range
	auto range = parseTimeRange(period);

	// Collect user activity
	auto activity = collectUserActivity(userId, chatId, range);

	// Convert to JSON
	QJsonObject result = userActivityToJson(activity);

	// Cache the result
	setCacheValue(cacheKey, result);

	return result;
}

QJsonObject Analytics::getChatActivity(
	qint64 chatId,
	const QString &period
) {
	if (!_isRunning || !_archiver) {
		QJsonObject error;
		error["error"] = "Analytics not running";
		return error;
	}

	// Check cache
	QString cacheKey = getCacheKey(chatId, QString("chatact_%1").arg(period));
	if (isCacheValid(cacheKey)) {
		return getCacheValue(cacheKey);
	}

	// Parse time range
	auto range = parseTimeRange(period);

	// Collect chat activity
	auto activity = collectChatActivity(chatId, range);

	// Convert to JSON
	QJsonObject result = chatActivityToJson(activity);

	// Cache the result
	setCacheValue(cacheKey, result);

	return result;
}

QJsonArray Analytics::getTimeSeries(
	qint64 chatId,
	const QString &granularity,
	const QDateTime &startDate,
	const QDateTime &endDate
) {
	if (!_isRunning || !_archiver) {
		return QJsonArray();
	}

	// Parse time range
	auto range = parseTimeRange("custom", startDate, endDate);

	// Generate time series
	auto points = generateTimeSeries(chatId, granularity, range);

	// Convert to JSON
	return timeSeriesPointsToJson(points);
}

QJsonArray Analytics::getTopUsers(
	qint64 chatId,
	int limit,
	const QString &metric
) {
	if (!_isRunning || !_archiver || !_archiver->isRunning()) {
		return QJsonArray();
	}

	// Query archiver database for user statistics
	auto db = _archiver->database();
	QSqlQuery query(db);

	QString sql;
	if (metric == "messages") {
		sql = "SELECT user_id, user_name, COUNT(*) as count "
		      "FROM messages WHERE chat_id = ? "
		      "GROUP BY user_id ORDER BY count DESC LIMIT ?";
	} else if (metric == "words") {
		sql = "SELECT user_id, user_name, SUM(word_count) as count "
		      "FROM messages WHERE chat_id = ? "
		      "GROUP BY user_id ORDER BY count DESC LIMIT ?";
	} else {
		sql = "SELECT user_id, user_name, COUNT(*) as count "
		      "FROM messages WHERE chat_id = ? "
		      "GROUP BY user_id ORDER BY count DESC LIMIT ?";
	}

	query.prepare(sql);
	query.addBindValue(chatId);
	query.addBindValue(limit);

	QJsonArray result;
	if (query.exec()) {
		while (query.next()) {
			QJsonObject user;
			user["userId"] = QString::number(query.value(0).toLongLong());
			user["userName"] = query.value(1).toString();
			user["count"] = query.value(2).toInt();
			result.append(user);
		}
	}

	return result;
}

QJsonArray Analytics::getTopWords(
	qint64 chatId,
	int limit,
	int minLength
) {
	if (!_isRunning || !_archiver) {
		return QJsonArray();
	}

	// Parse time range (all time)
	auto range = parseTimeRange("all");

	// Analyze word frequency
	auto wordFreq = analyzeWordFrequency(chatId, range);

	// Filter by minimum length and convert to sorted vector
	QVector<WordFrequency> frequencies;
	int totalWords = 0;
	for (auto it = wordFreq.constBegin(); it != wordFreq.constEnd(); ++it) {
		if (it.key().length() >= minLength && !isStopWord(it.key())) {
			WordFrequency wf;
			wf.word = it.key();
			wf.count = it.value();
			frequencies.append(wf);
			totalWords += it.value();
		}
	}

	// Sort by frequency
	std::sort(frequencies.begin(), frequencies.end(), [](const WordFrequency &a, const WordFrequency &b) {
		return a.count > b.count;
	});

	// Take top N and calculate percentages
	QVector<WordFrequency> topWords;
	for (int i = 0; i < qMin(limit, frequencies.size()); ++i) {
		frequencies[i].percentage = totalWords > 0 ? (100.0 * frequencies[i].count / totalWords) : 0.0;
		topWords.append(frequencies[i]);
	}

	return wordFrequenciesToJson(topWords);
}

QString Analytics::exportAnalytics(
	qint64 chatId,
	const QString &format,
	const QString &outputPath
) {
	if (!_isRunning) {
		return QString();
	}

	// Collect comprehensive analytics
	QJsonObject analytics;
	analytics["chatId"] = QString::number(chatId);
	analytics["exportDate"] = QDateTime::currentDateTime().toString(Qt::ISODate);
	analytics["messageStats"] = getMessageStatistics(chatId);
	analytics["chatActivity"] = getChatActivity(chatId);
	analytics["topUsers"] = getTopUsers(chatId, 10);
	analytics["topWords"] = getTopWords(chatId, 20);
	analytics["trends"] = getTrends(chatId);

	// Determine output path
	QString path = outputPath;
	if (path.isEmpty()) {
		path = QString("analytics_%1_%2.%3")
			.arg(chatId)
			.arg(QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss"))
			.arg(format.toLower());
	}

	// Export based on format
	if (format.toLower() == "json") {
		return exportToJSON(analytics, path);
	} else if (format.toLower() == "csv") {
		return exportToCSV(analytics, path);
	} else if (format.toLower() == "html") {
		return exportToHTML(analytics, path);
	}

	return QString();
}

QJsonObject Analytics::getTrends(
	qint64 chatId,
	const QString &metric,
	int daysBack
) {
	if (!_isRunning || !_archiver) {
		QJsonObject error;
		error["error"] = "Analytics not running";
		return error;
	}

	// Get time series data
	QDateTime endDate = QDateTime::currentDateTime();
	QDateTime startDate = endDate.addDays(-daysBack);
	auto timeSeries = getTimeSeries(chatId, "daily", startDate, endDate);

	// Extract values for trend analysis
	QVector<double> values;
	for (int i = 0; i < timeSeries.size(); ++i) {
		auto point = timeSeries[i].toObject();
		if (metric == "messages") {
			values.append(point["messageCount"].toInt());
		} else if (metric == "users") {
			values.append(point["userCount"].toInt());
		}
	}

	// Detect trend
	QString trend = detectTrend(values);
	double growthRate = calculateGrowthRate(values);

	QJsonObject result;
	result["metric"] = metric;
	result["period"] = QString("%1 days").arg(daysBack);
	result["trend"] = trend;
	result["growthRate"] = growthRate;
	result["dataPoints"] = timeSeries;

	return result;
}

QJsonObject Analytics::compareChats(
	const QVector<qint64> &chatIds,
	const QString &metric
) {
	if (!_isRunning) {
		QJsonObject error;
		error["error"] = "Analytics not running";
		return error;
	}

	QJsonArray comparisons;
	for (qint64 chatId : chatIds) {
		QJsonObject chatData;
		chatData["chatId"] = QString::number(chatId);
		
		if (metric == "activity") {
			chatData["data"] = getChatActivity(chatId);
		} else if (metric == "messages") {
			chatData["data"] = getMessageStatistics(chatId);
		}
		
		comparisons.append(chatData);
	}

	QJsonObject result;
	result["metric"] = metric;
	result["chats"] = comparisons;

	return result;
}

QJsonObject Analytics::compareUsers(
	qint64 chatId,
	const QVector<qint64> &userIds
) {
	if (!_isRunning) {
		QJsonObject error;
		error["error"] = "Analytics not running";
		return error;
	}

	QJsonArray comparisons;
	for (qint64 userId : userIds) {
		QJsonObject userData;
		userData["userId"] = QString::number(userId);
		userData["data"] = getUserActivity(userId, chatId);
		comparisons.append(userData);
	}

	QJsonObject result;
	result["chatId"] = QString::number(chatId);
	result["users"] = comparisons;

	return result;
}

QJsonObject Analytics::getLiveActivity(qint64 chatId) {
	// Stub implementation - would need real-time monitoring
	QJsonObject result;
	result["chatId"] = QString::number(chatId);
	result["timestamp"] = QDateTime::currentDateTime().toString(Qt::ISODate);
	result["activeUsers"] = 0;
	result["messagesLastHour"] = 0;
	return result;
}

QJsonArray Analytics::getActiveChats(int limit) {
	// Stub implementation - would query recent activity
	return QJsonArray();
}

void Analytics::clearCache() {
	_cache.clear();
}

void Analytics::refreshCache(qint64 chatId) {
	// Remove cached entries for this chat
	QStringList keysToRemove;
	for (auto it = _cache.constBegin(); it != _cache.constEnd(); ++it) {
		if (it.key().contains(QString::number(chatId))) {
			keysToRemove.append(it.key());
		}
	}
	
	for (const QString &key : keysToRemove) {
		_cache.remove(key);
	}

	Q_EMIT cacheRefreshed();
}

// Private methods implementation

MessageStats Analytics::collectMessageStats(
	qint64 chatId,
	const AnalyticsTimeRange &range
) {
	MessageStats stats;

	if (!_archiver || !_archiver->isRunning()) {
		qWarning() << "Analytics: Archiver not running";
		return stats;
	}

	auto db = _archiver->database();
	if (!db.isOpen()) {
		qWarning() << "Analytics: Database not open";
		return stats;
	}

	QSqlQuery query(db);

	// Build WHERE clause for time range
	QString whereClause = "chat_id = ?";
	QVector<QVariant> bindings = {chatId};

	if (!range.start.isNull()) {
		whereClause += " AND timestamp >= ?";
		bindings.append(range.start.toSecsSinceEpoch());
	}
	if (!range.end.isNull()) {
		whereClause += " AND timestamp <= ?";
		bindings.append(range.end.toSecsSinceEpoch());
	}

	// Query message statistics with proper column names from schema
	QString sql = QString(
		"SELECT COUNT(*) as total, "
		"SUM(CASE WHEN message_type = 'text' THEN 1 ELSE 0 END) as text_count, "
		"SUM(CASE WHEN has_media = 1 THEN 1 ELSE 0 END) as media_count, "
		"SUM(CASE WHEN edit_date IS NOT NULL THEN 1 ELSE 0 END) as edited_count, "
		"AVG(LENGTH(content)) as avg_length, "
		"MIN(timestamp) as first_ts, "
		"MAX(timestamp) as last_ts "
		"FROM messages WHERE %1"
	).arg(whereClause);

	query.prepare(sql);
	for (const auto &binding : bindings) {
		query.addBindValue(binding);
	}

	if (!query.exec()) {
		qWarning() << "Analytics: Failed to execute query:" << query.lastError().text();
		return stats;
	}

	if (query.next()) {
		stats.totalMessages = query.value(0).toInt();
		stats.textMessages = query.value(1).toInt();
		stats.mediaMessages = query.value(2).toInt();
		stats.editedMessages = query.value(3).toInt();
		stats.averageLength = query.value(4).toDouble();

		qint64 firstTs = query.value(5).toLongLong();
		qint64 lastTs = query.value(6).toLongLong();

		if (firstTs > 0) {
			stats.firstMessage = QDateTime::fromSecsSinceEpoch(firstTs);
		}
		if (lastTs > 0) {
			stats.lastMessage = QDateTime::fromSecsSinceEpoch(lastTs);
		}

		// Calculate messages per day
		if (stats.firstMessage.isValid() && stats.lastMessage.isValid()) {
			qint64 daysDiff = stats.firstMessage.daysTo(stats.lastMessage);
			if (daysDiff > 0) {
				stats.messagesPerDay = static_cast<double>(stats.totalMessages) / daysDiff;
			} else if (stats.totalMessages > 0) {
				stats.messagesPerDay = static_cast<double>(stats.totalMessages);
			}
		}
	}

	return stats;
}

UserActivity Analytics::collectUserActivity(
	qint64 userId,
	qint64 chatId,
	const AnalyticsTimeRange &range
) {
	UserActivity activity;
	activity.userId = userId;

	if (!_archiver || !_archiver->isRunning()) {
		qWarning() << "Analytics: Archiver not running";
		return activity;
	}

	auto db = _archiver->database();
	if (!db.isOpen()) {
		qWarning() << "Analytics: Database not open";
		return activity;
	}

	QSqlQuery query(db);

	// First try user_activity_summary table for efficient stats
	if (chatId != 0 && range.period == "all") {
		QString sql = "SELECT message_count, word_count, avg_message_length, "
		              "most_active_hour, first_message_date, last_message_date, days_active "
		              "FROM user_activity_summary WHERE user_id = ? AND chat_id = ?";
		query.prepare(sql);
		query.addBindValue(userId);
		query.addBindValue(chatId);

		if (query.exec() && query.next()) {
			activity.messageCount = query.value(0).toInt();
			// word_count could be used for additional metrics
			activity.averageMessageLength = query.value(2).toDouble();
			activity.firstSeen = QDateTime::fromSecsSinceEpoch(query.value(4).toLongLong());
			activity.lastSeen = QDateTime::fromSecsSinceEpoch(query.value(5).toLongLong());
		}
	}

	// If not found in summary or need filtered data, query messages table
	if (activity.messageCount == 0) {
		QString whereClause = "user_id = ?";
		QVector<QVariant> bindings = {userId};

		if (chatId != 0) {
			whereClause += " AND chat_id = ?";
			bindings.append(chatId);
		}

		if (!range.start.isNull()) {
			whereClause += " AND timestamp >= ?";
			bindings.append(range.start.toSecsSinceEpoch());
		}
		if (!range.end.isNull()) {
			whereClause += " AND timestamp <= ?";
			bindings.append(range.end.toSecsSinceEpoch());
		}

		// Query user statistics from messages table
		QString sql = QString(
			"SELECT "
			"COALESCE(first_name, username, 'Unknown') as name, "
			"COUNT(*) as msg_count, "
			"AVG(LENGTH(content)) as avg_len, "
			"MIN(timestamp) as first_ts, "
			"MAX(timestamp) as last_ts, "
			"SUM(CASE WHEN reply_to_message_id IS NOT NULL THEN 1 ELSE 0 END) as reply_count "
			"FROM messages WHERE %1"
		).arg(whereClause);

		query.prepare(sql);
		for (const auto &binding : bindings) {
			query.addBindValue(binding);
		}

		if (!query.exec()) {
			qWarning() << "Analytics: Failed to query user activity:" << query.lastError().text();
			return activity;
		}

		if (query.next()) {
			activity.userName = query.value(0).toString();
			activity.messageCount = query.value(1).toInt();
			activity.averageMessageLength = query.value(2).toDouble();
			activity.firstSeen = QDateTime::fromSecsSinceEpoch(query.value(3).toLongLong());
			activity.lastSeen = QDateTime::fromSecsSinceEpoch(query.value(4).toLongLong());
			activity.replyCount = query.value(5).toInt();
		}
	}

	// Get hourly and weekly distribution
	activity.hourlyActivity = QVector<int>(24, 0);
	activity.weeklyActivity = QVector<int>(7, 0);

	QString whereClause = "user_id = ?";
	QVector<QVariant> bindings = {userId};
	if (chatId != 0) {
		whereClause += " AND chat_id = ?";
		bindings.append(chatId);
	}
	if (!range.start.isNull()) {
		whereClause += " AND timestamp >= ?";
		bindings.append(range.start.toSecsSinceEpoch());
	}
	if (!range.end.isNull()) {
		whereClause += " AND timestamp <= ?";
		bindings.append(range.end.toSecsSinceEpoch());
	}

	// Hourly activity
	QString sqlHourly = QString(
		"SELECT strftime('%%H', datetime(timestamp, 'unixepoch')) as hour, COUNT(*) "
		"FROM messages WHERE %1 GROUP BY hour"
	).arg(whereClause);

	query.prepare(sqlHourly);
	for (const auto &binding : bindings) {
		query.addBindValue(binding);
	}

	if (query.exec()) {
		while (query.next()) {
			int hour = query.value(0).toInt();
			int count = query.value(1).toInt();
			if (hour >= 0 && hour < 24) {
				activity.hourlyActivity[hour] = count;
			}
		}
	}

	// Weekly activity (0 = Sunday)
	QString sqlWeekly = QString(
		"SELECT strftime('%%w', datetime(timestamp, 'unixepoch')) as dow, COUNT(*) "
		"FROM messages WHERE %1 GROUP BY dow"
	).arg(whereClause);

	query.prepare(sqlWeekly);
	for (const auto &binding : bindings) {
		query.addBindValue(binding);
	}

	if (query.exec()) {
		while (query.next()) {
			int dow = query.value(0).toInt();
			int count = query.value(1).toInt();
			if (dow >= 0 && dow < 7) {
				activity.weeklyActivity[dow] = count;
			}
		}
	}

	return activity;
}

ChatActivity Analytics::collectChatActivity(
	qint64 chatId,
	const AnalyticsTimeRange &range
) {
	ChatActivity activity;
	activity.chatId = chatId;

	if (!_archiver || !_archiver->isRunning()) {
		qWarning() << "Analytics: Archiver not running";
		return activity;
	}

	auto db = _archiver->database();
	if (!db.isOpen()) {
		qWarning() << "Analytics: Database not open";
		return activity;
	}

	QSqlQuery query(db);

	// First try chat_activity_summary table for efficient stats
	if (range.period == "all") {
		QString sql = "SELECT total_messages, unique_users, messages_per_day, peak_hour, "
		              "first_message_date, last_message_date, activity_trend "
		              "FROM chat_activity_summary WHERE chat_id = ?";
		query.prepare(sql);
		query.addBindValue(chatId);

		if (query.exec() && query.next()) {
			activity.totalMessages = query.value(0).toInt();
			activity.activeUsers = query.value(1).toInt();
			activity.messagesPerDay = query.value(2).toDouble();
			// peak_hour could be used
			activity.activityTrend = query.value(6).toString();

			if (activity.activeUsers > 0) {
				activity.messagesPerUser = static_cast<double>(activity.totalMessages) / activity.activeUsers;
			}
		}
	}

	// If not found or need filtered data, query messages table
	if (activity.totalMessages == 0) {
		QString whereClause = "chat_id = ?";
		QVector<QVariant> bindings = {chatId};

		if (!range.start.isNull()) {
			whereClause += " AND timestamp >= ?";
			bindings.append(range.start.toSecsSinceEpoch());
		}
		if (!range.end.isNull()) {
			whereClause += " AND timestamp <= ?";
			bindings.append(range.end.toSecsSinceEpoch());
		}

		// Query chat statistics
		QString sql = QString(
			"SELECT COUNT(*) as msg_count, "
			"COUNT(DISTINCT user_id) as user_count, "
			"MIN(timestamp) as first_ts, "
			"MAX(timestamp) as last_ts "
			"FROM messages WHERE %1"
		).arg(whereClause);

		query.prepare(sql);
		for (const auto &binding : bindings) {
			query.addBindValue(binding);
		}

		if (!query.exec()) {
			qWarning() << "Analytics: Failed to query chat activity:" << query.lastError().text();
			return activity;
		}

		if (query.next()) {
			activity.totalMessages = query.value(0).toInt();
			activity.activeUsers = query.value(1).toInt();

			if (activity.activeUsers > 0) {
				activity.messagesPerUser = static_cast<double>(activity.totalMessages) / activity.activeUsers;
			}

			qint64 firstTs = query.value(2).toLongLong();
			qint64 lastTs = query.value(3).toLongLong();

			if (firstTs > 0 && lastTs > 0) {
				QDateTime first = QDateTime::fromSecsSinceEpoch(firstTs);
				QDateTime last = QDateTime::fromSecsSinceEpoch(lastTs);
				qint64 daysDiff = first.daysTo(last);
				if (daysDiff > 0) {
					activity.messagesPerDay = static_cast<double>(activity.totalMessages) / daysDiff;
				}
			}
		}

		// Detect trend by comparing recent vs older activity
		activity.activityTrend = detectTrend(QVector<double>()); // Simplified for now
		if (activity.activityTrend.isEmpty()) {
			activity.activityTrend = "stable";
		}
	}

	// Get hourly and weekly distributions
	activity.hourlyDistribution = QVector<int>(24, 0);
	activity.weeklyDistribution = QVector<int>(7, 0);

	QString whereClause = "chat_id = ?";
	QVector<QVariant> bindings = {chatId};
	if (!range.start.isNull()) {
		whereClause += " AND timestamp >= ?";
		bindings.append(range.start.toSecsSinceEpoch());
	}
	if (!range.end.isNull()) {
		whereClause += " AND timestamp <= ?";
		bindings.append(range.end.toSecsSinceEpoch());
	}

	// Hourly distribution
	QString sqlHourly = QString(
		"SELECT strftime('%%H', datetime(timestamp, 'unixepoch')) as hour, COUNT(*) "
		"FROM messages WHERE %1 GROUP BY hour"
	).arg(whereClause);

	query.prepare(sqlHourly);
	for (const auto &binding : bindings) {
		query.addBindValue(binding);
	}

	if (query.exec()) {
		while (query.next()) {
			int hour = query.value(0).toInt();
			int count = query.value(1).toInt();
			if (hour >= 0 && hour < 24) {
				activity.hourlyDistribution[hour] = count;
			}
		}
	}

	// Weekly distribution
	QString sqlWeekly = QString(
		"SELECT strftime('%%w', datetime(timestamp, 'unixepoch')) as dow, COUNT(*) "
		"FROM messages WHERE %1 GROUP BY dow"
	).arg(whereClause);

	query.prepare(sqlWeekly);
	for (const auto &binding : bindings) {
		query.addBindValue(binding);
	}

	if (query.exec()) {
		while (query.next()) {
			int dow = query.value(0).toInt();
			int count = query.value(1).toInt();
			if (dow >= 0 && dow < 7) {
				activity.weeklyDistribution[dow] = count;
			}
		}
	}

	// Get chat title from chats table
	QString sqlChat = "SELECT title FROM chats WHERE chat_id = ?";
	query.prepare(sqlChat);
	query.addBindValue(chatId);
	if (query.exec() && query.next()) {
		activity.chatTitle = query.value(0).toString();
	}

	return activity;
}

QVector<TimeSeriesPoint> Analytics::generateTimeSeries(
	qint64 chatId,
	const QString &granularity,
	const AnalyticsTimeRange &range
) {
	QVector<TimeSeriesPoint> points;

	if (!_archiver || !_archiver->isRunning()) {
		qWarning() << "Analytics: Archiver not running";
		return points;
	}

	auto db = _archiver->database();
	if (!db.isOpen()) {
		qWarning() << "Analytics: Database not open";
		return points;
	}

	QSqlQuery query(db);

	// Build WHERE clause
	QString whereClause = "chat_id = ?";
	QVector<QVariant> bindings = {chatId};

	if (!range.start.isNull()) {
		whereClause += " AND timestamp >= ?";
		bindings.append(range.start.toSecsSinceEpoch());
	}
	if (!range.end.isNull()) {
		whereClause += " AND timestamp <= ?";
		bindings.append(range.end.toSecsSinceEpoch());
	}

	// Determine time grouping format based on granularity
	QString timeFormat;
	if (granularity == "hourly") {
		timeFormat = "%Y-%m-%d %H:00:00";
	} else if (granularity == "daily") {
		timeFormat = "%Y-%m-%d";
	} else if (granularity == "weekly") {
		timeFormat = "%Y-W%W";
	} else if (granularity == "monthly") {
		timeFormat = "%Y-%m";
	} else if (granularity == "yearly") {
		timeFormat = "%Y";
	} else {
		timeFormat = "%Y-%m-%d"; // default to daily
	}

	// Query time series data
	QString sql = QString(
		"SELECT "
		"strftime('%1', datetime(timestamp, 'unixepoch')) as time_bucket, "
		"COUNT(*) as msg_count, "
		"COUNT(DISTINCT user_id) as user_count, "
		"AVG(LENGTH(content)) as avg_len "
		"FROM messages WHERE %2 "
		"GROUP BY time_bucket "
		"ORDER BY time_bucket"
	).arg(timeFormat).arg(whereClause);

	query.prepare(sql);
	for (const auto &binding : bindings) {
		query.addBindValue(binding);
	}

	if (!query.exec()) {
		qWarning() << "Analytics: Failed to generate time series:" << query.lastError().text();
		return points;
	}

	while (query.next()) {
		TimeSeriesPoint point;
		QString timeBucket = query.value(0).toString();

		// Parse time bucket based on granularity
		if (granularity == "hourly") {
			point.timestamp = QDateTime::fromString(timeBucket, "yyyy-MM-dd HH:00:00");
		} else if (granularity == "daily") {
			point.timestamp = QDateTime::fromString(timeBucket, "yyyy-MM-dd");
		} else if (granularity == "monthly") {
			point.timestamp = QDateTime::fromString(timeBucket + "-01", "yyyy-MM-dd");
		} else if (granularity == "yearly") {
			point.timestamp = QDateTime::fromString(timeBucket + "-01-01", "yyyy-MM-dd");
		} else {
			point.timestamp = QDateTime::fromString(timeBucket, "yyyy-MM-dd");
		}

		point.messageCount = query.value(1).toInt();
		point.userCount = query.value(2).toInt();
		point.averageLength = query.value(3).toDouble();

		// Get message type distribution for this time bucket
		QString sqlTypes = QString(
			"SELECT message_type, COUNT(*) "
			"FROM messages WHERE %1 AND strftime('%2', datetime(timestamp, 'unixepoch')) = ? "
			"GROUP BY message_type"
		).arg(whereClause).arg(timeFormat);

		QSqlQuery typeQuery(db);
		typeQuery.prepare(sqlTypes);
		for (const auto &binding : bindings) {
			typeQuery.addBindValue(binding);
		}
		typeQuery.addBindValue(timeBucket);

		if (typeQuery.exec()) {
			while (typeQuery.next()) {
				QString type = typeQuery.value(0).toString();
				int count = typeQuery.value(1).toInt();
				point.messageTypes[type] = count;
			}
		}

		points.append(point);
	}

	return points;
}

QHash<QString, int> Analytics::analyzeWordFrequency(
	qint64 chatId,
	const AnalyticsTimeRange &range
) {
	QHash<QString, int> wordFreq;

	if (!_archiver || !_archiver->isRunning()) {
		qWarning() << "Analytics: Archiver not running";
		return wordFreq;
	}

	auto db = _archiver->database();
	if (!db.isOpen()) {
		qWarning() << "Analytics: Database not open";
		return wordFreq;
	}

	QSqlQuery query(db);

	// Query messages - use 'content' column from schema
	QString whereClause = "chat_id = ? AND content IS NOT NULL";
	QVector<QVariant> bindings = {chatId};

	if (!range.start.isNull()) {
		whereClause += " AND timestamp >= ?";
		bindings.append(range.start.toSecsSinceEpoch());
	}
	if (!range.end.isNull()) {
		whereClause += " AND timestamp <= ?";
		bindings.append(range.end.toSecsSinceEpoch());
	}

	// Limit to text messages or messages with content
	QString sql = QString(
		"SELECT content FROM messages WHERE %1 AND LENGTH(content) > 0 LIMIT 10000"
	).arg(whereClause);

	query.prepare(sql);
	for (const auto &binding : bindings) {
		query.addBindValue(binding);
	}

	if (!query.exec()) {
		qWarning() << "Analytics: Failed to query messages for word frequency:" << query.lastError().text();
		return wordFreq;
	}

	int messagesProcessed = 0;
	while (query.next()) {
		QString text = query.value(0).toString();
		if (text.isEmpty()) {
			continue;
		}

		QStringList words = extractWords(text);
		for (const QString &word : words) {
			QString lowercaseWord = word.toLower();
			// Filter out short words and stop words
			if (lowercaseWord.length() >= 3 && !isStopWord(lowercaseWord)) {
				wordFreq[lowercaseWord]++;
			}
		}

		messagesProcessed++;
		// Process in batches to avoid memory issues
		if (messagesProcessed % 1000 == 0) {
			qDebug() << "Analytics: Processed" << messagesProcessed << "messages for word frequency";
		}
	}

	qDebug() << "Analytics: Word frequency analysis complete." << wordFreq.size() << "unique words from" << messagesProcessed << "messages";

	return wordFreq;
}

QStringList Analytics::extractWords(const QString &text) const {
	// Split by non-word characters and convert to lowercase
	QRegularExpression wordRegex("\\b\\w+\\b");
	QRegularExpressionMatchIterator it = wordRegex.globalMatch(text);
	
	QStringList words;
	while (it.hasNext()) {
		QRegularExpressionMatch match = it.next();
		words.append(match.captured(0).toLower());
	}
	
	return words;
}

bool Analytics::isStopWord(const QString &word) const {
	return _stopWords.contains(word.toLower());
}

QString Analytics::detectTrend(const QVector<double> &data) const {
	if (data.size() < 2) {
		return "insufficient_data";
	}

	double growthRate = calculateGrowthRate(data);

	if (growthRate > 0.1) {
		return "increasing";
	} else if (growthRate < -0.1) {
		return "decreasing";
	} else {
		return "stable";
	}
}

double Analytics::calculateGrowthRate(const QVector<double> &data) const {
	if (data.size() < 2) {
		return 0.0;
	}

	// Simple linear regression slope
	double sumX = 0, sumY = 0, sumXY = 0, sumX2 = 0;
	int n = data.size();

	for (int i = 0; i < n; ++i) {
		sumX += i;
		sumY += data[i];
		sumXY += i * data[i];
		sumX2 += i * i;
	}

	double slope = (n * sumXY - sumX * sumY) / (n * sumX2 - sumX * sumX);
	double avgY = sumY / n;

	return avgY > 0 ? (slope / avgY) : 0.0;
}

QVector<double> Analytics::smoothData(const QVector<double> &data, int window) const {
	QVector<double> smoothed;
	if (data.isEmpty() || window < 1) {
		return smoothed;
	}

	for (int i = 0; i < data.size(); ++i) {
		int start = qMax(0, i - window / 2);
		int end = qMin(data.size() - 1, i + window / 2);
		
		double sum = 0.0;
		int count = 0;
		for (int j = start; j <= end; ++j) {
			sum += data[j];
			count++;
		}
		
		smoothed.append(sum / count);
	}

	return smoothed;
}

double Analytics::calculateAverage(const QVector<int> &data) const {
	if (data.isEmpty()) {
		return 0.0;
	}

	qint64 sum = 0;
	for (int value : data) {
		sum += value;
	}

	return static_cast<double>(sum) / data.size();
}

double Analytics::calculateStdDev(const QVector<int> &data) const {
	if (data.size() < 2) {
		return 0.0;
	}

	double avg = calculateAverage(data);
	double sumSquares = 0.0;

	for (int value : data) {
		double diff = value - avg;
		sumSquares += diff * diff;
	}

	return qSqrt(sumSquares / (data.size() - 1));
}

int Analytics::calculateMedian(QVector<int> data) const {
	if (data.isEmpty()) {
		return 0;
	}

	std::sort(data.begin(), data.end());

	int middle = data.size() / 2;
	if (data.size() % 2 == 0) {
		return (data[middle - 1] + data[middle]) / 2;
	} else {
		return data[middle];
	}
}

AnalyticsTimeRange Analytics::parseTimeRange(
	const QString &period,
	const QDateTime &start,
	const QDateTime &end
) const {
	AnalyticsTimeRange range;
	range.period = period;

	if (period == "custom") {
		range.start = start;
		range.end = end;
	} else if (period == "hour") {
		range.end = QDateTime::currentDateTime();
		range.start = range.end.addSecs(-3600);
	} else if (period == "day") {
		range.end = QDateTime::currentDateTime();
		range.start = range.end.addDays(-1);
	} else if (period == "week") {
		range.end = QDateTime::currentDateTime();
		range.start = range.end.addDays(-7);
	} else if (period == "month") {
		range.end = QDateTime::currentDateTime();
		range.start = range.end.addMonths(-1);
	} else if (period == "year") {
		range.end = QDateTime::currentDateTime();
		range.start = range.end.addYears(-1);
	} else { // "all"
		// No time constraints
		range.start = QDateTime();
		range.end = QDateTime();
	}

	return range;
}

QString Analytics::formatTimestamp(
	const QDateTime &dt,
	const QString &granularity
) const {
	if (granularity == "hourly") {
		return dt.toString("yyyy-MM-dd HH:00");
	} else if (granularity == "daily") {
		return dt.toString("yyyy-MM-dd");
	} else if (granularity == "weekly") {
		return dt.toString("yyyy-'W'ww");
	} else if (granularity == "monthly") {
		return dt.toString("yyyy-MM");
	} else if (granularity == "yearly") {
		return dt.toString("yyyy");
	}

	return dt.toString(Qt::ISODate);
}

QString Analytics::exportToJSON(const QJsonObject &analytics, const QString &path) {
	QFile file(path);
	if (!file.open(QIODevice::WriteOnly)) {
		return QString();
	}

	QJsonDocument doc(analytics);
	file.write(doc.toJson(QJsonDocument::Indented));
	file.close();

	return path;
}

QString Analytics::exportToCSV(const QJsonObject &analytics, const QString &path) {
	QFile file(path);
	if (!file.open(QIODevice::WriteOnly)) {
		return QString();
	}

	QTextStream stream(&file);
	
	// Simple CSV export of message statistics
	stream << "Metric,Value\n";
	
	QJsonObject msgStats = analytics["messageStats"].toObject();
	for (auto it = msgStats.constBegin(); it != msgStats.constEnd(); ++it) {
		stream << it.key() << "," << it.value().toVariant().toString() << "\n";
	}

	file.close();

	return path;
}

QString Analytics::exportToHTML(const QJsonObject &analytics, const QString &path) {
	QFile file(path);
	if (!file.open(QIODevice::WriteOnly)) {
		return QString();
	}

	QTextStream stream(&file);
	
	stream << "<!DOCTYPE html>\n<html>\n<head>\n";
	stream << "<title>Analytics Report</title>\n";
	stream << "<style>body{font-family:Arial,sans-serif;margin:20px;}</style>\n";
	stream << "</head>\n<body>\n";
	stream << "<h1>Analytics Report</h1>\n";
	stream << "<pre>" << QJsonDocument(analytics).toJson(QJsonDocument::Indented) << "</pre>\n";
	stream << "</body>\n</html>\n";

	file.close();

	return path;
}

QJsonObject Analytics::messageStatsToJson(const MessageStats &stats) const {
	QJsonObject json;
	json["totalMessages"] = stats.totalMessages;
	json["textMessages"] = stats.textMessages;
	json["mediaMessages"] = stats.mediaMessages;
	json["deletedMessages"] = stats.deletedMessages;
	json["editedMessages"] = stats.editedMessages;
	json["averageLength"] = stats.averageLength;
	json["messagesPerDay"] = stats.messagesPerDay;
	json["firstMessage"] = stats.firstMessage.toString(Qt::ISODate);
	json["lastMessage"] = stats.lastMessage.toString(Qt::ISODate);
	return json;
}

QJsonObject Analytics::userActivityToJson(const UserActivity &activity) const {
	QJsonObject json;
	json["userId"] = QString::number(activity.userId);
	json["userName"] = activity.userName;
	json["messageCount"] = activity.messageCount;
	json["replyCount"] = activity.replyCount;
	json["mentionCount"] = activity.mentionCount;
	json["averageMessageLength"] = activity.averageMessageLength;
	json["firstSeen"] = activity.firstSeen.toString(Qt::ISODate);
	json["lastSeen"] = activity.lastSeen.toString(Qt::ISODate);
	return json;
}

QJsonObject Analytics::chatActivityToJson(const ChatActivity &activity) const {
	QJsonObject json;
	json["chatId"] = QString::number(activity.chatId);
	json["chatTitle"] = activity.chatTitle;
	json["activeUsers"] = activity.activeUsers;
	json["totalMessages"] = activity.totalMessages;
	json["messagesPerDay"] = activity.messagesPerDay;
	json["messagesPerUser"] = activity.messagesPerUser;
	json["activityTrend"] = activity.activityTrend;
	return json;
}

QJsonArray Analytics::timeSeriesPointsToJson(const QVector<TimeSeriesPoint> &points) const {
	QJsonArray array;
	for (const auto &point : points) {
		QJsonObject json;
		json["timestamp"] = point.timestamp.toString(Qt::ISODate);
		json["messageCount"] = point.messageCount;
		json["userCount"] = point.userCount;
		json["averageLength"] = point.averageLength;
		array.append(json);
	}
	return array;
}

QJsonArray Analytics::wordFrequenciesToJson(const QVector<WordFrequency> &frequencies) const {
	QJsonArray array;
	for (const auto &wf : frequencies) {
		QJsonObject json;
		json["word"] = wf.word;
		json["count"] = wf.count;
		json["percentage"] = wf.percentage;
		array.append(json);
	}
	return array;
}

QString Analytics::getCacheKey(qint64 chatId, const QString &type) const {
	return QString("%1_%2").arg(chatId).arg(type);
}

bool Analytics::isCacheValid(const QString &key) const {
	if (!_cache.contains(key)) {
		return false;
	}

	const auto &cached = _cache[key];
	QDateTime now = QDateTime::currentDateTime();
	return cached.timestamp.secsTo(now) < _cacheLifetimeSeconds;
}

void Analytics::setCacheValue(const QString &key, const QJsonObject &data) {
	CachedAnalytics cached;
	cached.timestamp = QDateTime::currentDateTime();
	cached.data = data;
	_cache[key] = cached;
}

QJsonObject Analytics::getCacheValue(const QString &key) const {
	if (_cache.contains(key)) {
		return _cache[key].data;
	}
	return QJsonObject();
}

void Analytics::initializeStopWords() {
	// Common English stop words
	_stopWords = {
		"a", "an", "and", "are", "as", "at", "be", "by", "for", "from",
		"has", "he", "in", "is", "it", "its", "of", "on", "that", "the",
		"to", "was", "will", "with", "the", "this", "but", "they", "have",
		"had", "what", "when", "where", "who", "which", "why", "how"
	};
}

} // namespace MCP

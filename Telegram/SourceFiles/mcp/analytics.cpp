// This file is part of Telegram Desktop MCP Server.
// Licensed under GPLv3 with OpenSSL exception.

#include "analytics.h"
#include "chat_archiver.h"

#include <QtCore/QRegularExpression>
#include <QtCore/QSet>
#include <QtSql/QSqlQuery>
#include <QtSql/QSqlError>

namespace MCP {

namespace {

// Common English stop words to filter out
const QStringList kDefaultStopWords = {
	"a", "about", "above", "after", "again", "against", "all", "am", "an", "and", "any", "are",
	"as", "at", "be", "because", "been", "before", "being", "below", "between", "both", "but",
	"by", "can", "did", "do", "does", "doing", "down", "during", "each", "few", "for", "from",
	"further", "had", "has", "have", "having", "he", "her", "here", "hers", "herself", "him",
	"himself", "his", "how", "i", "if", "in", "into", "is", "it", "its", "itself", "just",
	"me", "might", "more", "most", "must", "my", "myself", "no", "nor", "not", "now", "of",
	"off", "on", "once", "only", "or", "other", "our", "ours", "ourselves", "out", "over",
	"own", "s", "same", "she", "should", "so", "some", "such", "t", "than", "that", "the",
	"their", "theirs", "them", "themselves", "then", "there", "these", "they", "this", "those",
	"through", "to", "too", "under", "until", "up", "very", "was", "we", "were", "what",
	"when", "where", "which", "while", "who", "whom", "why", "will", "with", "would", "you",
	"your", "yours", "yourself", "yourselves"
};

} // namespace

Analytics::Analytics(ChatArchiver *archiver, QObject *parent)
	: QObject(parent)
	, _archiver(archiver)
	, _db(archiver ? &archiver->_db : nullptr) {
}

Analytics::~Analytics() = default;

// Message Statistics Implementation
Analytics::MessageStats Analytics::getMessageStatistics(
		qint64 chatId,
		const QDateTime &start,
		const QDateTime &end) {

	MessageStats stats;

	if (!_db || !_db->isOpen()) {
		return stats;
	}

	QString sql = R"(
		SELECT
			COUNT(*) as total_messages,
			SUM(LENGTH(content)) as total_chars,
			SUM(LENGTH(content) - LENGTH(REPLACE(content, ' ', '')) + 1) as total_words,
			COUNT(DISTINCT user_id) as unique_users,
			AVG(LENGTH(content)) as avg_length,
			MIN(timestamp) as first_msg,
			MAX(timestamp) as last_msg
		FROM messages
		WHERE chat_id = :chat_id
	)";

	QStringList conditions;
	if (start.isValid()) {
		conditions << "timestamp >= :start";
	}
	if (end.isValid()) {
		conditions << "timestamp <= :end";
	}

	if (!conditions.isEmpty()) {
		sql += " AND " + conditions.join(" AND ");
	}

	QSqlQuery query(*_db);
	query.prepare(sql);
	query.bindValue(":chat_id", chatId);
	if (start.isValid()) {
		query.bindValue(":start", start.toSecsSinceEpoch());
	}
	if (end.isValid()) {
		query.bindValue(":end", end.toSecsSinceEpoch());
	}

	if (query.exec() && query.next()) {
		stats.totalMessages = query.value("total_messages").toInt();
		stats.totalCharacters = query.value("total_chars").toLongLong();
		stats.totalWords = query.value("total_words").toInt();
		stats.uniqueUsers = query.value("unique_users").toInt();
		stats.avgMessageLength = query.value("avg_length").toDouble();
		stats.firstMessage = QDateTime::fromSecsSinceEpoch(query.value("first_msg").toLongLong());
		stats.lastMessage = QDateTime::fromSecsSinceEpoch(query.value("last_msg").toLongLong());

		// Calculate messages per hour
		qint64 duration = stats.lastMessage.toSecsSinceEpoch() - stats.firstMessage.toSecsSinceEpoch();
		if (duration > 0) {
			stats.messagesPerHour = (stats.totalMessages * 3600.0) / duration;
		}
	}

	// Get top words
	stats.topWords = getTopWords(chatId, 20);

	// Get top authors
	QSqlQuery authorsQuery(*_db);
	authorsQuery.prepare(R"(
		SELECT user_id, COUNT(*) as count
		FROM messages
		WHERE chat_id = :chat_id AND user_id IS NOT NULL
		GROUP BY user_id
		ORDER BY count DESC
		LIMIT 10
	)");
	authorsQuery.bindValue(":chat_id", chatId);

	if (authorsQuery.exec()) {
		while (authorsQuery.next()) {
			stats.topAuthors[authorsQuery.value("user_id").toLongLong()] =
				authorsQuery.value("count").toInt();
		}
	}

	return stats;
}

// User Activity Analysis
Analytics::UserActivity Analytics::getUserActivityAnalysis(qint64 userId, qint64 chatId) {
	UserActivity activity;
	activity.userId = userId;

	if (!_db || !_db->isOpen()) {
		return activity;
	}

	QString sql = R"(
		SELECT
			COUNT(*) as message_count,
			SUM(LENGTH(content) - LENGTH(REPLACE(content, ' ', '')) + 1) as word_count,
			AVG(LENGTH(content)) as avg_length,
			MIN(timestamp) as first_msg,
			MAX(timestamp) as last_msg
		FROM messages
		WHERE user_id = :user_id
	)";

	if (chatId > 0) {
		sql += " AND chat_id = :chat_id";
	}

	QSqlQuery query(*_db);
	query.prepare(sql);
	query.bindValue(":user_id", userId);
	if (chatId > 0) {
		query.bindValue(":chat_id", chatId);
	}

	if (query.exec() && query.next()) {
		activity.messageCount = query.value("message_count").toInt();
		activity.wordCount = query.value("word_count").toInt();
		activity.avgMessageLength = query.value("avg_length").toDouble();
		activity.firstMessage = QDateTime::fromSecsSinceEpoch(query.value("first_msg").toLongLong());
		activity.lastMessage = QDateTime::fromSecsSinceEpoch(query.value("last_msg").toLongLong());
	}

	// Get username
	QSqlQuery userQuery(*_db);
	userQuery.prepare("SELECT username FROM users WHERE user_id = :user_id");
	userQuery.bindValue(":user_id", userId);
	if (userQuery.exec() && userQuery.next()) {
		activity.username = userQuery.value("username").toString();
	}

	// Calculate most active hour
	activity.mostActiveHour = findMostActiveHour(userId, chatId);

	// Calculate days active
	activity.daysActive = calculateDaysActive(userId, chatId);

	// Find most active channel (if chatId not specified)
	if (chatId == 0) {
		QSqlQuery channelQuery(*_db);
		channelQuery.prepare(R"(
			SELECT chat_id, COUNT(*) as count
			FROM messages
			WHERE user_id = :user_id
			GROUP BY chat_id
			ORDER BY count DESC
			LIMIT 1
		)");
		channelQuery.bindValue(":user_id", userId);

		if (channelQuery.exec() && channelQuery.next()) {
			qint64 topChatId = channelQuery.value("chat_id").toLongLong();

			// Get chat title
			QSqlQuery chatQuery(*_db);
			chatQuery.prepare("SELECT title FROM chats WHERE chat_id = :chat_id");
			chatQuery.bindValue(":chat_id", topChatId);
			if (chatQuery.exec() && chatQuery.next()) {
				activity.mostActiveChannel = chatQuery.value("title").toString();
			}
		}
	}

	return activity;
}

// Get top users
QVector<Analytics::UserActivity> Analytics::getTopUsers(qint64 chatId, int limit) {
	QVector<UserActivity> topUsers;

	if (!_db || !_db->isOpen()) {
		return topUsers;
	}

	QSqlQuery query(*_db);
	query.prepare(R"(
		SELECT user_id, COUNT(*) as count
		FROM messages
		WHERE chat_id = :chat_id AND user_id IS NOT NULL
		GROUP BY user_id
		ORDER BY count DESC
		LIMIT :limit
	)");
	query.bindValue(":chat_id", chatId);
	query.bindValue(":limit", limit);

	if (query.exec()) {
		while (query.next()) {
			qint64 userId = query.value("user_id").toLongLong();
			auto activity = getUserActivityAnalysis(userId, chatId);
			topUsers.append(activity);
		}
	}

	return topUsers;
}

// Chat Activity Analysis
Analytics::ChatActivity Analytics::getChatActivityAnalysis(qint64 chatId) {
	ChatActivity activity;
	activity.chatId = chatId;

	if (!_db || !_db->isOpen()) {
		return activity;
	}

	// Check if summary exists
	QSqlQuery summaryQuery(*_db);
	summaryQuery.prepare("SELECT * FROM chat_activity_summary WHERE chat_id = :chat_id");
	summaryQuery.bindValue(":chat_id", chatId);

	if (summaryQuery.exec() && summaryQuery.next()) {
		activity.totalMessages = summaryQuery.value("total_messages").toInt();
		activity.uniqueUsers = summaryQuery.value("unique_users").toInt();
		activity.messagesPerDay = summaryQuery.value("messages_per_day").toDouble();
		activity.peakHour = summaryQuery.value("peak_hour").toInt();
		activity.firstMessage = QDateTime::fromSecsSinceEpoch(
			summaryQuery.value("first_message_date").toLongLong());
		activity.lastMessage = QDateTime::fromSecsSinceEpoch(
			summaryQuery.value("last_message_date").toLongLong());

		QString trendStr = summaryQuery.value("activity_trend").toString();
		if (trendStr == "increasing") {
			activity.trend = ActivityTrend::Increasing;
		} else if (trendStr == "decreasing") {
			activity.trend = ActivityTrend::Decreasing;
		} else if (trendStr == "stable") {
			activity.trend = ActivityTrend::Stable;
		}
	} else {
		// Calculate from scratch
		QSqlQuery query(*_db);
		query.prepare(R"(
			SELECT
				COUNT(*) as total_messages,
				COUNT(DISTINCT user_id) as unique_users,
				MIN(timestamp) as first_msg,
				MAX(timestamp) as last_msg
			FROM messages
			WHERE chat_id = :chat_id
		)");
		query.bindValue(":chat_id", chatId);

		if (query.exec() && query.next()) {
			activity.totalMessages = query.value("total_messages").toInt();
			activity.uniqueUsers = query.value("unique_users").toInt();
			activity.firstMessage = QDateTime::fromSecsSinceEpoch(query.value("first_msg").toLongLong());
			activity.lastMessage = QDateTime::fromSecsSinceEpoch(query.value("last_msg").toLongLong());

			// Calculate messages per day
			qint64 days = activity.firstMessage.daysTo(activity.lastMessage);
			if (days > 0) {
				activity.messagesPerDay = static_cast<double>(activity.totalMessages) / days;
			}
		}

		activity.peakHour = findPeakHour(chatId);
		activity.trend = detectActivityTrend(chatId);
	}

	// Get chat title
	QSqlQuery chatQuery(*_db);
	chatQuery.prepare("SELECT title FROM chats WHERE chat_id = :chat_id");
	chatQuery.bindValue(":chat_id", chatId);
	if (chatQuery.exec() && chatQuery.next()) {
		activity.chatTitle = chatQuery.value("title").toString();
	}

	return activity;
}

// Activity trend detection
ActivityTrend Analytics::detectActivityTrend(qint64 chatId) {
	if (!_db || !_db->isOpen()) {
		return ActivityTrend::Unknown;
	}

	// Get total message count
	QSqlQuery totalQuery(*_db);
	totalQuery.prepare("SELECT COUNT(*) FROM messages WHERE chat_id = :chat_id");
	totalQuery.bindValue(":chat_id", chatId);

	if (!totalQuery.exec() || !totalQuery.next()) {
		return ActivityTrend::Unknown;
	}

	int totalMessages = totalQuery.value(0).toInt();
	if (totalMessages < 100) {
		return ActivityTrend::Unknown; // Not enough data
	}

	// Split into two halves and compare
	int halfPoint = totalMessages / 2;

	QSqlQuery firstHalfQuery(*_db);
	firstHalfQuery.prepare(R"(
		SELECT COUNT(*) as count,
		       MAX(timestamp) - MIN(timestamp) as duration
		FROM (
			SELECT timestamp FROM messages
			WHERE chat_id = :chat_id
			ORDER BY timestamp ASC
			LIMIT :half
		)
	)");
	firstHalfQuery.bindValue(":chat_id", chatId);
	firstHalfQuery.bindValue(":half", halfPoint);

	QSqlQuery secondHalfQuery(*_db);
	secondHalfQuery.prepare(R"(
		SELECT COUNT(*) as count,
		       MAX(timestamp) - MIN(timestamp) as duration
		FROM (
			SELECT timestamp FROM messages
			WHERE chat_id = :chat_id
			ORDER BY timestamp DESC
			LIMIT :half
		)
	)");
	secondHalfQuery.bindValue(":chat_id", chatId);
	secondHalfQuery.bindValue(":half", halfPoint);

	if (!firstHalfQuery.exec() || !firstHalfQuery.next() ||
	    !secondHalfQuery.exec() || !secondHalfQuery.next()) {
		return ActivityTrend::Unknown;
	}

	double firstRate = firstHalfQuery.value("count").toDouble() /
	                   std::max(1LL, firstHalfQuery.value("duration").toLongLong());
	double secondRate = secondHalfQuery.value("count").toDouble() /
	                    std::max(1LL, secondHalfQuery.value("duration").toLongLong());

	// 20% threshold for trend detection
	double threshold = 0.20;
	double change = (secondRate - firstRate) / firstRate;

	if (change > threshold) {
		return ActivityTrend::Increasing;
	} else if (change < -threshold) {
		return ActivityTrend::Decreasing;
	} else {
		return ActivityTrend::Stable;
	}
}

// Time Series Implementation
QVector<Analytics::TimeSeriesPoint> Analytics::getTimeSeries(
		qint64 chatId,
		TimeGranularity granularity,
		const QDateTime &start,
		const QDateTime &end) {

	QVector<TimeSeriesPoint> series;

	if (!_db || !_db->isOpen()) {
		return series;
	}

	QString dateFormat = granularityToSQL(granularity);

	QString sql = QString(R"(
		SELECT
			strftime('%1', datetime(timestamp, 'unixepoch')) as time_bucket,
			timestamp,
			COUNT(*) as message_count,
			COUNT(DISTINCT user_id) as unique_users,
			AVG(LENGTH(content)) as avg_length
		FROM messages
		WHERE chat_id = :chat_id
	)").arg(dateFormat);

	QStringList conditions;
	if (start.isValid()) {
		conditions << "timestamp >= :start";
	}
	if (end.isValid()) {
		conditions << "timestamp <= :end";
	}

	if (!conditions.isEmpty()) {
		sql += " AND " + conditions.join(" AND ");
	}

	sql += " GROUP BY time_bucket ORDER BY timestamp ASC";

	QSqlQuery query(*_db);
	query.prepare(sql);
	query.bindValue(":chat_id", chatId);
	if (start.isValid()) {
		query.bindValue(":start", start.toSecsSinceEpoch());
	}
	if (end.isValid()) {
		query.bindValue(":end", end.toSecsSinceEpoch());
	}

	if (query.exec()) {
		while (query.next()) {
			TimeSeriesPoint point;
			point.timestamp = QDateTime::fromSecsSinceEpoch(query.value("timestamp").toLongLong());
			point.messageCount = query.value("message_count").toInt();
			point.uniqueUsers = query.value("unique_users").toInt();
			point.avgLength = query.value("avg_length").toDouble();
			series.append(point);
		}
	}

	return series;
}

// Top Words Analysis
QMap<QString, int> Analytics::getTopWords(qint64 chatId, int limit, const QStringList &stopWords) {
	QMap<QString, int> wordCounts;

	if (!_db || !_db->isOpen()) {
		return wordCounts;
	}

	QStringList stopWordList = stopWords.isEmpty() ? getDefaultStopWords() : stopWords;
	QSet<QString> stopWordSet(stopWordList.begin(), stopWordList.end());

	// Get all message content
	QSqlQuery query(*_db);
	query.prepare("SELECT content FROM messages WHERE chat_id = :chat_id AND content IS NOT NULL");
	query.bindValue(":chat_id", chatId);

	if (query.exec()) {
		while (query.next()) {
			QString content = query.value("content").toString();
			QStringList words = extractWords(content);

			for (const QString &word : words) {
				QString lowerWord = word.toLower();
				if (!stopWordSet.contains(lowerWord) && lowerWord.length() > 2) {
					wordCounts[lowerWord]++;
				}
			}
		}
	}

	// Sort by count and return top N
	QList<QPair<QString, int>> sortedWords;
	for (auto it = wordCounts.begin(); it != wordCounts.end(); ++it) {
		sortedWords.append(qMakePair(it.key(), it.value()));
	}

	std::sort(sortedWords.begin(), sortedWords.end(),
	          [](const auto &a, const auto &b) { return a.second > b.second; });

	QMap<QString, int> topWords;
	for (int i = 0; i < std::min(limit, sortedWords.size()); ++i) {
		topWords[sortedWords[i].first] = sortedWords[i].second;
	}

	return topWords;
}

QMap<QString, int> Analytics::getUserTopWords(qint64 userId, int limit) {
	// Similar to getTopWords but filtered by user_id
	QMap<QString, int> wordCounts;

	if (!_db || !_db->isOpen()) {
		return wordCounts;
	}

	QSet<QString> stopWordSet(kDefaultStopWords.begin(), kDefaultStopWords.end());

	QSqlQuery query(*_db);
	query.prepare("SELECT content FROM messages WHERE user_id = :user_id AND content IS NOT NULL");
	query.bindValue(":user_id", userId);

	if (query.exec()) {
		while (query.next()) {
			QString content = query.value("content").toString();
			QStringList words = extractWords(content);

			for (const QString &word : words) {
				QString lowerWord = word.toLower();
				if (!stopWordSet.contains(lowerWord) && lowerWord.length() > 2) {
					wordCounts[lowerWord]++;
				}
			}
		}
	}

	// Sort and return top N
	QList<QPair<QString, int>> sortedWords;
	for (auto it = wordCounts.begin(); it != wordCounts.end(); ++it) {
		sortedWords.append(qMakePair(it.key(), it.value()));
	}

	std::sort(sortedWords.begin(), sortedWords.end(),
	          [](const auto &a, const auto &b) { return a.second > b.second; });

	QMap<QString, int> topWords;
	for (int i = 0; i < std::min(limit, sortedWords.size()); ++i) {
		topWords[sortedWords[i].first] = sortedWords[i].second;
	}

	return topWords;
}

// Export functions
QJsonObject Analytics::exportMessageStats(qint64 chatId) {
	auto stats = getMessageStatistics(chatId);

	QJsonObject json;
	json["chat_id"] = chatId;
	json["total_messages"] = stats.totalMessages;
	json["total_words"] = stats.totalWords;
	json["total_characters"] = static_cast<qint64>(stats.totalCharacters);
	json["unique_users"] = stats.uniqueUsers;
	json["avg_message_length"] = stats.avgMessageLength;
	json["messages_per_hour"] = stats.messagesPerHour;
	json["first_message"] = stats.firstMessage.toString(Qt::ISODate);
	json["last_message"] = stats.lastMessage.toString(Qt::ISODate);

	// Top words
	QJsonObject topWordsObj;
	for (auto it = stats.topWords.begin(); it != stats.topWords.end(); ++it) {
		topWordsObj[it.key()] = it.value();
	}
	json["top_words"] = topWordsObj;

	// Top authors
	QJsonObject topAuthorsObj;
	for (auto it = stats.topAuthors.begin(); it != stats.topAuthors.end(); ++it) {
		topAuthorsObj[QString::number(it.key())] = it.value();
	}
	json["top_authors"] = topAuthorsObj;

	return json;
}

QJsonObject Analytics::exportUserActivity(qint64 userId, qint64 chatId) {
	auto activity = getUserActivityAnalysis(userId, chatId);

	QJsonObject json;
	json["user_id"] = userId;
	json["username"] = activity.username;
	json["message_count"] = activity.messageCount;
	json["word_count"] = activity.wordCount;
	json["avg_message_length"] = activity.avgMessageLength;
	json["most_active_hour"] = activity.mostActiveHour;
	json["most_active_channel"] = activity.mostActiveChannel;
	json["first_message"] = activity.firstMessage.toString(Qt::ISODate);
	json["last_message"] = activity.lastMessage.toString(Qt::ISODate);
	json["days_active"] = activity.daysActive;

	return json;
}

QJsonObject Analytics::exportChatActivity(qint64 chatId) {
	auto activity = getChatActivityAnalysis(chatId);

	QJsonObject json;
	json["chat_id"] = chatId;
	json["chat_title"] = activity.chatTitle;
	json["total_messages"] = activity.totalMessages;
	json["unique_users"] = activity.uniqueUsers;
	json["messages_per_day"] = activity.messagesPerDay;
	json["peak_hour"] = activity.peakHour;
	json["first_message"] = activity.firstMessage.toString(Qt::ISODate);
	json["last_message"] = activity.lastMessage.toString(Qt::ISODate);

	QString trendStr;
	switch (activity.trend) {
	case ActivityTrend::Increasing: trendStr = "increasing"; break;
	case ActivityTrend::Decreasing: trendStr = "decreasing"; break;
	case ActivityTrend::Stable: trendStr = "stable"; break;
	default: trendStr = "unknown"; break;
	}
	json["activity_trend"] = trendStr;

	return json;
}

QJsonArray Analytics::exportTimeSeries(qint64 chatId, TimeGranularity granularity) {
	auto series = getTimeSeries(chatId, granularity);

	QJsonArray array;
	for (const auto &point : series) {
		QJsonObject obj;
		obj["timestamp"] = point.timestamp.toString(Qt::ISODate);
		obj["message_count"] = point.messageCount;
		obj["unique_users"] = point.uniqueUsers;
		obj["avg_length"] = point.avgLength;
		array.append(obj);
	}

	return array;
}

QString Analytics::exportToCSV(qint64 chatId, const QString &outputPath) {
	// Export time series to CSV format
	auto series = getTimeSeries(chatId, TimeGranularity::Daily);

	QFile file(outputPath);
	if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
		return QString();
	}

	QTextStream out(&file);
	out << "timestamp,message_count,unique_users,avg_length\n";

	for (const auto &point : series) {
		out << point.timestamp.toString(Qt::ISODate) << ","
		    << point.messageCount << ","
		    << point.uniqueUsers << ","
		    << point.avgLength << "\n";
	}

	file.close();
	return outputPath;
}

// Helper implementations
QStringList Analytics::extractWords(const QString &text) const {
	// Split on whitespace and punctuation
	QRegularExpression wordRegex(R"(\b\w+\b)");
	QRegularExpressionMatchIterator it = wordRegex.globalMatch(text);

	QStringList words;
	while (it.hasNext()) {
		QRegularExpressionMatch match = it.next();
		words.append(match.captured(0));
	}

	return words;
}

QStringList Analytics::getDefaultStopWords() const {
	return kDefaultStopWords;
}

QString Analytics::granularityToSQL(TimeGranularity granularity) const {
	switch (granularity) {
	case TimeGranularity::Hourly:
		return "%Y-%m-%d %H:00:00";
	case TimeGranularity::Daily:
		return "%Y-%m-%d";
	case TimeGranularity::Weekly:
		return "%Y-W%W";
	case TimeGranularity::Monthly:
		return "%Y-%m";
	default:
		return "%Y-%m-%d";
	}
}

int Analytics::calculateDaysActive(qint64 userId, qint64 chatId) const {
	if (!_db || !_db->isOpen()) {
		return 0;
	}

	QString sql = R"(
		SELECT COUNT(DISTINCT DATE(datetime(timestamp, 'unixepoch'))) as days
		FROM messages
		WHERE user_id = :user_id
	)";

	if (chatId > 0) {
		sql += " AND chat_id = :chat_id";
	}

	QSqlQuery query(*_db);
	query.prepare(sql);
	query.bindValue(":user_id", userId);
	if (chatId > 0) {
		query.bindValue(":chat_id", chatId);
	}

	if (query.exec() && query.next()) {
		return query.value("days").toInt();
	}

	return 0;
}

int Analytics::findMostActiveHour(qint64 userId, qint64 chatId) const {
	if (!_db || !_db->isOpen()) {
		return -1;
	}

	QString sql = R"(
		SELECT CAST(strftime('%H', datetime(timestamp, 'unixepoch')) AS INTEGER) as hour,
		       COUNT(*) as count
		FROM messages
		WHERE user_id = :user_id
	)";

	if (chatId > 0) {
		sql += " AND chat_id = :chat_id";
	}

	sql += " GROUP BY hour ORDER BY count DESC LIMIT 1";

	QSqlQuery query(*_db);
	query.prepare(sql);
	query.bindValue(":user_id", userId);
	if (chatId > 0) {
		query.bindValue(":chat_id", chatId);
	}

	if (query.exec() && query.next()) {
		return query.value("hour").toInt();
	}

	return -1;
}

int Analytics::findPeakHour(qint64 chatId) const {
	if (!_db || !_db->isOpen()) {
		return -1;
	}

	QSqlQuery query(*_db);
	query.prepare(R"(
		SELECT CAST(strftime('%H', datetime(timestamp, 'unixepoch')) AS INTEGER) as hour,
		       COUNT(*) as count
		FROM messages
		WHERE chat_id = :chat_id
		GROUP BY hour
		ORDER BY count DESC
		LIMIT 1
	)");
	query.bindValue(":chat_id", chatId);

	if (query.exec() && query.next()) {
		return query.value("hour").toInt();
	}

	return -1;
}

} // namespace MCP

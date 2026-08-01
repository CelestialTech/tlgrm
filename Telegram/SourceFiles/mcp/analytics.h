// This file is part of Telegram Desktop MCP Server.
// Licensed under GPLv3 with OpenSSL exception.

#pragma once

#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtCore/QJsonObject>
#include <QtCore/QJsonArray>
#include <QtCore/QDateTime>
#include <QtCore/QHash>
#include <QtCore/QVector>
#include <memory>

namespace Data {
class Session;
} // namespace Data

namespace MCP {

class ChatArchiver;

// Analytics time range
struct AnalyticsTimeRange {
	QDateTime start;
	QDateTime end;
	QString period; // "hour", "day", "week", "month", "year", "all"
};

// Message statistics
struct MessageStats {
	int totalMessages = 0;
	int textMessages = 0;
	int mediaMessages = 0;
	int deletedMessages = 0;
	int editedMessages = 0;
	double averageLength = 0.0;
	double messagesPerDay = 0.0;
	QDateTime firstMessage;
	QDateTime lastMessage;
};

// User activity metrics
struct UserActivity {
	qint64 userId = 0;
	QString userName;
	int messageCount = 0;
	int replyCount = 0;
	int mentionCount = 0;
	double averageMessageLength = 0.0;
	QDateTime firstSeen;
	QDateTime lastSeen;
	QVector<int> hourlyActivity; // 24 hours
	QVector<int> weeklyActivity; // 7 days
};

// Chat activity metrics
struct ChatActivity {
	qint64 chatId = 0;
	QString chatTitle;
	int activeUsers = 0;
	int totalMessages = 0;
	double messagesPerDay = 0.0;
	double messagesPerUser = 0.0;
	QString activityTrend; // "increasing", "decreasing", "stable"
	QVector<int> hourlyDistribution;
	QVector<int> weeklyDistribution;
};

// Time series data point
struct TimeSeriesPoint {
	QDateTime timestamp;
	int messageCount = 0;
	int userCount = 0;
	double averageLength = 0.0;
	QHash<QString, int> messageTypes;
};

// Word frequency data
struct WordFrequency {
	QString word;
	int count = 0;
	double percentage = 0.0;
};

// Analytics engine class
class Analytics : public QObject {
	Q_OBJECT

public:
	explicit Analytics(QObject *parent = nullptr);
	~Analytics();

	// Initialization
	bool start(Data::Session *session, ChatArchiver *archiver);
	void stop();
	[[nodiscard]] bool isRunning() const { return _isRunning; }

	// Core analytics functions
	QJsonObject getMessageStatistics(
		qint64 chatId,
		const QString &period = "all",
		const QDateTime &startDate = QDateTime(),
		const QDateTime &endDate = QDateTime()
	);

	QJsonObject getUserActivity(
		qint64 userId,
		qint64 chatId = 0,
		const QString &period = "all"
	);

	QJsonObject getChatActivity(
		qint64 chatId,
		const QString &period = "all"
	);

	QJsonArray getTimeSeries(
		qint64 chatId,
		const QString &granularity = "daily",
		const QDateTime &startDate = QDateTime(),
		const QDateTime &endDate = QDateTime()
	);

	QJsonArray getTopUsers(
		qint64 chatId,
		int limit = 10,
		const QString &metric = "messages"
	);

	QJsonArray getTopWords(
		qint64 chatId,
		int limit = 20,
		int minLength = 4
	);

	// Export analytics
	QString exportAnalytics(
		qint64 chatId,
		const QString &format = "json",
		const QString &outputPath = QString()
	);

	// Trend analysis
	QJsonObject getTrends(
		qint64 chatId,
		const QString &metric = "messages",
		int daysBack = 30
	);

	// Comparative analytics
	QJsonObject compareChats(
		const QVector<qint64> &chatIds,
		const QString &metric = "activity"
	);

	QJsonObject compareUsers(
		qint64 chatId,
		const QVector<qint64> &userIds
	);

	// Real-time analytics
	QJsonObject getLiveActivity(qint64 chatId = 0);
	QJsonArray getActiveChats(int limit = 10);

	// Cache management
	void clearCache();
	void refreshCache(qint64 chatId = 0);

Q_SIGNALS:
	void analyticsUpdated(qint64 chatId);
	void cacheRefreshed();
	void error(const QString &errorMessage);

private:
	// Data collection
	MessageStats collectMessageStats(
		qint64 chatId,
		const AnalyticsTimeRange &range
	);

	UserActivity collectUserActivity(
		qint64 userId,
		qint64 chatId,
		const AnalyticsTimeRange &range
	);

	ChatActivity collectChatActivity(
		qint64 chatId,
		const AnalyticsTimeRange &range
	);

	// Time series generation
	QVector<TimeSeriesPoint> generateTimeSeries(
		qint64 chatId,
		const QString &granularity,
		const AnalyticsTimeRange &range
	);

	// Word analysis
	QHash<QString, int> analyzeWordFrequency(
		qint64 chatId,
		const AnalyticsTimeRange &range
	);

	QStringList extractWords(const QString &text) const;
	bool isStopWord(const QString &word) const;

	// Trend detection
	QString detectTrend(const QVector<double> &data) const;
	double calculateGrowthRate(const QVector<double> &data) const;
	QVector<double> smoothData(const QVector<double> &data, int window) const;

	// Statistical helpers
	double calculateAverage(const QVector<int> &data) const;
	double calculateStdDev(const QVector<int> &data) const;
	int calculateMedian(QVector<int> data) const;

	// Time range helpers
	AnalyticsTimeRange parseTimeRange(
		const QString &period,
		const QDateTime &start = QDateTime(),
		const QDateTime &end = QDateTime()
	) const;

	QString formatTimestamp(
		const QDateTime &dt,
		const QString &granularity
	) const;

	// Export helpers
	QString exportToJSON(const QJsonObject &analytics, const QString &path);
	QString exportToCSV(const QJsonObject &analytics, const QString &path);
	QString exportToHTML(const QJsonObject &analytics, const QString &path);

	// Conversion helpers
	QJsonObject messageStatsToJson(const MessageStats &stats) const;
	QJsonObject userActivityToJson(const UserActivity &activity) const;
	QJsonObject chatActivityToJson(const ChatActivity &activity) const;
	QJsonArray timeSeriesPointsToJson(const QVector<TimeSeriesPoint> &points) const;
	QJsonArray wordFrequenciesToJson(const QVector<WordFrequency> &frequencies) const;

	// Cache
	struct CachedAnalytics {
		QDateTime timestamp;
		QJsonObject data;
	};
	QHash<QString, CachedAnalytics> _cache;
	int _cacheLifetimeSeconds = 300; // 5 minutes

	QString getCacheKey(qint64 chatId, const QString &type) const;
	bool isCacheValid(const QString &key) const;
	void setCacheValue(const QString &key, const QJsonObject &data);
	QJsonObject getCacheValue(const QString &key) const;

	Data::Session *_session = nullptr;
	ChatArchiver *_archiver = nullptr;
	bool _isRunning = false;

	// Stop words for word frequency analysis
	QSet<QString> _stopWords;
	void initializeStopWords();
};

} // namespace MCP

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

class ChatArchiver;

// Time series granularity
enum class TimeGranularity {
	Hourly,
	Daily,
	Weekly,
	Monthly
};

// Activity trend direction
enum class ActivityTrend {
	Increasing,
	Decreasing,
	Stable,
	Unknown
};

// Analytics manager - provides statistical insights
class Analytics : public QObject {
	Q_OBJECT

public:
	explicit Analytics(ChatArchiver *archiver, QObject *parent = nullptr);
	~Analytics();

	// Message statistics
	struct MessageStats {
		int totalMessages = 0;
		int totalWords = 0;
		int totalCharacters = 0;
		int uniqueUsers = 0;
		double avgMessageLength = 0.0;
		double messagesPerHour = 0.0;
		QDateTime firstMessage;
		QDateTime lastMessage;
		QMap<QString, int> topWords;  // word -> count
		QMap<qint64, int> topAuthors;  // user_id -> count
	};

	[[nodiscard]] MessageStats getMessageStatistics(
		qint64 chatId,
		const QDateTime &start = QDateTime(),
		const QDateTime &end = QDateTime()
	);

	// User activity analysis
	struct UserActivity {
		qint64 userId;
		QString username;
		int messageCount = 0;
		int wordCount = 0;
		double avgMessageLength = 0.0;
		int mostActiveHour = -1;  // 0-23
		QString mostActiveChannel;
		QDateTime firstMessage;
		QDateTime lastMessage;
		int daysActive = 0;
	};

	[[nodiscard]] UserActivity getUserActivityAnalysis(qint64 userId, qint64 chatId = 0);
	[[nodiscard]] QVector<UserActivity> getTopUsers(qint64 chatId, int limit = 10);

	// Chat activity analysis
	struct ChatActivity {
		qint64 chatId;
		QString chatTitle;
		int totalMessages = 0;
		int uniqueUsers = 0;
		double messagesPerDay = 0.0;
		int peakHour = -1;  // 0-23
		ActivityTrend trend = ActivityTrend::Unknown;
		QDateTime firstMessage;
		QDateTime lastMessage;
	};

	[[nodiscard]] ChatActivity getChatActivityAnalysis(qint64 chatId);
	[[nodiscard]] ActivityTrend detectActivityTrend(qint64 chatId);

	// Time series data
	struct TimeSeriesPoint {
		QDateTime timestamp;
		int messageCount = 0;
		int uniqueUsers = 0;
		double avgLength = 0.0;
	};

	[[nodiscard]] QVector<TimeSeriesPoint> getTimeSeries(
		qint64 chatId,
		TimeGranularity granularity,
		const QDateTime &start = QDateTime(),
		const QDateTime &end = QDateTime()
	);

	// Export functions
	QJsonObject exportMessageStats(qint64 chatId);
	QJsonObject exportUserActivity(qint64 userId, qint64 chatId = 0);
	QJsonObject exportChatActivity(qint64 chatId);
	QJsonArray exportTimeSeries(
		qint64 chatId,
		TimeGranularity granularity
	);
	QString exportToCSV(qint64 chatId, const QString &outputPath);

	// Word analysis
	[[nodiscard]] QMap<QString, int> getTopWords(
		qint64 chatId,
		int limit = 20,
		const QStringList &stopWords = QStringList()
	);
	[[nodiscard]] QMap<QString, int> getUserTopWords(qint64 userId, int limit = 20);

	// Sentiment and patterns (placeholder for future AI integration)
	struct SentimentScore {
		double positive = 0.0;
		double negative = 0.0;
		double neutral = 0.0;
	};

private:
	// Helper functions
	QStringList extractWords(const QString &text) const;
	QStringList getDefaultStopWords() const;
	QString granularityToSQL(TimeGranularity granularity) const;
	int calculateDaysActive(qint64 userId, qint64 chatId) const;
	int findMostActiveHour(qint64 userId, qint64 chatId) const;
	int findPeakHour(qint64 chatId) const;

	ChatArchiver *_archiver;
	QSqlDatabase *_db;
};

} // namespace MCP

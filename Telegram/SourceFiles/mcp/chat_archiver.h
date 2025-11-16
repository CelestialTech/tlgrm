// This file is part of Telegram Desktop MCP Server.
// Licensed under GPLv3 with OpenSSL exception.

#pragma once

#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtCore/QDateTime>
#include <QtCore/QJsonObject>
#include <QtCore/QJsonArray>
#include <QtSql/QSqlDatabase>
#include <QtSql/QSqlQuery>
#include <memory>

namespace Data {
class Session;
class Message;
} // namespace Data

namespace MCP {

// Archival statistics
struct ArchivalStats {
	int totalMessages = 0;
	int totalChats = 0;
	int totalUsers = 0;
	int ephemeralCaptured = 0;
	int mediaDownloaded = 0;
	qint64 databaseSize = 0;  // bytes
	QDateTime lastArchived;
};

// Export format options
enum class ExportFormat {
	JSON,      // Complete JSON export
	JSONL,     // Line-delimited JSON (AI-friendly)
	CSV        // Comma-separated values
};

// Message type enumeration
enum class MessageType {
	Text,
	Photo,
	Video,
	Voice,
	Audio,
	Document,
	Sticker,
	Animation,
	Contact,
	Location,
	Poll,
	Venue,
	Game,
	Unknown
};

// Chat archiver class - handles message storage and export
class ChatArchiver : public QObject {
	Q_OBJECT

public:
	explicit ChatArchiver(QObject *parent = nullptr);
	~ChatArchiver();

	// Initialization
	bool start(const QString &databasePath);
	void stop();
	[[nodiscard]] bool isRunning() const { return _isRunning; }

	// Core archival functions
	bool archiveMessage(const Data::Message *message);
	bool archiveChat(qint64 chatId, int messageLimit = -1);  // -1 = all messages
	bool archiveAllChats(int messagesPerChat = 1000);

	// Ephemeral message handling
	bool archiveEphemeralMessage(
		const Data::Message *message,
		const QString &ephemeralType,
		int ttlSeconds = 0
	);
	bool isEphemeral(const Data::Message *message) const;

	// Query functions
	QJsonArray getMessages(qint64 chatId, int limit = 100, qint64 beforeTimestamp = 0);
	QJsonArray searchMessages(qint64 chatId, const QString &query, int limit = 50);
	QJsonObject getChatInfo(qint64 chatId);
	QJsonArray listArchivedChats();

	// Analytics functions
	QJsonObject getMessageStats(qint64 chatId, const QString &period = "all");
	QJsonObject getUserActivity(qint64 userId, qint64 chatId = 0);
	QJsonObject getChatActivity(qint64 chatId);
	QJsonArray getTimeSeries(qint64 chatId, const QString &granularity = "daily");
	QJsonArray getTopUsers(qint64 chatId, int limit = 10);
	QJsonArray getTopWords(qint64 chatId, int limit = 20);

	// Export functions
	QString exportChat(
		qint64 chatId,
		ExportFormat format,
		const QString &outputPath,
		const QDateTime &startDate = QDateTime(),
		const QDateTime &endDate = QDateTime()
	);
	QString exportAllChats(ExportFormat format, const QString &outputDir);

	// Statistics
	ArchivalStats getStats() const;
	void updateStats();

	// Maintenance
	bool vacuum();  // Optimize database
	bool rebuildIndexes();
	bool purgeOldMessages(int daysToKeep);

Q_SIGNALS:
	void messageArchived(qint64 chatId, qint64 messageId);
	void chatArchived(qint64 chatId, int messageCount);
	void exportCompleted(const QString &filePath);
	void error(const QString &errorMessage);

private Q_SLOTS:
	void onNewMessage(const Data::Message *message);
	void onMessageEdited(const Data::Message *message);
	void checkForNewMessages();

private:
	// Database helpers
	bool initializeDatabase();
	bool executeSQLFile(const QString &filePath);
	QSqlQuery prepareQuery(const QString &sql);

	// Message conversion
	QJsonObject messageToJson(const QSqlQuery &query) const;
	MessageType detectMessageType(const Data::Message *message) const;
	QString messageTypeToString(MessageType type) const;

	// Text processing
	int countWords(const QString &text) const;
	QStringList extractWords(const QString &text) const;
	QString sanitizeForCSV(const QString &text) const;

	// Export helpers
	bool exportToJSON(qint64 chatId, const QString &outputPath,
	                  const QDateTime &start, const QDateTime &end);
	bool exportToJSONL(qint64 chatId, const QString &outputPath,
	                   const QDateTime &start, const QDateTime &end);
	bool exportToCSV(qint64 chatId, const QString &outputPath,
	                 const QDateTime &start, const QDateTime &end);

	// Analytics helpers
	void updateDailyStats(qint64 chatId, const QDate &date);
	void updateUserActivity(qint64 userId, qint64 chatId);
	void updateChatActivity(qint64 chatId);
	QString detectActivityTrend(qint64 chatId) const;

	// Media handling
	QString downloadMedia(const Data::Message *message);
	QString getMediaPath(qint64 chatId, qint64 messageId, const QString &extension);

	Data::Session *_session = nullptr;
	QSqlDatabase _db;
	QString _databasePath;
	bool _isRunning = false;
	ArchivalStats _stats;
	QTimer *_checkTimer = nullptr;
};

// Ephemeral message archiver - captures self-destructing messages
class EphemeralArchiver : public QObject {
	Q_OBJECT

public:
	explicit EphemeralArchiver(QObject *parent = nullptr);
	~EphemeralArchiver();

	bool start(ChatArchiver *archiver);
	void stop();
	[[nodiscard]] bool isRunning() const { return _isRunning; }

	// Configuration
	void setAutoCapture(bool enabled) { _autoCapture = enabled; }
	void setCaptureTypes(bool selfDestruct, bool viewOnce, bool vanishing);

	// Statistics
	struct EphemeralStats {
		int totalCaptured = 0;
		int selfDestructCount = 0;
		int viewOnceCount = 0;
		int vanishingCount = 0;
		int mediaSaved = 0;
		QDateTime lastCaptured;
	};
	[[nodiscard]] EphemeralStats getStats() const { return _stats; }

Q_SIGNALS:
	void ephemeralCaptured(qint64 chatId, qint64 messageId, const QString &type);
	void error(const QString &errorMessage);

private Q_SLOTS:
	void onNewMessage(const Data::Message *message);
	void onMessageDeleted(qint64 chatId, qint64 messageId);
	void checkForEphemeral();

private:
	bool captureMessage(const Data::Message *message, const QString &type, int ttl);
	bool detectEphemeralType(const Data::Message *message, QString &type, int &ttl);
	QString downloadEphemeralMedia(const Data::Message *message);

	ChatArchiver *_archiver = nullptr;
	Data::Session *_session = nullptr;
	bool _isRunning = false;
	bool _autoCapture = true;
	bool _captureSelfDestruct = true;
	bool _captureViewOnce = true;
	bool _captureVanishing = true;
	EphemeralStats _stats;
	QTimer *_checkTimer = nullptr;
};

} // namespace MCP

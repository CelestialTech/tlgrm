// This file is part of Telegram Desktop MCP Server.
// Licensed under GPLv3 with OpenSSL exception.

#pragma once

#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtCore/QDateTime>
#include <QtCore/QTimer>
#include <QtCore/QRandomGenerator>
#include <QtCore/QJsonObject>
#include <QtCore/QJsonArray>
#include <QtSql/QSqlDatabase>

namespace Main {
class Session;
} // namespace Main

namespace Data {
class Session;
} // namespace Data

namespace MCP {

class ChatArchiver;

// Configuration for gradual/covert archiving
struct GradualArchiveConfig {
	// Timing parameters (all in milliseconds)
	int minDelayMs = 3000;        // Min delay between batches (3 sec)
	int maxDelayMs = 15000;       // Max delay between batches (15 sec)
	int burstPauseMs = 60000;     // Pause after burst (1 min)
	int longPauseMs = 300000;     // Occasional long pause (5 min)

	// Batch parameters
	int minBatchSize = 10;        // Min messages per batch
	int maxBatchSize = 50;        // Max messages per batch
	int batchesBeforePause = 5;   // Batches before burst pause
	int batchesBeforeLongPause = 20; // Batches before long pause

	// Behavior patterns
	bool randomizeOrder = true;   // Don't always go oldest-to-newest
	bool simulateReading = true;  // Add "reading time" based on message length
	bool respectActiveHours = true; // Only run during typical usage hours
	int activeHourStart = 8;      // Start hour (24h format)
	int activeHourEnd = 23;       // End hour (24h format)

	// Safety limits
	int maxMessagesPerDay = 5000; // Daily limit to stay under radar
	int maxMessagesPerHour = 500; // Hourly limit
	bool stopOnFloodWait = true;  // Stop if rate limited
	int maxRetries = 3;           // Max retries on error

	// Export settings
	bool autoExportOnComplete = true;
	QString exportFormat = "html"; // html, markdown, or both
	QString exportPath;
};

// Status of a gradual archive job
struct GradualArchiveStatus {
	enum class State {
		Idle,
		Running,
		Paused,
		WaitingForActiveHours,
		RateLimited,
		Completed,
		Failed
	};

	State state = State::Idle;
	qint64 chatId = 0;
	QString chatTitle;

	int totalMessages = 0;        // Estimated total
	int archivedMessages = 0;     // Successfully archived
	int failedMessages = 0;
	int batchesCompleted = 0;

	int messagesArchivedToday = 0;
	int messagesArchivedThisHour = 0;

	// Size tracking
	qint64 totalBytesProcessed = 0;  // Text content size
	qint64 totalMediaBytes = 0;       // Media files size

	QDateTime startTime;
	QDateTime lastActivityTime;
	QDateTime estimatedCompletion;
	QDateTime nextActionTime;

	int currentDelayMs = 0;
	int floodWaitSeconds = 0;

	QString lastError;
};

// Gradual archiver - covert export with natural timing
class GradualArchiver : public QObject {
	Q_OBJECT

public:
	explicit GradualArchiver(QObject *parent = nullptr);
	~GradualArchiver();

	void setArchiver(ChatArchiver *archiver) { _archiver = archiver; }
	void setDataSession(Data::Session *session) { _session = session; }
	void setMainSession(Main::Session *mainSession) { _mainSession = mainSession; }

	// Start gradual archiving of a chat
	bool startGradualArchive(
		qint64 chatId,
		const GradualArchiveConfig &config = GradualArchiveConfig{});

	// Control methods
	void pause();
	void resume();
	void cancel();

	// Queue multiple chats
	bool queueChat(qint64 chatId, const GradualArchiveConfig &config);
	void clearQueue();
	QJsonArray getQueue() const;

	// Status
	GradualArchiveStatus status() const { return _status; }
	QJsonObject statusJson() const;
	bool isRunning() const { return _status.state == GradualArchiveStatus::State::Running; }

	// Configuration
	void setConfig(const GradualArchiveConfig &config) { _config = config; }
	GradualArchiveConfig config() const { return _config; }
	QJsonObject configJson() const;
	bool loadConfigFromJson(const QJsonObject &json);

Q_SIGNALS:
	void progressChanged(int archived, int total);
	void stateChanged(GradualArchiveStatus::State state);
	void batchCompleted(int batchSize, int totalArchived);
	void archiveCompleted(qint64 chatId, int totalMessages);
	void exportReady(const QString &path);
	void error(const QString &message);
	void rateLimited(int waitSeconds);
	void operationLog(const QString &message); // Real-time operation log for UI
	void sizeUpdated(qint64 textBytes, qint64 mediaBytes); // Size tracking

private Q_SLOTS:
	void processNextBatch();
	void checkActiveHours();
	void resetHourlyCounter();
	void resetDailyCounter();

private:
	// Timing helpers
	int calculateNextDelay();
	int calculateBatchSize();
	bool isWithinActiveHours() const;
	int calculateReadingTime(int messageLength);

	// Archive helpers
	bool fetchBatch(int limit, qint64 offsetId);
	void fetchBatchFromServer(int limit, qint64 offsetId);
	void processServerMessages(const QVector<HistoryItem*> &messages, int limit);
	void handleFloodWait(int seconds);
	void scheduleNextBatch();
	void completeArchive();
	void startExport();
	void downloadMedia(not_null<HistoryItem*> item);

	// Queue management
	void processNextInQueue();

	// State persistence
	void saveState();
	void loadState();
	QString stateFilePath() const;

	ChatArchiver *_archiver = nullptr;
	Data::Session *_session = nullptr;
	Main::Session *_mainSession = nullptr;

	GradualArchiveConfig _config;
	GradualArchiveStatus _status;

	QTimer *_batchTimer = nullptr;
	QTimer *_activeHoursTimer = nullptr;
	QTimer *_hourlyResetTimer = nullptr;
	QTimer *_dailyResetTimer = nullptr;

	qint64 _currentOffsetId = 0;
	int _consecutiveBatches = 0;
	int _retryCount = 0;

	// Queue of chats to archive
	struct QueuedChat {
		qint64 chatId;
		GradualArchiveConfig config;
	};
	QVector<QueuedChat> _queue;

	// In-memory message storage (when no database archiver)
	QJsonArray _collectedMessages;

	QRandomGenerator _rng;
};

} // namespace MCP

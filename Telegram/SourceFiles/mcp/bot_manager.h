// Bot Framework - Bot Manager
// This file is part of Telegram Desktop MCP Server.
// Licensed under GPLv3 with OpenSSL exception.

#pragma once

#include "bot_base.h"

#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtCore/QMap>
#include <QtCore/QVector>
#include <QtCore/QJsonObject>
#include <QtCore/QJsonArray>
#include <QtCore/QMutex>
#include <QtCore/QTimer>

namespace MCP {

// Forward declarations
class ChatArchiver;
class Analytics;
class SemanticSearch;
class MessageScheduler;
class AuditLogger;
class RBAC;

// Bot execution statistics
struct BotStats {
	qint64 messagesProcessed = 0;
	qint64 commandsExecuted = 0;
	qint64 errorsOccurred = 0;
	qint64 totalExecutionTimeMs = 0;
	qint64 lastExecutionTimeMs = 0;
	QDateTime lastActive;
	QDateTime registeredAt;

	double avgExecutionTimeMs() const {
		return messagesProcessed > 0
			? static_cast<double>(totalExecutionTimeMs) / messagesProcessed
			: 0.0;
	}
};

// Bot Manager - Central coordinator for all bots
class BotManager : public QObject {
	Q_OBJECT

public:
	explicit BotManager(QObject *parent = nullptr);
	~BotManager();

	// Initialization
	void initialize(
		ChatArchiver *archiver,
		Analytics *analytics,
		SemanticSearch *semanticSearch,
		MessageScheduler *scheduler,
		AuditLogger *auditLogger,
		RBAC *rbac
	);

	void shutdown();

	// Bot lifecycle management
	bool registerBot(BotBase *bot);
	bool unregisterBot(const QString &botId);
	bool startBot(const QString &botId);
	bool stopBot(const QString &botId);
	bool restartBot(const QString &botId);

	// Bot queries
	BotBase* getBot(const QString &botId) const;
	QVector<BotBase*> getAllBots() const;
	QVector<BotBase*> getRunningBots() const;
	QVector<BotBase*> getEnabledBots() const;
	bool isBotRegistered(const QString &botId) const;
	bool isBotRunning(const QString &botId) const;

	// Configuration management
	bool loadBotConfig(const QString &botId);
	bool saveBotConfig(const QString &botId, const QJsonObject &config);
	QJsonObject getBotConfig(const QString &botId) const;
	QJsonObject getAllConfigs() const;

	// Event dispatch
	void dispatchMessage(const Message &msg);
	void dispatchMessageEdited(const Message &oldMsg, const Message &newMsg);
	void dispatchMessageDeleted(qint64 messageId, qint64 chatId);
	void dispatchChatJoined(qint64 chatId);
	void dispatchChatLeft(qint64 chatId);
	void dispatchUserStatusChanged(qint64 userId, const QString &status);
	void dispatchCommand(const QString &botId, const QString &cmd, const QJsonObject &args);

	// Statistics
	BotStats getBotStats(const QString &botId) const;
	QMap<QString, BotStats> getAllStats() const;
	void resetStats(const QString &botId);

	// Bot discovery & auto-loading
	void discoverBots();
	void loadBuiltInBots();
	void loadPluginBots(const QString &pluginDir);

	// Global settings
	void setEventDispatchEnabled(bool enabled);
	bool isEventDispatchEnabled() const { return _eventDispatchEnabled; }

	void setMaxConcurrentBots(int max);
	int maxConcurrentBots() const { return _maxConcurrentBots; }

	// Debugging & monitoring
	QJsonObject getSystemStatus() const;
	void dumpBotInfo() const;

Q_SIGNALS:
	void botRegistered(const QString &botId);
	void botUnregistered(const QString &botId);
	void botStarted(const QString &botId);
	void botStopped(const QString &botId);
	void botError(const QString &botId, const QString &error);
	void eventDispatched(const QString &eventType, int botCount);

private Q_SLOTS:
	void onBotConfigChanged();
	void onBotStateChanged();
	void onBotError(const QString &error);
	void onPerformanceCheckTimer();

private:
	// Internal helpers
	bool initializeBot(BotBase *bot);
	void shutdownBot(BotBase *bot);
	void updateStats(const QString &botId, qint64 executionTimeMs, bool error = false);
	bool checkPermissions(BotBase *bot) const;
	void loadPersistedConfigs();
	void saveAllConfigs();
	QString configFilePath() const;

	// Service pointers (injected during initialization)
	ChatArchiver *_archiver = nullptr;
	Analytics *_analytics = nullptr;
	SemanticSearch *_semanticSearch = nullptr;
	MessageScheduler *_scheduler = nullptr;
	AuditLogger *_auditLogger = nullptr;
	RBAC *_rbac = nullptr;

	// Bot registry
	QMap<QString, BotBase*> _bots;
	QMap<QString, BotStats> _stats;
	QMap<QString, QJsonObject> _configs;

	// Thread safety
	mutable QMutex _mutex;

	// Settings
	bool _eventDispatchEnabled = true;
	int _maxConcurrentBots = 20;
	bool _isInitialized = false;

	// Performance monitoring
	QTimer *_performanceTimer = nullptr;
	static const int PERFORMANCE_CHECK_INTERVAL_MS = 60000; // 1 minute
};

} // namespace MCP

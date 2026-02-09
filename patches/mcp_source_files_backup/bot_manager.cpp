// Bot Framework - Bot Manager Implementation
// This file is part of Telegram Desktop MCP Server.
// Licensed under GPLv3 with OpenSSL exception.

#include "bot_manager.h"
#include "chat_archiver.h"
#include "analytics.h"
#include "semantic_search.h"
#include "message_scheduler.h"
#include "audit_logger.h"
#include "rbac.h"

#include <QtCore/QDebug>
#include <QtCore/QFile>
#include <QtCore/QJsonDocument>
#include <QtCore/QStandardPaths>
#include <QtCore/QDir>
#include <QtCore/QMutexLocker>
#include <QtCore/QElapsedTimer>

namespace MCP {

BotManager::BotManager(QObject *parent)
	: QObject(parent) {

	// Setup performance monitoring timer
	_performanceTimer = new QTimer(this);
	_performanceTimer->setInterval(PERFORMANCE_CHECK_INTERVAL_MS);
	connect(_performanceTimer, &QTimer::timeout, this, &BotManager::onPerformanceCheckTimer);
}

BotManager::~BotManager() {
	shutdown();
}

// Initialization

void BotManager::initialize(
		ChatArchiver *archiver,
		Analytics *analytics,
		SemanticSearch *semanticSearch,
		MessageScheduler *scheduler,
		AuditLogger *auditLogger,
		RBAC *rbac) {

	QMutexLocker locker(&_mutex);

	if (_isInitialized) {
		qWarning() << "[BotManager] Already initialized";
		return;
	}

	_archiver = archiver;
	_analytics = analytics;
	_semanticSearch = semanticSearch;
	_scheduler = scheduler;
	_auditLogger = auditLogger;
	_rbac = rbac;

	// Load persisted configurations
	loadPersistedConfigs();

	// Start performance monitoring
	_performanceTimer->start();

	_isInitialized = true;

	qInfo() << "[BotManager] Initialized successfully";

	if (_auditLogger) {
		_auditLogger->logSystemEvent("bot_manager_initialized", "Bot framework ready");
	}
}

void BotManager::shutdown() {
	QMutexLocker locker(&_mutex);

	if (!_isInitialized) {
		return;
	}

	qInfo() << "[BotManager] Shutting down bot framework...";

	// Stop performance timer
	_performanceTimer->stop();

	// Save all configurations
	saveAllConfigs();

	// Shutdown all running bots
	QStringList botIds = _bots.keys();
	for (const QString &botId : botIds) {
		BotBase *bot = _bots.value(botId);
		if (bot && bot->isRunning()) {
			shutdownBot(bot);
		}
	}

	// Clear registry
	_bots.clear();
	_stats.clear();
	_configs.clear();

	_isInitialized = false;

	qInfo() << "[BotManager] Shutdown complete";

	if (_auditLogger) {
		_auditLogger->logSystemEvent("bot_manager_shutdown", "Bot framework stopped");
	}
}

// Bot lifecycle management

bool BotManager::registerBot(BotBase *bot) {
	if (!bot) {
		qWarning() << "[BotManager] Cannot register null bot";
		return false;
	}

	QMutexLocker locker(&_mutex);

	auto botInfo = bot->info();

	if (_bots.contains(botInfo.id)) {
		qWarning() << "[BotManager] Bot already registered:" << botInfo.id;
		return false;
	}

	// Check if we've reached max concurrent bots
	if (_bots.size() >= _maxConcurrentBots) {
		qWarning() << "[BotManager] Max concurrent bots reached:" << _maxConcurrentBots;
		return false;
	}

	// Add to registry
	_bots.insert(botInfo.id, bot);

	// Initialize statistics
	BotStats stats;
	stats.registeredAt = QDateTime::currentDateTime();
	_stats.insert(botInfo.id, stats);

	// Load or create config
	if (_configs.contains(botInfo.id)) {
		bot->setConfig(_configs.value(botInfo.id));
	} else {
		QJsonObject defaultCfg = bot->defaultConfig();
		_configs.insert(botInfo.id, defaultCfg);
		bot->setConfig(defaultCfg);
	}

	// Connect signals
	connect(bot, &BotBase::configChanged, this, &BotManager::onBotConfigChanged);
	connect(bot, &BotBase::stateChanged, this, &BotManager::onBotStateChanged);
	connect(bot, &BotBase::errorOccurred, this, &BotManager::onBotError);

	qInfo() << "[BotManager] Registered bot:" << botInfo.id << "v" << botInfo.version;

	Q_EMIT botRegistered(botInfo.id);

	if (_auditLogger) {
		QJsonObject params;
		params["bot_id"] = botInfo.id;
		params["version"] = botInfo.version;
		params["author"] = botInfo.author;
		_auditLogger->logSystemEvent("bot_registered", QString(), params);
	}

	return true;
}

bool BotManager::unregisterBot(const QString &botId) {
	QMutexLocker locker(&_mutex);

	if (!_bots.contains(botId)) {
		qWarning() << "[BotManager] Bot not registered:" << botId;
		return false;
	}

	BotBase *bot = _bots.value(botId);

	// Stop bot if running
	if (bot->isRunning()) {
		shutdownBot(bot);
	}

	// Disconnect signals
	disconnect(bot, nullptr, this, nullptr);

	// Save final config
	saveBotConfig(botId, bot->config());

	// Remove from registry
	_bots.remove(botId);
	_stats.remove(botId);

	qInfo() << "[BotManager] Unregistered bot:" << botId;

	Q_EMIT botUnregistered(botId);

	if (_auditLogger) {
		QJsonObject params;
		params["bot_id"] = botId;
		_auditLogger->logSystemEvent("bot_unregistered", QString(), params);
	}

	return true;
}

bool BotManager::startBot(const QString &botId) {
	QMutexLocker locker(&_mutex);

	if (!_bots.contains(botId)) {
		qWarning() << "[BotManager] Bot not registered:" << botId;
		return false;
	}

	BotBase *bot = _bots.value(botId);

	if (bot->isRunning()) {
		qWarning() << "[BotManager] Bot already running:" << botId;
		return false;
	}

	if (!bot->isEnabled()) {
		qWarning() << "[BotManager] Bot is disabled:" << botId;
		return false;
	}

	// Initialize bot
	bool success = initializeBot(bot);

	if (success) {
		qInfo() << "[BotManager] Started bot:" << botId;
		Q_EMIT botStarted(botId);

		if (_auditLogger) {
			_auditLogger->logSystemEvent("bot_started", botId);
		}
	} else {
		qCritical() << "[BotManager] Failed to start bot:" << botId;
		Q_EMIT botError(botId, "Initialization failed");
	}

	return success;
}

bool BotManager::stopBot(const QString &botId) {
	QMutexLocker locker(&_mutex);

	if (!_bots.contains(botId)) {
		qWarning() << "[BotManager] Bot not registered:" << botId;
		return false;
	}

	BotBase *bot = _bots.value(botId);

	if (!bot->isRunning()) {
		qWarning() << "[BotManager] Bot not running:" << botId;
		return false;
	}

	shutdownBot(bot);

	qInfo() << "[BotManager] Stopped bot:" << botId;

	Q_EMIT botStopped(botId);

	if (_auditLogger) {
		_auditLogger->logSystemEvent("bot_stopped", botId);
	}

	return true;
}

bool BotManager::restartBot(const QString &botId) {
	qInfo() << "[BotManager] Restarting bot:" << botId;

	if (!stopBot(botId)) {
		return false;
	}

	// Small delay to ensure clean shutdown
	QThread::msleep(100);

	return startBot(botId);
}

bool BotManager::enableBot(const QString &botId) {
	QMutexLocker locker(&_mutex);

	auto *bot = getBot(botId);
	if (!bot) {
		qWarning() << "[BotManager] Cannot enable: bot not found:" << botId;
		return false;
	}

	bot->setEnabled(true);
	qInfo() << "[BotManager] Bot enabled:" << botId;
	return true;
}

bool BotManager::disableBot(const QString &botId) {
	QMutexLocker locker(&_mutex);

	auto *bot = getBot(botId);
	if (!bot) {
		qWarning() << "[BotManager] Cannot disable: bot not found:" << botId;
		return false;
	}

	bot->setEnabled(false);
	qInfo() << "[BotManager] Bot disabled:" << botId;
	return true;
}

// Bot queries

BotBase* BotManager::getBot(const QString &botId) const {
	QMutexLocker locker(&_mutex);
	return _bots.value(botId, nullptr);
}

QVector<BotBase*> BotManager::getAllBots() const {
	QMutexLocker locker(&_mutex);
	return _bots.values().toVector();
}

QVector<BotBase*> BotManager::getRunningBots() const {
	QMutexLocker locker(&_mutex);
	QVector<BotBase*> running;
	for (BotBase *bot : _bots.values()) {
		if (bot && bot->isRunning()) {
			running.append(bot);
		}
	}
	return running;
}

QVector<BotBase*> BotManager::getEnabledBots() const {
	QMutexLocker locker(&_mutex);
	QVector<BotBase*> enabled;
	for (BotBase *bot : _bots.values()) {
		if (bot && bot->isEnabled()) {
			enabled.append(bot);
		}
	}
	return enabled;
}

bool BotManager::isBotRegistered(const QString &botId) const {
	QMutexLocker locker(&_mutex);
	return _bots.contains(botId);
}

bool BotManager::isBotRunning(const QString &botId) const {
	QMutexLocker locker(&_mutex);
	BotBase *bot = _bots.value(botId, nullptr);
	return bot && bot->isRunning();
}

// Configuration management

bool BotManager::loadBotConfig(const QString &botId) {
	// Load from persistent storage
	// TODO: Implement database integration
	return false;
}

bool BotManager::saveBotConfig(const QString &botId, const QJsonObject &config) {
	QMutexLocker locker(&_mutex);

	_configs.insert(botId, config);

	if (_bots.contains(botId)) {
		_bots.value(botId)->setConfig(config);
	}

	// TODO: Persist to database

	return true;
}

QJsonObject BotManager::getBotConfig(const QString &botId) const {
	QMutexLocker locker(&_mutex);
	return _configs.value(botId, QJsonObject());
}

QJsonObject BotManager::getAllConfigs() const {
	QMutexLocker locker(&_mutex);

	QJsonObject allConfigs;
	for (auto it = _configs.constBegin(); it != _configs.constEnd(); ++it) {
		allConfigs.insert(it.key(), it.value());
	}

	return allConfigs;
}

// Event dispatch

void BotManager::dispatchMessage(const Message &msg) {
	if (!_eventDispatchEnabled) {
		return;
	}

	QMutexLocker locker(&_mutex);

	QVector<BotBase*> runningBots = getRunningBots();
	int dispatchCount = 0;

	for (BotBase *bot : runningBots) {
		if (!bot->isEnabled()) {
			continue;
		}

		QElapsedTimer timer;
		timer.start();

		try {
			bot->onMessage(msg);
			dispatchCount++;

			qint64 elapsed = timer.elapsed();
			updateStats(bot->info().id, elapsed, false);

		} catch (const std::exception &e) {
			qCritical() << "[BotManager] Bot crashed on message:" << bot->info().id << e.what();
			updateStats(bot->info().id, timer.elapsed(), true);
			Q_EMIT botError(bot->info().id, QString("Crash: %1").arg(e.what()));
		}
	}

	if (dispatchCount > 0) {
		Q_EMIT eventDispatched("message", dispatchCount);
	}
}

void BotManager::dispatchMessageEdited(const Message &oldMsg, const Message &newMsg) {
	if (!_eventDispatchEnabled) {
		return;
	}

	QMutexLocker locker(&_mutex);

	for (BotBase *bot : getRunningBots()) {
		if (bot->isEnabled()) {
			try {
				bot->onMessageEdited(oldMsg, newMsg);
			} catch (const std::exception &e) {
				qCritical() << "[BotManager] Bot crashed:" << bot->info().id << e.what();
			}
		}
	}

	Q_EMIT eventDispatched("message_edited", getRunningBots().size());
}

void BotManager::dispatchMessageDeleted(qint64 messageId, qint64 chatId) {
	if (!_eventDispatchEnabled) {
		return;
	}

	QMutexLocker locker(&_mutex);

	for (BotBase *bot : getRunningBots()) {
		if (bot->isEnabled()) {
			try {
				bot->onMessageDeleted(messageId, chatId);
			} catch (const std::exception &e) {
				qCritical() << "[BotManager] Bot crashed:" << bot->info().id << e.what();
			}
		}
	}

	Q_EMIT eventDispatched("message_deleted", getRunningBots().size());
}

void BotManager::dispatchChatJoined(qint64 chatId) {
	if (!_eventDispatchEnabled) {
		return;
	}

	QMutexLocker locker(&_mutex);

	for (BotBase *bot : getRunningBots()) {
		if (bot->isEnabled()) {
			try {
				bot->onChatJoined(chatId);
			} catch (const std::exception &e) {
				qCritical() << "[BotManager] Bot crashed:" << bot->info().id << e.what();
			}
		}
	}

	Q_EMIT eventDispatched("chat_joined", getRunningBots().size());
}

void BotManager::dispatchChatLeft(qint64 chatId) {
	if (!_eventDispatchEnabled) {
		return;
	}

	QMutexLocker locker(&_mutex);

	for (BotBase *bot : getRunningBots()) {
		if (bot->isEnabled()) {
			try {
				bot->onChatLeft(chatId);
			} catch (const std::exception &e) {
				qCritical() << "[BotManager] Bot crashed:" << bot->info().id << e.what();
			}
		}
	}

	Q_EMIT eventDispatched("chat_left", getRunningBots().size());
}

void BotManager::dispatchUserStatusChanged(qint64 userId, const QString &status) {
	if (!_eventDispatchEnabled) {
		return;
	}

	QMutexLocker locker(&_mutex);

	for (BotBase *bot : getRunningBots()) {
		if (bot->isEnabled()) {
			try {
				bot->onUserStatusChanged(userId, status);
			} catch (const std::exception &e) {
				qCritical() << "[BotManager] Bot crashed:" << bot->info().id << e.what();
			}
		}
	}

	Q_EMIT eventDispatched("user_status_changed", getRunningBots().size());
}

void BotManager::dispatchCommand(const QString &botId, const QString &cmd, const QJsonObject &args) {
	QMutexLocker locker(&_mutex);

	if (!_bots.contains(botId)) {
		qWarning() << "[BotManager] Cannot dispatch command to unregistered bot:" << botId;
		return;
	}

	BotBase *bot = _bots.value(botId);

	if (!bot->isRunning() || !bot->isEnabled()) {
		qWarning() << "[BotManager] Cannot dispatch command to inactive bot:" << botId;
		return;
	}

	QElapsedTimer timer;
	timer.start();

	try {
		bot->onCommand(cmd, args);

		if (_stats.contains(botId)) {
			_stats[botId].commandsExecuted++;
		}

		qint64 elapsed = timer.elapsed();
		updateStats(botId, elapsed, false);

	} catch (const std::exception &e) {
		qCritical() << "[BotManager] Bot crashed on command:" << botId << cmd << e.what();
		updateStats(botId, timer.elapsed(), true);
		Q_EMIT botError(botId, QString("Command failed: %1").arg(e.what()));
	}

	Q_EMIT eventDispatched("command", 1);
}

// Statistics

BotStats BotManager::getBotStats(const QString &botId) const {
	QMutexLocker locker(&_mutex);
	return _stats.value(botId, BotStats());
}

QMap<QString, BotStats> BotManager::getAllStats() const {
	QMutexLocker locker(&_mutex);
	return _stats;
}

void BotManager::resetStats(const QString &botId) {
	QMutexLocker locker(&_mutex);

	if (_stats.contains(botId)) {
		BotStats newStats;
		newStats.registeredAt = _stats.value(botId).registeredAt;
		_stats.insert(botId, newStats);

		qInfo() << "[BotManager] Reset statistics for bot:" << botId;
	}
}

// Bot discovery & auto-loading

void BotManager::discoverBots() {
	qInfo() << "[BotManager] Discovering bots...";

	loadBuiltInBots();

	// TODO: Load plugins from plugin directory
	QString pluginDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/bot_plugins";
	loadPluginBots(pluginDir);

	qInfo() << "[BotManager] Discovery complete. Total bots:" << _bots.size();
}

void BotManager::loadBuiltInBots() {
	// Built-in bots will be registered manually or via factory pattern
	// TODO: Implement factory-based bot creation
	qInfo() << "[BotManager] Loading built-in bots...";
}

void BotManager::loadPluginBots(const QString &pluginDir) {
	QDir dir(pluginDir);
	if (!dir.exists()) {
		qInfo() << "[BotManager] Plugin directory does not exist:" << pluginDir;
		return;
	}

	// TODO: Implement dynamic plugin loading
	qInfo() << "[BotManager] Loading plugins from:" << pluginDir;
}

// Global settings

void BotManager::setEventDispatchEnabled(bool enabled) {
	_eventDispatchEnabled = enabled;

	qInfo() << "[BotManager] Event dispatch" << (enabled ? "enabled" : "disabled");

	if (_auditLogger) {
		_auditLogger->logSystemEvent(
			"bot_event_dispatch_changed",
			enabled ? "enabled" : "disabled"
		);
	}
}

void BotManager::setMaxConcurrentBots(int max) {
	_maxConcurrentBots = max;
	qInfo() << "[BotManager] Max concurrent bots set to:" << max;
}

// Debugging & monitoring

QJsonObject BotManager::getSystemStatus() const {
	QMutexLocker locker(&_mutex);

	QJsonObject status;
	status["initialized"] = _isInitialized;
	status["event_dispatch_enabled"] = _eventDispatchEnabled;
	status["max_concurrent_bots"] = _maxConcurrentBots;
	status["total_bots"] = _bots.size();
	status["running_bots"] = getRunningBots().size();
	status["enabled_bots"] = getEnabledBots().size();

	QJsonArray botsArray;
	for (auto it = _bots.constBegin(); it != _bots.constEnd(); ++it) {
		BotBase *bot = it.value();
		auto botInfo = bot->info();

		QJsonObject botObj;
		botObj["id"] = botInfo.id;
		botObj["name"] = botInfo.name;
		botObj["version"] = botInfo.version;
		botObj["running"] = bot->isRunning();
		botObj["enabled"] = bot->isEnabled();

		if (_stats.contains(botInfo.id)) {
			BotStats stats = _stats.value(botInfo.id);
			QJsonObject statsObj;
			statsObj["messages_processed"] = static_cast<qint64>(stats.messagesProcessed);
			statsObj["commands_executed"] = static_cast<qint64>(stats.commandsExecuted);
			statsObj["errors"] = static_cast<qint64>(stats.errorsOccurred);
			statsObj["avg_execution_ms"] = stats.avgExecutionTimeMs();
			botObj["stats"] = statsObj;
		}

		botsArray.append(botObj);
	}
	status["bots"] = botsArray;

	return status;
}

void BotManager::dumpBotInfo() const {
	QMutexLocker locker(&_mutex);

	qInfo() << "=== Bot Manager Status ===";
	qInfo() << "Total bots:" << _bots.size();
	qInfo() << "Running bots:" << getRunningBots().size();
	qInfo() << "Enabled bots:" << getEnabledBots().size();
	qInfo() << "";

	for (auto it = _bots.constBegin(); it != _bots.constEnd(); ++it) {
		BotBase *bot = it.value();
		auto botInfo = bot->info();
		BotStats stats = _stats.value(botInfo.id);

		qInfo() << "Bot:" << botInfo.id;
		qInfo() << "  Name:" << botInfo.name << "v" << botInfo.version;
		qInfo() << "  Running:" << bot->isRunning() << "Enabled:" << bot->isEnabled();
		qInfo() << "  Messages:" << stats.messagesProcessed;
		qInfo() << "  Commands:" << stats.commandsExecuted;
		qInfo() << "  Errors:" << stats.errorsOccurred;
		qInfo() << "  Avg time:" << stats.avgExecutionTimeMs() << "ms";
		qInfo() << "";
	}

	qInfo() << "========================";
}

// Private slots

void BotManager::onBotConfigChanged() {
	BotBase *bot = qobject_cast<BotBase*>(sender());
	if (bot) {
		auto botInfo = bot->info();
		qInfo() << "[BotManager] Bot config changed:" << botInfo.id;

		QMutexLocker locker(&_mutex);
		_configs.insert(botInfo.id, bot->config());
	}
}

void BotManager::onBotStateChanged() {
	BotBase *bot = qobject_cast<BotBase*>(sender());
	if (bot) {
		auto botInfo = bot->info();
		qDebug() << "[BotManager] Bot state changed:" << botInfo.id;
	}
}

void BotManager::onBotError(const QString &error) {
	BotBase *bot = qobject_cast<BotBase*>(sender());
	if (bot) {
		auto botInfo = bot->info();
		qCritical() << "[BotManager] Bot error:" << botInfo.id << error;

		QMutexLocker locker(&_mutex);
		if (_stats.contains(botInfo.id)) {
			_stats[botInfo.id].errorsOccurred++;
		}

		Q_EMIT botError(botInfo.id, error);
	}
}

void BotManager::onPerformanceCheckTimer() {
	// Periodic performance check
	QMutexLocker locker(&_mutex);

	for (auto it = _stats.constBegin(); it != _stats.constEnd(); ++it) {
		const QString &botId = it.key();
		const BotStats &stats = it.value();

		// Warn if bot has high error rate
		if (stats.messagesProcessed > 0) {
			double errorRate = static_cast<double>(stats.errorsOccurred) / stats.messagesProcessed;
			if (errorRate > 0.1) { // >10% error rate
				qWarning() << "[BotManager] High error rate for bot:" << botId
					<< "(" << (errorRate * 100) << "%)";
			}
		}

		// Warn if bot is slow
		if (stats.avgExecutionTimeMs() > 1000.0) { // >1 second avg
			qWarning() << "[BotManager] Slow bot detected:" << botId
				<< "avg:" << stats.avgExecutionTimeMs() << "ms";
		}
	}
}

// Internal helpers

bool BotManager::initializeBot(BotBase *bot) {
	if (!bot) {
		return false;
	}

	// Check permissions
	if (!checkPermissions(bot)) {
		auto botInfo = bot->info();
		qCritical() << "[BotManager] Bot missing required permissions:" << botInfo.id;
		return false;
	}

	// Call internal initialization
	bool success = bot->internalInitialize(
		_archiver,
		_analytics,
		_semanticSearch,
		_scheduler,
		_auditLogger,
		_rbac
	);

	if (success) {
		auto botInfo = bot->info();
		BotStats &stats = _stats[botInfo.id];
		stats.botId = botInfo.id;  // Ensure botId is set
		stats.lastActive = QDateTime::currentDateTime();
	}

	return success;
}

void BotManager::shutdownBot(BotBase *bot) {
	if (!bot || !bot->isRunning()) {
		return;
	}

	auto botInfo = bot->info();

	try {
		bot->onShutdown();
	} catch (const std::exception &e) {
		qCritical() << "[BotManager] Bot crashed during shutdown:" << botInfo.id << e.what();
	}
}

void BotManager::updateStats(const QString &botId, qint64 executionTimeMs, bool error) {
	if (!_stats.contains(botId)) {
		return;
	}

	BotStats &stats = _stats[botId];
	stats.messagesProcessed++;
	stats.totalExecutionTimeMs += executionTimeMs;
	stats.lastExecutionTimeMs = executionTimeMs;
	stats.lastActive = QDateTime::currentDateTime();

	if (error) {
		stats.errorsOccurred++;
	}
}

bool BotManager::checkPermissions(BotBase *bot) const {
	if (!bot || !_rbac) {
		return false;
	}

	// TODO: Implement proper permission checking with QString to Permission conversion
	// auto botInfo = bot->info();
	// QStringList requiredPerms = bot->requiredPermissions();

	// for (const QString &perm : requiredPerms) {
	// 	// Need to convert QString to Permission enum
	// 	// and handle PermissionCheckResult return type
	// 	if (!_rbac->checkPermission(botInfo.id, perm)) {
	// 		qWarning() << "[BotManager] Bot missing permission:" << botInfo.id << perm;
	// 		return false;
	// 	}
	// }

	return true;
}

void BotManager::loadPersistedConfigs() {
	// TODO: Load from database
	qInfo() << "[BotManager] Loading persisted configs...";
}

void BotManager::saveAllConfigs() {
	// TODO: Persist to database
	qInfo() << "[BotManager] Saving all bot configs...";
}

QString BotManager::configFilePath() const {
	QString dataPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
	return dataPath + "/bot_configs.json";
}

} // namespace MCP

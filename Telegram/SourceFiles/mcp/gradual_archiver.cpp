// This file is part of Telegram Desktop MCP Server.
// Licensed under GPLv3 with OpenSSL exception.

#include "mcp/gradual_archiver.h"
#include "mcp/chat_archiver.h"
#include "mcp/export_html.h"
#include "mcp/export_markdown.h"
#include "data/data_session.h"
#include "data/data_peer.h"
#include "data/data_document.h"
#include "data/data_photo.h"
#include "data/data_photo_media.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/view/history_view_element.h"

#include <QtCore/QFile>
#include <QtCore/QDir>
#include <QtCore/QJsonDocument>
#include <QtCore/QStandardPaths>

namespace MCP {

GradualArchiver::GradualArchiver(QObject *parent)
	: QObject(parent)
	, _rng(QRandomGenerator::securelySeeded()) {

	_batchTimer = new QTimer(this);
	_batchTimer->setSingleShot(true);
	connect(_batchTimer, &QTimer::timeout, this, &GradualArchiver::processNextBatch);

	_activeHoursTimer = new QTimer(this);
	_activeHoursTimer->setInterval(60000); // Check every minute
	connect(_activeHoursTimer, &QTimer::timeout, this, &GradualArchiver::checkActiveHours);

	_hourlyResetTimer = new QTimer(this);
	_hourlyResetTimer->setInterval(3600000); // 1 hour
	connect(_hourlyResetTimer, &QTimer::timeout, this, &GradualArchiver::resetHourlyCounter);

	_dailyResetTimer = new QTimer(this);
	_dailyResetTimer->setInterval(86400000); // 24 hours
	connect(_dailyResetTimer, &QTimer::timeout, this, &GradualArchiver::resetDailyCounter);

	loadState();
}

GradualArchiver::~GradualArchiver() {
	saveState();
}

bool GradualArchiver::startGradualArchive(
		qint64 chatId,
		const GradualArchiveConfig &config) {

	if (_status.state == GradualArchiveStatus::State::Running) {
		Q_EMIT error("Archive already in progress. Use queue or cancel first.");
		return false;
	}

	if (!_session) {
		Q_EMIT error("Session not available");
		return false;
	}

	_config = config;
	_status = GradualArchiveStatus{};
	_status.chatId = chatId;
	_status.state = GradualArchiveStatus::State::Running;
	_status.startTime = QDateTime::currentDateTime();

	// Clear in-memory message storage
	_collectedMessages = QJsonArray{};

	// Get chat info
	PeerId peerId(chatId);
	auto peer = _session->peer(peerId);
	if (peer) {
		_status.chatTitle = peer->name();
	}

	Q_EMIT operationLog(QString("Starting export of \"%1\"").arg(_status.chatTitle));

	// Get estimated total (from history if available)
	auto history = _session->history(peerId);
	if (history) {
		// Count messages in local blocks as estimate
		int count = 0;
		for (const auto &block : history->blocks) {
			if (block) {
				count += block->messages.size();
			}
		}
		_status.totalMessages = count > 0 ? count : 1000; // Default estimate
	}

	_currentOffsetId = 0;
	_consecutiveBatches = 0;
	_retryCount = 0;

	// Start timers
	_hourlyResetTimer->start();
	_dailyResetTimer->start();

	if (_config.respectActiveHours) {
		_activeHoursTimer->start();
		if (!isWithinActiveHours()) {
			_status.state = GradualArchiveStatus::State::WaitingForActiveHours;
			Q_EMIT stateChanged(_status.state);
			return true;
		}
	}

	// Schedule first batch with initial random delay
	int initialDelay = _rng.bounded(_config.minDelayMs, _config.maxDelayMs);
	_batchTimer->start(initialDelay);
	_status.nextActionTime = QDateTime::currentDateTime().addMSecs(initialDelay);

	Q_EMIT stateChanged(_status.state);
	saveState();

	return true;
}

void GradualArchiver::pause() {
	if (_status.state != GradualArchiveStatus::State::Running) {
		return;
	}

	_batchTimer->stop();
	_status.state = GradualArchiveStatus::State::Paused;
	Q_EMIT stateChanged(_status.state);
	saveState();
}

void GradualArchiver::resume() {
	if (_status.state != GradualArchiveStatus::State::Paused &&
		_status.state != GradualArchiveStatus::State::WaitingForActiveHours) {
		return;
	}

	if (_config.respectActiveHours && !isWithinActiveHours()) {
		_status.state = GradualArchiveStatus::State::WaitingForActiveHours;
		Q_EMIT stateChanged(_status.state);
		return;
	}

	_status.state = GradualArchiveStatus::State::Running;
	scheduleNextBatch();
	Q_EMIT stateChanged(_status.state);
	saveState();
}

void GradualArchiver::cancel() {
	_batchTimer->stop();
	_activeHoursTimer->stop();
	_hourlyResetTimer->stop();
	_dailyResetTimer->stop();

	_status.state = GradualArchiveStatus::State::Idle;
	Q_EMIT stateChanged(_status.state);

	// Clear saved state
	QFile::remove(stateFilePath());
}

bool GradualArchiver::queueChat(qint64 chatId, const GradualArchiveConfig &config) {
	QueuedChat queued;
	queued.chatId = chatId;
	queued.config = config;
	_queue.append(queued);
	saveState();

	// Start if not already running
	if (_status.state == GradualArchiveStatus::State::Idle) {
		processNextInQueue();
	}

	return true;
}

void GradualArchiver::clearQueue() {
	_queue.clear();
	saveState();
}

QJsonArray GradualArchiver::getQueue() const {
	QJsonArray arr;
	for (const auto &q : _queue) {
		QJsonObject obj;
		obj["chat_id"] = q.chatId;
		arr.append(obj);
	}
	return arr;
}

void GradualArchiver::processNextBatch() {
	if (_status.state != GradualArchiveStatus::State::Running) {
		return;
	}

	// Check limits
	if (_status.messagesArchivedThisHour >= _config.maxMessagesPerHour) {
		// Wait until next hour
		int waitMs = 3600000 - (QTime::currentTime().msecsSinceStartOfDay() % 3600000);
		_batchTimer->start(waitMs);
		_status.nextActionTime = QDateTime::currentDateTime().addMSecs(waitMs);
		return;
	}

	if (_status.messagesArchivedToday >= _config.maxMessagesPerDay) {
		// Wait until next day
		_status.state = GradualArchiveStatus::State::Paused;
		Q_EMIT stateChanged(_status.state);
		Q_EMIT error("Daily limit reached. Will resume tomorrow.");
		return;
	}

	// Check active hours
	if (_config.respectActiveHours && !isWithinActiveHours()) {
		_status.state = GradualArchiveStatus::State::WaitingForActiveHours;
		Q_EMIT stateChanged(_status.state);
		return;
	}

	// Calculate batch size with randomization
	int batchSize = calculateBatchSize();

	// Fetch the batch
	bool success = fetchBatch(batchSize, _currentOffsetId);

	if (!success) {
		_retryCount++;
		if (_retryCount >= _config.maxRetries) {
			_status.state = GradualArchiveStatus::State::Failed;
			Q_EMIT stateChanged(_status.state);
			Q_EMIT error("Max retries exceeded");
			return;
		}
		// Retry with longer delay
		int retryDelay = _config.maxDelayMs * (_retryCount + 1);
		_batchTimer->start(retryDelay);
		return;
	}

	_retryCount = 0;
	_consecutiveBatches++;
	_status.batchesCompleted++;
	_status.lastActivityTime = QDateTime::currentDateTime();

	Q_EMIT operationLog(QString("Batch %1: archived %2 messages (%3/%4 total)")
		.arg(_status.batchesCompleted)
		.arg(batchSize)
		.arg(_status.archivedMessages)
		.arg(_status.totalMessages));
	Q_EMIT batchCompleted(batchSize, _status.archivedMessages);
	Q_EMIT progressChanged(_status.archivedMessages, _status.totalMessages);
	Q_EMIT sizeUpdated(_status.totalBytesProcessed, _status.totalMediaBytes);

	// Check if complete
	if (_currentOffsetId == 0 || _status.archivedMessages >= _status.totalMessages) {
		completeArchive();
		return;
	}

	// Schedule next batch
	scheduleNextBatch();
	saveState();
}

bool GradualArchiver::fetchBatch(int limit, qint64 offsetId) {
	if (!_session || !_archiver) {
		return false;
	}

	PeerId peerId(_status.chatId);
	auto history = _session->history(peerId);
	if (!history) {
		_status.lastError = "History not available";
		return false;
	}

	int archived = 0;
	qint64 lastMsgId = offsetId;

	// Iterate through available messages
	for (auto blockIt = history->blocks.begin();
		 blockIt != history->blocks.end() && archived < limit;
		 ++blockIt) {
		const auto &block = *blockIt;
		if (!block) continue;

		for (auto msgIt = block->messages.begin();
			 msgIt != block->messages.end() && archived < limit;
			 ++msgIt) {
			const auto &element = *msgIt;
			if (!element) continue;
			auto item = element->data();
			if (!item) continue;

			// Skip if before offset
			if (offsetId > 0 && item->id.bare >= offsetId) {
				continue;
			}

			// Archive the message
			bool msgArchived = false;
			if (_archiver) {
				msgArchived = _archiver->archiveMessage(item);
			} else {
				// Store in memory instead
				QJsonObject msgObj;
				msgObj["id"] = QString::number(item->id.bare);
				msgObj["date"] = QString::number(item->date());
				msgObj["text"] = item->originalText().text;
				if (const auto from = item->from()) {
					msgObj["from"] = from->name();
					msgObj["from_id"] = QString::number(from->id.value);
				}
				_collectedMessages.append(msgObj);
				msgArchived = true;
			}
			if (msgArchived) {
				archived++;
				_status.archivedMessages++;
				_status.messagesArchivedThisHour++;
				_status.messagesArchivedToday++;
				lastMsgId = item->id.bare;

				// Track text content size
				const auto textSize = item->originalText().text.toUtf8().size();
				_status.totalBytesProcessed += textSize;

				// Track media size if present
				if (item->media()) {
					if (const auto doc = item->media()->document()) {
						_status.totalMediaBytes += doc->size;
					} else if (item->media()->photo()) {
						// Estimate photo size (~500KB typical)
						_status.totalMediaBytes += 512 * 1024;
					}
				}

				// Simulate reading time if configured
				if (_config.simulateReading) {
					int readTime = calculateReadingTime(
						item->originalText().text.length());
					if (readTime > 0) {
						QThread::msleep(readTime);
					}
				}
			} else {
				_status.failedMessages++;
			}
		}
	}

	_currentOffsetId = lastMsgId;
	return archived > 0;
}

void GradualArchiver::scheduleNextBatch() {
	int delay = calculateNextDelay();
	_status.currentDelayMs = delay;
	_status.nextActionTime = QDateTime::currentDateTime().addMSecs(delay);
	_batchTimer->start(delay);
}

int GradualArchiver::calculateNextDelay() {
	int baseDelay = _rng.bounded(_config.minDelayMs, _config.maxDelayMs);

	// Add burst pause
	if (_consecutiveBatches >= _config.batchesBeforePause) {
		_consecutiveBatches = 0;
		baseDelay = _config.burstPauseMs + _rng.bounded(0, 10000);
	}

	// Occasional long pause (natural behavior)
	if (_status.batchesCompleted > 0 &&
		_status.batchesCompleted % _config.batchesBeforeLongPause == 0) {
		baseDelay = _config.longPauseMs + _rng.bounded(0, 60000);
	}

	// Add some jitter (±20%)
	int jitter = baseDelay / 5;
	baseDelay += _rng.bounded(-jitter, jitter);

	return qMax(1000, baseDelay); // Minimum 1 second
}

int GradualArchiver::calculateBatchSize() {
	int size = _rng.bounded(_config.minBatchSize, _config.maxBatchSize + 1);

	// Vary batch size to seem more natural
	if (_rng.bounded(100) < 20) {
		// 20% chance of smaller batch
		size = _config.minBatchSize + _rng.bounded(0, 5);
	} else if (_rng.bounded(100) < 10) {
		// 10% chance of larger batch
		size = _config.maxBatchSize;
	}

	return size;
}

bool GradualArchiver::isWithinActiveHours() const {
	int currentHour = QTime::currentTime().hour();
	return currentHour >= _config.activeHourStart &&
		   currentHour < _config.activeHourEnd;
}

int GradualArchiver::calculateReadingTime(int messageLength) {
	if (messageLength == 0) return 0;

	// Average reading speed: ~200 words per minute
	// Average word length: ~5 characters
	// So ~1000 characters per minute = ~16 chars per second
	int readTimeMs = (messageLength / 16) * 1000;

	// Cap at 5 seconds, minimum 100ms
	return qBound(100, readTimeMs, 5000);
}

void GradualArchiver::handleFloodWait(int seconds) {
	_status.state = GradualArchiveStatus::State::RateLimited;
	_status.floodWaitSeconds = seconds;
	Q_EMIT stateChanged(_status.state);
	Q_EMIT rateLimited(seconds);

	if (_config.stopOnFloodWait) {
		pause();
	} else {
		// Wait and retry
		_batchTimer->start(seconds * 1000 + 5000); // Add 5 sec buffer
		_status.nextActionTime = QDateTime::currentDateTime().addSecs(seconds + 5);
	}

	saveState();
}

void GradualArchiver::checkActiveHours() {
	if (_status.state == GradualArchiveStatus::State::WaitingForActiveHours) {
		if (isWithinActiveHours()) {
			resume();
		}
	} else if (_status.state == GradualArchiveStatus::State::Running) {
		if (!isWithinActiveHours()) {
			_status.state = GradualArchiveStatus::State::WaitingForActiveHours;
			_batchTimer->stop();
			Q_EMIT stateChanged(_status.state);
		}
	}
}

void GradualArchiver::resetHourlyCounter() {
	_status.messagesArchivedThisHour = 0;
}

void GradualArchiver::resetDailyCounter() {
	_status.messagesArchivedToday = 0;

	// Resume if paused due to daily limit
	if (_status.state == GradualArchiveStatus::State::Paused) {
		resume();
	}
}

void GradualArchiver::completeArchive() {
	_batchTimer->stop();
	_status.state = GradualArchiveStatus::State::Completed;

	Q_EMIT operationLog(QString("Archive complete: %1 messages from \"%2\"")
		.arg(_status.archivedMessages)
		.arg(_status.chatTitle));
	Q_EMIT stateChanged(_status.state);
	Q_EMIT archiveCompleted(_status.chatId, _status.archivedMessages);

	if (_config.autoExportOnComplete) {
		Q_EMIT operationLog("Starting file export...");
		startExport();
	}

	// Process next in queue
	processNextInQueue();
}

void GradualArchiver::startExport() {
	if (_config.exportPath.isEmpty()) {
		return;
	}

	QJsonArray messages = _archiver
		? _archiver->getMessages(_status.chatId, -1)
		: _collectedMessages;
	if (messages.isEmpty()) {
		Q_EMIT operationLog("No messages to export");
		return;
	}

	bool exportHtml = _config.exportFormat == "html" ||
					  _config.exportFormat == "both";
	bool exportMd = _config.exportFormat == "markdown" ||
					_config.exportFormat == "both";

	if (exportHtml) {
		HtmlExporter htmlExporter;
		htmlExporter.setDataSession(_session);
		QString htmlPath = _config.exportPath;
		if (!htmlPath.endsWith(".html")) {
			htmlPath += ".html";
		}
		HtmlExportOptions htmlOpts;
		htmlOpts.respectContentRestrictions = false;
		Q_EMIT operationLog(QString("Exporting to HTML: %1").arg(htmlPath));
		if (htmlExporter.exportFromArchive(_status.chatTitle, messages, htmlPath, htmlOpts)) {
			Q_EMIT operationLog("HTML export complete");
			Q_EMIT exportReady(htmlPath);
		} else {
			Q_EMIT operationLog("HTML export failed");
		}
	}

	if (exportMd) {
		MarkdownExporter mdExporter;
		mdExporter.setDataSession(_session);
		QString mdPath = _config.exportPath;
		if (!mdPath.endsWith(".md")) {
			mdPath += ".md";
		}
		MarkdownExportOptions mdOpts;
		Q_EMIT operationLog(QString("Exporting to Markdown: %1").arg(mdPath));
		if (mdExporter.exportFromArchive(_status.chatTitle, messages, mdPath, mdOpts)) {
			Q_EMIT operationLog("Markdown export complete");
			Q_EMIT exportReady(mdPath);
		} else {
			Q_EMIT operationLog("Markdown export failed");
		}
	}
}

void GradualArchiver::processNextInQueue() {
	if (_queue.isEmpty()) {
		_status.state = GradualArchiveStatus::State::Idle;
		Q_EMIT stateChanged(_status.state);
		return;
	}

	QueuedChat next = _queue.takeFirst();
	startGradualArchive(next.chatId, next.config);
}

QJsonObject GradualArchiver::statusJson() const {
	QJsonObject obj;

	QString stateStr;
	switch (_status.state) {
	case GradualArchiveStatus::State::Idle: stateStr = "idle"; break;
	case GradualArchiveStatus::State::Running: stateStr = "running"; break;
	case GradualArchiveStatus::State::Paused: stateStr = "paused"; break;
	case GradualArchiveStatus::State::WaitingForActiveHours:
		stateStr = "waiting_for_active_hours"; break;
	case GradualArchiveStatus::State::RateLimited: stateStr = "rate_limited"; break;
	case GradualArchiveStatus::State::Completed: stateStr = "completed"; break;
	case GradualArchiveStatus::State::Failed: stateStr = "failed"; break;
	}

	obj["state"] = stateStr;
	obj["chat_id"] = _status.chatId;
	obj["chat_title"] = _status.chatTitle;
	obj["total_messages"] = _status.totalMessages;
	obj["archived_messages"] = _status.archivedMessages;
	obj["failed_messages"] = _status.failedMessages;
	obj["batches_completed"] = _status.batchesCompleted;
	obj["messages_today"] = _status.messagesArchivedToday;
	obj["messages_this_hour"] = _status.messagesArchivedThisHour;

	if (_status.startTime.isValid()) {
		obj["start_time"] = _status.startTime.toString(Qt::ISODate);
	}
	if (_status.lastActivityTime.isValid()) {
		obj["last_activity"] = _status.lastActivityTime.toString(Qt::ISODate);
	}
	if (_status.nextActionTime.isValid()) {
		obj["next_action"] = _status.nextActionTime.toString(Qt::ISODate);
		obj["next_action_in_seconds"] =
			QDateTime::currentDateTime().secsTo(_status.nextActionTime);
	}

	obj["current_delay_ms"] = _status.currentDelayMs;
	obj["flood_wait_seconds"] = _status.floodWaitSeconds;
	obj["queue_size"] = _queue.size();

	if (!_status.lastError.isEmpty()) {
		obj["last_error"] = _status.lastError;
	}

	return obj;
}

QJsonObject GradualArchiver::configJson() const {
	QJsonObject obj;
	obj["min_delay_ms"] = _config.minDelayMs;
	obj["max_delay_ms"] = _config.maxDelayMs;
	obj["burst_pause_ms"] = _config.burstPauseMs;
	obj["long_pause_ms"] = _config.longPauseMs;
	obj["min_batch_size"] = _config.minBatchSize;
	obj["max_batch_size"] = _config.maxBatchSize;
	obj["batches_before_pause"] = _config.batchesBeforePause;
	obj["batches_before_long_pause"] = _config.batchesBeforeLongPause;
	obj["randomize_order"] = _config.randomizeOrder;
	obj["simulate_reading"] = _config.simulateReading;
	obj["respect_active_hours"] = _config.respectActiveHours;
	obj["active_hour_start"] = _config.activeHourStart;
	obj["active_hour_end"] = _config.activeHourEnd;
	obj["max_messages_per_day"] = _config.maxMessagesPerDay;
	obj["max_messages_per_hour"] = _config.maxMessagesPerHour;
	obj["stop_on_flood_wait"] = _config.stopOnFloodWait;
	obj["max_retries"] = _config.maxRetries;
	obj["auto_export_on_complete"] = _config.autoExportOnComplete;
	obj["export_format"] = _config.exportFormat;
	obj["export_path"] = _config.exportPath;
	return obj;
}

bool GradualArchiver::loadConfigFromJson(const QJsonObject &json) {
	if (json.contains("min_delay_ms"))
		_config.minDelayMs = json["min_delay_ms"].toInt();
	if (json.contains("max_delay_ms"))
		_config.maxDelayMs = json["max_delay_ms"].toInt();
	if (json.contains("burst_pause_ms"))
		_config.burstPauseMs = json["burst_pause_ms"].toInt();
	if (json.contains("long_pause_ms"))
		_config.longPauseMs = json["long_pause_ms"].toInt();
	if (json.contains("min_batch_size"))
		_config.minBatchSize = json["min_batch_size"].toInt();
	if (json.contains("max_batch_size"))
		_config.maxBatchSize = json["max_batch_size"].toInt();
	if (json.contains("batches_before_pause"))
		_config.batchesBeforePause = json["batches_before_pause"].toInt();
	if (json.contains("batches_before_long_pause"))
		_config.batchesBeforeLongPause = json["batches_before_long_pause"].toInt();
	if (json.contains("randomize_order"))
		_config.randomizeOrder = json["randomize_order"].toBool();
	if (json.contains("simulate_reading"))
		_config.simulateReading = json["simulate_reading"].toBool();
	if (json.contains("respect_active_hours"))
		_config.respectActiveHours = json["respect_active_hours"].toBool();
	if (json.contains("active_hour_start"))
		_config.activeHourStart = json["active_hour_start"].toInt();
	if (json.contains("active_hour_end"))
		_config.activeHourEnd = json["active_hour_end"].toInt();
	if (json.contains("max_messages_per_day"))
		_config.maxMessagesPerDay = json["max_messages_per_day"].toInt();
	if (json.contains("max_messages_per_hour"))
		_config.maxMessagesPerHour = json["max_messages_per_hour"].toInt();
	if (json.contains("stop_on_flood_wait"))
		_config.stopOnFloodWait = json["stop_on_flood_wait"].toBool();
	if (json.contains("max_retries"))
		_config.maxRetries = json["max_retries"].toInt();
	if (json.contains("auto_export_on_complete"))
		_config.autoExportOnComplete = json["auto_export_on_complete"].toBool();
	if (json.contains("export_format"))
		_config.exportFormat = json["export_format"].toString();
	if (json.contains("export_path"))
		_config.exportPath = json["export_path"].toString();
	return true;
}

QString GradualArchiver::stateFilePath() const {
	QString dataDir = QStandardPaths::writableLocation(
		QStandardPaths::AppDataLocation);
	return dataDir + "/mcp_gradual_archive_state.json";
}

void GradualArchiver::saveState() {
	QJsonObject state;
	state["status"] = statusJson();
	state["config"] = configJson();
	state["current_offset_id"] = _currentOffsetId;
	state["consecutive_batches"] = _consecutiveBatches;

	QJsonArray queueArr;
	for (const auto &q : _queue) {
		QJsonObject qObj;
		qObj["chat_id"] = q.chatId;
		// Could save config too if needed
		queueArr.append(qObj);
	}
	state["queue"] = queueArr;

	QFile file(stateFilePath());
	if (file.open(QIODevice::WriteOnly)) {
		file.write(QJsonDocument(state).toJson());
	}
}

void GradualArchiver::loadState() {
	QFile file(stateFilePath());
	if (!file.open(QIODevice::ReadOnly)) {
		return;
	}

	QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
	if (!doc.isObject()) {
		return;
	}

	QJsonObject state = doc.object();

	if (state.contains("config")) {
		loadConfigFromJson(state["config"].toObject());
	}

	if (state.contains("current_offset_id")) {
		_currentOffsetId = state["current_offset_id"].toVariant().toLongLong();
	}
	if (state.contains("consecutive_batches")) {
		_consecutiveBatches = state["consecutive_batches"].toInt();
	}

	// Restore status
	if (state.contains("status")) {
		QJsonObject statusObj = state["status"].toObject();
		_status.chatId = statusObj["chat_id"].toVariant().toLongLong();
		_status.chatTitle = statusObj["chat_title"].toString();
		_status.totalMessages = statusObj["total_messages"].toInt();
		_status.archivedMessages = statusObj["archived_messages"].toInt();
		_status.batchesCompleted = statusObj["batches_completed"].toInt();

		QString stateStr = statusObj["state"].toString();
		if (stateStr == "running" || stateStr == "paused") {
			// Resume as paused
			_status.state = GradualArchiveStatus::State::Paused;
		}
	}

	// Restore queue
	if (state.contains("queue")) {
		QJsonArray queueArr = state["queue"].toArray();
		for (const auto &item : queueArr) {
			QueuedChat q;
			q.chatId = item.toObject()["chat_id"].toVariant().toLongLong();
			q.config = _config; // Use current config
			_queue.append(q);
		}
	}
}

} // namespace MCP

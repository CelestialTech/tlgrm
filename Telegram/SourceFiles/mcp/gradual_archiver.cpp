// This file is part of Telegram Desktop MCP Server.
// Licensed under GPLv3 with OpenSSL exception.

#include "mcp/gradual_archiver.h"
#include "mcp/chat_archiver.h"
#include "mcp/export_html.h"
#include "mcp/export_markdown.h"
#include "data/data_session.h"
#include "data/data_file_origin.h"
#include "data/data_peer.h"
#include "data/data_peer_id.h"
#include "data/data_document.h"
#include "data/data_photo.h"
#include "data/data_photo_media.h"
#include "data/data_document_media.h"
#include "data/data_histories.h"
#include "data/data_user.h"
#include "data/data_chat.h"
#include "data/data_channel.h"
#include "data/data_folder.h"
#include "dialogs/dialogs_main_list.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/view/history_view_element.h"
#include "main/main_session.h"
#include "apiwrap.h"
#include "api/api_common.h"
#include "api/api_sending.h"
#include "storage/file_download.h"
#include "storage/storage_account.h"

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
	// Preserve forward-mode status fields across reset
	const auto savedForwardTarget = _status.forwardTargetGroupId;
	const auto savedTotalDeleted = _status.totalDeletedChats;
	const auto savedProcessedDeleted = _status.processedDeletedChats;
	const auto savedDeletedChatName = _status.currentDeletedChatName;
	_status = GradualArchiveStatus{};
	_status.chatId = chatId;
	_status.state = GradualArchiveStatus::State::Running;
	_status.startTime = QDateTime::currentDateTime();
	if (_config.forwardMode) {
		_status.forwardTargetGroupId = savedForwardTarget;
		_status.totalDeletedChats = savedTotalDeleted;
		_status.processedDeletedChats = savedProcessedDeleted;
		_status.currentDeletedChatName = savedDeletedChatName;
	}

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

	// Use server fetching if main session is available (mimics scrolling)
	// This bypasses export restrictions by using normal message loading API
	if (_mainSession) {
		fetchBatchFromServer(batchSize, _currentOffsetId);
		return; // Async - will callback when done
	}

	// Fallback: Fetch from local cache only (may be incomplete)
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
		if (_config.forwardMode && !_deletedChats.isEmpty()) {
			// All deleted chats archived
			_status.state = GradualArchiveStatus::State::Completed;
			Q_EMIT operationLog(QString("All %1 deleted account chats archived successfully")
				.arg(_deletedChats.size()));
			Q_EMIT stateChanged(_status.state);
			Q_EMIT archiveCompleted(0, _status.archivedMessages);
			return;
		}
		_status.state = GradualArchiveStatus::State::Idle;
		Q_EMIT stateChanged(_status.state);
		return;
	}

	QueuedChat next = _queue.takeFirst();

	// In forward mode, send chat separator before starting next chat
	if (_config.forwardMode && _config.addChatSeparators) {
		_status.processedDeletedChats++;
		// Find the deleted chat info for the separator
		for (const auto &dc : _deletedChats) {
			if (dc.peerId == next.chatId) {
				_status.currentDeletedChatName = dc.name;
				int chatDelay = _rng.bounded(2000, _config.longPauseMs / 2);
				Q_EMIT operationLog(QString("Waiting %1s before next chat...")
					.arg(chatDelay / 1000));
				QTimer::singleShot(chatDelay, this, [this, next, dc]() {
					sendChatSeparator(dc, [this, next]() {
						_lastDateHeaderSent = QDate();
						startGradualArchive(next.chatId, next.config);
					});
				});
				return;
			}
		}
	}

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

	// Deleted account archiving fields
	if (_config.forwardMode) {
		obj["forward_mode"] = true;
		obj["forward_target_group_id"] = _status.forwardTargetGroupId;
		obj["total_deleted_chats"] = _status.totalDeletedChats;
		obj["processed_deleted_chats"] = _status.processedDeletedChats;
		if (!_status.currentDeletedChatName.isEmpty()) {
			obj["current_deleted_chat"] = _status.currentDeletedChatName;
		}
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

	// Forward mode config
	obj["forward_mode"] = _config.forwardMode;
	obj["forward_target_group_id"] = _config.forwardTargetGroupId;
	obj["add_date_headers"] = _config.addDateHeaders;
	obj["date_header_format"] = _config.dateHeaderFormat;
	obj["add_chat_separators"] = _config.addChatSeparators;
	obj["group_title"] = _config.groupTitle;

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

	if (json.contains("forward_mode"))
		_config.forwardMode = json["forward_mode"].toBool();
	if (json.contains("forward_target_group_id"))
		_config.forwardTargetGroupId = json["forward_target_group_id"].toVariant().toLongLong();
	if (json.contains("add_date_headers"))
		_config.addDateHeaders = json["add_date_headers"].toBool();
	if (json.contains("date_header_format"))
		_config.dateHeaderFormat = json["date_header_format"].toString();
	if (json.contains("add_chat_separators"))
		_config.addChatSeparators = json["add_chat_separators"].toBool();
	if (json.contains("group_title"))
		_config.groupTitle = json["group_title"].toString();

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

void GradualArchiver::fetchBatchFromServer(int limit, qint64 offsetId) {
	if (!_mainSession || !_session) {
		_status.lastError = "Session not available for server fetch";
		Q_EMIT error(_status.lastError);
		scheduleNextBatch();
		return;
	}

	PeerId peerId(_status.chatId);
	auto history = _session->history(peerId);
	if (!history) {
		_status.lastError = "History not available";
		Q_EMIT error(_status.lastError);
		scheduleNextBatch();
		return;
	}

	// Forward mode: use direct MTP API (messages.getHistory) to bypass
	// requestHistory's optimization that only populates history->blocks
	// for UI-visible chats. For non-UI chats (deleted accounts etc),
	// requestHistory loads to data layer but blocks stay empty.
	if (_config.forwardMode) {
		auto peer = _session->peer(peerId);
		if (!peer) {
			_status.lastError = "Peer not available";
			Q_EMIT error(_status.lastError);
			scheduleNextBatch();
			return;
		}

		const auto offsetIdInt = int(std::clamp(
			offsetId,
			qint64(0),
			qint64(0x3FFFFFFF)));

		Q_EMIT operationLog(QString("Fetching %1 messages from server (offset: %2)...")
			.arg(limit)
			.arg(offsetIdInt));

		_mainSession->api().request(MTPmessages_GetHistory(
			peer->input(),
			MTP_int(offsetIdInt),  // offset_id
			MTP_int(0),            // offset_date
			MTP_int(0),            // add_offset
			MTP_int(limit),        // limit
			MTP_int(0),            // max_id
			MTP_int(0),            // min_id
			MTP_long(0)            // hash
		)).done([this, limit](const MTPmessages_Messages &result) {
			const QVector<MTPMessage> *messagesList = nullptr;
			bool hasMore = false;

			result.match([&](const MTPDmessages_messages &data) {
				_mainSession->data().processUsers(data.vusers());
				_mainSession->data().processChats(data.vchats());
				messagesList = &data.vmessages().v;
				hasMore = false;
			}, [&](const MTPDmessages_messagesSlice &data) {
				_mainSession->data().processUsers(data.vusers());
				_mainSession->data().processChats(data.vchats());
				messagesList = &data.vmessages().v;
				hasMore = (data.vmessages().v.size() >= limit);
			}, [&](const MTPDmessages_channelMessages &data) {
				_mainSession->data().processUsers(data.vusers());
				_mainSession->data().processChats(data.vchats());
				messagesList = &data.vmessages().v;
				hasMore = (data.vmessages().v.size() >= limit);
			}, [&](const MTPDmessages_messagesNotModified &) {
				hasMore = false;
			});

			if (!messagesList || messagesList->isEmpty()) {
				Q_EMIT operationLog("No more messages from server");
				completeArchive();
				return;
			}

			// Process MTP messages into HistoryItem objects via addNewMessage.
			// This creates proper items in the data layer so forwardMessages
			// can forward them with all media (photos, videos, documents).
			int archived = 0;
			qint64 oldestMsgId = INT64_MAX;
			_forwardItems.clear();

			for (const auto &message : *messagesList) {
				qint64 msgId = 0;
				bool isService = false;
				bool isEmpty = false;

				message.match([&](const MTPDmessage &data) {
					msgId = data.vid().v;
				}, [&](const MTPDmessageService &data) {
					msgId = data.vid().v;
					isService = true;
				}, [&](const MTPDmessageEmpty &data) {
					msgId = data.vid().v;
					isEmpty = true;
				});

				if (msgId < oldestMsgId) {
					oldestMsgId = msgId;
				}

				if (isService || isEmpty) {
					continue;
				}

				// Create HistoryItem from MTP message
				auto *item = _mainSession->data().addNewMessage(
					message,
					MessageFlags(),
					NewMessageType::Existing);

				if (item) {
					_forwardItems.append(item);
					archived++;
					_status.archivedMessages++;
					_status.messagesArchivedThisHour++;
					_status.messagesArchivedToday++;
					_status.totalBytesProcessed += item->originalText().text.toUtf8().size();
					if (item->media()) {
						if (const auto doc = item->media()->document()) {
							_status.totalMediaBytes += doc->size;
						} else if (item->media()->photo()) {
							_status.totalMediaBytes += 512 * 1024;
						}
					}
				}
			}

			_currentOffsetId = oldestMsgId;

			Q_EMIT operationLog(QString("Batch: %1 messages fetched (%2 total), hasMore=%3")
				.arg(archived)
				.arg(_status.archivedMessages)
				.arg(hasMore));

			if (archived > 0) {
				_retryCount = 0;
				_consecutiveBatches++;
				_status.batchesCompleted++;
				_status.lastActivityTime = QDateTime::currentDateTime();

				// Sort by date (oldest first) for chronological forwarding
				std::sort(_forwardItems.begin(), _forwardItems.end(),
					[](HistoryItem *a, HistoryItem *b) {
						return a->date() < b->date();
					});

				_forwardIndex = 0;
				Q_EMIT operationLog(QString("Forwarding %1 messages to archive group")
					.arg(_forwardItems.size()));
				forwardNextItem();
			} else if (!hasMore) {
				completeArchive();
			} else {
				_retryCount++;
				if (_retryCount >= _config.maxRetries) {
					completeArchive();
				} else {
					int retryDelay = _config.maxDelayMs * (_retryCount + 1);
					_batchTimer->start(retryDelay);
				}
			}
		}).fail([this](const MTP::Error &error) {
			if (error.type().startsWith("FLOOD_WAIT_")) {
				int waitSecs = error.type().mid(11).toInt();
				if (waitSecs < 1) waitSecs = 5;
				Q_EMIT operationLog(QString("FLOOD_WAIT: retrying in %1s").arg(waitSecs));
				handleFloodWait(waitSecs);
			} else {
				_status.lastError = "Fetch failed: " + error.type();
				Q_EMIT this->error(_status.lastError);
				_retryCount++;
				if (_retryCount >= _config.maxRetries) {
					completeArchive();
				} else {
					scheduleNextBatch();
				}
			}
		}).send();
		return;
	}

	// Non-forward mode: use requestHistory which populates history->blocks
	// (works for chats that have been opened in the UI)
	Q_EMIT operationLog(QString("Fetching %1 messages from server (offset: %2)...")
		.arg(limit)
		.arg(offsetId));

	_mainSession->api().requestHistory(
		history,
		MsgId(offsetId),
		Data::LoadDirection::Before);

	QTimer::singleShot(1500, this, [this, limit, offsetId]() {
		PeerId peerId(_status.chatId);
		auto history = _session->history(peerId);
		if (!history) {
			_status.lastError = "History became unavailable";
			scheduleNextBatch();
			return;
		}

		int archived = 0;
		qint64 lastMsgId = offsetId;
		qint64 oldestMsgId = offsetId > 0 ? offsetId : INT64_MAX;

		for (auto blockIt = history->blocks.rbegin();
			 blockIt != history->blocks.rend() && archived < limit;
			 ++blockIt) {
			const auto &block = *blockIt;
			if (!block) continue;

			for (auto msgIt = block->messages.rbegin();
				 msgIt != block->messages.rend() && archived < limit;
				 ++msgIt) {
				const auto &element = *msgIt;
				if (!element) continue;
				auto item = element->data();
				if (!item) continue;

				if (offsetId > 0 && item->id.bare >= offsetId) {
					continue;
				}

				if (item->id.bare < oldestMsgId) {
					oldestMsgId = item->id.bare;
				}

				bool msgArchived = false;
				if (_archiver) {
					msgArchived = _archiver->archiveMessage(item);
				} else {
					QJsonObject msgObj;
					msgObj["id"] = QString::number(item->id.bare);
					msgObj["date"] = QString::number(item->date());
					msgObj["text"] = item->originalText().text;
					if (const auto from = item->from()) {
						msgObj["from"] = from->name();
						msgObj["from_id"] = QString::number(from->id.value);
					}
					if (item->media()) {
						if (const auto doc = item->media()->document()) {
							msgObj["has_document"] = true;
							msgObj["document_name"] = doc->filename();
							msgObj["document_size"] = QString::number(doc->size);
							msgObj["document_mime"] = doc->mimeString();
							downloadMedia(item);
						}
						if (item->media()->photo()) {
							msgObj["has_photo"] = true;
							downloadMedia(item);
						}
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
					const auto textSize = item->originalText().text.toUtf8().size();
					_status.totalBytesProcessed += textSize;
					if (item->media()) {
						if (const auto doc = item->media()->document()) {
							_status.totalMediaBytes += doc->size;
						} else if (item->media()->photo()) {
							_status.totalMediaBytes += 512 * 1024;
						}
					}
					if (_config.simulateReading) {
						int readTime = calculateReadingTime(
							item->originalText().text.length());
						if (readTime > 50) {
							QThread::msleep(qMin(readTime / 2, 500));
						}
					}
				} else {
					_status.failedMessages++;
				}
			}
		}

		_currentOffsetId = oldestMsgId;

		if (archived > 0) {
			_retryCount = 0;
			_consecutiveBatches++;
			_status.batchesCompleted++;
			_status.lastActivityTime = QDateTime::currentDateTime();

			Q_EMIT operationLog(QString("Server batch %1: archived %2 messages (%3/%4 total)")
				.arg(_status.batchesCompleted)
				.arg(archived)
				.arg(_status.archivedMessages)
				.arg(_status.totalMessages));
			Q_EMIT batchCompleted(archived, _status.archivedMessages);
			Q_EMIT progressChanged(_status.archivedMessages, _status.totalMessages);
			Q_EMIT sizeUpdated(_status.totalBytesProcessed, _status.totalMediaBytes);

			if (oldestMsgId <= 1 || archived < limit / 2) {
				completeArchive();
				return;
			}

			scheduleNextBatch();
			saveState();
		} else {
			_retryCount++;
			if (_retryCount >= _config.maxRetries) {
				completeArchive();
			} else {
				int retryDelay = _config.maxDelayMs * (_retryCount + 1);
				_batchTimer->start(retryDelay);
			}
		}
	});
}

void GradualArchiver::processServerMessages(const QVector<HistoryItem*> &messages, int limit) {
	// This is called when we have a batch of messages from the server
	// Process them in natural order (oldest to newest or randomized)

	int archived = 0;
	qint64 oldestMsgId = _currentOffsetId > 0 ? _currentOffsetId : INT64_MAX;

	for (auto item : messages) {
		if (!item) continue;
		if (archived >= limit) break;

		// Track oldest message
		if (item->id.bare < oldestMsgId) {
			oldestMsgId = item->id.bare;
		}

		// Archive the message
		bool msgArchived = false;
		if (_archiver) {
			msgArchived = _archiver->archiveMessage(item);
		} else {
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

			// Track content size
			_status.totalBytesProcessed += item->originalText().text.toUtf8().size();

			// Download media if present
			if (item->media()) {
				downloadMedia(item);
			}
		}
	}

	_currentOffsetId = oldestMsgId;
}

void GradualArchiver::downloadMedia(not_null<HistoryItem*> item) {
	// Download media without restrictions by using normal media loading
	// This bypasses any export restrictions because it's the same as viewing the media

	if (!item->media()) return;

	// For documents (files, videos, voice messages, etc.)
	if (const auto doc = item->media()->document()) {
		// Create or get the document media holder
		auto docMedia = doc->createMediaView();
		if (docMedia) {
			// Request automatic download - this uses normal loading path
			// which bypasses export restrictions
			doc->save(
				Data::FileOrigin(FullMsgId(item->history()->peer->id, item->id)),
				QString()); // Empty path = use default cache location

			Q_EMIT operationLog(QString("Downloading: %1 (%2 bytes)")
				.arg(doc->filename())
				.arg(doc->size));
		}
	}

	// For photos
	if (const auto photo = item->media()->photo()) {
		auto photoMedia = photo->createMediaView();
		if (photoMedia) {
			// Load the largest available size
			photoMedia->wanted(Data::PhotoSize::Large,
				Data::FileOrigin(FullMsgId(item->history()->peer->id, item->id)));

			Q_EMIT operationLog("Downloading photo...");
		}
	}
}

QVector<GradualArchiver::DeletedAccountChat> GradualArchiver::scanDeletedAccounts() {
	QVector<DeletedAccountChat> result;
	if (!_session) return result;

	auto scanList = [&](not_null<Dialogs::MainList*> list) {
		for (const auto &row : *list->indexed()) {
			if (!row || !row->thread()) continue;
			auto peer = row->thread()->peer();
			if (!peer || !peer->isUser()) continue;
			if (!peer->asUser()->isInaccessible()) continue;

			DeletedAccountChat chat;
			chat.peerId = peer->id.value;
			chat.name = peer->name();

			if (auto history = _session->historyLoaded(peer->id)) {
				TimeId first = INT32_MAX, last = 0;
				for (const auto &block : history->blocks) {
					if (!block) continue;
					for (const auto &elem : block->messages) {
						auto item = elem->data();
						if (!item) continue;
						chat.messageCount++;
						if (item->date() < first) first = item->date();
						if (item->date() > last) last = item->date();
					}
				}
				chat.firstMessageDate = (first == INT32_MAX) ? 0 : first;
				chat.lastMessageDate = last;
			}

			result.append(chat);
		}
	};

	scanList(_session->chatsList());
	if (auto folder = _session->folderLoaded(1)) {
		scanList(folder->chatsList());
	}

	return result;
}

void GradualArchiver::createArchiveGroup(const QString &title, Fn<void(qint64)> done) {
	if (!_mainSession) {
		Q_EMIT error("Main session not available for group creation");
		return;
	}

	Q_EMIT operationLog(QString("Creating archive group: %1").arg(title));

	_mainSession->api().request(MTPchannels_CreateChannel(
		MTP_flags(MTPchannels_CreateChannel::Flag::f_megagroup),
		MTP_string(title),
		MTP_string("Archive of deleted account conversations"),
		MTPInputGeoPoint(), // geo_point
		MTPstring(), // address
		MTP_int(0) // ttl_period
	)).done([this, done](const MTPUpdates &result) {
		_mainSession->api().applyUpdates(result);

		// Extract channel from updates (same pattern as add_contact_box.cpp)
		ChannelData *channel = nullptr;
		const QVector<MTPChat> *chats = nullptr;
		result.match([&](const MTPDupdates &data) {
			chats = &data.vchats().v;
		}, [&](const MTPDupdatesCombined &data) {
			chats = &data.vchats().v;
		}, [](const auto &) {});

		if (chats && !chats->isEmpty() && chats->front().type() == mtpc_channel) {
			channel = _mainSession->data().channel(
				chats->front().c_channel().vid());
		}

		if (channel) {
			qint64 peerId = channel->id.value;
			Q_EMIT operationLog(QString("Archive group created (peer ID: %1)").arg(peerId));
			if (done) done(peerId);
		} else {
			Q_EMIT error("Failed to extract group ID from creation response");
		}
	}).fail([this](const MTP::Error &error) {
		_status.lastError = "Group creation failed: " + error.type();
		_status.state = GradualArchiveStatus::State::Failed;
		Q_EMIT stateChanged(_status.state);
		Q_EMIT this->error(_status.lastError);
	}).send();
}

bool GradualArchiver::startDeletedAccountArchive(const GradualArchiveConfig &config) {
	if (_status.state == GradualArchiveStatus::State::Running) {
		Q_EMIT error("Archive already in progress. Cancel first.");
		return false;
	}

	if (!_session || !_mainSession) {
		Q_EMIT error("Session not available");
		return false;
	}

	auto localConfig = config;
	localConfig.forwardMode = true;

	_deletedChats = scanDeletedAccounts();

	// Filter to specific peer IDs if requested
	if (!localConfig.specificPeerIds.isEmpty()) {
		QSet<qint64> wanted(localConfig.specificPeerIds.begin(),
			localConfig.specificPeerIds.end());
		QVector<DeletedAccountChat> filtered;
		for (const auto &chat : _deletedChats) {
			if (wanted.contains(chat.peerId)) {
				filtered.append(chat);
			}
		}
		_deletedChats = filtered;
	}

	if (_deletedChats.isEmpty()) {
		Q_EMIT error("No matching deleted account chats found");
		return false;
	}

	_currentDeletedChatIndex = 0;
	_lastDateHeaderSent = QDate();
	_forwardItems.clear();
	_forwardIndex = 0;

	Q_EMIT operationLog(QString("Found %1 deleted account chats to archive")
		.arg(_deletedChats.size()));

	// Create archive group if no target specified
	if (localConfig.forwardTargetGroupId == 0) {
		Q_EMIT operationLog("Creating archive group...");
		createArchiveGroup(
			localConfig.groupTitle.isEmpty()
				? QString("Deleted Accounts Archive")
				: localConfig.groupTitle,
			[this, localConfig](qint64 groupId) mutable {
				localConfig.forwardTargetGroupId = groupId;
				_config = localConfig;
				_status.forwardTargetGroupId = groupId;
				_status.totalDeletedChats = _deletedChats.size();
				_status.processedDeletedChats = 0;
				startNextDeletedChat();
			});
		return true;
	}

	// Target group already specified
	_config = localConfig;
	_status.forwardTargetGroupId = localConfig.forwardTargetGroupId;
	_status.totalDeletedChats = _deletedChats.size();
	_status.processedDeletedChats = 0;
	startNextDeletedChat();
	return true;
}

void GradualArchiver::startNextDeletedChat() {
	if (_currentDeletedChatIndex >= _deletedChats.size()) {
		// All chats processed
		_status.state = GradualArchiveStatus::State::Completed;
		Q_EMIT operationLog(QString("All %1 deleted account chats archived")
			.arg(_deletedChats.size()));
		Q_EMIT stateChanged(_status.state);
		Q_EMIT archiveCompleted(0, _status.archivedMessages);
		return;
	}

	const auto &chat = _deletedChats[_currentDeletedChatIndex];
	_status.currentDeletedChatName = chat.name;
	_lastDateHeaderSent = QDate();

	Q_EMIT operationLog(QString("Starting chat %1/%2: %3 (ID: %4)")
		.arg(_currentDeletedChatIndex + 1)
		.arg(_deletedChats.size())
		.arg(chat.name)
		.arg(chat.peerId));

	// Send chat separator first
	if (_config.addChatSeparators) {
		sendChatSeparator(chat, [this, chat]() {
			// Then start the gradual archive for this chat
			// Use the existing startGradualArchive which handles everything
			startGradualArchive(chat.peerId, _config);
		});
	} else {
		startGradualArchive(chat.peerId, _config);
	}
}

void GradualArchiver::sendTextToGroup(qint64 groupPeerId, const QString &text, Fn<void()> done) {
	if (!_mainSession) {
		if (done) done();
		return;
	}

	PeerId peerId(groupPeerId);
	auto history = _mainSession->data().history(peerId);
	if (!history) {
		Q_EMIT operationLog("Warning: target group history not available");
		if (done) done();
		return;
	}

	auto action = Api::SendAction(history);
	action.clearDraft = false;
	auto message = Api::MessageToSend(action);
	message.textWithTags.text = text;

	_mainSession->api().sendMessage(std::move(message));

	// Use human-like delay after sending
	int delay = calculateReadingTime(text.length());
	QTimer::singleShot(qMax(delay, 500), this, [done]() {
		if (done) done();
	});
}

void GradualArchiver::sendDateHeader(const QDate &date, Fn<void()> done) {
	if (_config.forwardTargetGroupId == 0) {
		if (done) done();
		return;
	}

	// Use YYYYMMDD format (no hyphens) for valid Telegram hashtag
	QString header = _config.dateHeaderFormat.arg(date.toString("yyyyMMdd"));
	_lastDateHeaderSent = date;

	Q_EMIT operationLog(QString("Sending date tag: %1").arg(header));
	sendTextToGroup(_config.forwardTargetGroupId, header, done);
}

void GradualArchiver::sendChatSeparator(const DeletedAccountChat &chat, Fn<void()> done) {
	if (_config.forwardTargetGroupId == 0) {
		if (done) done();
		return;
	}

	QString dateRange;
	if (chat.firstMessageDate > 0 && chat.lastMessageDate > 0) {
		QString first = QDateTime::fromSecsSinceEpoch(chat.firstMessageDate).date().toString("yyyy-MM-dd");
		QString last = QDateTime::fromSecsSinceEpoch(chat.lastMessageDate).date().toString("yyyy-MM-dd");
		dateRange = QString("%1 to %2").arg(first, last);
	}

	QString separator = QString::fromUtf8(
		"\xe2\x94\x81\xe2\x94\x81\xe2\x94\x81\xe2\x94\x81\xe2\x94\x81"
		"\xe2\x94\x81\xe2\x94\x81\xe2\x94\x81\xe2\x94\x81\xe2\x94\x81"
		"\xe2\x94\x81\xe2\x94\x81\xe2\x94\x81\xe2\x94\x81\xe2\x94\x81"
		"\xe2\x94\x81\xe2\x94\x81\xe2\x94\x81\xe2\x94\x81\xe2\x94\x81"
		"\xe2\x94\x81\xe2\x94\x81\xe2\x94\x81\xe2\x94\x81\xe2\x94\x81"
		"\xe2\x94\x81\xe2\x94\x81\xe2\x94\x81");

	QString text = separator + "\n"
		+ QString("Chat: %1 (ID: %2)").arg(chat.name).arg(chat.peerId);
	if (!dateRange.isEmpty()) {
		text += "\n" + QString("Messages: %1 | %2").arg(chat.messageCount).arg(dateRange);
	}
	text += "\n" + separator;

	Q_EMIT operationLog(QString("Sending chat separator for: %1").arg(chat.name));
	sendTextToGroup(_config.forwardTargetGroupId, text, done);
}

void GradualArchiver::forwardCollectedBatch() {
	// Legacy path — forwarding is now initiated directly from fetchBatchFromServer
	// after sorting _forwardItems and calling forwardNextItem.
	// This method is kept for the non-forward-mode path.
	if (!_forwardItems.isEmpty()) {
		_forwardIndex = 0;
		forwardNextItem();
		return;
	}
	scheduleNextBatch();
}

void GradualArchiver::forwardNextItem() {
	if (_forwardIndex >= _forwardItems.size()) {
		_forwardItems.clear();
		scheduleNextBatch();
		return;
	}

	doForwardItem();
}

void GradualArchiver::doForwardItem() {
	if (_forwardIndex >= _forwardItems.size()) {
		_forwardItems.clear();
		scheduleNextBatch();
		return;
	}

	auto item = _forwardItems[_forwardIndex];

	PeerId targetPeerId(_config.forwardTargetGroupId);
	auto targetHistory = _mainSession->data().history(targetPeerId);
	if (!targetHistory) {
		Q_EMIT operationLog("Target history not available");
		_forwardItems.clear();
		scheduleNextBatch();
		return;
	}

	// Build metadata header: sender, timestamp, date hashtag
	QDateTime msgTime = QDateTime::fromSecsSinceEpoch(item->date());
	QString dateTag;
	if (_config.addDateHeaders) {
		dateTag = _config.dateHeaderFormat.arg(msgTime.date().toString("yyyyMMdd"));
	}

	// Sender identification
	QString senderName;
	if (const auto from = item->from()) {
		senderName = from->name();
	}

	// Build header: #dYYYYMMDD | Sender | HH:MM
	QString header;
	if (!dateTag.isEmpty()) {
		header = dateTag;
	}
	if (!senderName.isEmpty()) {
		header += (header.isEmpty() ? "" : " | ") + senderName;
	}
	header += (header.isEmpty() ? "" : " | ") + msgTime.toString("HH:mm");

	// Combine header + original text
	QString originalText = item->originalText().text;
	QString fullText = originalText.isEmpty()
		? header
		: (header + "\n" + originalText);

	auto action = Api::SendAction(targetHistory);
	action.clearDraft = false;
	auto message = Api::MessageToSend(action);
	message.textWithTags.text = fullText;

	// Send with media if present, otherwise plain text
	bool sentMedia = false;
	if (item->media()) {
		if (const auto doc = item->media()->document()) {
			Api::SendExistingDocument(std::move(message), doc);
			sentMedia = true;
		} else if (const auto photo = item->media()->photo()) {
			Api::SendExistingPhoto(std::move(message), photo);
			sentMedia = true;
		}
	}

	if (!sentMedia) {
		_mainSession->api().sendMessage(std::move(message));
	}

	_forwardIndex++;
	Q_EMIT progressChanged(_status.archivedMessages, _status.totalMessages);

	// Human-like delay
	int delay = 300;
	if (_config.simulateReading) {
		delay = qMax(calculateReadingTime(originalText.length()), 300);
		delay = qMin(delay, 2000);
	}
	delay += _rng.bounded(0, delay / 3);

	QTimer::singleShot(delay, this, [this]() {
		forwardNextItem();
	});
}

} // namespace MCP

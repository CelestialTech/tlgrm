// This file is part of Telegram Desktop MCP Server.
// Licensed under GPLv3 with OpenSSL exception.

#include "batch_operations.h"
#include "data/data_session.h"
#include "data/data_histories.h"
#include "data/data_chat_filters.h"
#include "history/history.h"
#include "history/history_item.h"
#include "main/main_session.h"
#include "apiwrap.h"
#include "data/data_peer.h"
#include "api/api_sending.h"

#include <QtCore/QFile>
#include <QtCore/QTextStream>
#include <QtCore/QJsonDocument>
#include <QtCore/QTimer>
#include <QtCore/QThread>

namespace MCP {

BatchOperations::BatchOperations(QObject *parent)
: QObject(parent) {
}

BatchOperations::~BatchOperations() {
	stop();
}

bool BatchOperations::start(Main::Session *session) {
	if (_isRunning) {
		return true;
	}

	if (!session) {
		return false;
	}

	_session = session;

	// Set up queue processing timer
	_queueTimer = new QTimer(this);
	connect(_queueTimer, &QTimer::timeout, this, &BatchOperations::processOperationQueue);
	_queueTimer->start(_queueProcessIntervalMs);

	_isRunning = true;

	return true;
}

void BatchOperations::stop() {
	if (!_isRunning) {
		return;
	}

	if (_queueTimer) {
		_queueTimer->stop();
		delete _queueTimer;
		_queueTimer = nullptr;
	}

	_session = nullptr;
	_isRunning = false;
}

qint64 BatchOperations::batchDeleteMessages(const BatchDeleteParams &params) {
	if (!_isRunning) {
		return 0;
	}

	qint64 operationId = createOperation(BatchOperationType::Delete);
	
	auto &result = _operations[operationId];
	result.totalItems = params.messageIds.size();

	Q_EMIT operationStarted(operationId, "delete");

	// Execute operation
	executeDeleteOperation(operationId, params);

	return operationId;
}

qint64 BatchOperations::deleteMessageRange(
	qint64 chatId,
	qint64 startMessageId,
	qint64 endMessageId,
	bool deleteForAll
) {
	// Collect message IDs in range
	QVector<qint64> messageIds;
	for (qint64 id = startMessageId; id <= endMessageId; ++id) {
		messageIds.append(id);
	}

	BatchDeleteParams params;
	params.chatId = chatId;
	params.messageIds = messageIds;
	params.deleteForAll = deleteForAll;

	return batchDeleteMessages(params);
}

qint64 BatchOperations::deleteMessagesByFilter(
	qint64 chatId,
	const QString &filter,
	const QJsonObject &filterParams
) {
	QVector<qint64> messageIds;

	if (filter == "date") {
		QDateTime startDate = QDateTime::fromString(filterParams["startDate"].toString(), Qt::ISODate);
		QDateTime endDate = QDateTime::fromString(filterParams["endDate"].toString(), Qt::ISODate);
		messageIds = filterMessagesByDate(chatId, startDate, endDate);
	} else if (filter == "user") {
		qint64 userId = filterParams["userId"].toString().toLongLong();
		messageIds = filterMessagesByUser(chatId, userId);
	} else if (filter == "type") {
		QString messageType = filterParams["messageType"].toString();
		messageIds = filterMessagesByType(chatId, messageType);
	}

	BatchDeleteParams params;
	params.chatId = chatId;
	params.messageIds = messageIds;
	params.deleteForAll = filterParams["deleteForAll"].toBool(false);

	return batchDeleteMessages(params);
}

qint64 BatchOperations::batchForwardMessages(const BatchForwardParams &params) {
	if (!_isRunning) {
		return 0;
	}

	qint64 operationId = createOperation(BatchOperationType::Forward);
	
	auto &result = _operations[operationId];
	result.totalItems = params.messageIds.size();

	Q_EMIT operationStarted(operationId, "forward");

	// Execute operation
	executeForwardOperation(operationId, params);

	return operationId;
}

qint64 BatchOperations::forwardAllMessages(
	qint64 sourceChatId,
	qint64 targetChatId,
	int limit
) {
	if (!_session) return 0;

	// Collect message IDs from loaded history
	QVector<qint64> messageIds;
	auto &owner = _session->data();
	const auto peerId = PeerId(sourceChatId);
	const auto history = owner.historyLoaded(peerId);
	if (history) {
		int collected = 0;
		for (auto blockIt = history->blocks.rbegin();
		     blockIt != history->blocks.rend() && collected < limit;
		     ++blockIt) {
			const auto &block = *blockIt;
			if (!block) continue;
			for (auto msgIt = block->messages.rbegin();
			     msgIt != block->messages.rend() && collected < limit;
			     ++msgIt) {
				const auto &element = *msgIt;
				if (!element) continue;
				auto item = element->data();
				if (!item) continue;
				messageIds.append(item->id.bare);
				collected++;
			}
		}
	}

	BatchForwardParams params;
	params.sourceChatId = sourceChatId;
	params.targetChatId = targetChatId;
	params.messageIds = messageIds;

	return batchForwardMessages(params);
}

qint64 BatchOperations::batchExportMessages(const BatchExportParams &params) {
	if (!_isRunning) {
		return 0;
	}

	qint64 operationId = createOperation(BatchOperationType::Export);
	
	auto &result = _operations[operationId];
	result.totalItems = params.messageIds.size();

	Q_EMIT operationStarted(operationId, "export");

	// Execute operation
	executeExportOperation(operationId, params);

	return operationId;
}

qint64 BatchOperations::exportChatMessages(
	qint64 chatId,
	const QString &format,
	const QString &outputPath,
	int limit
) {
	if (!_session) return 0;

	// Collect message IDs from loaded history
	QVector<qint64> messageIds;
	auto &owner = _session->data();
	const auto peerId = PeerId(chatId);
	const auto history = owner.historyLoaded(peerId);
	if (history) {
		int collected = 0;
		const int maxCollect = (limit < 0) ? INT_MAX : limit;
		for (auto blockIt = history->blocks.rbegin();
		     blockIt != history->blocks.rend() && collected < maxCollect;
		     ++blockIt) {
			const auto &block = *blockIt;
			if (!block) continue;
			for (auto msgIt = block->messages.rbegin();
			     msgIt != block->messages.rend() && collected < maxCollect;
			     ++msgIt) {
				const auto &element = *msgIt;
				if (!element) continue;
				auto item = element->data();
				if (!item) continue;
				messageIds.append(item->id.bare);
				collected++;
			}
		}
	}

	BatchExportParams params;
	params.chatId = chatId;
	params.format = format;
	params.outputPath = outputPath;
	params.messageIds = messageIds;

	return batchExportMessages(params);
}

qint64 BatchOperations::batchMarkAsRead(const BatchMarkReadParams &params) {
	if (!_isRunning) {
		return 0;
	}

	qint64 operationId = createOperation(BatchOperationType::MarkAsRead);
	
	auto &result = _operations[operationId];
	result.totalItems = params.chatIds.size();

	Q_EMIT operationStarted(operationId, "mark_read");

	// Execute operation
	executeMarkReadOperation(operationId, params);

	return operationId;
}

qint64 BatchOperations::markAllChatsRead() {
	if (!_session) return 0;

	// Collect all chat IDs from the main chat list
	QVector<qint64> chatIds;
	auto chatsList = _session->data().chatsList();
	if (chatsList) {
		auto indexed = chatsList->indexed();
		if (indexed) {
			for (const auto &row : *indexed) {
				if (!row) continue;
				auto thread = row->thread();
				if (!thread) continue;
				auto peer = thread->peer();
				if (!peer) continue;
				chatIds.append(peer->id.value);
			}
		}
	}

	BatchMarkReadParams params;
	params.chatIds = chatIds;

	return batchMarkAsRead(params);
}

bool BatchOperations::cancelOperation(qint64 operationId) {
	if (!_operations.contains(operationId)) {
		return false;
	}

	auto &result = _operations[operationId];
	if (result.status != BatchStatus::Running && result.status != BatchStatus::Pending) {
		return false;
	}

	result.status = BatchStatus::Cancelled;
	result.endTime = QDateTime::currentDateTime();

	Q_EMIT operationCancelled(operationId);

	return true;
}

bool BatchOperations::pauseOperation(qint64 operationId) {
	if (!_operations.contains(operationId)) {
		return false;
	}

	auto &result = _operations[operationId];
	if (result.status != BatchStatus::Running) {
		return false;
	}

	result.status = BatchStatus::Pending;  // Revert to pending (paused)
	_currentConcurrentOperations--;
	return true;
}

bool BatchOperations::resumeOperation(qint64 operationId) {
	if (!_operations.contains(operationId)) {
		return false;
	}

	auto &result = _operations[operationId];
	if (result.status != BatchStatus::Pending) {
		return false;
	}

	result.status = BatchStatus::Running;
	_currentConcurrentOperations++;
	return true;
}

QJsonObject BatchOperations::getOperationStatus(qint64 operationId) {
	if (!_operations.contains(operationId)) {
		QJsonObject error;
		error["error"] = "Operation not found";
		return error;
	}

	return operationResultToJson(_operations[operationId]);
}

QJsonArray BatchOperations::listOperations(BatchStatus status) {
	QJsonArray result;

	for (const auto &op : _operations) {
		if (op.status == status) {
			result.append(operationResultToJson(op));
		}
	}

	return result;
}

QJsonArray BatchOperations::getRecentOperations(int limit) {
	// Collect all operations and sort by start time
	QVector<BatchOperationResult> operations;
	for (const auto &op : _operations) {
		operations.append(op);
	}

	std::sort(operations.begin(), operations.end(), 
		[](const BatchOperationResult &a, const BatchOperationResult &b) {
			return a.startTime > b.startTime;
		});

	QJsonArray result;
	for (int i = 0; i < qMin(limit, operations.size()); ++i) {
		result.append(operationResultToJson(operations[i]));
	}

	return result;
}

QJsonObject BatchOperations::getOperationStatistics() {
	QJsonObject stats;

	int total = _operations.size();
	int pending = 0, running = 0, completed = 0, failed = 0, cancelled = 0;

	for (const auto &op : _operations) {
		switch (op.status) {
			case BatchStatus::Pending: pending++; break;
			case BatchStatus::Running: running++; break;
			case BatchStatus::Completed: completed++; break;
			case BatchStatus::Failed: failed++; break;
			case BatchStatus::Cancelled: cancelled++; break;
		}
	}

	stats["total"] = total;
	stats["pending"] = pending;
	stats["running"] = running;
	stats["completed"] = completed;
	stats["failed"] = failed;
	stats["cancelled"] = cancelled;

	return stats;
}

// Private slots

void BatchOperations::processOperationQueue() {
	// Process pending operations up to max concurrent limit
	for (auto &op : _operations) {
		if (_currentConcurrentOperations >= _maxConcurrentOperations) {
			break;
		}

		if (op.status == BatchStatus::Pending) {
			// Start processing this operation
			op.status = BatchStatus::Running;
			op.startTime = QDateTime::currentDateTime();
			_currentConcurrentOperations++;

			// Actual processing is handled by execute* methods
		}
	}
}

// Private methods - Operation execution

void BatchOperations::executeDeleteOperation(qint64 operationId, const BatchDeleteParams &params) {
	auto &result = _operations[operationId];

	int successful = 0;
	int failed = 0;

	for (qint64 messageId : params.messageIds) {
		if (result.status == BatchStatus::Cancelled) {
			break;
		}

		bool success = deleteMessage(params.chatId, messageId, params.deleteForAll);
		if (success) {
			successful++;
		} else {
			failed++;
		}

		result.processedItems++;
		Q_EMIT operationProgress(operationId, result.processedItems, result.totalItems);

		// Rate limiting
		QThread::msleep(1000 / _operationsPerSecond);
	}

	completeOperation(operationId, failed == 0, failed > 0 ? "Some deletions failed" : QString());
}

void BatchOperations::executeForwardOperation(qint64 operationId, const BatchForwardParams &params) {
	auto &result = _operations[operationId];

	int successful = 0;
	int failed = 0;

	for (qint64 messageId : params.messageIds) {
		if (result.status == BatchStatus::Cancelled) {
			break;
		}

		bool success = forwardMessage(params.sourceChatId, params.targetChatId, messageId, params);
		if (success) {
			successful++;
		} else {
			failed++;
		}

		result.processedItems++;
		Q_EMIT operationProgress(operationId, result.processedItems, result.totalItems);

		QThread::msleep(1000 / _operationsPerSecond);
	}

	completeOperation(operationId, failed == 0, failed > 0 ? "Some forwards failed" : QString());
}

void BatchOperations::executeExportOperation(qint64 operationId, const BatchExportParams &params) {
	auto &result = _operations[operationId];

	QFile file(params.outputPath);
	if (!file.open(QIODevice::WriteOnly)) {
		completeOperation(operationId, false, "Failed to open output file");
		return;
	}

	QTextStream stream(&file);

	int successful = 0;
	int failed = 0;

	for (qint64 messageId : params.messageIds) {
		if (result.status == BatchStatus::Cancelled) {
			break;
		}

		bool success = exportMessage(params.chatId, messageId, params.format, stream);
		if (success) {
			successful++;
		} else {
			failed++;
		}

		result.processedItems++;
		Q_EMIT operationProgress(operationId, result.processedItems, result.totalItems);
	}

	file.close();

	completeOperation(operationId, failed == 0, failed > 0 ? "Some exports failed" : QString());
}

void BatchOperations::executeMarkReadOperation(qint64 operationId, const BatchMarkReadParams &params) {
	auto &result = _operations[operationId];

	int successful = 0;
	int failed = 0;

	for (qint64 chatId : params.chatIds) {
		if (result.status == BatchStatus::Cancelled) {
			break;
		}

		bool success = markChatAsRead(chatId);
		if (success) {
			successful++;
		} else {
			failed++;
		}

		result.processedItems++;
		Q_EMIT operationProgress(operationId, result.processedItems, result.totalItems);

		QThread::msleep(1000 / _operationsPerSecond);
	}

	completeOperation(operationId, failed == 0, failed > 0 ? "Some mark-reads failed" : QString());
}

// Helper methods

bool BatchOperations::deleteMessage(qint64 chatId, qint64 messageId, bool deleteForAll) {
	if (!_session) {
		qWarning() << "BatchOperations: Session not available";
		return false;
	}

	auto &owner = _session->data();
	const auto peerId = PeerId(chatId);
	const auto history = owner.historyLoaded(peerId);
	if (!history) {
		qWarning() << "BatchOperations: Chat not found" << chatId;
		return false;
	}

	auto item = owner.message(history->peer->id, MsgId(messageId));
	if (!item) {
		qWarning() << "BatchOperations: Message not found" << messageId;
		return false;
	}

	// Use Data::Histories::deleteMessages with MessageIdsList
	MessageIdsList ids = { item->fullId() };
	_session->data().histories().deleteMessages(ids, deleteForAll);
	_session->data().sendHistoryChangeNotifications();

	return true;
}

bool BatchOperations::forwardMessage(
	qint64 sourceChatId,
	qint64 targetChatId,
	qint64 messageId,
	const BatchForwardParams &params
) {
	if (!_session) {
		qWarning() << "BatchOperations: Session not available";
		return false;
	}

	auto &owner = _session->data();

	// Get source message
	const auto fromPeerId = PeerId(sourceChatId);
	const auto fromHistory = owner.historyLoaded(fromPeerId);
	if (!fromHistory) {
		qWarning() << "BatchOperations: Source chat not found" << sourceChatId;
		return false;
	}

	auto item = owner.message(fromHistory->peer->id, MsgId(messageId));
	if (!item) {
		qWarning() << "BatchOperations: Message not found" << messageId;
		return false;
	}

	// Get destination
	const auto toPeerId = PeerId(targetChatId);
	auto toHistory = _session->data().history(toPeerId);
	if (!toHistory) {
		qWarning() << "BatchOperations: Destination chat not found" << targetChatId;
		return false;
	}

	// Build forward draft
	HistoryItemsList items;
	items.push_back(item);

	Data::ResolvedForwardDraft draft;
	draft.items = items;
	draft.options = params.dropAuthor
		? Data::ForwardOptions::NoNamesAndCaptions
		: Data::ForwardOptions::PreserveInfo;

	// Create send action
	auto thread = (Data::Thread*)toHistory;
	Api::SendAction action(thread, Api::SendOptions());

	// Forward via API
	_session->api().forwardMessages(std::move(draft), action);

	return true;
}

bool BatchOperations::exportMessage(qint64 chatId, qint64 messageId, const QString &format, QTextStream &stream) {
	if (!_session) {
		qWarning() << "BatchOperations: Session not available";
		return false;
	}

	auto peer = _session->data().peer(PeerId(chatId));
	if (!peer) {
		qWarning() << "BatchOperations: Invalid peer ID" << chatId;
		return false;
	}

	auto history = _session->data().history(peer);
	if (!history) {
		qWarning() << "BatchOperations: History not found";
		return false;
	}

	// Find the message
	auto item = history->owner().message(peer->id, MsgId(messageId));
	if (!item) {
		qWarning() << "BatchOperations: Message not found" << messageId;
		return false;
	}

	// Export based on format
	// Note: item->date() returns TimeId (int32 unix timestamp), not QDateTime
	const auto timestamp = item->date();
	const auto dateTime = QDateTime::fromSecsSinceEpoch(timestamp);

	if (format.toLower() == "json") {
		QJsonObject json;
		json["messageId"] = QString::number(messageId);
		json["chatId"] = QString::number(chatId);
		json["date"] = dateTime.toString(Qt::ISODate);
		json["text"] = item->originalText().text;
		json["fromId"] = QString::number(item->from()->id.value);
		json["fromName"] = item->from()->name();

		stream << QJsonDocument(json).toJson(QJsonDocument::Compact) << "\n";
	} else if (format.toLower() == "txt" || format.toLower() == "text") {
		stream << "[" << dateTime.toString("yyyy-MM-dd HH:mm:ss") << "] ";
		stream << item->from()->name() << ": ";
		stream << item->originalText().text << "\n";
	} else if (format.toLower() == "html") {
		stream << "<div class=\"message\" data-id=\"" << messageId << "\">";
		stream << "<span class=\"date\">" << dateTime.toString("yyyy-MM-dd HH:mm:ss") << "</span> ";
		stream << "<span class=\"author\">" << item->from()->name() << "</span>: ";
		stream << "<span class=\"text\">" << item->originalText().text << "</span>";
		stream << "</div>\n";
	} else {
		// Default to simple format
		stream << item->originalText().text << "\n";
	}

	return true;
}

bool BatchOperations::markChatAsRead(qint64 chatId) {
	if (!_session) {
		qWarning() << "BatchOperations: Session not available";
		return false;
	}

	auto peer = _session->data().peer(PeerId(chatId));
	if (!peer) {
		qWarning() << "BatchOperations: Invalid peer ID" << chatId;
		return false;
	}

	auto history = _session->data().history(peer);
	if (!history) {
		qWarning() << "BatchOperations: History not found";
		return false;
	}

	// Use Data::Histories::readInbox to mark the entire chat as read
	_session->data().histories().readInbox(history);

	return true;
}

// Message filtering

QVector<qint64> BatchOperations::filterMessagesByDate(
	qint64 chatId,
	const QDateTime &startDate,
	const QDateTime &endDate
) {
	QVector<qint64> result;
	if (!_session) return result;

	auto &owner = _session->data();
	const auto peerId = PeerId(chatId);
	const auto history = owner.historyLoaded(peerId);
	if (!history) return result;

	const auto startTs = static_cast<TimeId>(startDate.toSecsSinceEpoch());
	const auto endTs = static_cast<TimeId>(endDate.toSecsSinceEpoch());

	for (const auto &block : history->blocks) {
		if (!block) continue;
		for (const auto &element : block->messages) {
			if (!element) continue;
			auto item = element->data();
			if (!item) continue;
			const auto ts = item->date();
			if (ts >= startTs && ts <= endTs) {
				result.append(item->id.bare);
			}
		}
	}

	return result;
}

QVector<qint64> BatchOperations::filterMessagesByUser(qint64 chatId, qint64 userId) {
	QVector<qint64> result;
	if (!_session) return result;

	auto &owner = _session->data();
	const auto peerId = PeerId(chatId);
	const auto history = owner.historyLoaded(peerId);
	if (!history) return result;

	for (const auto &block : history->blocks) {
		if (!block) continue;
		for (const auto &element : block->messages) {
			if (!element) continue;
			auto item = element->data();
			if (!item) continue;
			auto from = item->from();
			if (from && from->id.value == static_cast<uint64>(userId)) {
				result.append(item->id.bare);
			}
		}
	}

	return result;
}

QVector<qint64> BatchOperations::filterMessagesByType(qint64 chatId, const QString &messageType) {
	QVector<qint64> result;
	if (!_session) return result;

	auto &owner = _session->data();
	const auto peerId = PeerId(chatId);
	const auto history = owner.historyLoaded(peerId);
	if (!history) return result;

	for (const auto &block : history->blocks) {
		if (!block) continue;
		for (const auto &element : block->messages) {
			if (!element) continue;
			auto item = element->data();
			if (!item) continue;

			bool match = false;
			if (messageType == "text") {
				match = !item->originalText().text.isEmpty() && !item->media();
			} else if (messageType == "media" || messageType == "photo" || messageType == "video") {
				match = (item->media() != nullptr);
			} else if (messageType == "service") {
				match = item->isService();
			}

			if (match) {
				result.append(item->id.bare);
			}
		}
	}

	return result;
}

// Operation management

qint64 BatchOperations::createOperation(BatchOperationType type) {
	qint64 operationId = _nextOperationId++;

	BatchOperationResult result;
	result.operationId = operationId;
	result.type = type;
	result.status = BatchStatus::Pending;
	result.startTime = QDateTime::currentDateTime();

	_operations[operationId] = result;

	return operationId;
}

void BatchOperations::updateOperationStatus(qint64 operationId, BatchStatus status) {
	if (_operations.contains(operationId)) {
		_operations[operationId].status = status;
	}
}

void BatchOperations::updateOperationProgress(qint64 operationId, int processed, int successful, int failed) {
	if (_operations.contains(operationId)) {
		auto &result = _operations[operationId];
		result.processedItems = processed;
		result.successfulItems = successful;
		result.failedItems = failed;
	}
}

void BatchOperations::completeOperation(qint64 operationId, bool success, const QString &error) {
	if (!_operations.contains(operationId)) {
		return;
	}

	auto &result = _operations[operationId];
	result.status = success ? BatchStatus::Completed : BatchStatus::Failed;
	result.endTime = QDateTime::currentDateTime();
	result.errorMessage = error;

	_currentConcurrentOperations--;

	if (success) {
		Q_EMIT operationCompleted(operationId);
	} else {
		Q_EMIT operationFailed(operationId, error);
	}
}

// Conversion

QJsonObject BatchOperations::operationResultToJson(const BatchOperationResult &result) const {
	QJsonObject json;
	json["operationId"] = QString::number(result.operationId);
	json["type"] = batchOperationTypeToString(result.type);
	json["status"] = batchStatusToString(result.status);
	json["totalItems"] = result.totalItems;
	json["processedItems"] = result.processedItems;
	json["successfulItems"] = result.successfulItems;
	json["failedItems"] = result.failedItems;
	json["startTime"] = result.startTime.toString(Qt::ISODate);
	if (result.endTime.isValid()) {
		json["endTime"] = result.endTime.toString(Qt::ISODate);
	}
	if (!result.errorMessage.isEmpty()) {
		json["errorMessage"] = result.errorMessage;
	}
	json["details"] = result.details;
	return json;
}

QString BatchOperations::batchOperationTypeToString(BatchOperationType type) const {
	switch (type) {
		case BatchOperationType::Delete: return "delete";
		case BatchOperationType::Forward: return "forward";
		case BatchOperationType::Export: return "export";
		case BatchOperationType::MarkAsRead: return "mark_read";
		case BatchOperationType::Search: return "search";
	}
	return "unknown";
}

QString BatchOperations::batchStatusToString(BatchStatus status) const {
	switch (status) {
		case BatchStatus::Pending: return "pending";
		case BatchStatus::Running: return "running";
		case BatchStatus::Completed: return "completed";
		case BatchStatus::Failed: return "failed";
		case BatchStatus::Cancelled: return "cancelled";
	}
	return "unknown";
}

} // namespace MCP

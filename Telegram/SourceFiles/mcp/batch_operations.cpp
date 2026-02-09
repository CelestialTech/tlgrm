// This file is part of Telegram Desktop MCP Server.
// Licensed under GPLv3 with OpenSSL exception.

#include "batch_operations.h"
#include "data/data_session.h"
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
	// This is a stub - would need to collect all message IDs from source chat
	BatchForwardParams params;
	params.sourceChatId = sourceChatId;
	params.targetChatId = targetChatId;
	// params.messageIds would be populated with actual message IDs

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
	// This is a stub - would need to collect message IDs
	BatchExportParams params;
	params.chatId = chatId;
	params.format = format;
	params.outputPath = outputPath;

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
	// This is a stub - would need to collect all chat IDs
	BatchMarkReadParams params;
	// params.chatIds would be populated

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
	// Stub - would need more complex state management
	return false;
}

bool BatchOperations::resumeOperation(qint64 operationId) {
	// Stub - would need more complex state management
	return false;
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

	// TODO: Implement deleteMessages with correct tdesktop API
	// canDeleteForEveryone expects TimeId (int32), not QDateTime
	// ApiWrap::deleteMessages doesn't exist with this signature
	qWarning() << "TODO: Implement deleteMessages with correct tdesktop API";
	qDebug() << "BatchOperations: Stub - message not actually deleted" << messageId << "from chat" << chatId;
	return false;
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

	auto sourcePeer = _session->data().peer(PeerId(sourceChatId));
	auto targetPeer = _session->data().peer(PeerId(targetChatId));

	if (!sourcePeer || !targetPeer) {
		qWarning() << "BatchOperations: Invalid peer IDs";
		return false;
	}

	auto sourceHistory = _session->data().history(sourcePeer);
	auto targetHistory = _session->data().history(targetPeer);

	if (!sourceHistory || !targetHistory) {
		qWarning() << "BatchOperations: History not found";
		return false;
	}

	// Find the message
	auto item = sourceHistory->owner().message(sourcePeer->id, MsgId(messageId));
	if (!item) {
		qWarning() << "BatchOperations: Message not found" << messageId;
		return false;
	}

	// TODO: Implement forwardMessages with correct tdesktop API
	// Api::SendOptions doesn't have dropAuthor or dropCaption members
	// forwardMessages expects HistoryItemsList, not FullMsgId directly
	qWarning() << "TODO: Implement forwardMessages with correct tdesktop API";
	qDebug() << "BatchOperations: Stub - message not actually forwarded" << messageId << "from" << sourceChatId << "to" << targetChatId;
	return false;
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

	// TODO: Implement readInbox with correct tdesktop API
	// ApiWrap::readInbox doesn't exist with this signature
	qWarning() << "TODO: Implement readInbox with correct tdesktop API";
	qDebug() << "BatchOperations: Stub - chat not actually marked as read" << chatId;
	return false;
}

// Message filtering

QVector<qint64> BatchOperations::filterMessagesByDate(
	qint64 chatId,
	const QDateTime &startDate,
	const QDateTime &endDate
) {
	// Stub - would need to query messages from data session
	return QVector<qint64>();
}

QVector<qint64> BatchOperations::filterMessagesByUser(qint64 chatId, qint64 userId) {
	// Stub
	return QVector<qint64>();
}

QVector<qint64> BatchOperations::filterMessagesByType(qint64 chatId, const QString &messageType) {
	// Stub
	return QVector<qint64>();
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

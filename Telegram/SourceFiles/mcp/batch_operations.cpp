// This file is part of Telegram Desktop MCP Server.
// Licensed under GPLv3 with OpenSSL exception.

#include "batch_operations.h"

#include <QtCore/QThread>
#include <QtCore/QFile>
#include <QtCore/QTextStream>
#include <QtCore/QDateTime>

namespace MCP {

BatchOperations::BatchOperations(QObject *parent)
	: QObject(parent) {
}

BatchOperations::~BatchOperations() = default;

// Send messages to multiple chats
BatchResult BatchOperations::sendMessages(
		const QVector<qint64> &chatIds,
		const QString &message,
		bool silent) {

	return executeOperations<qint64>(
		"SEND_MESSAGES",
		chatIds,
		[this, &message, silent](const qint64 &chatId, OperationResult &result) -> bool {
			auto start = QDateTime::currentMSecsSinceEpoch();

			// TODO: Implement actual message sending via tdesktop API
			// For now, this is a placeholder
			Q_UNUSED(message);
			Q_UNUSED(silent);

			// Simulate operation
			result.data["chat_id"] = chatId;
			result.data["message"] = message;
			result.duration = QDateTime::currentMSecsSinceEpoch() - start;

			return true; // Success placeholder
		}
	);
}

// Send multiple messages to one chat
BatchResult BatchOperations::sendMessagesToChat(
		qint64 chatId,
		const QVector<QString> &messages,
		int delayMs) {

	return executeOperations<QString>(
		"SEND_MESSAGES_TO_CHAT",
		messages,
		[this, chatId, delayMs](const QString &message, OperationResult &result) -> bool {
			auto start = QDateTime::currentMSecsSinceEpoch();

			// TODO: Implement via tdesktop API
			Q_UNUSED(chatId);
			Q_UNUSED(message);

			// Apply delay
			if (delayMs > 0) {
				QThread::msleep(delayMs);
			}

			result.data["chat_id"] = chatId;
			result.data["message"] = message;
			result.duration = QDateTime::currentMSecsSinceEpoch() - start;

			return true;
		}
	);
}

// Delete multiple messages
BatchResult BatchOperations::deleteMessages(
		qint64 chatId,
		const QVector<qint64> &messageIds) {

	return executeOperations<qint64>(
		"DELETE_MESSAGES",
		messageIds,
		[this, chatId](const qint64 &messageId, OperationResult &result) -> bool {
			auto start = QDateTime::currentMSecsSinceEpoch();

			// TODO: Implement via tdesktop API
			Q_UNUSED(chatId);
			Q_UNUSED(messageId);

			result.data["chat_id"] = chatId;
			result.data["message_id"] = messageId;
			result.duration = QDateTime::currentMSecsSinceEpoch() - start;

			return true;
		}
	);
}

// Delete messages in multiple chats
BatchResult BatchOperations::deleteMessagesInChats(
		const QVector<qint64> &chatIds,
		const QVector<qint64> &messageIds) {

	struct ChatMessage {
		qint64 chatId;
		qint64 messageId;
	};

	QVector<ChatMessage> operations;
	for (qint64 chatId : chatIds) {
		for (qint64 messageId : messageIds) {
			operations.append({chatId, messageId});
		}
	}

	return executeOperations<ChatMessage>(
		"DELETE_MESSAGES_IN_CHATS",
		operations,
		[this](const ChatMessage &op, OperationResult &result) -> bool {
			auto start = QDateTime::currentMSecsSinceEpoch();

			// TODO: Implement via tdesktop API
			Q_UNUSED(op);

			result.data["chat_id"] = op.chatId;
			result.data["message_id"] = op.messageId;
			result.duration = QDateTime::currentMSecsSinceEpoch() - start;

			return true;
		}
	);
}

// Forward messages
BatchResult BatchOperations::forwardMessages(
		qint64 fromChatId,
		qint64 toChatId,
		const QVector<qint64> &messageIds) {

	return executeOperations<qint64>(
		"FORWARD_MESSAGES",
		messageIds,
		[this, fromChatId, toChatId](const qint64 &messageId, OperationResult &result) -> bool {
			auto start = QDateTime::currentMSecsSinceEpoch();

			// TODO: Implement via tdesktop API
			Q_UNUSED(fromChatId);
			Q_UNUSED(toChatId);
			Q_UNUSED(messageId);

			result.data["from_chat_id"] = fromChatId;
			result.data["to_chat_id"] = toChatId;
			result.data["message_id"] = messageId;
			result.duration = QDateTime::currentMSecsSinceEpoch() - start;

			return true;
		}
	);
}

// Pin messages
BatchResult BatchOperations::pinMessages(
		qint64 chatId,
		const QVector<qint64> &messageIds,
		bool notify) {

	return executeOperations<qint64>(
		"PIN_MESSAGES",
		messageIds,
		[this, chatId, notify](const qint64 &messageId, OperationResult &result) -> bool {
			auto start = QDateTime::currentMSecsSinceEpoch();

			// TODO: Implement via tdesktop API
			Q_UNUSED(chatId);
			Q_UNUSED(messageId);
			Q_UNUSED(notify);

			result.data["chat_id"] = chatId;
			result.data["message_id"] = messageId;
			result.data["notify"] = notify;
			result.duration = QDateTime::currentMSecsSinceEpoch() - start;

			return true;
		}
	);
}

// Unpin messages
BatchResult BatchOperations::unpinMessages(
		qint64 chatId,
		const QVector<qint64> &messageIds) {

	return executeOperations<qint64>(
		"UNPIN_MESSAGES",
		messageIds,
		[this, chatId](const qint64 &messageId, OperationResult &result) -> bool {
			auto start = QDateTime::currentMSecsSinceEpoch();

			// TODO: Implement via tdesktop API
			Q_UNUSED(chatId);
			Q_UNUSED(messageId);

			result.data["chat_id"] = chatId;
			result.data["message_id"] = messageId;
			result.duration = QDateTime::currentMSecsSinceEpoch() - start;

			return true;
		}
	);
}

// Add reactions
BatchResult BatchOperations::addReactions(
		qint64 chatId,
		const QVector<qint64> &messageIds,
		const QString &emoji) {

	return executeOperations<qint64>(
		"ADD_REACTIONS",
		messageIds,
		[this, chatId, &emoji](const qint64 &messageId, OperationResult &result) -> bool {
			auto start = QDateTime::currentMSecsSinceEpoch();

			// TODO: Implement via tdesktop API
			Q_UNUSED(chatId);
			Q_UNUSED(messageId);
			Q_UNUSED(emoji);

			result.data["chat_id"] = chatId;
			result.data["message_id"] = messageId;
			result.data["emoji"] = emoji;
			result.duration = QDateTime::currentMSecsSinceEpoch() - start;

			return true;
		}
	);
}

// Generic batch executor
BatchResult BatchOperations::executeBatch(
		const QString &operationType,
		const QVector<QJsonObject> &operations,
		std::function<bool(const QJsonObject&, OperationResult&)> executor) {

	return executeOperations<QJsonObject>(operationType, operations, executor);
}

// Template implementation for batch operations
template<typename T>
BatchResult BatchOperations::executeOperations(
		const QString &operationType,
		const QVector<T> &items,
		std::function<bool(const T&, OperationResult&)> executor) {

	BatchResult batch;
	batch.operationType = operationType;
	batch.total = items.size();
	batch.successful = 0;
	batch.failed = 0;
	batch.status = OperationStatus::Running;
	batch.startedAt = QDateTime::currentDateTime();

	Q_EMIT batchStarted(operationType, items.size());

	auto overallStart = QDateTime::currentMSecsSinceEpoch();

	// Process items with concurrency control
	int processed = 0;
	int concurrentCount = 0;

	for (int i = 0; i < items.size(); ++i) {
		const T &item = items[i];

		OperationResult opResult;
		opResult.index = i;
		opResult.success = false;

		try {
			// Apply rate limiting
			if (concurrentCount >= _concurrencyLimit) {
				applyRateLimit();
				concurrentCount = 0;
			}

			// Execute operation
			opResult.success = executor(item, opResult);

			if (opResult.success) {
				batch.successful++;
			} else {
				batch.failed++;
				batch.errors.append(opResult.error);
			}

			concurrentCount++;

		} catch (const std::exception &e) {
			opResult.success = false;
			opResult.error = QString::fromStdString(e.what());
			batch.failed++;
			batch.errors.append(opResult.error);
		}

		batch.results.append(opResult);
		processed++;

		Q_EMIT batchProgress(processed, items.size());

		// Check if we should continue on error
		if (!opResult.success && !shouldContinueOnError()) {
			batch.status = OperationStatus::Failed;
			break;
		}
	}

	batch.totalDuration = QDateTime::currentMSecsSinceEpoch() - overallStart;
	batch.completedAt = QDateTime::currentDateTime();

	// Determine final status
	if (batch.failed == 0) {
		batch.status = OperationStatus::Completed;
	} else if (batch.successful > 0) {
		batch.status = OperationStatus::Partial;
	} else {
		batch.status = OperationStatus::Failed;
	}

	Q_EMIT batchCompleted(operationType, batch.successful, batch.failed);

	return batch;
}

// Apply rate limiting delay
void BatchOperations::applyRateLimit() {
	if (_rateLimitDelay > 0) {
		auto now = QDateTime::currentDateTime();
		qint64 elapsed = _lastOperationTime.msecsTo(now);

		if (elapsed < _rateLimitDelay) {
			QThread::msleep(_rateLimitDelay - elapsed);
		}

		_lastOperationTime = QDateTime::currentDateTime();
	}
}

// Export batch result to JSON
QJsonObject BatchOperations::exportBatchResult(const BatchResult &result) {
	QJsonObject json;
	json["operation_type"] = result.operationType;

	QString statusStr;
	switch (result.status) {
	case OperationStatus::Pending: statusStr = "pending"; break;
	case OperationStatus::Running: statusStr = "running"; break;
	case OperationStatus::Completed: statusStr = "completed"; break;
	case OperationStatus::Failed: statusStr = "failed"; break;
	case OperationStatus::Partial: statusStr = "partial"; break;
	}
	json["status"] = statusStr;

	json["total"] = result.total;
	json["successful"] = result.successful;
	json["failed"] = result.failed;
	json["total_duration_ms"] = static_cast<qint64>(result.totalDuration);
	json["started_at"] = result.startedAt.toString(Qt::ISODate);
	json["completed_at"] = result.completedAt.toString(Qt::ISODate);

	// Results array
	QJsonArray resultsArray;
	for (const auto &opResult : result.results) {
		QJsonObject opJson;
		opJson["index"] = opResult.index;
		opJson["success"] = opResult.success;
		opJson["duration_ms"] = opResult.duration;
		if (!opResult.error.isEmpty()) {
			opJson["error"] = opResult.error;
		}
		opJson["data"] = opResult.data;
		resultsArray.append(opJson);
	}
	json["results"] = resultsArray;

	// Errors array
	QJsonArray errorsArray;
	for (const QString &error : result.errors) {
		errorsArray.append(error);
	}
	json["errors"] = errorsArray;

	return json;
}

// Export to CSV
QString BatchOperations::exportBatchResultToCSV(const BatchResult &result, const QString &outputPath) {
	QFile file(outputPath);
	if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
		return QString();
	}

	QTextStream out(&file);

	// Header
	out << "index,success,duration_ms,error\n";

	// Rows
	for (const auto &opResult : result.results) {
		out << opResult.index << ","
		    << (opResult.success ? "true" : "false") << ","
		    << opResult.duration << ","
		    << "\"" << opResult.error.replace("\"", "\"\"") << "\"\n";
	}

	file.close();
	return outputPath;
}

} // namespace MCP

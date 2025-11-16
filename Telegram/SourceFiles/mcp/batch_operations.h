// This file is part of Telegram Desktop MCP Server.
// Licensed under GPLv3 with OpenSSL exception.

#pragma once

#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtCore/QVector>
#include <QtCore/QJsonObject>
#include <QtCore/QJsonArray>
#include <QtCore/QDateTime>
#include <functional>

namespace MCP {

// Operation status
enum class OperationStatus {
	Pending,
	Running,
	Completed,
	Failed,
	Partial  // Some operations succeeded, some failed
};

// Operation result for a single item
struct OperationResult {
	int index;
	bool success;
	QString error;
	double duration;  // milliseconds
	QJsonObject data;  // Operation-specific result data
};

// Batch operation result
struct BatchResult {
	QString operationType;
	OperationStatus status;
	int total;
	int successful;
	int failed;
	double totalDuration;  // milliseconds
	QVector<OperationResult> results;
	QStringList errors;
	QDateTime startedAt;
	QDateTime completedAt;
};

// Batch operations manager
class BatchOperations : public QObject {
	Q_OBJECT

public:
	explicit BatchOperations(QObject *parent = nullptr);
	~BatchOperations();

	// Configuration
	void setConcurrencyLimit(int limit) { _concurrencyLimit = limit; }
	void setRateLimitDelay(int delayMs) { _rateLimitDelay = delayMs; }
	[[nodiscard]] int concurrencyLimit() const { return _concurrencyLimit; }

	// Batch send messages
	BatchResult sendMessages(
		const QVector<qint64> &chatIds,
		const QString &message,
		bool silent = false
	);

	BatchResult sendMessagesToChat(
		qint64 chatId,
		const QVector<QString> &messages,
		int delayMs = 100
	);

	// Batch delete messages
	BatchResult deleteMessages(
		qint64 chatId,
		const QVector<qint64> &messageIds
	);

	BatchResult deleteMessagesInChats(
		const QVector<qint64> &chatIds,
		const QVector<qint64> &messageIds
	);

	// Batch forward messages
	BatchResult forwardMessages(
		qint64 fromChatId,
		qint64 toChatId,
		const QVector<qint64> &messageIds
	);

	// Batch pin/unpin
	BatchResult pinMessages(
		qint64 chatId,
		const QVector<qint64> &messageIds,
		bool notify = false
	);

	BatchResult unpinMessages(
		qint64 chatId,
		const QVector<qint64> &messageIds
	);

	// Batch reactions
	BatchResult addReactions(
		qint64 chatId,
		const QVector<qint64> &messageIds,
		const QString &emoji
	);

	// Generic batch operation
	BatchResult executeBatch(
		const QString &operationType,
		const QVector<QJsonObject> &operations,
		std::function<bool(const QJsonObject&, OperationResult&)> executor
	);

	// Export batch result
	QJsonObject exportBatchResult(const BatchResult &result);
	QString exportBatchResultToCSV(const BatchResult &result, const QString &outputPath);

Q_SIGNALS:
	void batchStarted(const QString &operationType, int total);
	void batchProgress(int current, int total);
	void batchCompleted(const QString &operationType, int successful, int failed);
	void error(const QString &errorMessage);

private:
	// Execution helpers
	template<typename T>
	BatchResult executeOperations(
		const QString &operationType,
		const QVector<T> &items,
		std::function<bool(const T&, OperationResult&)> executor
	);

	void applyRateLimit();
	bool shouldContinueOnError() const { return true; }  // Configurable

	int _concurrencyLimit = 5;  // Max concurrent operations
	int _rateLimitDelay = 100;  // Delay between batches (ms)
	QDateTime _lastOperationTime;
};

} // namespace MCP

// This file is part of Telegram Desktop MCP Server.
// Licensed under GPLv3 with OpenSSL exception.

#pragma once

#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtCore/QJsonObject>
#include <QtCore/QJsonArray>
#include <QtCore/QVector>
#include <QtCore/QHash>
#include <memory>

namespace Main {
class Session;
} // namespace Main

namespace MCP {

// Batch operation type
enum class BatchOperationType {
	Delete,
	Forward,
	Export,
	MarkAsRead,
	Search
};

// Batch operation status
enum class BatchStatus {
	Pending,
	Running,
	Completed,
	Failed,
	Cancelled
};

// Batch operation result
struct BatchOperationResult {
	qint64 operationId = 0;
	BatchOperationType type;
	BatchStatus status = BatchStatus::Pending;
	int totalItems = 0;
	int processedItems = 0;
	int successfulItems = 0;
	int failedItems = 0;
	QDateTime startTime;
	QDateTime endTime;
	QString errorMessage;
	QJsonObject details;
};

// Batch delete parameters
struct BatchDeleteParams {
	qint64 chatId = 0;
	QVector<qint64> messageIds;
	bool deleteForAll = false;
	bool revoke = true;
};

// Batch forward parameters
struct BatchForwardParams {
	qint64 sourceChatId = 0;
	qint64 targetChatId = 0;
	QVector<qint64> messageIds;
	bool silent = false;
	bool dropAuthor = false;
	bool dropCaption = false;
};

// Batch export parameters
struct BatchExportParams {
	qint64 chatId = 0;
	QVector<qint64> messageIds;
	QString format = "json"; // "json", "txt", "html"
	QString outputPath;
	bool includeMedia = false;
};

// Batch mark read parameters
struct BatchMarkReadParams {
	QVector<qint64> chatIds;
	bool markAsRead = true; // false = mark as unread
};

// Batch operations class
class BatchOperations : public QObject {
	Q_OBJECT

public:
	explicit BatchOperations(QObject *parent = nullptr);
	~BatchOperations();

	// Initialization
	bool start(Main::Session *session);
	void stop();
	[[nodiscard]] bool isRunning() const { return _isRunning; }

	// Batch delete operations
	qint64 batchDeleteMessages(const BatchDeleteParams &params);
	qint64 deleteMessageRange(
		qint64 chatId,
		qint64 startMessageId,
		qint64 endMessageId,
		bool deleteForAll = false
	);
	qint64 deleteMessagesByFilter(
		qint64 chatId,
		const QString &filter, // "date", "user", "type"
		const QJsonObject &filterParams
	);

	// Batch forward operations
	qint64 batchForwardMessages(const BatchForwardParams &params);
	qint64 forwardAllMessages(
		qint64 sourceChatId,
		qint64 targetChatId,
		int limit = 1000
	);

	// Batch export operations
	qint64 batchExportMessages(const BatchExportParams &params);
	qint64 exportChatMessages(
		qint64 chatId,
		const QString &format,
		const QString &outputPath,
		int limit = -1 // -1 = all
	);

	// Batch mark as read/unread
	qint64 batchMarkAsRead(const BatchMarkReadParams &params);
	qint64 markAllChatsRead();

	// Operation management
	bool cancelOperation(qint64 operationId);
	bool pauseOperation(qint64 operationId);
	bool resumeOperation(qint64 operationId);

	// Query operations
	QJsonObject getOperationStatus(qint64 operationId);
	QJsonArray listOperations(BatchStatus status = BatchStatus::Running);
	QJsonArray getRecentOperations(int limit = 10);

	// Batch statistics
	QJsonObject getOperationStatistics();

Q_SIGNALS:
	void operationStarted(qint64 operationId, const QString &type);
	void operationProgress(qint64 operationId, int processed, int total);
	void operationCompleted(qint64 operationId);
	void operationFailed(qint64 operationId, const QString &error);
	void operationCancelled(qint64 operationId);

private Q_SLOTS:
	void processOperationQueue();

private:
	// Operation execution
	void executeDeleteOperation(qint64 operationId, const BatchDeleteParams &params);
	void executeForwardOperation(qint64 operationId, const BatchForwardParams &params);
	void executeExportOperation(qint64 operationId, const BatchExportParams &params);
	void executeMarkReadOperation(qint64 operationId, const BatchMarkReadParams &params);

	// Helper methods
	bool deleteMessage(qint64 chatId, qint64 messageId, bool deleteForAll);
	bool forwardMessage(
		qint64 sourceChatId,
		qint64 targetChatId,
		qint64 messageId,
		const BatchForwardParams &params
	);
	bool exportMessage(qint64 chatId, qint64 messageId, const QString &format, QTextStream &stream);
	bool markChatAsRead(qint64 chatId);

	// Message filtering
	QVector<qint64> filterMessagesByDate(
		qint64 chatId,
		const QDateTime &startDate,
		const QDateTime &endDate
	);
	QVector<qint64> filterMessagesByUser(qint64 chatId, qint64 userId);
	QVector<qint64> filterMessagesByType(qint64 chatId, const QString &messageType);

	// Operation management
	qint64 createOperation(BatchOperationType type);
	void updateOperationStatus(qint64 operationId, BatchStatus status);
	void updateOperationProgress(qint64 operationId, int processed, int successful, int failed);
	void completeOperation(qint64 operationId, bool success, const QString &error = QString());

	// Conversion
	QJsonObject operationResultToJson(const BatchOperationResult &result) const;
	QString batchOperationTypeToString(BatchOperationType type) const;
	QString batchStatusToString(BatchStatus status) const;

	Main::Session *_session = nullptr;
	bool _isRunning = false;

	// Operation tracking
	QHash<qint64, BatchOperationResult> _operations;
	qint64 _nextOperationId = 1;

	// Queue management
	QTimer *_queueTimer = nullptr;
	int _queueProcessIntervalMs = 100;
	int _maxConcurrentOperations = 3;
	int _currentConcurrentOperations = 0;

	// Rate limiting
	int _operationsPerSecond = 10;
	QDateTime _lastOperationTime;
};

} // namespace MCP

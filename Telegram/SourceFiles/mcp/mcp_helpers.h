// MCP Helpers - Utility classes for validation, logging, and error handling
//
// This file is part of Telegram Desktop MCP integration.
// Provides centralized utilities for consistent code patterns.

#pragma once

#include <QtCore/QObject>
#include <QtCore/QJsonObject>
#include <QtCore/QJsonArray>
#include <QtCore/QString>
#include <QtCore/QStringList>
#include <QtCore/QDateTime>
#include <QtCore/QMutex>
#include <QtSql/QSqlQuery>
#include <QtSql/QSqlError>
#include <QtSql/QSqlDatabase>

namespace MCP {

// ============================================================
// ERROR CODES - Standardized error codes for all tools
// ============================================================

enum class ErrorCode {
	// Input validation errors (1xxx)
	MissingParameter = 1001,
	InvalidParameter = 1002,
	InvalidChatId = 1003,
	InvalidUserId = 1004,
	InvalidMessageId = 1005,
	InvalidLimit = 1006,
	InvalidTimestamp = 1007,

	// Resource errors (2xxx)
	ResourceNotFound = 2001,
	ChatNotFound = 2002,
	UserNotFound = 2003,
	MessageNotFound = 2004,

	// State errors (3xxx)
	SessionNotAvailable = 3001,
	DatabaseNotOpen = 3002,
	ComponentNotInitialized = 3003,

	// Operation errors (4xxx)
	OperationFailed = 4001,
	PermissionDenied = 4002,
	RateLimited = 4003,

	// Internal errors (5xxx)
	InternalError = 5001,
	SqlError = 5002
};

inline QString errorCodeToString(ErrorCode code) {
	switch (code) {
		case ErrorCode::MissingParameter: return "MISSING_PARAMETER";
		case ErrorCode::InvalidParameter: return "INVALID_PARAMETER";
		case ErrorCode::InvalidChatId: return "INVALID_CHAT_ID";
		case ErrorCode::InvalidUserId: return "INVALID_USER_ID";
		case ErrorCode::InvalidMessageId: return "INVALID_MESSAGE_ID";
		case ErrorCode::InvalidLimit: return "INVALID_LIMIT";
		case ErrorCode::InvalidTimestamp: return "INVALID_TIMESTAMP";
		case ErrorCode::ResourceNotFound: return "RESOURCE_NOT_FOUND";
		case ErrorCode::ChatNotFound: return "CHAT_NOT_FOUND";
		case ErrorCode::UserNotFound: return "USER_NOT_FOUND";
		case ErrorCode::MessageNotFound: return "MESSAGE_NOT_FOUND";
		case ErrorCode::SessionNotAvailable: return "SESSION_NOT_AVAILABLE";
		case ErrorCode::DatabaseNotOpen: return "DATABASE_NOT_OPEN";
		case ErrorCode::ComponentNotInitialized: return "COMPONENT_NOT_INITIALIZED";
		case ErrorCode::OperationFailed: return "OPERATION_FAILED";
		case ErrorCode::PermissionDenied: return "PERMISSION_DENIED";
		case ErrorCode::RateLimited: return "RATE_LIMITED";
		case ErrorCode::InternalError: return "INTERNAL_ERROR";
		case ErrorCode::SqlError: return "SQL_ERROR";
		default: return "UNKNOWN_ERROR";
	}
}

// ============================================================
// MCPLogger - Structured logging with consistent format
// ============================================================

class MCPLogger {
public:
	enum class Level {
		Debug,
		Info,
		Warning,
		Error
	};

	static void setMinLevel(Level level) { _minLevel = level; }
	static Level minLevel() { return _minLevel; }

	static void debug(const QString &component, const QString &message) {
		log(Level::Debug, component, message);
	}

	static void info(const QString &component, const QString &message) {
		log(Level::Info, component, message);
	}

	static void warning(const QString &component, const QString &message) {
		log(Level::Warning, component, message);
	}

	static void error(const QString &component, const QString &message) {
		log(Level::Error, component, message);
	}

	static void log(Level level, const QString &component, const QString &message) {
		if (level < _minLevel) return;

		QString timestamp = QDateTime::currentDateTime().toString(Qt::ISODate);
		QString levelStr = levelToString(level);
		QString formatted = QString("[%1] [%2] [MCP:%3] %4")
			.arg(timestamp, levelStr, component, message);

		// Output to stderr with MCP prefix for easy filtering
		fprintf(stderr, "%s\n", formatted.toUtf8().constData());
		fflush(stderr);
	}

private:
	static QString levelToString(Level level) {
		switch (level) {
			case Level::Debug: return "DEBUG";
			case Level::Info: return "INFO";
			case Level::Warning: return "WARN";
			case Level::Error: return "ERROR";
			default: return "UNKNOWN";
		}
	}

	static inline Level _minLevel = Level::Info;
};

// Convenience macros for logging
#define MCP_LOG_DEBUG(component, msg) MCP::MCPLogger::debug(component, msg)
#define MCP_LOG_INFO(component, msg) MCP::MCPLogger::info(component, msg)
#define MCP_LOG_WARN(component, msg) MCP::MCPLogger::warning(component, msg)
#define MCP_LOG_ERROR(component, msg) MCP::MCPLogger::error(component, msg)

// ============================================================
// ValidationResult - Result of input validation
// ============================================================

struct ValidationResult {
	bool isValid = true;
	ErrorCode errorCode = ErrorCode::MissingParameter;
	QString errorMessage;
	QJsonObject errorDetails;

	static ValidationResult success() {
		return ValidationResult{true, ErrorCode::MissingParameter, QString(), QJsonObject()};
	}

	static ValidationResult failure(ErrorCode code, const QString &message, const QJsonObject &details = QJsonObject()) {
		return ValidationResult{false, code, message, details};
	}
};

// ============================================================
// InputValidator - Centralized input validation
// ============================================================

class InputValidator {
public:
	// Check required fields exist
	static ValidationResult validateRequired(
		const QJsonObject &args,
		const QStringList &requiredFields
	) {
		for (const auto &field : requiredFields) {
			if (!args.contains(field)) {
				QJsonObject details;
				details["missing_field"] = field;
				details["provided_fields"] = QJsonArray::fromStringList(args.keys());
				return ValidationResult::failure(
					ErrorCode::MissingParameter,
					QString("Missing required parameter: %1").arg(field),
					details
				);
			}
		}
		return ValidationResult::success();
	}

	// Validate chat_id parameter
	static ValidationResult validateChatId(const QJsonObject &args, bool required = true) {
		if (!args.contains("chat_id")) {
			if (required) {
				return ValidationResult::failure(
					ErrorCode::MissingParameter,
					"Missing required parameter: chat_id"
				);
			}
			return ValidationResult::success();
		}

		qint64 chatId = args["chat_id"].toVariant().toLongLong();
		if (chatId == 0) {
			QJsonObject details;
			details["provided_value"] = args["chat_id"].toString();
			return ValidationResult::failure(
				ErrorCode::InvalidChatId,
				"Invalid chat_id: must be a non-zero integer",
				details
			);
		}

		return ValidationResult::success();
	}

	// Validate user_id parameter
	static ValidationResult validateUserId(const QJsonObject &args, bool required = true) {
		if (!args.contains("user_id")) {
			if (required) {
				return ValidationResult::failure(
					ErrorCode::MissingParameter,
					"Missing required parameter: user_id"
				);
			}
			return ValidationResult::success();
		}

		qint64 userId = args["user_id"].toVariant().toLongLong();
		if (userId <= 0) {
			QJsonObject details;
			details["provided_value"] = args["user_id"].toString();
			return ValidationResult::failure(
				ErrorCode::InvalidUserId,
				"Invalid user_id: must be a positive integer",
				details
			);
		}

		return ValidationResult::success();
	}

	// Validate message_id parameter
	static ValidationResult validateMessageId(const QJsonObject &args, bool required = true) {
		if (!args.contains("message_id")) {
			if (required) {
				return ValidationResult::failure(
					ErrorCode::MissingParameter,
					"Missing required parameter: message_id"
				);
			}
			return ValidationResult::success();
		}

		qint64 messageId = args["message_id"].toVariant().toLongLong();
		if (messageId <= 0) {
			QJsonObject details;
			details["provided_value"] = args["message_id"].toString();
			return ValidationResult::failure(
				ErrorCode::InvalidMessageId,
				"Invalid message_id: must be a positive integer",
				details
			);
		}

		return ValidationResult::success();
	}

	// Validate limit parameter with bounds
	static ValidationResult validateLimit(
		const QJsonObject &args,
		int minLimit = 1,
		int maxLimit = 1000
	) {
		if (!args.contains("limit")) {
			return ValidationResult::success(); // Optional, will use default
		}

		int limit = args["limit"].toInt(0);
		if (limit < minLimit || limit > maxLimit) {
			QJsonObject details;
			details["provided_value"] = limit;
			details["min_allowed"] = minLimit;
			details["max_allowed"] = maxLimit;
			return ValidationResult::failure(
				ErrorCode::InvalidLimit,
				QString("Invalid limit: must be between %1 and %2").arg(minLimit).arg(maxLimit),
				details
			);
		}

		return ValidationResult::success();
	}

	// Validate non-empty string
	static ValidationResult validateNonEmptyString(
		const QJsonObject &args,
		const QString &fieldName,
		bool required = true
	) {
		if (!args.contains(fieldName)) {
			if (required) {
				return ValidationResult::failure(
					ErrorCode::MissingParameter,
					QString("Missing required parameter: %1").arg(fieldName)
				);
			}
			return ValidationResult::success();
		}

		QString value = args[fieldName].toString();
		if (value.isEmpty()) {
			QJsonObject details;
			details["field"] = fieldName;
			return ValidationResult::failure(
				ErrorCode::InvalidParameter,
				QString("Parameter %1 cannot be empty").arg(fieldName),
				details
			);
		}

		return ValidationResult::success();
	}
};

// ============================================================
// ArgExtractor - Extract and convert parameters with defaults
// ============================================================

class ArgExtractor {
public:
	explicit ArgExtractor(const QJsonObject &args) : _args(args) {}

	// Extract chat_id (returns defaultValue if invalid/missing)
	qint64 chatId(qint64 defaultValue = 0) const {
		if (!_args.contains("chat_id")) return defaultValue;
		bool ok = false;
		qint64 val = _args["chat_id"].toVariant().toLongLong(&ok);
		return ok ? val : defaultValue;
	}

	// Extract user_id (returns defaultValue if invalid/missing)
	qint64 userId(qint64 defaultValue = 0) const {
		if (!_args.contains("user_id")) return defaultValue;
		bool ok = false;
		qint64 val = _args["user_id"].toVariant().toLongLong(&ok);
		return ok ? val : defaultValue;
	}

	// Extract message_id (returns defaultValue if invalid/missing)
	qint64 messageId(qint64 defaultValue = 0) const {
		if (!_args.contains("message_id")) return defaultValue;
		bool ok = false;
		qint64 val = _args["message_id"].toVariant().toLongLong(&ok);
		return ok ? val : defaultValue;
	}

	// Extract limit with bounds enforcement
	int limit(int defaultValue = 50, int maxValue = 1000) const {
		int value = _args.value("limit").toInt(defaultValue);
		return qBound(1, value, maxValue);
	}

	// Extract offset with bounds
	int offset(int defaultValue = 0) const {
		int value = _args.value("offset").toInt(defaultValue);
		return qMax(0, value);
	}

	// Extract timestamp
	qint64 timestamp(const QString &key = "timestamp", qint64 defaultValue = 0) const {
		if (!_args.contains(key)) return defaultValue;
		bool ok = false;
		qint64 val = _args.value(key).toVariant().toLongLong(&ok);
		return ok ? val : defaultValue;
	}

	// Extract string with default
	QString string(const QString &key, const QString &defaultValue = QString()) const {
		return _args.value(key).toString(defaultValue);
	}

	// Extract boolean with default
	bool boolean(const QString &key, bool defaultValue = false) const {
		return _args.value(key).toBool(defaultValue);
	}

	// Extract integer with default
	int integer(const QString &key, int defaultValue = 0) const {
		return _args.value(key).toInt(defaultValue);
	}

	// Extract double with default
	double number(const QString &key, double defaultValue = 0.0) const {
		return _args.value(key).toDouble(defaultValue);
	}

	// Extract array
	QJsonArray array(const QString &key) const {
		return _args.value(key).toArray();
	}

	// Extract object
	QJsonObject object(const QString &key) const {
		return _args.value(key).toObject();
	}

	// Check if field exists
	bool has(const QString &key) const {
		return _args.contains(key);
	}

private:
	const QJsonObject &_args;
};

// ============================================================
// ToolResponse - Standardized tool response builder
// ============================================================

class ToolResponse {
public:
	// Create success response
	static QJsonObject success(const QJsonObject &data = QJsonObject()) {
		QJsonObject response;
		response["success"] = true;
		if (!data.isEmpty()) {
			for (auto it = data.begin(); it != data.end(); ++it) {
				response[it.key()] = it.value();
			}
		}
		return response;
	}

	// Create success response with content array (MCP format)
	static QJsonObject successWithContent(const QString &text) {
		QJsonObject contentItem;
		contentItem["type"] = "text";
		contentItem["text"] = text;

		QJsonArray contentArray;
		contentArray.append(contentItem);

		QJsonObject response;
		response["content"] = contentArray;
		return response;
	}

	// Create error response
	static QJsonObject error(
		ErrorCode code,
		const QString &message,
		const QJsonObject &details = QJsonObject()
	) {
		QJsonObject errorObj;
		errorObj["code"] = errorCodeToString(code);
		errorObj["message"] = message;
		if (!details.isEmpty()) {
			errorObj["details"] = details;
		}

		QJsonObject response;
		response["success"] = false;
		response["error"] = errorObj;
		return response;
	}

	// Create error from ValidationResult
	static QJsonObject fromValidation(const ValidationResult &result) {
		return error(result.errorCode, result.errorMessage, result.errorDetails);
	}

	// Create session not available error
	static QJsonObject sessionNotAvailable() {
		return error(
			ErrorCode::SessionNotAvailable,
			"Session not available. Please wait for Telegram to fully initialize."
		);
	}

	// Create chat not found error
	static QJsonObject chatNotFound(qint64 chatId) {
		QJsonObject details;
		details["chat_id"] = QString::number(chatId);
		return error(ErrorCode::ChatNotFound, "Chat not found", details);
	}

	// Create user not found error
	static QJsonObject userNotFound(qint64 userId) {
		QJsonObject details;
		details["user_id"] = QString::number(userId);
		return error(ErrorCode::UserNotFound, "User not found", details);
	}

	// Create message not found error
	static QJsonObject messageNotFound(qint64 messageId) {
		QJsonObject details;
		details["message_id"] = QString::number(messageId);
		return error(ErrorCode::MessageNotFound, "Message not found", details);
	}
};

// ============================================================
// SqlHelper - Database operation helper with error handling
// ============================================================

class SqlHelper {
public:
	explicit SqlHelper(QSqlDatabase &db) : _db(db) {}

	// Check if database is open
	bool isOpen() const {
		return _db.isOpen();
	}

	// Execute query with error handling
	struct QueryResult {
		bool success = false;
		QString errorMessage;
		int rowsAffected = 0;
	};

	QueryResult execute(QSqlQuery &query, const QString &operationName) {
		QueryResult result;

		if (!_db.isOpen()) {
			result.errorMessage = "Database connection is not open";
			MCP_LOG_ERROR("SQL", QString("%1: %2").arg(operationName, result.errorMessage));
			return result;
		}

		if (!query.exec()) {
			result.errorMessage = query.lastError().text();
			MCP_LOG_ERROR("SQL", QString("%1 failed: %2").arg(operationName, result.errorMessage));
			return result;
		}

		result.success = true;
		result.rowsAffected = query.numRowsAffected();
		MCP_LOG_DEBUG("SQL", QString("%1 succeeded, %2 rows affected")
			.arg(operationName)
			.arg(result.rowsAffected));
		return result;
	}

	// Execute and expect rows
	QueryResult executeAndFetch(QSqlQuery &query, const QString &operationName) {
		QueryResult result = execute(query, operationName);
		if (result.success && !query.next()) {
			result.success = false;
			result.errorMessage = "No matching records found";
		}
		return result;
	}

	// Create error response for SQL operations
	static QJsonObject sqlError(const QString &operation, const QString &errorMessage) {
		QJsonObject details;
		details["operation"] = operation;
		details["sql_error"] = errorMessage;
		return ToolResponse::error(ErrorCode::SqlError, "Database operation failed", details);
	}

private:
	QSqlDatabase &_db;
};

// ============================================================
// SessionGuard - RAII-style session validation
// ============================================================

class SessionGuard {
public:
	explicit SessionGuard(void *session) : _session(session) {}

	bool isValid() const { return _session != nullptr; }

	operator bool() const { return isValid(); }

	QJsonObject errorIfInvalid() const {
		if (!isValid()) {
			return ToolResponse::sessionNotAvailable();
		}
		return QJsonObject();
	}

private:
	void *_session;
};

// Macro for session check at start of tool methods
#define MCP_REQUIRE_SESSION(session) \
	do { \
		if (!(session)) { \
			return ToolResponse::sessionNotAvailable(); \
		} \
	} while (0)

// Macro for validation check
#define MCP_VALIDATE(validation) \
	do { \
		auto _vr = (validation); \
		if (!_vr.isValid) { \
			return ToolResponse::fromValidation(_vr); \
		} \
	} while (0)

} // namespace MCP

// Bot Framework - Base Class Implementation
// This file is part of Telegram Desktop MCP Server.
// Licensed under GPLv3 with OpenSSL exception.

#include "bot_base.h"
#include "chat_archiver.h"
#include "analytics.h"
#include "semantic_search.h"
#include "message_scheduler.h"
#include "audit_logger.h"
#include "rbac.h"

#include <QtCore/QDebug>
#include <QtSql/QSqlDatabase>
#include <QtSql/QSqlQuery>

namespace MCP {

BotBase::BotBase(QObject *parent)
	: QObject(parent) {
}

BotBase::~BotBase() {
	if (_isRunning) {
		onShutdown();
	}
}

// Virtual method implementations (optional overrides)

void BotBase::onMessageEdited(const Message &oldMsg, const Message &newMsg) {
	Q_UNUSED(oldMsg);
	Q_UNUSED(newMsg);
	// Default: do nothing
}

void BotBase::onMessageDeleted(qint64 messageId, qint64 chatId) {
	Q_UNUSED(messageId);
	Q_UNUSED(chatId);
	// Default: do nothing
}

void BotBase::onChatJoined(qint64 chatId) {
	Q_UNUSED(chatId);
	// Default: do nothing
}

void BotBase::onChatLeft(qint64 chatId) {
	Q_UNUSED(chatId);
	// Default: do nothing
}

void BotBase::onUserStatusChanged(qint64 userId, const QString &status) {
	Q_UNUSED(userId);
	Q_UNUSED(status);
	// Default: do nothing
}

QJsonObject BotBase::defaultConfig() const {
	return QJsonObject();
}

// Configuration

void BotBase::setConfig(const QJsonObject &config) {
	_config = config;
	Q_EMIT configChanged();
}

void BotBase::setEnabled(bool enabled) {
	if (_isEnabled != enabled) {
		_isEnabled = enabled;
		Q_EMIT stateChanged();

		auto botInfo = info();
		if (_auditLogger) {
			_auditLogger->logSystemEvent(
				enabled ? "bot_enabled" : "bot_disabled",
				QString("Bot %1 %2").arg(botInfo.id, enabled ? "enabled" : "disabled")
			);
		}
	}
}

// Permissions

bool BotBase::hasPermission(const QString &permission) const {
	if (!_rbac) {
		// If no RBAC is configured, allow all operations
		return true;
	}

	// Map permission strings to RBAC Permission enum
	static const QHash<QString, Permission> permissionMap = {
		{ Permissions::READ_MESSAGES, Permission::ReadMessages },
		{ Permissions::READ_CHATS, Permission::ReadChats },
		{ Permissions::READ_USERS, Permission::ReadUsers },
		{ Permissions::READ_HISTORY, Permission::ReadMessages },
		{ Permissions::READ_ANALYTICS, Permission::ReadAnalytics },
		{ Permissions::READ_EPHEMERAL, Permission::ReadMessages },
		{ Permissions::SEND_MESSAGES, Permission::WriteMessages },
		{ Permissions::EDIT_MESSAGES, Permission::EditMessages },
		{ Permissions::DELETE_MESSAGES, Permission::DeleteMessages },
		{ Permissions::PIN_MESSAGES, Permission::PinMessages },
		{ Permissions::FORWARD_MESSAGES, Permission::ForwardMessages },
		{ Permissions::ADD_REACTIONS, Permission::WriteMessages },
		{ Permissions::MANAGE_CHATS, Permission::ManageChats },
		{ Permissions::MANAGE_USERS, Permission::ManageUsers },
		{ Permissions::MANAGE_BOTS, Permission::ManageSystem },
		{ Permissions::ACCESS_AUDIT_LOG, Permission::ViewAuditLog },
		{ Permissions::CAPTURE_EPHEMERAL, Permission::WriteArchive },
		{ Permissions::EXPORT_DATA, Permission::ExportArchive },
		{ Permissions::EXTERNAL_API, Permission::ManageSystem },
	};

	auto it = permissionMap.constFind(permission);
	if (it == permissionMap.constEnd()) {
		// Unknown permission string — deny by default
		return false;
	}

	auto botInfo = info();
	auto result = _rbac->checkPermission(botInfo.id, it.value());
	return result.granted;
}

void BotBase::addRequiredPermission(const QString &permission) {
	if (!_requiredPermissions.contains(permission)) {
		_requiredPermissions.append(permission);
	}
}

// Utility methods

void BotBase::sendMessage(qint64 chatId, const QString &text) {
	if (!hasPermission(Permissions::SEND_MESSAGES)) {
		logError("No permission to send messages");
		Q_EMIT errorOccurred("Permission denied: send_messages");
		return;
	}

	logInfo(QString("Sending message to chat %1: %2").arg(chatId).arg(text));

	// Emit signal so the MCP server (which holds the session) can send via API
	Q_EMIT messagePosted(chatId, text);

	if (_auditLogger) {
		_auditLogger->logTelegramOp("send_message", chatId, 0, QString(), true);
	}
}

void BotBase::editMessage(qint64 chatId, qint64 messageId, const QString &newText) {
	if (!hasPermission(Permissions::EDIT_MESSAGES)) {
		logError("No permission to edit messages");
		Q_EMIT errorOccurred("Permission denied: edit_messages");
		return;
	}

	// Bot framework delegates actual API calls to MCP server tool handlers.
	// Bots should use the MCP tool interface (send_bot_command with action=edit)
	// for real edits. This method serves as permission-checked audit logging.
	logInfo(QString("Editing message %1 in chat %2").arg(messageId).arg(chatId));

	if (_auditLogger) {
		_auditLogger->logTelegramOp("edit_message", chatId, messageId, newText, true);
	}
}

void BotBase::deleteMessage(qint64 chatId, qint64 messageId) {
	if (!hasPermission(Permissions::DELETE_MESSAGES)) {
		logError("No permission to delete messages");
		Q_EMIT errorOccurred("Permission denied: delete_messages");
		return;
	}

	logInfo(QString("Deleting message %1 in chat %2").arg(messageId).arg(chatId));

	if (_auditLogger) {
		_auditLogger->logTelegramOp("delete_message", chatId, messageId, QString(), true);
	}
}

Message BotBase::getMessage(qint64 chatId, qint64 messageId) {
	if (!hasPermission(Permissions::READ_MESSAGES)) {
		logError("No permission to read messages");
		return Message();
	}

	if (!_archiver) {
		logError("Archiver not available");
		return Message();
	}

	// Query archived messages and find by ID
	QJsonArray messagesJson = _archiver->getMessages(chatId, 100, 0);

	for (const auto &msgValue : messagesJson) {
		QJsonObject msgObj = msgValue.toObject();
		qint64 id = msgObj["id"].toVariant().toLongLong();
		if (id == messageId) {
			Message msg;
			msg.id = id;
			msg.chatId = msgObj["chat_id"].toVariant().toLongLong();
			msg.userId = msgObj["user_id"].toVariant().toLongLong();
			msg.username = msgObj["username"].toString();
			msg.text = msgObj["text"].toString();
			msg.timestamp = msgObj["timestamp"].toVariant().toLongLong();
			msg.messageType = msgObj["type"].toString();
			msg.reactionCount = msgObj["reaction_count"].toInt();
			msg.isThreadStart = msgObj["is_thread_start"].toBool();
			msg.threadReplyCount = msgObj["thread_reply_count"].toInt();
			return msg;
		}
	}

	return Message();
}

QVector<Message> BotBase::getMessages(qint64 chatId, int limit) {
	if (!hasPermission(Permissions::READ_MESSAGES)) {
		logError("No permission to read messages");
		return QVector<Message>();
	}

	if (!_archiver) {
		logError("Archiver not available");
		return QVector<Message>();
	}

	// Get messages from archiver
	QJsonArray messagesJson = _archiver->getMessages(chatId, limit, 0);

	QVector<Message> messages;
	for (const auto &msgValue : messagesJson) {
		QJsonObject msgObj = msgValue.toObject();

		Message msg;
		msg.id = msgObj["id"].toVariant().toLongLong();
		msg.chatId = msgObj["chat_id"].toVariant().toLongLong();
		msg.userId = msgObj["user_id"].toVariant().toLongLong();
		msg.username = msgObj["username"].toString();
		msg.text = msgObj["text"].toString();
		msg.timestamp = msgObj["timestamp"].toVariant().toLongLong();
		msg.messageType = msgObj["type"].toString();
		msg.reactionCount = msgObj["reaction_count"].toInt();
		msg.isThreadStart = msgObj["is_thread_start"].toBool();
		msg.threadReplyCount = msgObj["thread_reply_count"].toInt();

		messages.append(msg);
	}

	return messages;
}

// Logging

void BotBase::logInfo(const QString &message) {
	auto botInfo = info();
	QString logMsg = QString("[Bot:%1] %2").arg(botInfo.id, message);
	qInfo().noquote() << logMsg;

	if (_auditLogger) {
		_auditLogger->logSystemEvent("bot_info", logMsg);
	}
}

void BotBase::logWarning(const QString &message) {
	auto botInfo = info();
	QString logMsg = QString("[Bot:%1] WARNING: %2").arg(botInfo.id, message);
	qWarning().noquote() << logMsg;

	if (_auditLogger) {
		_auditLogger->logSystemEvent("bot_warning", logMsg);
	}
}

void BotBase::logError(const QString &message) {
	auto botInfo = info();
	QString logMsg = QString("[Bot:%1] ERROR: %2").arg(botInfo.id, message);
	qCritical().noquote() << logMsg;

	if (_auditLogger) {
		_auditLogger->logError("bot_error", logMsg);
	}
}

// State management

void BotBase::saveState(const QString &key, const QVariant &value) {
	_state[key] = value;
	Q_EMIT stateChanged();

	// Persist to database via archiver
	if (_archiver) {
		QSqlDatabase db = _archiver->database();
		if (db.isOpen()) {
			QSqlQuery query(db);
			query.prepare(R"(
				INSERT OR REPLACE INTO bot_state (bot_id, state_key, state_value)
				VALUES (:bot_id, :key, :value)
			)");
			auto botInfo = info();
			query.bindValue(":bot_id", botInfo.id);
			query.bindValue(":key", key);
			query.bindValue(":value", value.toString());
			query.exec();
		}
	}
}

QVariant BotBase::loadState(const QString &key, const QVariant &defaultValue) {
	if (_state.contains(key)) {
		return _state.value(key);
	}

	// Try loading from database
	if (_archiver) {
		QSqlDatabase db = _archiver->database();
		if (db.isOpen()) {
			QSqlQuery query(db);
			query.prepare("SELECT state_value FROM bot_state WHERE bot_id = :bot_id AND state_key = :key");
			auto botInfo = info();
			query.bindValue(":bot_id", botInfo.id);
			query.bindValue(":key", key);
			if (query.exec() && query.next()) {
				QVariant value = query.value(0);
				_state[key] = value;
				return value;
			}
		}
	}

	return defaultValue;
}

// Internal initialization (called by BotManager)

bool BotBase::internalInitialize(
		ChatArchiver *archiver,
		Analytics *analytics,
		SemanticSearch *semanticSearch,
		MessageScheduler *scheduler,
		AuditLogger *auditLogger,
		RBAC *rbac) {

	_archiver = archiver;
	_analytics = analytics;
	_semanticSearch = semanticSearch;
	_scheduler = scheduler;
	_auditLogger = auditLogger;
	_rbac = rbac;

	// Call bot's initialization
	bool success = onInitialize();

	if (success) {
		_isRunning = true;

		auto botInfo = info();
		logInfo(QString("Bot initialized: %1 v%2").arg(botInfo.name, botInfo.version));

		if (_auditLogger) {
			QJsonObject params;
			params["bot_id"] = botInfo.id;
			params["version"] = botInfo.version;
			_auditLogger->logSystemEvent("bot_initialized", QString(), params);
		}
	} else {
		logError("Bot initialization failed");
	}

	return success;
}

} // namespace MCP

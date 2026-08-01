// Bot Framework - Base Class for All Bots
// This file is part of Telegram Desktop MCP Server.
// Licensed under GPLv3 with OpenSSL exception.

#pragma once

#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtCore/QJsonObject>
#include <QtCore/QJsonArray>
#include <QtCore/QVector>
#include <QtCore/QVariant>
#include <QtCore/QDateTime>
#include <QtCore/QMap>

namespace MCP {

// Forward declarations
class ChatArchiver;
class Analytics;
class SemanticSearch;
class MessageScheduler;
class AuditLogger;
class RBAC;
class BotManager;

// Message structure
struct Message {
	qint64 id;
	qint64 chatId;
	qint64 userId;
	QString username;
	QString text;
	qint64 timestamp;
	QString messageType;  // "text", "photo", "video", etc.
	int reactionCount;
	bool isThreadStart;
	int threadReplyCount;
};

// Bot base class
class BotBase : public QObject {
	Q_OBJECT

public:
	// Bot metadata
	struct BotInfo {
		QString id;              // Unique identifier (e.g., "context_assistant")
		QString name;            // Display name
		QString version;         // Semantic version
		QString description;     // Short description
		QString author;          // Author/organization
		QStringList tags;        // Categorization tags
		bool isPremium;          // Requires paid tier
	};

	// Constructor/Destructor
	explicit BotBase(QObject *parent = nullptr);
	virtual ~BotBase();

	// Pure virtual methods (must be implemented)
	virtual BotInfo info() const = 0;
	virtual bool onInitialize() = 0;
	virtual void onShutdown() = 0;
	virtual void onMessage(const Message &msg) = 0;
	virtual void onCommand(const QString &cmd, const QJsonObject &args) = 0;

	// Optional virtual methods
	virtual void onMessageEdited(const Message &oldMsg, const Message &newMsg);
	virtual void onMessageDeleted(qint64 messageId, qint64 chatId);
	virtual void onChatJoined(qint64 chatId);
	virtual void onChatLeft(qint64 chatId);
	virtual void onUserStatusChanged(qint64 userId, const QString &status);

	// Configuration
	virtual QJsonObject defaultConfig() const;
	QJsonObject config() const { return _config; }
	void setConfig(const QJsonObject &config);

	// State management
	bool isRunning() const { return _isRunning; }
	bool isEnabled() const { return _isEnabled; }
	void setEnabled(bool enabled);

	// Permissions
	QStringList requiredPermissions() const { return _requiredPermissions; }
	bool hasPermission(const QString &permission) const;

protected:
	// Access to MCP services
	ChatArchiver *archiver() const { return _archiver; }
	Analytics *analytics() const { return _analytics; }
	SemanticSearch *semanticSearch() const { return _semanticSearch; }
	MessageScheduler *scheduler() const { return _scheduler; }
	AuditLogger *auditLogger() const { return _auditLogger; }
	RBAC *rbac() const { return _rbac; }

	// Utility methods
	void sendMessage(qint64 chatId, const QString &text);
	void editMessage(qint64 chatId, qint64 messageId, const QString &newText);
	void deleteMessage(qint64 chatId, qint64 messageId);
	Message getMessage(qint64 chatId, qint64 messageId);
	QVector<Message> getMessages(qint64 chatId, int limit);

	// Logging
	void logInfo(const QString &message);
	void logWarning(const QString &message);
	void logError(const QString &message);

	// Configuration storage
	void saveState(const QString &key, const QVariant &value);
	QVariant loadState(const QString &key, const QVariant &defaultValue = QVariant());

	// Required permissions
	void addRequiredPermission(const QString &permission);

Q_SIGNALS:
	void configChanged();
	void stateChanged();
	void errorOccurred(const QString &error);
	void messagePosted(qint64 chatId, const QString &text);

private:
	friend class BotManager;

	// Internal initialization
	bool internalInitialize(
		ChatArchiver *archiver,
		Analytics *analytics,
		SemanticSearch *semanticSearch,
		MessageScheduler *scheduler,
		AuditLogger *auditLogger,
		RBAC *rbac
	);

	// Services (injected by BotManager)
	ChatArchiver *_archiver = nullptr;
	Analytics *_analytics = nullptr;
	SemanticSearch *_semanticSearch = nullptr;
	MessageScheduler *_scheduler = nullptr;
	AuditLogger *_auditLogger = nullptr;
	RBAC *_rbac = nullptr;

	// State
	bool _isRunning = false;
	bool _isEnabled = true;
	QJsonObject _config;
	QStringList _requiredPermissions;
	QMap<QString, QVariant> _state;
};

// Permission constants
namespace Permissions {
	// Read permissions
	const QString READ_MESSAGES = "read:messages";
	const QString READ_CHATS = "read:chats";
	const QString READ_USERS = "read:users";
	const QString READ_HISTORY = "read:history";
	const QString READ_ANALYTICS = "read:analytics";
	const QString READ_EPHEMERAL = "read:ephemeral";

	// Write permissions
	const QString SEND_MESSAGES = "send:messages";
	const QString EDIT_MESSAGES = "edit:messages";
	const QString DELETE_MESSAGES = "delete:messages";
	const QString PIN_MESSAGES = "pin:messages";
	const QString FORWARD_MESSAGES = "forward:messages";
	const QString ADD_REACTIONS = "add:reactions";

	// Administrative
	const QString MANAGE_CHATS = "admin:chats";
	const QString MANAGE_USERS = "admin:users";
	const QString MANAGE_BOTS = "admin:bots";
	const QString ACCESS_AUDIT_LOG = "admin:audit_log";

	// Privacy-sensitive
	const QString CAPTURE_EPHEMERAL = "privacy:capture_ephemeral";
	const QString EXPORT_DATA = "privacy:export_data";
	const QString EXTERNAL_API = "privacy:external_api";
}

} // namespace MCP

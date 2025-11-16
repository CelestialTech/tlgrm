// This file is part of Telegram Desktop MCP Server.
// Licensed under GPLv3 with OpenSSL exception.

#pragma once

#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtCore/QDateTime>
#include <QtCore/QSet>
#include <QtCore/QJsonObject>
#include <QtCore/QJsonArray>
#include <QtSql/QSqlDatabase>
#include <QtCore/QCryptographicHash>

namespace MCP {

// Permission enumeration
enum class Permission {
	// Message permissions
	ReadMessages,
	WriteMessages,
	DeleteMessages,
	EditMessages,
	PinMessages,
	ForwardMessages,

	// Chat permissions
	ReadChats,
	ManageChats,

	// User permissions
	ReadUsers,
	ManageUsers,

	// Archive permissions
	ReadArchive,
	WriteArchive,
	ExportArchive,
	DeleteArchive,

	// Analytics permissions
	ReadAnalytics,

	// System permissions
	ManageScheduler,
	ManageAPIKeys,
	ViewAuditLog,
	ManageSystem,

	// Special
	Admin  // All permissions
};

// Predefined roles
enum class Role {
	Admin,       // Full access
	Developer,   // Write + Read + Manage
	Bot,         // Write + Read
	ReadOnly,    // Read only
	Custom       // Custom permission set
};

// API Key
struct APIKey {
	QString keyHash;  // SHA-256 hash
	QString keyPrefix;  // First 8 chars for identification
	QString name;
	Role role;
	QSet<Permission> customPermissions;
	QDateTime createdAt;
	QDateTime expiresAt;
	QDateTime lastUsedAt;
	bool isRevoked;
	QJsonObject metadata;
};

// Permission check result
struct PermissionCheckResult {
	bool granted;
	QString reason;  // Why denied (if applicable)
	QString userId;
	Role role;
};

// Role-based access control manager
class RBAC : public QObject {
	Q_OBJECT

public:
	explicit RBAC(QObject *parent = nullptr);
	~RBAC();

	// Initialization
	bool start(QSqlDatabase *db);
	void stop();
	[[nodiscard]] bool isRunning() const { return _isRunning; }

	// API Key management
	QString createAPIKey(
		const QString &name,
		Role role,
		const QDateTime &expiresAt = QDateTime(),
		const QSet<Permission> &customPermissions = QSet<Permission>()
	);

	bool revokeAPIKey(const QString &keyHash);
	bool updateAPIKey(const QString &keyHash, const QJsonObject &updates);
	bool extendExpiration(const QString &keyHash, const QDateTime &newExpiration);

	// Key validation
	bool validateAPIKey(const QString &apiKey, QString &keyHash);
	bool isAPIKeyValid(const QString &keyHash) const;
	void recordKeyUsage(const QString &keyHash);

	// Permission checking
	PermissionCheckResult checkPermission(
		const QString &keyHash,
		Permission permission
	);

	PermissionCheckResult checkToolPermission(
		const QString &keyHash,
		const QString &toolName
	);

	bool hasPermission(const QString &keyHash, Permission permission);
	bool hasAnyPermission(const QString &keyHash, const QSet<Permission> &permissions);
	bool hasAllPermissions(const QString &keyHash, const QSet<Permission> &permissions);

	// Role queries
	Role getRole(const QString &keyHash) const;
	QSet<Permission> getPermissions(const QString &keyHash) const;
	QSet<Permission> getRolePermissions(Role role) const;

	// Key queries
	APIKey getAPIKey(const QString &keyHash) const;
	QVector<APIKey> getAllAPIKeys(bool includeRevoked = false) const;
	QVector<APIKey> getActiveAPIKeys() const;
	int getActiveKeyCount() const;

	// Export
	QJsonObject exportAPIKey(const APIKey &key);
	QJsonArray exportAllAPIKeys();

	// Maintenance
	bool purgeExpiredKeys();
	bool purgeRevokedKeys(int daysOld = 30);

	// Tool permission mapping
	static QSet<Permission> getToolPermissions(const QString &toolName);
	static void registerToolPermissions(const QString &toolName, const QSet<Permission> &permissions);

Q_SIGNALS:
	void apiKeyCreated(const QString &keyHash, const QString &name);
	void apiKeyRevoked(const QString &keyHash);
	void permissionDenied(const QString &keyHash, const QString &permission);
	void error(const QString &errorMessage);

private:
	// Database operations
	bool loadAPIKeys();
	bool saveAPIKey(const APIKey &key);
	bool updateAPIKeyInDB(const APIKey &key);
	bool deleteAPIKeyFromDB(const QString &keyHash);

	// Helpers
	QString generateAPIKey() const;
	QString hashAPIKey(const QString &apiKey) const;
	QString getKeyPrefix(const QString &apiKey) const;
	QString roleToString(Role role) const;
	Role stringToRole(const QString &str) const;
	QString permissionToString(Permission permission) const;
	Permission stringToPermission(const QString &str) const;
	QJsonArray permissionsToJson(const QSet<Permission> &permissions) const;
	QSet<Permission> jsonToPermissions(const QJsonArray &json) const;

	// Default role permissions
	QSet<Permission> getDefaultRolePermissions(Role role) const;

	QSqlDatabase *_db = nullptr;
	bool _isRunning = false;
	QMap<QString, APIKey> _apiKeys;  // keyHash -> APIKey

	// Tool permission mappings
	static QMap<QString, QSet<Permission>> _toolPermissions;
};

} // namespace MCP

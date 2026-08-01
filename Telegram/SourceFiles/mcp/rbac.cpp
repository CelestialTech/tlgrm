// This file is part of Telegram Desktop MCP Server.
// Licensed under GPLv3 with OpenSSL exception.

#include "rbac.h"

#include <QtCore/QCryptographicHash>
#include <QtCore/QRandomGenerator>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonArray>
#include <QtSql/QSqlQuery>
#include <QtSql/QSqlError>

namespace MCP {

// Static tool permission mappings
QMap<QString, QSet<Permission>> RBAC::_toolPermissions;

RBAC::RBAC(QObject *parent)
	: QObject(parent) {

	// Initialize default tool permissions
	registerToolPermissions("read_messages", {Permission::ReadMessages});
	registerToolPermissions("send_message", {Permission::WriteMessages});
	registerToolPermissions("delete_message", {Permission::DeleteMessages});
	registerToolPermissions("edit_message", {Permission::EditMessages});
	registerToolPermissions("forward_message", {Permission::ForwardMessages});
	registerToolPermissions("pin_message", {Permission::PinMessages});
	registerToolPermissions("list_chats", {Permission::ReadChats});
	registerToolPermissions("get_chat_info", {Permission::ReadChats});
	registerToolPermissions("archive_chat", {Permission::WriteArchive});
	registerToolPermissions("export_chat", {Permission::ExportArchive});
	registerToolPermissions("get_analytics", {Permission::ReadAnalytics});
	registerToolPermissions("schedule_message", {Permission::ManageScheduler});
	registerToolPermissions("get_audit_log", {Permission::ViewAuditLog});
	registerToolPermissions("manage_api_keys", {Permission::ManageAPIKeys});
}

RBAC::~RBAC() {
	stop();
}

bool RBAC::start(QSqlDatabase *db) {
	if (_isRunning || !db || !db->isOpen()) {
		return false;
	}

	_db = db;

	// Load API keys from database
	if (!loadAPIKeys()) {
		return false;
	}

	_isRunning = true;
	return true;
}

void RBAC::stop() {
	if (!_isRunning) {
		return;
	}

	_apiKeys.clear();
	_db = nullptr;
	_isRunning = false;
}

// Create API key
QString RBAC::createAPIKey(
		const QString &name,
		Role role,
		const QDateTime &expiresAt,
		const QSet<Permission> &customPermissions) {

	// Generate API key
	QString apiKey = generateAPIKey();
	QString keyHash = hashAPIKey(apiKey);
	QString keyPrefix = getKeyPrefix(apiKey);

	APIKey key;
	key.keyHash = keyHash;
	key.keyPrefix = keyPrefix;
	key.name = name;
	key.role = role;
	key.customPermissions = customPermissions;
	key.createdAt = QDateTime::currentDateTime();
	key.expiresAt = expiresAt;
	key.isRevoked = false;

	// Save to database
	if (!saveAPIKey(key)) {
		return QString();
	}

	// Add to memory
	_apiKeys[keyHash] = key;

	Q_EMIT apiKeyCreated(keyHash, name);

	return apiKey;  // Return the full key (only time it's visible)
}

// Revoke API key
bool RBAC::revokeAPIKey(const QString &keyHash) {
	if (!_apiKeys.contains(keyHash)) {
		return false;
	}

	_apiKeys[keyHash].isRevoked = true;
	_apiKeys[keyHash].lastUsedAt = QDateTime::currentDateTime();

	updateAPIKeyInDB(_apiKeys[keyHash]);

	Q_EMIT apiKeyRevoked(keyHash);
	return true;
}

// Update API key
bool RBAC::updateAPIKey(const QString &keyHash, const QJsonObject &updates) {
	if (!_apiKeys.contains(keyHash)) {
		return false;
	}

	APIKey &key = _apiKeys[keyHash];

	if (updates.contains("name")) {
		key.name = updates["name"].toString();
	}

	if (updates.contains("expires_at")) {
		key.expiresAt = QDateTime::fromString(updates["expires_at"].toString(), Qt::ISODate);
	}

	updateAPIKeyInDB(key);
	return true;
}

// Extend expiration
bool RBAC::extendExpiration(const QString &keyHash, const QDateTime &newExpiration) {
	if (!_apiKeys.contains(keyHash)) {
		return false;
	}

	_apiKeys[keyHash].expiresAt = newExpiration;
	updateAPIKeyInDB(_apiKeys[keyHash]);
	return true;
}

// Validate API key
bool RBAC::validateAPIKey(const QString &apiKey, QString &keyHash) {
	QString hash = hashAPIKey(apiKey);

	if (!_apiKeys.contains(hash)) {
		return false;
	}

	const APIKey &key = _apiKeys[hash];

	// Check if revoked
	if (key.isRevoked) {
		return false;
	}

	// Check expiration
	if (key.expiresAt.isValid() && key.expiresAt < QDateTime::currentDateTime()) {
		return false;
	}

	keyHash = hash;
	return true;
}

// Check if valid
bool RBAC::isAPIKeyValid(const QString &keyHash) const {
	if (!_apiKeys.contains(keyHash)) {
		return false;
	}

	const APIKey &key = _apiKeys[keyHash];

	if (key.isRevoked) {
		return false;
	}

	if (key.expiresAt.isValid() && key.expiresAt < QDateTime::currentDateTime()) {
		return false;
	}

	return true;
}

// Record usage
void RBAC::recordKeyUsage(const QString &keyHash) {
	if (_apiKeys.contains(keyHash)) {
		_apiKeys[keyHash].lastUsedAt = QDateTime::currentDateTime();

		// Update in database asynchronously
		updateAPIKeyInDB(_apiKeys[keyHash]);
	}
}

// Check permission
PermissionCheckResult RBAC::checkPermission(
		const QString &keyHash,
		Permission permission) {

	PermissionCheckResult result;
	result.granted = false;
	result.userId = keyHash;

	if (!_apiKeys.contains(keyHash)) {
		result.reason = "API key not found";
		return result;
	}

	const APIKey &key = _apiKeys[keyHash];

	if (!isAPIKeyValid(keyHash)) {
		result.reason = "API key invalid or expired";
		return result;
	}

	result.role = key.role;

	// Get permissions for this role/key
	QSet<Permission> permissions = getPermissions(keyHash);

	// Check if permission is granted
	if (permissions.contains(Permission::Admin) || permissions.contains(permission)) {
		result.granted = true;
		recordKeyUsage(keyHash);
	} else {
		result.reason = "Permission denied: " + permissionToString(permission);
		Q_EMIT permissionDenied(keyHash, permissionToString(permission));
	}

	return result;
}

// Check tool permission
PermissionCheckResult RBAC::checkToolPermission(
		const QString &keyHash,
		const QString &toolName) {

	auto toolPerms = getToolPermissions(toolName);

	// If no specific permissions required, allow
	if (toolPerms.isEmpty()) {
		PermissionCheckResult result;
		result.granted = true;
		result.userId = keyHash;
		return result;
	}

	// Check if user has ANY of the required permissions
	return checkPermission(keyHash, *toolPerms.begin());
}

// Has permission
bool RBAC::hasPermission(const QString &keyHash, Permission permission) {
	return checkPermission(keyHash, permission).granted;
}

// Has any permission
bool RBAC::hasAnyPermission(const QString &keyHash, const QSet<Permission> &permissions) {
	for (const auto &perm : permissions) {
		if (hasPermission(keyHash, perm)) {
			return true;
		}
	}
	return false;
}

// Has all permissions
bool RBAC::hasAllPermissions(const QString &keyHash, const QSet<Permission> &permissions) {
	for (const auto &perm : permissions) {
		if (!hasPermission(keyHash, perm)) {
			return false;
		}
	}
	return true;
}

// Get role
Role RBAC::getRole(const QString &keyHash) const {
	if (_apiKeys.contains(keyHash)) {
		return _apiKeys[keyHash].role;
	}
	return Role::ReadOnly;
}

// Get permissions
QSet<Permission> RBAC::getPermissions(const QString &keyHash) const {
	if (!_apiKeys.contains(keyHash)) {
		return QSet<Permission>();
	}

	const APIKey &key = _apiKeys[keyHash];

	// If custom permissions are set, use those
	if (!key.customPermissions.isEmpty()) {
		return key.customPermissions;
	}

	// Otherwise use role-based permissions
	return getRolePermissions(key.role);
}

// Get role permissions
QSet<Permission> RBAC::getRolePermissions(Role role) const {
	return getDefaultRolePermissions(role);
}

// Get API key
APIKey RBAC::getAPIKey(const QString &keyHash) const {
	if (_apiKeys.contains(keyHash)) {
		return _apiKeys[keyHash];
	}
	return APIKey();
}

// Get all API keys
QVector<APIKey> RBAC::getAllAPIKeys(bool includeRevoked) const {
	QVector<APIKey> keys;
	for (const auto &key : _apiKeys) {
		if (includeRevoked || !key.isRevoked) {
			keys.append(key);
		}
	}
	return keys;
}

// Get active API keys
QVector<APIKey> RBAC::getActiveAPIKeys() const {
	QVector<APIKey> keys;
	for (const auto &key : _apiKeys) {
		if (isAPIKeyValid(key.keyHash)) {
			keys.append(key);
		}
	}
	return keys;
}

// Get active key count
int RBAC::getActiveKeyCount() const {
	return getActiveAPIKeys().size();
}

// Export API key
QJsonObject RBAC::exportAPIKey(const APIKey &key) {
	QJsonObject json;
	json["key_hash"] = key.keyHash;
	json["key_prefix"] = key.keyPrefix;
	json["name"] = key.name;
	json["role"] = roleToString(key.role);
	json["created_at"] = key.createdAt.toString(Qt::ISODate);

	if (key.expiresAt.isValid()) {
		json["expires_at"] = key.expiresAt.toString(Qt::ISODate);
	}

	if (key.lastUsedAt.isValid()) {
		json["last_used_at"] = key.lastUsedAt.toString(Qt::ISODate);
	}

	json["is_revoked"] = key.isRevoked;

	// Custom permissions
	if (!key.customPermissions.isEmpty()) {
		json["custom_permissions"] = permissionsToJson(key.customPermissions);
	}

	return json;
}

// Export all API keys
QJsonArray RBAC::exportAllAPIKeys() {
	QJsonArray array;
	for (const auto &key : _apiKeys) {
		array.append(exportAPIKey(key));
	}
	return array;
}

// Maintenance
bool RBAC::purgeExpiredKeys() {
	if (!_db || !_db->isOpen()) {
		return false;
	}

	qint64 now = QDateTime::currentSecsSinceEpoch();

	QSqlQuery query(*_db);
	query.prepare("DELETE FROM api_keys WHERE expires_at < :now AND expires_at IS NOT NULL");
	query.bindValue(":now", now);

	bool success = query.exec();

	if (success) {
		// Reload keys
		loadAPIKeys();
	}

	return success;
}

bool RBAC::purgeRevokedKeys(int daysOld) {
	if (!_db || !_db->isOpen()) {
		return false;
	}

	qint64 cutoff = QDateTime::currentDateTime().addDays(-daysOld).toSecsSinceEpoch();

	QSqlQuery query(*_db);
	query.prepare("DELETE FROM api_keys WHERE is_revoked = 1 AND last_used_at < :cutoff");
	query.bindValue(":cutoff", cutoff);

	bool success = query.exec();

	if (success) {
		// Reload keys
		loadAPIKeys();
	}

	return success;
}

// Get tool permissions
QSet<Permission> RBAC::getToolPermissions(const QString &toolName) {
	return _toolPermissions.value(toolName, QSet<Permission>());
}

// Register tool permissions
void RBAC::registerToolPermissions(const QString &toolName, const QSet<Permission> &permissions) {
	_toolPermissions[toolName] = permissions;
}

// Private helpers
QString RBAC::generateAPIKey() const {
	// Generate 32 random bytes
	QByteArray randomBytes(32, Qt::Uninitialized);
	for (int i = 0; i < 32; ++i) {
		randomBytes[i] = static_cast<char>(QRandomGenerator::global()->generate());
	}

	// Convert to hex and prepend prefix
	return "tmcp_" + randomBytes.toHex();
}

QString RBAC::hashAPIKey(const QString &apiKey) const {
	return QString(QCryptographicHash::hash(apiKey.toUtf8(), QCryptographicHash::Sha256).toHex());
}

QString RBAC::getKeyPrefix(const QString &apiKey) const {
	return apiKey.left(8);  // First 8 characters
}

QString RBAC::roleToString(Role role) const {
	switch (role) {
	case Role::Admin: return "admin";
	case Role::Developer: return "developer";
	case Role::Bot: return "bot";
	case Role::ReadOnly: return "readonly";
	case Role::Custom: return "custom";
	default: return "readonly";
	}
}

Role RBAC::stringToRole(const QString &str) const {
	if (str == "admin") return Role::Admin;
	if (str == "developer") return Role::Developer;
	if (str == "bot") return Role::Bot;
	if (str == "readonly") return Role::ReadOnly;
	if (str == "custom") return Role::Custom;
	return Role::ReadOnly;
}

QString RBAC::permissionToString(Permission permission) const {
	switch (permission) {
	case Permission::ReadMessages: return "read_messages";
	case Permission::WriteMessages: return "write_messages";
	case Permission::DeleteMessages: return "delete_messages";
	case Permission::EditMessages: return "edit_messages";
	case Permission::PinMessages: return "pin_messages";
	case Permission::ForwardMessages: return "forward_messages";
	case Permission::ReadChats: return "read_chats";
	case Permission::ManageChats: return "manage_chats";
	case Permission::ReadUsers: return "read_users";
	case Permission::ManageUsers: return "manage_users";
	case Permission::ReadArchive: return "read_archive";
	case Permission::WriteArchive: return "write_archive";
	case Permission::ExportArchive: return "export_archive";
	case Permission::DeleteArchive: return "delete_archive";
	case Permission::ReadAnalytics: return "read_analytics";
	case Permission::ManageScheduler: return "manage_scheduler";
	case Permission::ManageAPIKeys: return "manage_api_keys";
	case Permission::ViewAuditLog: return "view_audit_log";
	case Permission::ManageSystem: return "manage_system";
	case Permission::Admin: return "admin";
	default: return "unknown";
	}
}

Permission RBAC::stringToPermission(const QString &str) const {
	if (str == "read_messages") return Permission::ReadMessages;
	if (str == "write_messages") return Permission::WriteMessages;
	if (str == "delete_messages") return Permission::DeleteMessages;
	if (str == "edit_messages") return Permission::EditMessages;
	if (str == "pin_messages") return Permission::PinMessages;
	if (str == "forward_messages") return Permission::ForwardMessages;
	if (str == "read_chats") return Permission::ReadChats;
	if (str == "manage_chats") return Permission::ManageChats;
	if (str == "read_users") return Permission::ReadUsers;
	if (str == "manage_users") return Permission::ManageUsers;
	if (str == "read_archive") return Permission::ReadArchive;
	if (str == "write_archive") return Permission::WriteArchive;
	if (str == "export_archive") return Permission::ExportArchive;
	if (str == "delete_archive") return Permission::DeleteArchive;
	if (str == "read_analytics") return Permission::ReadAnalytics;
	if (str == "manage_scheduler") return Permission::ManageScheduler;
	if (str == "manage_api_keys") return Permission::ManageAPIKeys;
	if (str == "view_audit_log") return Permission::ViewAuditLog;
	if (str == "manage_system") return Permission::ManageSystem;
	if (str == "admin") return Permission::Admin;
	return Permission::ReadMessages;
}

QJsonArray RBAC::permissionsToJson(const QSet<Permission> &permissions) const {
	QJsonArray array;
	for (const auto &perm : permissions) {
		array.append(permissionToString(perm));
	}
	return array;
}

QSet<Permission> RBAC::jsonToPermissions(const QJsonArray &json) const {
	QSet<Permission> permissions;
	for (const auto &value : json) {
		permissions.insert(stringToPermission(value.toString()));
	}
	return permissions;
}

QSet<Permission> RBAC::getDefaultRolePermissions(Role role) const {
	switch (role) {
	case Role::Admin:
		return {Permission::Admin};  // All permissions

	case Role::Developer:
		return {
			Permission::ReadMessages, Permission::WriteMessages, Permission::DeleteMessages,
			Permission::EditMessages, Permission::PinMessages, Permission::ForwardMessages,
			Permission::ReadChats, Permission::ManageChats,
			Permission::ReadUsers, Permission::ManageUsers,
			Permission::ReadArchive, Permission::WriteArchive, Permission::ExportArchive,
			Permission::ReadAnalytics, Permission::ManageScheduler,
			Permission::ViewAuditLog
		};

	case Role::Bot:
		return {
			Permission::ReadMessages, Permission::WriteMessages,
			Permission::ReadChats, Permission::ReadUsers,
			Permission::ReadArchive
		};

	case Role::ReadOnly:
		return {
			Permission::ReadMessages, Permission::ReadChats,
			Permission::ReadUsers, Permission::ReadArchive, Permission::ReadAnalytics
		};

	default:
		return {};
	}
}

// Database operations
bool RBAC::loadAPIKeys() {
	if (!_db || !_db->isOpen()) {
		return false;
	}

	_apiKeys.clear();

	QSqlQuery query(*_db);
	query.exec("SELECT * FROM api_keys");

	while (query.next()) {
		APIKey key;
		key.keyHash = query.value("api_key_hash").toString();
		key.keyPrefix = query.value("api_key_prefix").toString();
		key.name = query.value("name").toString();
		key.role = stringToRole(query.value("role").toString());
		key.createdAt = QDateTime::fromSecsSinceEpoch(query.value("created_at").toLongLong());

		if (!query.value("expires_at").isNull()) {
			key.expiresAt = QDateTime::fromSecsSinceEpoch(query.value("expires_at").toLongLong());
		}

		if (!query.value("last_used_at").isNull()) {
			key.lastUsedAt = QDateTime::fromSecsSinceEpoch(query.value("last_used_at").toLongLong());
		}

		key.isRevoked = query.value("is_revoked").toBool();

		// Parse custom permissions
		QString permsJson = query.value("permissions").toString();
		if (!permsJson.isEmpty()) {
			QJsonArray permsArray = QJsonDocument::fromJson(permsJson.toUtf8()).array();
			key.customPermissions = jsonToPermissions(permsArray);
		}

		_apiKeys[key.keyHash] = key;
	}

	return true;
}

bool RBAC::saveAPIKey(const APIKey &key) {
	if (!_db || !_db->isOpen()) {
		return false;
	}

	QSqlQuery query(*_db);
	query.prepare(R"(
		INSERT INTO api_keys (
			api_key_hash, api_key_prefix, name, role, permissions,
			created_at, expires_at, is_revoked
		) VALUES (
			:key_hash, :key_prefix, :name, :role, :permissions,
			:created_at, :expires_at, :is_revoked
		)
	)");

	query.bindValue(":key_hash", key.keyHash);
	query.bindValue(":key_prefix", key.keyPrefix);
	query.bindValue(":name", key.name);
	query.bindValue(":role", roleToString(key.role));

	if (!key.customPermissions.isEmpty()) {
		QJsonDocument doc(permissionsToJson(key.customPermissions));
		query.bindValue(":permissions", QString(doc.toJson(QJsonDocument::Compact)));
	} else {
		query.bindValue(":permissions", QVariant());
	}

	query.bindValue(":created_at", key.createdAt.toSecsSinceEpoch());
	query.bindValue(":expires_at", key.expiresAt.isValid() ? key.expiresAt.toSecsSinceEpoch() : QVariant());
	query.bindValue(":is_revoked", key.isRevoked);

	return query.exec();
}

bool RBAC::updateAPIKeyInDB(const APIKey &key) {
	if (!_db || !_db->isOpen()) {
		return false;
	}

	QSqlQuery query(*_db);
	query.prepare(R"(
		UPDATE api_keys SET
			name = :name,
			expires_at = :expires_at,
			last_used_at = :last_used_at,
			is_revoked = :is_revoked
		WHERE api_key_hash = :key_hash
	)");

	query.bindValue(":name", key.name);
	query.bindValue(":expires_at", key.expiresAt.isValid() ? key.expiresAt.toSecsSinceEpoch() : QVariant());
	query.bindValue(":last_used_at", key.lastUsedAt.isValid() ? key.lastUsedAt.toSecsSinceEpoch() : QVariant());
	query.bindValue(":is_revoked", key.isRevoked);
	query.bindValue(":key_hash", key.keyHash);

	return query.exec();
}

bool RBAC::deleteAPIKeyFromDB(const QString &keyHash) {
	if (!_db || !_db->isOpen()) {
		return false;
	}

	QSqlQuery query(*_db);
	query.prepare("DELETE FROM api_keys WHERE api_key_hash = :key_hash");
	query.bindValue(":key_hash", keyHash);

	return query.exec();
}

} // namespace MCP

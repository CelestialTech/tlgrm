// This file is part of Telegram Desktop MCP Server.
// Licensed under GPLv3 with OpenSSL exception.

#include "chat_archiver.h"
#include "data/data_session.h"
#include "data/data_peer.h"
#include "data/data_user.h"
#include "data/data_chat.h"
#include "data/data_channel.h"
#include "data/data_messages.h"
#include "data/data_document.h"
#include "data/data_chat_filters.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/history_item_components.h"
#include "history/view/history_view_element.h"

#include <QtCore/QFile>
#include <QtCore/QDir>
#include <QtCore/QFileInfo>
#include <QtCore/QTextStream>
#include <QtCore/QCryptographicHash>
#include <QtCore/QTimer>
#include <QtCore/QJsonDocument>
#include <QtSql/QSqlError>
#include <QtSql/QSqlRecord>

namespace MCP {

// ===================================
// ChatArchiver Implementation
// ===================================

ChatArchiver::ChatArchiver(QObject *parent)
	: QObject(parent)
	, _checkTimer(new QTimer(this)) {

	connect(_checkTimer, &QTimer::timeout, this, &ChatArchiver::checkForNewMessages);
}

ChatArchiver::~ChatArchiver() {
	stop();
}

bool ChatArchiver::start(const QString &databasePath) {
	if (_isRunning) {
		return true;
	}

	_databasePath = databasePath;

	// Initialize SQLite database
	_db = QSqlDatabase::addDatabase("QSQLITE", "telegram_mcp_archive");
	_db.setDatabaseName(databasePath);

	if (!_db.open()) {
		Q_EMIT error("Failed to open database: " + _db.lastError().text());
		return false;
	}

	// Enable performance optimizations
	QSqlQuery pragmas(_db);
	pragmas.exec("PRAGMA journal_mode = WAL");  // Write-Ahead Logging for better concurrency
	pragmas.exec("PRAGMA synchronous = NORMAL");  // Balance safety/performance
	pragmas.exec("PRAGMA cache_size = -64000");  // 64MB cache
	pragmas.exec("PRAGMA temp_store = MEMORY");  // Use memory for temp tables
	pragmas.exec("PRAGMA mmap_size = 268435456");  // 256MB memory-mapped I/O

	// Initialize schema
	if (!initializeDatabase()) {
		Q_EMIT error("Failed to initialize database schema");
		_db.close();
		return false;
	}

	// Start periodic message checking (every 5 seconds)
	_checkTimer->start(5000);

	_isRunning = true;
	updateStats();

	return true;
}

void ChatArchiver::stop() {
	if (!_isRunning) {
		return;
	}

	_checkTimer->stop();
	_db.close();
	_isRunning = false;
}

bool ChatArchiver::initializeDatabase() {
	// Check if schema exists
	QSqlQuery query(_db);
	query.exec("SELECT name FROM sqlite_master WHERE type='table' AND name='messages'");

	if (query.next()) {
		// Schema already exists
		return true;
	}

	// Create essential tables directly (embedded schema)
	const QStringList statements = {
		// Main message archive table
		R"(CREATE TABLE IF NOT EXISTS messages (
			id INTEGER PRIMARY KEY AUTOINCREMENT,
			message_id INTEGER NOT NULL,
			chat_id INTEGER NOT NULL,
			user_id INTEGER,
			username TEXT,
			first_name TEXT,
			last_name TEXT,
			content TEXT,
			timestamp INTEGER NOT NULL,
			date TEXT,
			message_type TEXT DEFAULT 'text',
			reply_to_message_id INTEGER,
			forward_from_chat_id INTEGER,
			forward_from_message_id INTEGER,
			edit_date INTEGER,
			media_path TEXT,
			media_url TEXT,
			media_size INTEGER,
			media_mime_type TEXT,
			has_media BOOLEAN DEFAULT 0,
			is_forwarded BOOLEAN DEFAULT 0,
			is_reply BOOLEAN DEFAULT 0,
			metadata TEXT,
			created_at INTEGER DEFAULT (strftime('%s', 'now')),
			UNIQUE(chat_id, message_id)
		))",

		// Indexes for messages
		R"(CREATE INDEX IF NOT EXISTS idx_messages_chat_timestamp ON messages(chat_id, timestamp DESC))",
		R"(CREATE INDEX IF NOT EXISTS idx_messages_user ON messages(user_id, timestamp DESC))",

		// Ephemeral messages table
		R"(CREATE TABLE IF NOT EXISTS ephemeral_messages (
			id INTEGER PRIMARY KEY AUTOINCREMENT,
			message_id INTEGER NOT NULL,
			chat_id INTEGER NOT NULL,
			user_id INTEGER,
			username TEXT,
			ephemeral_type TEXT NOT NULL,
			ttl_seconds INTEGER,
			content TEXT,
			media_type TEXT,
			media_path TEXT,
			captured_at INTEGER NOT NULL,
			scheduled_deletion INTEGER,
			views_count INTEGER DEFAULT 0,
			metadata TEXT,
			UNIQUE(chat_id, message_id)
		))",

		// Chats metadata table
		R"(CREATE TABLE IF NOT EXISTS chats (
			chat_id INTEGER PRIMARY KEY,
			chat_type TEXT NOT NULL,
			title TEXT,
			username TEXT,
			description TEXT,
			member_count INTEGER,
			photo_path TEXT,
			is_archived BOOLEAN DEFAULT 0,
			first_seen INTEGER,
			last_updated INTEGER,
			metadata TEXT
		))",

		// Chat activity summary
		R"(CREATE TABLE IF NOT EXISTS chat_activity_summary (
			chat_id INTEGER PRIMARY KEY,
			total_messages INTEGER DEFAULT 0,
			unique_users INTEGER DEFAULT 0,
			messages_per_day REAL DEFAULT 0,
			peak_hour INTEGER,
			first_message_date INTEGER,
			last_message_date INTEGER,
			activity_trend TEXT,
			updated_at INTEGER
		))",

		// Daily stats table
		R"(CREATE TABLE IF NOT EXISTS message_stats_daily (
			id INTEGER PRIMARY KEY AUTOINCREMENT,
			date TEXT NOT NULL,
			chat_id INTEGER NOT NULL,
			message_count INTEGER DEFAULT 0,
			unique_users INTEGER DEFAULT 0,
			avg_message_length REAL DEFAULT 0,
			total_words INTEGER DEFAULT 0,
			media_count INTEGER DEFAULT 0,
			UNIQUE(date, chat_id)
		))",

		// User activity summary
		R"(CREATE TABLE IF NOT EXISTS user_activity_summary (
			id INTEGER PRIMARY KEY AUTOINCREMENT,
			user_id INTEGER NOT NULL,
			chat_id INTEGER NOT NULL,
			message_count INTEGER DEFAULT 0,
			word_count INTEGER DEFAULT 0,
			avg_message_length REAL DEFAULT 0,
			most_active_hour INTEGER,
			first_message_date INTEGER,
			last_message_date INTEGER,
			days_active INTEGER DEFAULT 0,
			updated_at INTEGER,
			UNIQUE(user_id, chat_id)
		))",

		// Schema version
		R"(CREATE TABLE IF NOT EXISTS schema_version (
			version INTEGER PRIMARY KEY,
			applied_at INTEGER DEFAULT (strftime('%s', 'now'))
		))",

		// Trigger to update chat stats
		R"(CREATE TRIGGER IF NOT EXISTS update_chat_stats_on_insert
		AFTER INSERT ON messages
		BEGIN
			INSERT OR REPLACE INTO chat_activity_summary (
				chat_id, total_messages, unique_users,
				first_message_date, last_message_date, updated_at
			)
			SELECT NEW.chat_id, COUNT(*), COUNT(DISTINCT user_id),
				MIN(timestamp), MAX(timestamp), strftime('%s', 'now')
			FROM messages WHERE chat_id = NEW.chat_id;
		END)",

		// Set schema version
		R"(INSERT OR REPLACE INTO schema_version (version) VALUES (2))"
	};

	for (const QString &statement : statements) {
		if (!query.exec(statement)) {
			qWarning() << "SQL Error:" << query.lastError().text();
			qWarning() << "Statement:" << statement.left(100);
			return false;
		}
	}

	return true;
}

bool ChatArchiver::executeSQLFile(const QString &filePath) {
	QFile file(filePath);
	if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
		return false;
	}

	QString sql = QTextStream(&file).readAll();
	file.close();

	// Split by semicolons and execute each statement
	QStringList statements = sql.split(';', Qt::SkipEmptyParts);

	QSqlQuery query(_db);
	for (const QString &statement : statements) {
		QString trimmed = statement.trimmed();
		if (trimmed.isEmpty() || trimmed.startsWith("--")) {
			continue;
		}

		if (!query.exec(trimmed)) {
			qWarning() << "SQL Error:" << query.lastError().text();
			qWarning() << "Statement:" << trimmed;
			return false;
		}
	}

	return true;
}

bool ChatArchiver::archiveMessage(HistoryItem *message) {
	if (!message || !_isRunning) {
		return false;
	}

	// Extract message data
	const auto item = message;
	const auto history = item->history();
	const auto peer = history->peer;
	const qint64 chatId = peer->id.value;
	const qint64 messageId = item->id.bare;
	const auto from = item->from();
	const qint64 userId = from ? from->id.value : 0;

	// Get message content
	QString content = item->originalText().text;
	const qint64 timestamp = item->date();  // TimeId is already a Unix timestamp (int32)
	QString date = QDateTime::fromSecsSinceEpoch(timestamp).toString(Qt::ISODate);

	// Detect message type
	MessageType msgType = detectMessageType(message);
	QString messageTypeStr = messageTypeToString(msgType);

	// Check for reply
	qint64 replyToId = 0;
	if (const auto replyTo = item->replyToId()) {
		replyToId = replyTo.bare;
	}

	// Check for forward
	qint64 forwardChatId = 0;
	qint64 forwardMessageId = 0;
	bool isForwarded = item->Has<HistoryMessageForwarded>();

	// Media handling
	QString mediaPath;
	QString mediaUrl;
	qint64 mediaSize = 0;
	QString mediaMimeType;
	bool hasMedia = false;

	if (item->media()) {
		hasMedia = true;
		mediaPath = downloadMedia(message);
		// Media URL would be file_id in Telegram
		// mediaUrl = ... (extract from media)
	}

	// Prepare SQL insert
	QSqlQuery query(_db);
	query.prepare(R"(
		INSERT OR REPLACE INTO messages (
			message_id, chat_id, user_id, username, first_name, last_name,
			content, timestamp, date, message_type,
			reply_to_message_id, forward_from_chat_id, forward_from_message_id,
			media_path, media_url, media_size, media_mime_type,
			has_media, is_forwarded, is_reply
		) VALUES (
			:message_id, :chat_id, :user_id, :username, :first_name, :last_name,
			:content, :timestamp, :date, :message_type,
			:reply_to_id, :fwd_chat_id, :fwd_msg_id,
			:media_path, :media_url, :media_size, :media_mime_type,
			:has_media, :is_forwarded, :is_reply
		)
	)");

	query.bindValue(":message_id", messageId);
	query.bindValue(":chat_id", chatId);
	query.bindValue(":user_id", userId);

	if (from) {
		if (const auto user = from->asUser()) {
			query.bindValue(":username", user->username());
			query.bindValue(":first_name", user->firstName);
			query.bindValue(":last_name", user->lastName);
		} else {
			query.bindValue(":username", from->name());
			query.bindValue(":first_name", QVariant());
			query.bindValue(":last_name", QVariant());
		}
	} else {
		query.bindValue(":username", QVariant());
		query.bindValue(":first_name", QVariant());
		query.bindValue(":last_name", QVariant());
	}

	query.bindValue(":content", content);
	query.bindValue(":timestamp", timestamp);
	query.bindValue(":date", date);
	query.bindValue(":message_type", messageTypeStr);
	query.bindValue(":reply_to_id", replyToId > 0 ? replyToId : QVariant());
	query.bindValue(":fwd_chat_id", forwardChatId > 0 ? forwardChatId : QVariant());
	query.bindValue(":fwd_msg_id", forwardMessageId > 0 ? forwardMessageId : QVariant());
	query.bindValue(":media_path", mediaPath.isEmpty() ? QVariant() : mediaPath);
	query.bindValue(":media_url", mediaUrl.isEmpty() ? QVariant() : mediaUrl);
	query.bindValue(":media_size", mediaSize > 0 ? mediaSize : QVariant());
	query.bindValue(":media_mime_type", mediaMimeType.isEmpty() ? QVariant() : mediaMimeType);
	query.bindValue(":has_media", hasMedia);
	query.bindValue(":is_forwarded", isForwarded);
	query.bindValue(":is_reply", replyToId > 0);

	if (!query.exec()) {
		qWarning() << "Failed to archive message:" << query.lastError().text();
		return false;
	}

	// Update analytics
	updateDailyStats(chatId, QDateTime::fromSecsSinceEpoch(timestamp).date());
	updateUserActivity(userId, chatId);
	updateChatActivity(chatId);

	Q_EMIT messageArchived(chatId, messageId);
	return true;
}

bool ChatArchiver::archiveChat(qint64 chatId, int messageLimit) {
	if (!_isRunning || !_session) {
		return false;
	}

	auto peer = _session->peer(PeerId(chatId));
	if (!peer) {
		qWarning() << "[ChatArchiver] Peer not found:" << chatId;
		return false;
	}

	auto history = peer->owner().history(peer);
	if (!history) {
		return false;
	}

	int archived = 0;
	int limit = (messageLimit <= 0) ? INT_MAX : messageLimit;

	// Iterate through history blocks in reverse (newest first)
	for (auto blockIt = history->blocks.rbegin();
	     blockIt != history->blocks.rend() && archived < limit;
	     ++blockIt) {
		auto *block = blockIt->get();
		for (auto itemIt = block->messages.rbegin();
		     itemIt != block->messages.rend() && archived < limit;
		     ++itemIt) {
			auto *view = itemIt->get();
			{
			const auto item = view->data();
				if (archiveMessage(item)) {
					archived++;
				}
			}
		}
	}

	if (archived > 0) {
		Q_EMIT chatArchived(chatId, archived);
	}

	qInfo() << "[ChatArchiver] Archived" << archived << "messages from chat" << chatId;
	return archived > 0;
}

bool ChatArchiver::archiveAllChats(int messagesPerChat) {
	if (!_isRunning || !_session) {
		return false;
	}

	int totalArchived = 0;
	auto &chatsList = _session->chatsList()->indexed()->all();

	for (const auto &row : chatsList) {
		if (const auto history = row->history()) {
			qint64 chatId = history->peer->id.value;
			if (archiveChat(chatId, messagesPerChat)) {
				totalArchived++;
			}
		}
	}

	qInfo() << "[ChatArchiver] Archived" << totalArchived << "chats";
	return totalArchived > 0;
}

bool ChatArchiver::archiveEphemeralMessage(
		HistoryItem *message,
		const QString &ephemeralType,
		int ttlSeconds) {

	if (!message || !_isRunning) {
		return false;
	}

	const auto item = message;
	const auto history = item->history();
	const auto peer = history->peer;
	const qint64 chatId = peer->id.value;
	const qint64 messageId = item->id.bare;
	const auto from = item->from();
	const qint64 userId = from ? from->id.value : 0;
	QString username = from ? from->name() : QString();
	QString content = item->originalText().text;
	const qint64 capturedAt = QDateTime::currentSecsSinceEpoch();
	const qint64 scheduledDeletion = capturedAt + ttlSeconds;

	// Download media if present
	QString mediaPath;
	QString mediaType;
	if (item->media()) {
		mediaPath = downloadMedia(message);
		mediaType = messageTypeToString(detectMessageType(message));
	}

	QSqlQuery query(_db);
	query.prepare(R"(
		INSERT OR REPLACE INTO ephemeral_messages (
			message_id, chat_id, user_id, username,
			ephemeral_type, ttl_seconds, content,
			media_type, media_path, captured_at, scheduled_deletion
		) VALUES (
			:message_id, :chat_id, :user_id, :username,
			:ephemeral_type, :ttl_seconds, :content,
			:media_type, :media_path, :captured_at, :scheduled_deletion
		)
	)");

	query.bindValue(":message_id", messageId);
	query.bindValue(":chat_id", chatId);
	query.bindValue(":user_id", userId);
	query.bindValue(":username", username);
	query.bindValue(":ephemeral_type", ephemeralType);
	query.bindValue(":ttl_seconds", ttlSeconds);
	query.bindValue(":content", content);
	query.bindValue(":media_type", mediaType.isEmpty() ? QVariant() : mediaType);
	query.bindValue(":media_path", mediaPath.isEmpty() ? QVariant() : mediaPath);
	query.bindValue(":captured_at", capturedAt);
	query.bindValue(":scheduled_deletion", scheduledDeletion);

	if (!query.exec()) {
		qWarning() << "Failed to archive ephemeral message:" << query.lastError().text();
		return false;
	}

	_stats.ephemeralCaptured++;
	return true;
}

bool ChatArchiver::isEphemeral(HistoryItem *message) const {
	if (!message) {
		return false;
	}

	const auto item = message;

	// Check for self-destruct timer
	if (item->ttlDestroyAt() > 0) {
		return true;
	}

	// Check for view-once media (photos/videos sent with "view once")
	if (const auto media = item->media()) {
		if (media->ttlSeconds() > 0) {
			return true;
		}
	}

	return false;
}

QJsonArray ChatArchiver::getMessages(qint64 chatId, int limit, qint64 beforeTimestamp) {
	QJsonArray result;

	QString sql = "SELECT * FROM messages WHERE chat_id = :chat_id";
	if (beforeTimestamp > 0) {
		sql += " AND timestamp < :before";
	}
	sql += " ORDER BY timestamp DESC LIMIT :limit";

	QSqlQuery query(_db);
	query.prepare(sql);
	query.bindValue(":chat_id", chatId);
	if (beforeTimestamp > 0) {
		query.bindValue(":before", beforeTimestamp);
	}
	query.bindValue(":limit", limit);

	if (!query.exec()) {
		qWarning() << "Query failed:" << query.lastError().text();
		return result;
	}

	while (query.next()) {
		result.append(messageToJson(query));
	}

	return result;
}

QJsonArray ChatArchiver::searchMessages(qint64 chatId, const QString &query, int limit) {
	QJsonArray result;

	QSqlQuery sqlQuery(_db);
	sqlQuery.prepare(R"(
		SELECT * FROM messages
		WHERE chat_id = :chat_id
		AND content LIKE :query
		ORDER BY timestamp DESC
		LIMIT :limit
	)");
	sqlQuery.bindValue(":chat_id", chatId);
	sqlQuery.bindValue(":query", "%" + query + "%");
	sqlQuery.bindValue(":limit", limit);

	if (!sqlQuery.exec()) {
		return result;
	}

	while (sqlQuery.next()) {
		result.append(messageToJson(sqlQuery));
	}

	return result;
}

QJsonObject ChatArchiver::getChatInfo(qint64 chatId) {
	QJsonObject info;

	QSqlQuery query(_db);
	query.prepare("SELECT * FROM chats WHERE chat_id = :chat_id");
	query.bindValue(":chat_id", chatId);

	if (query.exec() && query.next()) {
		info["chat_id"] = query.value("chat_id").toLongLong();
		info["type"] = query.value("chat_type").toString();
		info["title"] = query.value("title").toString();
		info["username"] = query.value("username").toString();
		info["description"] = query.value("description").toString();
		info["member_count"] = query.value("member_count").toInt();
	}

	// Get message count
	query.prepare("SELECT COUNT(*) as count FROM messages WHERE chat_id = :chat_id");
	query.bindValue(":chat_id", chatId);
	if (query.exec() && query.next()) {
		info["message_count"] = query.value("count").toInt();
	}

	return info;
}

QJsonArray ChatArchiver::listArchivedChats() {
	QJsonArray result;

	QSqlQuery query(_db);
	query.exec(R"(
		SELECT
			c.chat_id,
			c.chat_type,
			c.title,
			COUNT(m.id) as message_count,
			MAX(m.timestamp) as last_message
		FROM chats c
		LEFT JOIN messages m ON c.chat_id = m.chat_id
		GROUP BY c.chat_id, c.chat_type, c.title
		ORDER BY last_message DESC
	)");

	while (query.next()) {
		QJsonObject chat;
		chat["chat_id"] = query.value("chat_id").toLongLong();
		chat["type"] = query.value("chat_type").toString();
		chat["title"] = query.value("title").toString();
		chat["message_count"] = query.value("message_count").toInt();
		chat["last_message"] = query.value("last_message").toLongLong();
		result.append(chat);
	}

	return result;
}

// Analytics implementations
QJsonObject ChatArchiver::getMessageStats(qint64 chatId, const QString &period) {
	QJsonObject stats;

	QString timeFilter;
	if (period == "day") {
		timeFilter = "AND timestamp > (strftime('%s', 'now') - 86400)";
	} else if (period == "week") {
		timeFilter = "AND timestamp > (strftime('%s', 'now') - 604800)";
	} else if (period == "month") {
		timeFilter = "AND timestamp > (strftime('%s', 'now') - 2592000)";
	}

	QSqlQuery query(_db);
	query.prepare(QString(R"(
		SELECT
			COUNT(*) as total_messages,
			COUNT(DISTINCT user_id) as unique_users,
			AVG(LENGTH(content)) as avg_length,
			SUM(LENGTH(content) - LENGTH(REPLACE(content, ' ', '')) + 1) as total_words,
			COUNT(CASE WHEN has_media = 1 THEN 1 END) as media_count
		FROM messages
		WHERE chat_id = :chat_id %1
	)").arg(timeFilter));
	query.bindValue(":chat_id", chatId);

	if (query.exec() && query.next()) {
		stats["total_messages"] = query.value("total_messages").toInt();
		stats["unique_users"] = query.value("unique_users").toInt();
		stats["avg_message_length"] = query.value("avg_length").toDouble();
		stats["total_words"] = query.value("total_words").toInt();
		stats["media_count"] = query.value("media_count").toInt();
	}

	stats["period"] = period;
	stats["chat_id"] = chatId;

	return stats;
}

QJsonObject ChatArchiver::getUserActivity(qint64 userId, qint64 chatId) {
	QJsonObject activity;

	QString chatFilter = chatId > 0 ? "AND chat_id = :chat_id" : "";

	QSqlQuery query(_db);
	query.prepare(QString(R"(
		SELECT
			COUNT(*) as message_count,
			SUM(LENGTH(content) - LENGTH(REPLACE(content, ' ', '')) + 1) as word_count,
			AVG(LENGTH(content)) as avg_length,
			MIN(timestamp) as first_message,
			MAX(timestamp) as last_message
		FROM messages
		WHERE user_id = :user_id %1
	)").arg(chatFilter));
	query.bindValue(":user_id", userId);
	if (chatId > 0) {
		query.bindValue(":chat_id", chatId);
	}

	if (query.exec() && query.next()) {
		activity["user_id"] = userId;
		activity["message_count"] = query.value("message_count").toInt();
		activity["word_count"] = query.value("word_count").toInt();
		activity["avg_message_length"] = query.value("avg_length").toDouble();
		activity["first_message"] = query.value("first_message").toLongLong();
		activity["last_message"] = query.value("last_message").toLongLong();
	}

	return activity;
}

QJsonObject ChatArchiver::getChatActivity(qint64 chatId) {
	QJsonObject activity;

	QSqlQuery query(_db);
	query.prepare("SELECT * FROM chat_activity_summary WHERE chat_id = :chat_id");
	query.bindValue(":chat_id", chatId);

	if (query.exec() && query.next()) {
		activity["chat_id"] = chatId;
		activity["total_messages"] = query.value("total_messages").toInt();
		activity["unique_users"] = query.value("unique_users").toInt();
		activity["messages_per_day"] = query.value("messages_per_day").toDouble();
		activity["peak_hour"] = query.value("peak_hour").toInt();
		activity["activity_trend"] = query.value("activity_trend").toString();
	}

	return activity;
}

QString ChatArchiver::exportChat(
		qint64 chatId,
		ExportFormat format,
		const QString &outputPath,
		const QDateTime &startDate,
		const QDateTime &endDate) {

	bool success = false;

	switch (format) {
	case ExportFormat::JSON:
		success = exportToJSON(chatId, outputPath, startDate, endDate);
		break;
	case ExportFormat::JSONL:
		success = exportToJSONL(chatId, outputPath, startDate, endDate);
		break;
	case ExportFormat::CSV:
		success = exportToCSV(chatId, outputPath, startDate, endDate);
		break;
	}

	if (success) {
		Q_EMIT exportCompleted(outputPath);
		return outputPath;
	}

	return QString();
}

ArchivalStats ChatArchiver::getStats() const {
	return _stats;
}

void ChatArchiver::updateStats() {
	QSqlQuery query(_db);

	// Total messages
	query.exec("SELECT COUNT(*) FROM messages");
	if (query.next()) {
		_stats.totalMessages = query.value(0).toInt();
	}

	// Total chats
	query.exec("SELECT COUNT(DISTINCT chat_id) FROM messages");
	if (query.next()) {
		_stats.totalChats = query.value(0).toInt();
	}

	// Total users
	query.exec("SELECT COUNT(DISTINCT user_id) FROM messages WHERE user_id IS NOT NULL");
	if (query.next()) {
		_stats.totalUsers = query.value(0).toInt();
	}

	// Ephemeral captured
	query.exec("SELECT COUNT(*) FROM ephemeral_messages");
	if (query.next()) {
		_stats.ephemeralCaptured = query.value(0).toInt();
	}

	// Database size
	QFileInfo dbFile(_databasePath);
	_stats.databaseSize = dbFile.size();

	// Last archived
	query.exec("SELECT MAX(created_at) FROM messages");
	if (query.next()) {
		_stats.lastArchived = QDateTime::fromSecsSinceEpoch(query.value(0).toLongLong());
	}
}

// Helper implementations
MessageType ChatArchiver::detectMessageType(HistoryItem *message) const {
	if (!message || !message->media()) {
		return MessageType::Text;
	}

	const auto media = message->media();

	if (media->photo()) {
		return MessageType::Photo;
	} else if (media->document()) {
		const auto document = media->document();
		if (document->isVoiceMessage()) {
			return MessageType::Voice;
		} else if (document->isVideoMessage()) {
			return MessageType::Video;
		} else if (document->sticker()) {
			return MessageType::Sticker;
		} else if (document->isAnimation()) {
			return MessageType::Animation;
		} else {
			return MessageType::Document;
		}
	}

	return MessageType::Unknown;
}

QString ChatArchiver::messageTypeToString(MessageType type) const {
	switch (type) {
	case MessageType::Text: return "text";
	case MessageType::Photo: return "photo";
	case MessageType::Video: return "video";
	case MessageType::Voice: return "voice";
	case MessageType::Audio: return "audio";
	case MessageType::Document: return "document";
	case MessageType::Sticker: return "sticker";
	case MessageType::Animation: return "animation";
	case MessageType::Contact: return "contact";
	case MessageType::Location: return "location";
	case MessageType::Poll: return "poll";
	default: return "unknown";
	}
}

QJsonObject ChatArchiver::messageToJson(const QSqlQuery &query) const {
	QJsonObject msg;
	msg["message_id"] = query.value("message_id").toLongLong();
	msg["chat_id"] = query.value("chat_id").toLongLong();
	msg["user_id"] = query.value("user_id").toLongLong();
	msg["username"] = query.value("username").toString();
	msg["first_name"] = query.value("first_name").toString();
	msg["last_name"] = query.value("last_name").toString();
	msg["content"] = query.value("content").toString();
	msg["timestamp"] = query.value("timestamp").toLongLong();
	msg["date"] = query.value("date").toString();
	msg["type"] = query.value("message_type").toString();
	msg["has_media"] = query.value("has_media").toBool();
	msg["is_forwarded"] = query.value("is_forwarded").toBool();
	msg["is_reply"] = query.value("is_reply").toBool();

	if (!query.value("media_path").isNull()) {
		msg["media_path"] = query.value("media_path").toString();
	}

	return msg;
}

QString ChatArchiver::downloadMedia(HistoryItem *message) {
	if (!message || !message->media()) {
		return QString();
	}

	const auto media = message->media();
	const auto document = media->document();

	if (!document) {
		// Photo or other non-document media — extract path from local cache
		// Photos are auto-cached by tdesktop; we reference the cache location
		return QString();
	}

	// Check if document is already downloaded locally
	const auto &location = document->location(true);
	if (!location.isEmpty()) {
		return location.name();
	}

	// Document exists but not downloaded — return the file reference info
	// Actual download requires async API call which we can't block on here
	QString extension;
	if (!document->filename().isEmpty()) {
		QFileInfo fi(document->filename());
		extension = fi.suffix();
	} else if (!document->mimeString().isEmpty()) {
		// Derive extension from MIME type
		static const QHash<QString, QString> mimeToExt = {
			{"audio/ogg", "ogg"},
			{"audio/mpeg", "mp3"},
			{"video/mp4", "mp4"},
			{"image/jpeg", "jpg"},
			{"image/png", "png"},
			{"application/pdf", "pdf"},
		};
		extension = mimeToExt.value(document->mimeString(), "bin");
	}

	qint64 chatId = message->history()->peer->id.value;
	qint64 messageId = message->id.bare;
	return getMediaPath(chatId, messageId, extension);
}

bool ChatArchiver::exportToJSONL(qint64 chatId, const QString &outputPath,
                                 const QDateTime &start, const QDateTime &end) {
	QFile file(outputPath);
	if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
		return false;
	}

	QTextStream out(&file);

	QString sql = "SELECT * FROM messages WHERE chat_id = :chat_id";
	QStringList conditions;

	if (start.isValid()) {
		conditions << "timestamp >= :start";
	}
	if (end.isValid()) {
		conditions << "timestamp <= :end";
	}

	if (!conditions.isEmpty()) {
		sql += " AND " + conditions.join(" AND ");
	}
	sql += " ORDER BY timestamp ASC";

	QSqlQuery query(_db);
	query.prepare(sql);
	query.bindValue(":chat_id", chatId);
	if (start.isValid()) {
		query.bindValue(":start", start.toSecsSinceEpoch());
	}
	if (end.isValid()) {
		query.bindValue(":end", end.toSecsSinceEpoch());
	}

	if (!query.exec()) {
		return false;
	}

	while (query.next()) {
		QJsonObject msg = messageToJson(query);
		out << QJsonDocument(msg).toJson(QJsonDocument::Compact) << "\n";
	}

	file.close();
	return true;
}

bool ChatArchiver::exportToJSON(qint64 chatId, const QString &outputPath,
                                const QDateTime &start, const QDateTime &end) {
	QFile file(outputPath);
	if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
		return false;
	}

	QTextStream out(&file);

	QString sql = "SELECT * FROM messages WHERE chat_id = :chat_id";
	QStringList conditions;
	if (start.isValid()) {
		conditions << "timestamp >= :start";
	}
	if (end.isValid()) {
		conditions << "timestamp <= :end";
	}
	if (!conditions.isEmpty()) {
		sql += " AND " + conditions.join(" AND ");
	}
	sql += " ORDER BY timestamp ASC";

	QSqlQuery query(_db);
	query.prepare(sql);
	query.bindValue(":chat_id", chatId);
	if (start.isValid()) {
		query.bindValue(":start", start.toSecsSinceEpoch());
	}
	if (end.isValid()) {
		query.bindValue(":end", end.toSecsSinceEpoch());
	}

	if (!query.exec()) {
		return false;
	}

	QJsonArray messages;
	while (query.next()) {
		messages.append(messageToJson(query));
	}

	QJsonObject root;
	root["chat_id"] = chatId;
	root["message_count"] = messages.size();
	root["exported_at"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
	root["messages"] = messages;

	out << QJsonDocument(root).toJson(QJsonDocument::Indented);
	file.close();
	return true;
}

bool ChatArchiver::exportToCSV(qint64 chatId, const QString &outputPath,
                               const QDateTime &start, const QDateTime &end) {
	QFile file(outputPath);
	if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
		return false;
	}

	QTextStream out(&file);

	// CSV header
	out << "message_id,chat_id,user_id,username,first_name,last_name,content,timestamp,date,type,has_media,is_forwarded,is_reply\n";

	QString sql = "SELECT * FROM messages WHERE chat_id = :chat_id";
	QStringList conditions;
	if (start.isValid()) {
		conditions << "timestamp >= :start";
	}
	if (end.isValid()) {
		conditions << "timestamp <= :end";
	}
	if (!conditions.isEmpty()) {
		sql += " AND " + conditions.join(" AND ");
	}
	sql += " ORDER BY timestamp ASC";

	QSqlQuery query(_db);
	query.prepare(sql);
	query.bindValue(":chat_id", chatId);
	if (start.isValid()) {
		query.bindValue(":start", start.toSecsSinceEpoch());
	}
	if (end.isValid()) {
		query.bindValue(":end", end.toSecsSinceEpoch());
	}

	if (!query.exec()) {
		return false;
	}

	while (query.next()) {
		auto escapeCsv = [](const QString &s) -> QString {
			if (s.contains(',') || s.contains('"') || s.contains('\n')) {
				QString escaped = s;
				escaped.replace('"', "\"\"");
				return '"' + escaped + '"';
			}
			return s;
		};

		out << query.value("message_id").toLongLong() << ","
			<< query.value("chat_id").toLongLong() << ","
			<< query.value("user_id").toLongLong() << ","
			<< escapeCsv(query.value("username").toString()) << ","
			<< escapeCsv(query.value("first_name").toString()) << ","
			<< escapeCsv(query.value("last_name").toString()) << ","
			<< escapeCsv(query.value("content").toString()) << ","
			<< query.value("timestamp").toLongLong() << ","
			<< escapeCsv(query.value("date").toString()) << ","
			<< escapeCsv(query.value("message_type").toString()) << ","
			<< (query.value("has_media").toBool() ? "true" : "false") << ","
			<< (query.value("is_forwarded").toBool() ? "true" : "false") << ","
			<< (query.value("is_reply").toBool() ? "true" : "false") << "\n";
	}

	file.close();
	return true;
}

void ChatArchiver::updateDailyStats(qint64 chatId, const QDate &date) {
	QString dateStr = date.toString("yyyy-MM-dd");

	QSqlQuery query(_db);
	query.prepare(
		"INSERT OR REPLACE INTO message_stats_daily "
		"(date, chat_id, message_count, unique_users, avg_message_length, total_words, media_count) "
		"SELECT :date, :chat_id, "
		"COUNT(*), COUNT(DISTINCT user_id), "
		"AVG(LENGTH(content)), SUM(LENGTH(content) - LENGTH(REPLACE(content, ' ', '')) + 1), "
		"SUM(CASE WHEN has_media = 1 THEN 1 ELSE 0 END) "
		"FROM messages WHERE chat_id = :chat_id2 "
		"AND date(timestamp, 'unixepoch') = :date2");
	query.bindValue(":date", dateStr);
	query.bindValue(":chat_id", chatId);
	query.bindValue(":chat_id2", chatId);
	query.bindValue(":date2", dateStr);
	query.exec();
}

void ChatArchiver::updateUserActivity(qint64 userId, qint64 chatId) {
	QSqlQuery query(_db);
	query.prepare(
		"INSERT OR REPLACE INTO user_activity_summary "
		"(user_id, chat_id, message_count, word_count, avg_message_length, "
		"first_message_date, last_message_date, days_active, updated_at) "
		"SELECT :user_id, :chat_id, "
		"COUNT(*), "
		"SUM(LENGTH(content) - LENGTH(REPLACE(content, ' ', '')) + 1), "
		"AVG(LENGTH(content)), "
		"MIN(timestamp), MAX(timestamp), "
		"COUNT(DISTINCT date(timestamp, 'unixepoch')), "
		"strftime('%s', 'now') "
		"FROM messages WHERE user_id = :user_id2 AND chat_id = :chat_id2");
	query.bindValue(":user_id", userId);
	query.bindValue(":chat_id", chatId);
	query.bindValue(":user_id2", userId);
	query.bindValue(":chat_id2", chatId);
	query.exec();
}

void ChatArchiver::updateChatActivity(qint64 chatId) {
	QSqlQuery query(_db);
	query.prepare(
		"INSERT OR REPLACE INTO chat_activity_summary "
		"(chat_id, total_messages, unique_users, "
		"first_message_date, last_message_date, updated_at) "
		"SELECT :chat_id, COUNT(*), COUNT(DISTINCT user_id), "
		"MIN(timestamp), MAX(timestamp), strftime('%s', 'now') "
		"FROM messages WHERE chat_id = :chat_id2");
	query.bindValue(":chat_id", chatId);
	query.bindValue(":chat_id2", chatId);
	query.exec();
}

// Slots
void ChatArchiver::onNewMessage(HistoryItem *message) {
	archiveMessage(message);
}

void ChatArchiver::onMessageEdited(HistoryItem *message) {
	if (!message || !_isRunning) return;

	qint64 chatId = message->history()->peer->id.value;
	qint64 messageId = message->id.bare;
	QString newContent = message->originalText().text;

	QSqlQuery query(_db);
	query.prepare("UPDATE messages SET content = ?, date = ? WHERE chat_id = ? AND message_id = ?");
	query.addBindValue(newContent);
	query.addBindValue(QDateTime::fromSecsSinceEpoch(message->date()).toString(Qt::ISODate));
	query.addBindValue(chatId);
	query.addBindValue(messageId);
	query.exec();
}

void ChatArchiver::checkForNewMessages() {
	// Periodic check for new messages
	// This would poll the Data::Session for new messages
}

bool ChatArchiver::purgeOldMessages(int daysToKeep) {
	if (!_isRunning || daysToKeep < 0) {
		return false;
	}

	// Calculate cutoff timestamp
	const qint64 cutoffTime = QDateTime::currentSecsSinceEpoch() - (daysToKeep * 86400);

	// Delete messages older than cutoff
	QSqlQuery query(_db);
	query.prepare("DELETE FROM messages WHERE timestamp < :cutoff");
	query.bindValue(":cutoff", cutoffTime);

	if (!query.exec()) {
		Q_EMIT error(QString("Failed to purge old messages: %1").arg(query.lastError().text()));
		return false;
	}

	// Update statistics
	updateStats();

	return true;
}

// ===================================
// EphemeralArchiver Implementation
// ===================================

EphemeralArchiver::EphemeralArchiver(QObject *parent)
	: QObject(parent)
	, _checkTimer(new QTimer(this)) {

	connect(_checkTimer, &QTimer::timeout, this, &EphemeralArchiver::checkForEphemeral);
}

EphemeralArchiver::~EphemeralArchiver() {
	stop();
}

bool EphemeralArchiver::start(ChatArchiver *archiver) {
	if (_isRunning || !archiver) {
		return false;
	}

	_archiver = archiver;
	_checkTimer->start(1000);  // Check every second
	_isRunning = true;

	return true;
}

void EphemeralArchiver::stop() {
	if (!_isRunning) {
		return;
	}

	_checkTimer->stop();
	_archiver = nullptr;
	_isRunning = false;
}

void EphemeralArchiver::setCaptureTypes(bool selfDestruct, bool viewOnce, bool vanishing) {
	_captureSelfDestruct = selfDestruct;
	_captureViewOnce = viewOnce;
	_captureVanishing = vanishing;
}

void EphemeralArchiver::onNewMessage(HistoryItem *message) {
	if (!_autoCapture || !message) {
		return;
	}

	QString type;
	int ttl = 0;

	if (detectEphemeralType(message, type, ttl)) {
		captureMessage(message, type, ttl);
	}
}

bool EphemeralArchiver::detectEphemeralType(
		HistoryItem *message,
		QString &type,
		int &ttl) {

	if (!message) {
		return false;
	}

	const auto item = message;

	// Check for self-destruct timer
	if (item->ttlDestroyAt() > 0) {
		type = "self_destruct";
		ttl = item->ttlDestroyAt() - QDateTime::currentSecsSinceEpoch();
		return _captureSelfDestruct;
	}

	// Check for view-once media (photos/videos sent as "view once")
	if (const auto media = item->media()) {
		if (media->ttlSeconds() > 0) {
			type = "view_once";
			ttl = media->ttlSeconds();
			return _captureViewOnce;
		}
	}

	return false;
}

bool EphemeralArchiver::captureMessage(
		HistoryItem *message,
		const QString &type,
		int ttl) {

	if (!_archiver->archiveEphemeralMessage(message, type, ttl)) {
		return false;
	}

	_stats.totalCaptured++;
	if (type == "self_destruct") {
		_stats.selfDestructCount++;
	} else if (type == "view_once") {
		_stats.viewOnceCount++;
	} else if (type == "vanishing") {
		_stats.vanishingCount++;
	}

	if (message->media()) {
		_stats.mediaSaved++;
	}

	_stats.lastCaptured = QDateTime::currentDateTime();

	const auto history = message->history();
	const qint64 chatId = history->peer->id.value;
	const qint64 messageId = message->id.bare;

	Q_EMIT ephemeralCaptured(chatId, messageId, type);
	return true;
}

void EphemeralArchiver::checkForEphemeral() {
	// Periodic check for ephemeral messages
	// This would monitor Data::Session for messages with TTL
}

void EphemeralArchiver::onMessageDeleted(qint64 chatId, qint64 messageId) {
	Q_UNUSED(chatId);
	Q_UNUSED(messageId);
	// Track deleted messages (may have been ephemeral)
}

QString ChatArchiver::getMediaPath(qint64 chatId, qint64 messageId, const QString &extension) {
	QString mediaDir = QFileInfo(_databasePath).absolutePath() + "/media/" + QString::number(chatId);
	QDir dir;
	dir.mkpath(mediaDir);
	return mediaDir + "/" + QString::number(messageId) + "." + extension;
}

} // namespace MCP

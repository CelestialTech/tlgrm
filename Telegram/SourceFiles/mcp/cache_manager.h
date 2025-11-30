// MCP Cache Manager - Performance optimization layer
//
// This file is part of Telegram Desktop MCP integration.
// Provides LRU caching for frequently accessed MCP data.

#pragma once

#include <QtCore/QObject>
#include <QtCore/QCache>
#include <QtCore/QHash>
#include <QtCore/QJsonObject>
#include <QtCore/QJsonArray>
#include <QtCore/QMutex>
#include <QtCore/QDateTime>

namespace MCP {

// Cache entry with TTL (time-to-live) and LRU timestamp
struct CacheEntry {
	QJsonObject data;
	QDateTime expiration;
	QDateTime lastAccess;  // For true LRU eviction
	int hitCount = 0;

	bool isExpired() const {
		return QDateTime::currentDateTime() > expiration;
	}
};

// High-performance LRU cache with TTL support
class CacheManager : public QObject {
	Q_OBJECT

public:
	explicit CacheManager(QObject *parent = nullptr);
	~CacheManager() override;

	// Core cache operations
	bool get(const QString &key, QJsonObject &outData);
	void put(const QString &key, const QJsonObject &data, int ttlSeconds = 300);
	void invalidate(const QString &key);
	void invalidatePattern(const QString &pattern);
	void clear();

	// Cache statistics
	struct Stats {
		qint64 hits = 0;
		qint64 misses = 0;
		qint64 evictions = 0;
		qint64 size = 0;
		qint64 maxSize = 0;

		double hitRate() const {
			qint64 total = hits + misses;
			return total > 0 ? (double)hits / total : 0.0;
		}
	};

	Stats getStats() const;
	void resetStats();

	// Configuration
	void setMaxSize(int maxSizeMB);
	void setDefaultTTL(int seconds);

	// Specialized cache key helpers - centralized key generation for consistency
	// Chat-related keys
	QString chatListKey() const { return QStringLiteral("chats:list"); }
	QString chatInfoKey(qint64 chatId) const { return QStringLiteral("chat:%1:info").arg(chatId); }
	QString messagesKey(qint64 chatId, int limit) const { return QStringLiteral("messages:%1:limit:%2").arg(chatId).arg(limit); }

	// User-related keys
	QString userInfoKey(qint64 userId) const { return QStringLiteral("user:%1:info").arg(userId); }
	QString profileSettingsKey() const { return QStringLiteral("settings:profile"); }
	QString privacySettingsKey() const { return QStringLiteral("settings:privacy"); }
	QString securitySettingsKey() const { return QStringLiteral("settings:security"); }
	QString blockedUsersKey() const { return QStringLiteral("users:blocked"); }

	// Search-related keys
	QString searchKey(const QString &query, qint64 chatId = 0) const {
		return chatId ? QStringLiteral("search:%1:chat:%2").arg(query).arg(chatId)
		              : QStringLiteral("search:%1:global").arg(query);
	}

	// Analytics keys
	QString analyticsKey(const QString &type, qint64 chatId = 0) const {
		return chatId ? QStringLiteral("analytics:%1:chat:%2").arg(type).arg(chatId)
		              : QStringLiteral("analytics:%1:global").arg(type);
	}
	QString statsKey(const QString &category) const { return QStringLiteral("stats:%1").arg(category); }

	// Archive keys
	QString archiveListKey() const { return QStringLiteral("archive:list"); }
	QString archiveStatsKey() const { return QStringLiteral("archive:stats"); }
	QString ephemeralStatsKey() const { return QStringLiteral("ephemeral:stats"); }

	// Bot keys
	QString botListKey() const { return QStringLiteral("bots:list"); }
	QString botInfoKey(qint64 botId) const { return QStringLiteral("bot:%1:info").arg(botId); }

	// Wallet/Stars keys
	QString walletBalanceKey() const { return QStringLiteral("wallet:balance"); }
	QString transactionsKey(int limit) const { return QStringLiteral("wallet:transactions:limit:%1").arg(limit); }
	QString giftsKey() const { return QStringLiteral("gifts:list"); }
	QString subscriptionsKey() const { return QStringLiteral("subscriptions:list"); }

private:
	void cleanupExpired();
	int estimateSize(const QJsonObject &obj) const;

	mutable QMutex _mutex;
	QHash<QString, CacheEntry> _cache;

	// Configuration
	int _maxSizeBytes = 50 * 1024 * 1024;  // 50 MB default
	int _defaultTTL = 300;  // 5 minutes default
	int _currentSizeBytes = 0;

	// Statistics
	mutable Stats _stats;

	// Cleanup timer
	QTimer *_cleanupTimer = nullptr;
};

} // namespace MCP

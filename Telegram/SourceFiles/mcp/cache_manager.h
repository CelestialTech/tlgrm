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

// Cache entry with TTL (time-to-live)
struct CacheEntry {
	QJsonObject data;
	QDateTime expiration;
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

	// Specialized cache helpers
	QString chatListKey() const { return QStringLiteral("chats:list"); }
	QString chatInfoKey(qint64 chatId) const { return QStringLiteral("chat:%1:info").arg(chatId); }
	QString userInfoKey(qint64 userId) const { return QStringLiteral("user:%1:info").arg(userId); }
	QString messagesKey(qint64 chatId, int limit) const { return QStringLiteral("messages:%1:limit:%2").arg(chatId).arg(limit); }

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

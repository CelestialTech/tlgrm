// MCP Cache Manager Implementation - Thread-safe LRU cache with TTL

#include "cache_manager.h"
#include <QtCore/QJsonDocument>
#include <QtCore/QMutexLocker>
#include <QtCore/QTimer>

namespace MCP {

CacheManager::CacheManager(QObject *parent)
	: QObject(parent) {
	// Setup periodic cleanup timer (every 60 seconds)
	_cleanupTimer = new QTimer(this);
	connect(_cleanupTimer, &QTimer::timeout, this, &CacheManager::cleanupExpired);
	_cleanupTimer->start(60000);  // 60 seconds
}

CacheManager::~CacheManager() {
	clear();
}

bool CacheManager::get(const QString &key, QJsonObject &outData) {
	QMutexLocker locker(&_mutex);

	// Use constFind for thread-safe lookup
	auto it = _cache.constFind(key);
	if (it == _cache.constEnd()) {
		_stats.misses++;
		return false;
	}

	// Check if expired - need to copy key before potential removal
	if (it->isExpired()) {
		QString keyToRemove = it.key();
		int sizeToRemove = estimateSize(it->data);

		// Now safe to remove using the key (not iterator)
		_cache.remove(keyToRemove);
		_currentSizeBytes -= sizeToRemove;
		_stats.misses++;
		_stats.evictions++;
		_stats.size = _cache.size();
		return false;
	}

	// Cache hit - update access time for LRU
	// Need to get mutable reference
	auto mutIt = _cache.find(key);
	if (mutIt != _cache.end()) {
		mutIt->lastAccess = QDateTime::currentDateTime();
		mutIt->hitCount++;
		outData = mutIt->data;
	}

	_stats.hits++;
	return true;
}

void CacheManager::put(const QString &key, const QJsonObject &data, int ttlSeconds) {
	QMutexLocker locker(&_mutex);

	int dataSize = estimateSize(data);
	QDateTime now = QDateTime::currentDateTime();

	// Check if we need to evict entries to make space
	while (_currentSizeBytes + dataSize > _maxSizeBytes && !_cache.isEmpty()) {
		// Find LRU entry (oldest lastAccess timestamp)
		QString lruKey;
		QDateTime oldestAccess = QDateTime::currentDateTime();

		for (auto it = _cache.constBegin(); it != _cache.constEnd(); ++it) {
			if (it->lastAccess < oldestAccess) {
				oldestAccess = it->lastAccess;
				lruKey = it.key();
			}
		}

		// Evict LRU entry using key (not iterator)
		if (!lruKey.isEmpty()) {
			auto lruIt = _cache.find(lruKey);
			if (lruIt != _cache.end()) {
				_currentSizeBytes -= estimateSize(lruIt->data);
				_cache.erase(lruIt);
				_stats.evictions++;
			}
		} else {
			// Safety: if we can't find LRU, remove first entry
			auto first = _cache.begin();
			if (first != _cache.end()) {
				_currentSizeBytes -= estimateSize(first->data);
				_cache.erase(first);
				_stats.evictions++;
			}
		}
	}

	// Remove old entry if exists
	auto it = _cache.find(key);
	if (it != _cache.end()) {
		_currentSizeBytes -= estimateSize(it->data);
		_cache.erase(it);
	}

	// Insert new entry with current timestamp
	CacheEntry entry;
	entry.data = data;
	entry.expiration = now.addSecs(ttlSeconds > 0 ? ttlSeconds : _defaultTTL);
	entry.lastAccess = now;
	entry.hitCount = 0;

	_cache[key] = entry;
	_currentSizeBytes += dataSize;
	_stats.size = _cache.size();
	_stats.maxSize = qMax(_stats.maxSize, _stats.size);
}

void CacheManager::invalidate(const QString &key) {
	QMutexLocker locker(&_mutex);

	auto it = _cache.find(key);
	if (it != _cache.end()) {
		_currentSizeBytes -= estimateSize(it->data);
		_cache.erase(it);
		_stats.size = _cache.size();
	}
}

void CacheManager::invalidatePattern(const QString &pattern) {
	QMutexLocker locker(&_mutex);

	// Collect keys to remove first (avoid iterator invalidation)
	QList<QString> keysToRemove;
	keysToRemove.reserve(_cache.size() / 4);  // Reserve estimated capacity

	// Find matching keys using const iteration
	for (auto it = _cache.constBegin(); it != _cache.constEnd(); ++it) {
		if (it.key().contains(pattern, Qt::CaseInsensitive)) {
			keysToRemove.append(it.key());
		}
	}

	// Remove them using keys (not iterators)
	for (const QString &key : keysToRemove) {
		auto it = _cache.find(key);
		if (it != _cache.end()) {
			_currentSizeBytes -= estimateSize(it->data);
			_cache.erase(it);
		}
	}

	_stats.size = _cache.size();
}

void CacheManager::clear() {
	QMutexLocker locker(&_mutex);
	_cache.clear();
	_currentSizeBytes = 0;
	_stats.size = 0;
}

CacheManager::Stats CacheManager::getStats() const {
	QMutexLocker locker(&_mutex);
	return _stats;
}

void CacheManager::resetStats() {
	QMutexLocker locker(&_mutex);
	_stats.hits = 0;
	_stats.misses = 0;
	_stats.evictions = 0;
}

void CacheManager::setMaxSize(int maxSizeMB) {
	QMutexLocker locker(&_mutex);
	_maxSizeBytes = maxSizeMB * 1024 * 1024;

	// Trigger cleanup if over limit using LRU eviction
	while (_currentSizeBytes > _maxSizeBytes && !_cache.isEmpty()) {
		QString lruKey;
		QDateTime oldestAccess = QDateTime::currentDateTime();

		for (auto it = _cache.constBegin(); it != _cache.constEnd(); ++it) {
			if (it->lastAccess < oldestAccess) {
				oldestAccess = it->lastAccess;
				lruKey = it.key();
			}
		}

		if (!lruKey.isEmpty()) {
			auto lruIt = _cache.find(lruKey);
			if (lruIt != _cache.end()) {
				_currentSizeBytes -= estimateSize(lruIt->data);
				_cache.erase(lruIt);
				_stats.evictions++;
			}
		} else {
			break;  // Safety exit
		}
	}

	_stats.size = _cache.size();
}

void CacheManager::setDefaultTTL(int seconds) {
	QMutexLocker locker(&_mutex);
	_defaultTTL = seconds;
}

void CacheManager::cleanupExpired() {
	QMutexLocker locker(&_mutex);

	// Collect expired keys first (const iteration)
	QList<QString> expiredKeys;
	expiredKeys.reserve(_cache.size() / 10);  // Estimate 10% expired

	QDateTime now = QDateTime::currentDateTime();

	for (auto it = _cache.constBegin(); it != _cache.constEnd(); ++it) {
		if (it->expiration <= now) {
			expiredKeys.append(it.key());
		}
	}

	// Remove using keys (not iterators)
	for (const QString &key : expiredKeys) {
		auto it = _cache.find(key);
		if (it != _cache.end()) {
			_currentSizeBytes -= estimateSize(it->data);
			_cache.erase(it);
			_stats.evictions++;
		}
	}

	_stats.size = _cache.size();
}

int CacheManager::estimateSize(const QJsonObject &obj) const {
	// Estimate size by converting to JSON and measuring
	QJsonDocument doc(obj);
	return doc.toJson(QJsonDocument::Compact).size();
}

} // namespace MCP

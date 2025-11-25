// MCP Cache Manager Implementation

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

	auto it = _cache.find(key);
	if (it == _cache.end()) {
		_stats.misses++;
		return false;
	}

	// Check if expired
	if (it->isExpired()) {
		_currentSizeBytes -= estimateSize(it->data);
		_cache.erase(it);
		_stats.misses++;
		_stats.evictions++;
		return false;
	}

	// Cache hit
	it->hitCount++;
	_stats.hits++;
	outData = it->data;
	return true;
}

void CacheManager::put(const QString &key, const QJsonObject &data, int ttlSeconds) {
	QMutexLocker locker(&_mutex);

	int dataSize = estimateSize(data);

	// Check if we need to evict entries to make space
	while (_currentSizeBytes + dataSize > _maxSizeBytes && !_cache.isEmpty()) {
		// Find LRU entry (lowest hit count)
		auto lruIt = _cache.begin();
		int minHits = lruIt->hitCount;

		for (auto it = _cache.begin(); it != _cache.end(); ++it) {
			if (it->hitCount < minHits) {
				minHits = it->hitCount;
				lruIt = it;
			}
		}

		// Evict LRU entry
		_currentSizeBytes -= estimateSize(lruIt->data);
		_cache.erase(lruIt);
		_stats.evictions++;
	}

	// Remove old entry if exists
	auto it = _cache.find(key);
	if (it != _cache.end()) {
		_currentSizeBytes -= estimateSize(it->data);
	}

	// Insert new entry
	CacheEntry entry;
	entry.data = data;
	entry.expiration = QDateTime::currentDateTime().addSecs(ttlSeconds > 0 ? ttlSeconds : _defaultTTL);
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

	QList<QString> keysToRemove;

	// Find matching keys
	for (auto it = _cache.begin(); it != _cache.end(); ++it) {
		if (it.key().contains(pattern, Qt::CaseInsensitive)) {
			keysToRemove.append(it.key());
		}
	}

	// Remove them
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

	// Trigger cleanup if over limit
	while (_currentSizeBytes > _maxSizeBytes && !_cache.isEmpty()) {
		auto lruIt = _cache.begin();
		int minHits = lruIt->hitCount;

		for (auto it = _cache.begin(); it != _cache.end(); ++it) {
			if (it->hitCount < minHits) {
				minHits = it->hitCount;
				lruIt = it;
			}
		}

		_currentSizeBytes -= estimateSize(lruIt->data);
		_cache.erase(lruIt);
		_stats.evictions++;
	}

	_stats.size = _cache.size();
}

void CacheManager::setDefaultTTL(int seconds) {
	QMutexLocker locker(&_mutex);
	_defaultTTL = seconds;
}

void CacheManager::cleanupExpired() {
	QMutexLocker locker(&_mutex);

	QList<QString> expiredKeys;
	QDateTime now = QDateTime::currentDateTime();

	// Find expired entries
	for (auto it = _cache.begin(); it != _cache.end(); ++it) {
		if (it->expiration <= now) {
			expiredKeys.append(it.key());
		}
	}

	// Remove them
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

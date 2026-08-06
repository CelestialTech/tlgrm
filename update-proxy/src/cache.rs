use std::time::{Duration, Instant, SystemTime, UNIX_EPOCH};

use crate::config::Config;

// ---------------------------------------------------------------------------
// In-memory cache entry
// ---------------------------------------------------------------------------

/// A single cached metadata manifest held behind an `RwLock`.
pub struct CachedManifest {
    /// Transformed Telegram-format JSON.
    pub json: String,
    /// ETag returned by GitHub (used for conditional requests).
    pub etag: Option<String>,
    /// Wall-clock time when the entry was populated.
    pub fetched_at: Instant,
}

// ---------------------------------------------------------------------------
// Disk cache (survives restarts)
// ---------------------------------------------------------------------------

/// Serialisable representation of the metadata cache, written to
/// `{CACHE_DIR}/current4.json`.
#[derive(serde::Serialize, serde::Deserialize)]
pub struct DiskCache {
    pub json: String,
    pub etag: Option<String>,
    /// Unix epoch seconds when the entry was written.
    pub timestamp: u64,
}

/// Persist transformed JSON and optional ETag to disk so the cache survives
/// proxy restarts.
pub fn save_disk_cache(config: &Config, json: &str, etag: &Option<String>) {
    let dc = DiskCache {
        json: json.to_string(),
        etag: etag.clone(),
        timestamp: SystemTime::now()
            .duration_since(UNIX_EPOCH)
            .unwrap_or_default()
            .as_secs(),
    };

    if let Some(parent) = std::path::Path::new(&config.cache_file_path).parent() {
        let _ = std::fs::create_dir_all(parent);
    }

    match serde_json::to_string(&dc) {
        Ok(data) => {
            if let Err(e) = std::fs::write(&config.cache_file_path, data) {
                log::error!("Failed to write disk cache to {}: {}", config.cache_file_path, e);
            } else {
                log::debug!("Disk cache written to {}", config.cache_file_path);
            }
        }
        Err(e) => log::error!("Failed to serialize disk cache: {}", e),
    }
}

/// Load cached manifest from disk.  Returns `None` if the file is missing,
/// corrupt, or expired (older than `cache_ttl`).
pub fn load_disk_cache(config: &Config) -> Option<DiskCache> {
    let data = match std::fs::read_to_string(&config.cache_file_path) {
        Ok(d) => d,
        Err(_) => {
            log::info!("No disk cache found at {}", config.cache_file_path);
            return None;
        }
    };

    let dc: DiskCache = match serde_json::from_str(&data) {
        Ok(d) => d,
        Err(e) => {
            log::warn!("Disk cache corrupt ({}), ignoring", e);
            return None;
        }
    };

    let now = SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .unwrap_or_default()
        .as_secs();

    let age = now.saturating_sub(dc.timestamp);
    if age < config.cache_ttl.as_secs() {
        log::info!(
            "Loaded disk cache (age {}s, TTL {}s)",
            age,
            config.cache_ttl.as_secs()
        );
        Some(dc)
    } else {
        log::info!(
            "Disk cache expired (age {}s > TTL {}s), ignoring",
            age,
            config.cache_ttl.as_secs()
        );
        None
    }
}

/// Delete the on-disk cache file (used by webhook / manual purge).
pub fn delete_disk_cache(config: &Config) {
    if std::path::Path::new(&config.cache_file_path).exists() {
        if let Err(e) = std::fs::remove_file(&config.cache_file_path) {
            log::error!("Failed to delete disk cache: {}", e);
        } else {
            log::info!("Disk cache deleted");
        }
    }
}

/// Reconstruct a `CachedManifest` from a `DiskCache` entry, adjusting the
/// `fetched_at` instant so that elapsed-time checks remain correct.
pub fn disk_cache_to_manifest(dc: DiskCache) -> CachedManifest {
    let now_epoch = SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .unwrap_or_default()
        .as_secs();
    let age_secs = now_epoch.saturating_sub(dc.timestamp);

    CachedManifest {
        json: dc.json,
        etag: dc.etag,
        fetched_at: Instant::now() - Duration::from_secs(age_secs),
    }
}

use hmac::{Hmac, Mac};
use sha2::Sha256;
use std::sync::Arc;
use tokio::sync::RwLock;

use crate::cache::{self, CachedManifest};
use crate::config::{Config, PLATFORM_ASSET_PREFIXES};
use crate::transform::tag_to_version_int;

type HmacSha256 = Hmac<Sha256>;

// ---------------------------------------------------------------------------
// Signature verification
// ---------------------------------------------------------------------------

/// Verify the `X-Hub-Signature-256` header against the request body.
/// Returns `true` if the HMAC matches, `false` otherwise.
pub fn verify_signature(secret: &str, signature_header: &str, body: &[u8]) -> bool {
    // GitHub sends: "sha256=<hex>"
    let hex_sig = match signature_header.strip_prefix("sha256=") {
        Some(h) => h,
        None => {
            log::warn!("Webhook signature header missing 'sha256=' prefix");
            return false;
        }
    };

    let expected_bytes = match hex::decode(hex_sig) {
        Ok(b) => b,
        Err(e) => {
            log::warn!("Webhook signature hex decode failed: {}", e);
            return false;
        }
    };

    let mut mac = match HmacSha256::new_from_slice(secret.as_bytes()) {
        Ok(m) => m,
        Err(e) => {
            log::error!("HMAC init failed: {}", e);
            return false;
        }
    };
    mac.update(body);

    mac.verify_slice(&expected_bytes).is_ok()
}

// ---------------------------------------------------------------------------
// Cache invalidation
// ---------------------------------------------------------------------------

/// Purge in-memory and disk caches.  Called on webhook events and manual
/// `/purge`.
pub async fn purge_caches(
    config: &Config,
    cache: &Arc<RwLock<Option<CachedManifest>>>,
) {
    {
        let mut guard = cache.write().await;
        *guard = None;
    }
    cache::delete_disk_cache(config);
    log::info!("In-memory and disk caches purged");
}

// ---------------------------------------------------------------------------
// Cloudflare edge purge
// ---------------------------------------------------------------------------

/// Fire-and-forget Cloudflare edge cache purge for binary URLs associated with
/// the given tag.  Spawns a background task so the webhook handler returns
/// immediately.
pub fn purge_cloudflare_edge(config: &Config, tag: &str) {
    let purge_url = match config.cloudflare_purge_url() {
        Some(u) => u,
        None => {
            log::info!("Cloudflare purge skipped: CLOUDFLARE_ZONE_ID not configured");
            return;
        }
    };

    let api_token = match &config.cloudflare_api_token {
        Some(t) => t.clone(),
        None => {
            log::info!("Cloudflare purge skipped: CLOUDFLARE_API_TOKEN not configured");
            return;
        }
    };

    let version_int = match tag_to_version_int(tag) {
        Ok(v) => v,
        Err(e) => {
            log::error!("Cannot purge CF edge: version conversion failed for '{}': {}", tag, e);
            return;
        }
    };

    // Build the list of URLs to purge (one per platform prefix).
    let public_domain = config.public_domain.clone();
    let urls: Vec<String> = PLATFORM_ASSET_PREFIXES
        .iter()
        .map(|prefix| format!("https://{}/{}{}", public_domain, prefix, version_int))
        .collect();

    log::info!("Purging Cloudflare edge cache for: {:?}", urls);

    tokio::spawn(async move {
        let client = reqwest::Client::new();
        let body = serde_json::json!({ "files": urls });
        match client
            .post(&purge_url)
            .header("Authorization", format!("Bearer {}", api_token))
            .header("Content-Type", "application/json")
            .json(&body)
            .send()
            .await
        {
            Ok(resp) => {
                if resp.status().is_success() {
                    log::info!("Cloudflare edge purge succeeded");
                } else {
                    log::error!(
                        "Cloudflare edge purge failed: {} {}",
                        resp.status(),
                        resp.text().await.unwrap_or_default()
                    );
                }
            }
            Err(e) => {
                log::error!("Cloudflare edge purge request failed: {}", e);
            }
        }
    });
}

// ---------------------------------------------------------------------------
// Webhook event dispatch
// ---------------------------------------------------------------------------

/// Actions that trigger cache invalidation according to FR-8.
const INVALIDATING_ACTIONS: &[&str] = &["published", "edited", "released", "deleted"];

/// Process a validated webhook payload.  Returns a short human-readable status
/// string for the HTTP response.
pub async fn handle_webhook_payload(
    config: &Config,
    cache: &Arc<RwLock<Option<CachedManifest>>>,
    body: &[u8],
) -> String {
    let payload: serde_json::Value = match serde_json::from_slice(body) {
        Ok(v) => v,
        Err(e) => {
            log::error!("Webhook JSON parse error: {}", e);
            return "malformed JSON".to_string();
        }
    };

    let action = payload["action"].as_str().unwrap_or("unknown");
    log::info!("Webhook event: action={}", action);

    if !INVALIDATING_ACTIONS.contains(&action) {
        // Harmless event (e.g. GitHub ping) — acknowledge without side-effects.
        return format!("ignored action: {}", action);
    }

    // Purge in-memory + disk cache for all invalidating actions.
    purge_caches(config, cache).await;

    // On "deleted", also purge Cloudflare edge cache for the binary URLs.
    if action == "deleted" {
        if let Some(tag) = payload["release"]["tag_name"].as_str() {
            purge_cloudflare_edge(config, tag);
        } else {
            log::warn!("Webhook 'deleted' event missing release.tag_name");
        }
    }

    format!("processed: {}", action)
}

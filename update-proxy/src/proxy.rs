use async_trait::async_trait;
use bytes::Bytes;
use pingora::upstreams::peer::HttpPeer;
use pingora::Result;
use pingora::http::{RequestHeader, ResponseHeader};
use pingora::proxy::{ProxyHttp, Session};
use std::path::{Path, PathBuf};
use std::sync::Arc;
use std::time::Duration;
use tokio::sync::RwLock;

use crate::cache::{self, CachedManifest};
use crate::config::{Config, PROXY_USER_AGENT, STRIPPED_UPSTREAM_HEADERS};
use crate::config::{BINARY_UPSTREAM, METADATA_UPSTREAM};
use crate::transform::{
    extract_update_filename, extract_version_int, transform_github_to_telegram,
    version_int_to_tag,
};
use crate::webhook;

// ---------------------------------------------------------------------------
// Per-request context
// ---------------------------------------------------------------------------

/// Carries per-request state through the Pingora filter chain.
pub struct RequestCtx {
    /// `true` for `/current4`, `/current2`, `/current` (metadata requests).
    pub is_metadata: bool,
    /// Accumulates upstream response body chunks for metadata requests so the
    /// full JSON can be transformed before forwarding to the client.
    pub body_buffer: Vec<u8>,
    /// For webhook/purge: if we handled the request entirely in
    /// `request_filter`, the upstream path is never entered.  This flag is
    /// unused by Pingora itself but kept for clarity.
    pub handled: bool,
}

// ---------------------------------------------------------------------------
// Proxy struct
// ---------------------------------------------------------------------------

/// The core reverse-proxy that serves update metadata and stream-proxies
/// binary downloads from GitHub Releases.
pub struct TlgrmUpdateProxy {
    /// In-memory metadata cache (Telegram-format JSON + ETag).
    pub cache: Arc<RwLock<Option<CachedManifest>>>,
    /// Runtime configuration loaded from environment variables.
    pub config: Arc<Config>,
}

// ---------------------------------------------------------------------------
// ProxyHttp implementation
// ---------------------------------------------------------------------------

#[async_trait]
impl ProxyHttp for TlgrmUpdateProxy {
    type CTX = RequestCtx;

    fn new_ctx(&self) -> Self::CTX {
        RequestCtx {
            is_metadata: false,
            body_buffer: Vec::new(),
            handled: false,
        }
    }

    // ----- request_filter ---------------------------------------------------
    // Intercepts requests that can be handled without going upstream:
    //   - /health           -> cache status JSON
    //   - /purge            -> clear caches
    //   - /webhook/github   -> verify signature, purge on release events
    //   - /current*         -> serve from cache if fresh
    // Everything else falls through to upstream_peer.
    // ------------------------------------------------------------------------

    async fn request_filter(
        &self,
        session: &mut Session,
        ctx: &mut Self::CTX,
    ) -> Result<bool> {
        let path = session.req_header().uri.path().to_string();

        // --- Binary download: fall through to upstream_peer ---
        if extract_update_filename(&path).is_some() {
            ctx.is_metadata = false;
            return Ok(false);
        }

        // --- /health ---
        if path == "/health" {
            let cache_guard = self.cache.read().await;
            let status = match cache_guard.as_ref() {
                Some(c) => format!(
                    "{{\"status\":\"ok\",\"cache_age_sec\":{},\"has_etag\":{}}}",
                    c.fetched_at.elapsed().as_secs(),
                    c.etag.is_some()
                ),
                None => "{\"status\":\"ok\",\"cache\":\"empty\"}".to_string(),
            };
            drop(cache_guard);
            self.send_json(session, 200, &status).await?;
            return Ok(true);
        }

        // --- /purge (POST) ---
        if path == "/purge" {
            webhook::purge_caches(&self.config, &self.cache).await;
            self.send_json(session, 200, "{\"purged\":true}").await?;
            return Ok(true);
        }

        // --- /webhook/github (POST) ---
        if path == "/webhook/github" {
            return self.handle_webhook(session).await;
        }

        // --- /current4, /current2, /current  (metadata) ---
        if path == "/current4" || path == "/current2" || path == "/current" {
            ctx.is_metadata = true;

            let cache_guard = self.cache.read().await;
            if let Some(ref cached) = *cache_guard {
                if cached.fetched_at.elapsed() < self.config.cache_ttl {
                    let body = Bytes::from(cached.json.clone());
                    let mut resp = ResponseHeader::build(200, None)?;
                    resp.insert_header("Content-Type", "application/json")?;
                    resp.insert_header(
                        "Cache-Control",
                        &self.config.metadata_cache_control(),
                    )?;
                    resp.insert_header("Content-Length", &body.len().to_string())?;
                    resp.insert_header("X-Cache", "HIT")?;
                    session
                        .write_response_header(Box::new(resp), false)
                        .await?;
                    session.write_response_body(Some(body), true).await?;
                    log::info!("Metadata cache HIT for {}", path);
                    return Ok(true);
                }
            }
            drop(cache_guard);
            // Cache miss — fall through to upstream_peer.
            log::info!("Metadata cache MISS for {}", path);
            return Ok(false);
        }

        // --- /api/latest (JSON version info derived from cache) ---
        if path == "/api/latest" {
            return self.handle_api_latest(session).await;
        }

        // --- Static file serving (lowest priority fallback) ---
        if self.config.site_enabled {
            return self.handle_static_file(session, &path).await;
        }

        Ok(false)
    }

    // ----- upstream_peer ----------------------------------------------------
    // Route to the appropriate upstream based on request type:
    //   - Metadata -> api.github.com:443
    //   - Binaries -> github.com:443
    // ------------------------------------------------------------------------

    async fn upstream_peer(
        &self,
        _session: &mut Session,
        ctx: &mut Self::CTX,
    ) -> Result<Box<HttpPeer>> {
        if ctx.is_metadata {
            let peer = HttpPeer::new(
                (METADATA_UPSTREAM.0, METADATA_UPSTREAM.1),
                true,
                METADATA_UPSTREAM.0.to_string(),
            );
            Ok(Box::new(peer))
        } else {
            let peer = HttpPeer::new(
                (BINARY_UPSTREAM.0, BINARY_UPSTREAM.1),
                true,
                BINARY_UPSTREAM.0.to_string(),
            );
            Ok(Box::new(peer))
        }
    }

    // ----- upstream_request_filter ------------------------------------------
    // Rewrite path + headers for the upstream.
    // ------------------------------------------------------------------------

    async fn upstream_request_filter(
        &self,
        _session: &mut Session,
        upstream_request: &mut RequestHeader,
        ctx: &mut Self::CTX,
    ) -> Result<()> {
        if ctx.is_metadata {
            let new_uri = self.config.releases_api_path();
            upstream_request.set_uri(
                new_uri.parse().unwrap(), // safe: URI constructed from known-good config
            );
            upstream_request.insert_header("Host", METADATA_UPSTREAM.0)?;
            upstream_request.insert_header("User-Agent", PROXY_USER_AGENT)?;
            upstream_request.insert_header("Accept", "application/vnd.github+json")?;

            // Conditional request with ETag (saves rate limit).
            let cache_guard = self.cache.read().await;
            if let Some(ref cached) = *cache_guard {
                if let Some(ref etag) = cached.etag {
                    upstream_request.insert_header("If-None-Match", etag)?;
                }
            }
        } else {
            // Binary: rewrite path to GitHub release asset URL.
            let path = upstream_request.uri.path().to_string();
            let filename = path.strip_prefix('/').unwrap_or(&path);
            let version_int = extract_version_int(filename);
            let tag = version_int_to_tag(version_int);
            let new_uri = self.config.asset_download_path(&tag, filename);
            upstream_request.set_uri(
                new_uri.parse().unwrap(), // safe: URI constructed from known-good config
            );
            upstream_request.insert_header("Host", BINARY_UPSTREAM.0)?;
            upstream_request.insert_header("User-Agent", PROXY_USER_AGENT)?;
        }

        // Authenticated requests (5 000/hr vs 60/hr).
        if let Some(ref token) = self.config.github_token {
            upstream_request
                .insert_header("Authorization", &format!("Bearer {}", token))?;
        }

        Ok(())
    }

    // ----- response_filter --------------------------------------------------
    // Adjust response headers before they reach the client.
    // ------------------------------------------------------------------------

    async fn response_filter(
        &self,
        _session: &mut Session,
        upstream_response: &mut ResponseHeader,
        ctx: &mut Self::CTX,
    ) -> Result<()> {
        if ctx.is_metadata && upstream_response.status.as_u16() == 200 {
            // Body size changes after JSON transformation; remove
            // Content-Length and switch to chunked.
            upstream_response.remove_header("Content-Length");
            upstream_response.insert_header("Transfer-Encoding", "chunked")?;
        }

        if !ctx.is_metadata {
            // Binary response: add cache headers for Cloudflare edge and the
            // client, strip headers that reveal the storage backend.
            upstream_response.insert_header(
                "Cache-Control",
                &self.config.binary_cache_control(),
            )?;
            for hdr in STRIPPED_UPSTREAM_HEADERS {
                upstream_response.remove_header(*hdr);
            }
        }

        Ok(())
    }

    // ----- response_body_filter ---------------------------------------------
    // Buffer metadata response bodies, then transform to Telegram format.
    // Binary responses pass through untouched (streamed).
    // ------------------------------------------------------------------------

    fn response_body_filter(
        &self,
        _session: &mut Session,
        body: &mut Option<Bytes>,
        end_of_stream: bool,
        ctx: &mut Self::CTX,
    ) -> Result<Option<Duration>> {
        if !ctx.is_metadata {
            return Ok(None); // binary — stream through
        }

        // Buffer chunks.
        if let Some(ref b) = body {
            ctx.body_buffer.extend_from_slice(b);
            *body = Some(Bytes::new()); // consume, don't forward yet
        }

        if end_of_stream && !ctx.body_buffer.is_empty() {
            match transform_github_to_telegram(&ctx.body_buffer) {
                Ok(telegram_json) => {
                    // Persist to disk so cache survives restarts.
                    cache::save_disk_cache(&self.config, &telegram_json, &None);

                    // Update in-memory cache.
                    let json_clone = telegram_json.clone();
                    let cache = Arc::clone(&self.cache);
                    // We cannot .await inside a non-async fn, so spawn a task.
                    tokio::spawn(async move {
                        let mut guard = cache.write().await;
                        *guard = Some(CachedManifest {
                            json: json_clone,
                            etag: None,
                            fetched_at: std::time::Instant::now(),
                        });
                    });

                    *body = Some(Bytes::from(telegram_json));
                }
                Err(e) => {
                    log::error!("JSON transform failed: {}", e);
                    let error = format!("{{\"error\":\"{}\"}}", e);
                    *body = Some(Bytes::from(error));
                }
            }
        }

        Ok(None)
    }
}

// ---------------------------------------------------------------------------
// Helper methods
// ---------------------------------------------------------------------------

impl TlgrmUpdateProxy {
    /// Send a complete JSON response and signal end-of-stream.
    async fn send_json(
        &self,
        session: &mut Session,
        status: u16,
        json_body: &str,
    ) -> Result<()> {
        let body = Bytes::from(json_body.to_string());
        let mut resp = ResponseHeader::build(status, None)?;
        resp.insert_header("Content-Type", "application/json")?;
        resp.insert_header("Content-Length", &body.len().to_string())?;
        session
            .write_response_header(Box::new(resp), false)
            .await?;
        session.write_response_body(Some(body), true).await?;
        Ok(())
    }

    /// Send a complete response with arbitrary content-type and body.
    async fn send_response(
        &self,
        session: &mut Session,
        status: u16,
        content_type: &str,
        cache_control: &str,
        body_bytes: Bytes,
    ) -> Result<()> {
        let mut resp = ResponseHeader::build(status, None)?;
        resp.insert_header("Content-Type", content_type)?;
        resp.insert_header("Cache-Control", cache_control)?;
        resp.insert_header("Content-Length", &body_bytes.len().to_string())?;
        session
            .write_response_header(Box::new(resp), false)
            .await?;
        session.write_response_body(Some(body_bytes), true).await?;
        Ok(())
    }

    /// Handle `GET /api/latest` — return JSON derived from the cached update
    /// manifest (version string, version_int, download URLs, platforms).
    async fn handle_api_latest(
        &self,
        session: &mut Session,
    ) -> Result<bool> {
        let cache_guard = self.cache.read().await;
        let cached = match cache_guard.as_ref() {
            Some(c) => c,
            None => {
                drop(cache_guard);
                self.send_json(session, 503, "{\"error\":\"no cached manifest available\"}")
                    .await?;
                return Ok(true);
            }
        };

        // Parse the Telegram-format cached JSON to extract fields.
        let manifest: serde_json::Value = match serde_json::from_str(&cached.json) {
            Ok(v) => v,
            Err(e) => {
                drop(cache_guard);
                let err = format!("{{\"error\":\"corrupt cache: {}\"}}", e);
                self.send_json(session, 500, &err).await?;
                return Ok(true);
            }
        };

        // Build a user-friendly /api/latest response.
        let mut platforms = serde_json::Map::new();
        let mut version_int: Option<&str> = None;

        // Map internal platform keys to friendly names.
        let platform_map: &[(&str, &str)] = &[
            ("armac", "macos_arm64"),
            ("mac", "macos_x86_64"),
        ];

        let public_domain = &self.config.public_domain;

        for (key, friendly) in platform_map {
            if let Some(released) = manifest[key]["stable"]["released"].as_str() {
                if version_int.is_none() {
                    version_int = Some(released);
                }
                let link = manifest[key]["stable"]["link"]
                    .as_str()
                    .unwrap_or("");
                let download_url = format!("https://{}{}", public_domain, link);
                platforms.insert(
                    friendly.to_string(),
                    serde_json::json!({
                        "download_url": download_url,
                        "filename": link.strip_prefix('/').unwrap_or(link),
                    }),
                );
            }
        }

        let vi_str = version_int.unwrap_or("0");
        let vi_num: u64 = vi_str.parse().unwrap_or(0);
        let version_tag = crate::transform::version_int_to_tag(vi_num);

        let response = serde_json::json!({
            "version": version_tag,
            "version_int": vi_num,
            "platforms": platforms,
        });

        drop(cache_guard);
        let body = serde_json::to_string(&response).unwrap_or_else(|_| "{}".to_string());
        self.send_json(session, 200, &body).await?;
        Ok(true)
    }

    /// Serve a static file from `SITE_ROOT`.  Directory requests serve
    /// `index.html`.  Returns `Ok(true)` if the file was served, `Ok(true)`
    /// with a 404 if the file was not found (we always handle the request so
    /// it never falls through to upstream).
    async fn handle_static_file(
        &self,
        session: &mut Session,
        url_path: &str,
    ) -> Result<bool> {
        let site_root = Path::new(&self.config.site_root);

        // Decode percent-encoded path and normalise.
        let decoded = percent_decode(url_path);
        let relative = decoded.strip_prefix('/').unwrap_or(&decoded);
        let mut file_path: PathBuf = site_root.join(relative);

        // Prevent directory traversal: resolved path must be under site_root.
        let canonical_root = match std::fs::canonicalize(site_root) {
            Ok(p) => p,
            Err(_) => {
                log::warn!("SITE_ROOT {} does not exist on disk", self.config.site_root);
                self.send_not_found(session).await?;
                return Ok(true);
            }
        };

        // If path is a directory, append index.html.
        if file_path.is_dir() {
            file_path.push("index.html");
        }

        // If no extension and file doesn't exist, try appending .html
        // (for clean URLs like /about -> /about.html).
        if !file_path.exists() && file_path.extension().is_none() {
            let mut with_html = file_path.clone();
            with_html.set_extension("html");
            if with_html.exists() {
                file_path = with_html;
            }
        }

        // Canonicalize the resolved file path and check traversal.
        let canonical_file = match std::fs::canonicalize(&file_path) {
            Ok(p) => p,
            Err(_) => {
                self.send_not_found(session).await?;
                return Ok(true);
            }
        };

        if !canonical_file.starts_with(&canonical_root) {
            log::warn!(
                "Path traversal blocked: {} resolved to {}",
                url_path,
                canonical_file.display()
            );
            self.send_not_found(session).await?;
            return Ok(true);
        }

        // Read the file.
        let content = match tokio::fs::read(&canonical_file).await {
            Ok(c) => c,
            Err(_) => {
                self.send_not_found(session).await?;
                return Ok(true);
            }
        };

        // Determine MIME type from extension.
        let mime = mime_guess::from_path(&canonical_file)
            .first_raw()
            .unwrap_or("application/octet-stream");

        // Cache-Control: hashed filenames get immutable long-cache,
        // everything else gets a short TTL.
        let filename = canonical_file
            .file_name()
            .and_then(|n| n.to_str())
            .unwrap_or("");
        let cache_control = if is_hashed_filename(filename) {
            "public, max-age=31536000, immutable"
        } else {
            "public, max-age=300"
        };

        log::debug!("Static: {} -> {} ({})", url_path, canonical_file.display(), mime);

        self.send_response(
            session,
            200,
            mime,
            cache_control,
            Bytes::from(content),
        )
        .await?;
        Ok(true)
    }

    /// Send a minimal 404 HTML response.
    async fn send_not_found(&self, session: &mut Session) -> Result<()> {
        let body = Bytes::from_static(b"<!doctype html><title>404</title><h1>404 Not Found</h1>");
        let mut resp = ResponseHeader::build(404, None)?;
        resp.insert_header("Content-Type", "text/html; charset=utf-8")?;
        resp.insert_header("Cache-Control", "no-cache")?;
        resp.insert_header("Content-Length", &body.len().to_string())?;
        session
            .write_response_header(Box::new(resp), false)
            .await?;
        session.write_response_body(Some(body), true).await?;
        Ok(())
    }

    /// Handle `POST /webhook/github` entirely within `request_filter`.
    ///
    /// Reads the full POST body via the downstream session, verifies the
    /// HMAC signature, and dispatches the event.
    async fn handle_webhook(
        &self,
        session: &mut Session,
    ) -> Result<bool> {
        // Buffer the full request body from the downstream connection.
        let mut body_buf = Vec::new();
        loop {
            let body = session.read_request_body().await?;
            match body {
                Some(bytes) if !bytes.is_empty() => body_buf.extend_from_slice(&bytes),
                _ => break,
            }
        }

        if body_buf.is_empty() {
            self.send_json(session, 400, "{\"error\":\"empty body\"}")
                .await?;
            return Ok(true);
        }

        // Verify signature.
        let secret = match &self.config.webhook_secret {
            Some(s) => s.clone(),
            None => {
                log::warn!("Webhook received but WEBHOOK_SECRET is not configured");
                self.send_json(session, 500, "{\"error\":\"webhook secret not configured\"}")
                    .await?;
                return Ok(true);
            }
        };

        let sig_header = session
            .req_header()
            .headers
            .get("X-Hub-Signature-256")
            .and_then(|v| v.to_str().ok())
            .unwrap_or("")
            .to_string();

        if !webhook::verify_signature(&secret, &sig_header, &body_buf) {
            log::warn!("Webhook signature verification failed");
            self.send_json(session, 403, "{\"error\":\"forbidden\"}")
                .await?;
            return Ok(true);
        }

        let status_msg =
            webhook::handle_webhook_payload(&self.config, &self.cache, &body_buf).await;
        let resp_body = format!("{{\"status\":\"{}\"}}", status_msg);
        self.send_json(session, 200, &resp_body).await?;
        Ok(true)
    }
}

// ---------------------------------------------------------------------------
// Free helper functions
// ---------------------------------------------------------------------------

/// Detect hashed asset filenames — patterns like `main.a1b2c3.js` or
/// `style.d4e5f6.css` where a hex hash segment sits between two dots.
fn is_hashed_filename(filename: &str) -> bool {
    let parts: Vec<&str> = filename.split('.').collect();
    if parts.len() < 3 {
        return false;
    }
    // Check if any middle segment looks like a content hash (hex, 6-32 chars).
    parts[1..parts.len() - 1].iter().any(|segment| {
        let len = segment.len();
        (6..=32).contains(&len) && segment.chars().all(|c| c.is_ascii_hexdigit())
    })
}

/// Minimal percent-decoding (spaces, common URL-encoded chars).
fn percent_decode(input: &str) -> String {
    let mut result = String::with_capacity(input.len());
    let mut chars = input.bytes();
    while let Some(b) = chars.next() {
        if b == b'%' {
            let hi = chars.next();
            let lo = chars.next();
            if let (Some(h), Some(l)) = (hi, lo) {
                let hex = [h, l];
                if let Ok(s) = std::str::from_utf8(&hex) {
                    if let Ok(val) = u8::from_str_radix(s, 16) {
                        result.push(val as char);
                        continue;
                    }
                }
                // Malformed percent-encoding: emit as-is.
                result.push('%');
                result.push(h as char);
                result.push(l as char);
            } else {
                result.push('%');
            }
        } else {
            result.push(b as char);
        }
    }
    result
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_is_hashed_filename() {
        assert!(is_hashed_filename("main.a1b2c3.js"));
        assert!(is_hashed_filename("style.d4e5f6ab.css"));
        assert!(is_hashed_filename("chunk.abc123def456.js"));
        assert!(!is_hashed_filename("index.html"));
        assert!(!is_hashed_filename("robots.txt"));
        assert!(!is_hashed_filename("favicon.ico"));
        assert!(!is_hashed_filename("no-dots"));
        // Short segments (< 6 chars) should not match.
        assert!(!is_hashed_filename("app.v2.js"));
    }

    #[test]
    fn test_percent_decode() {
        assert_eq!(percent_decode("/hello%20world"), "/hello world");
        assert_eq!(percent_decode("/no-encoding"), "/no-encoding");
        assert_eq!(percent_decode("/%2Fetc%2Fpasswd"), "//etc/passwd");
    }
}

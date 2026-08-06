use std::time::Duration;

// ---------------------------------------------------------------------------
// Compile-time constants
// ---------------------------------------------------------------------------

/// Recognized update-binary filename prefixes.
/// Used for request routing, asset extraction, and Cloudflare purge URL
/// construction.
pub const PLATFORM_ASSET_PREFIXES: &[&str] = &["tarmacupd", "tmacupd"];

/// GitHub Releases API path template.  `{repo}` is replaced at runtime with
/// `Config::github_repo`.
pub const GITHUB_RELEASES_API_PATH: &str = "/repos/{repo}/releases/latest";

/// GitHub asset download URL template.
pub const GITHUB_ASSET_URL_TEMPLATE: &str = "/{repo}/releases/download/{tag}/{filename}";

/// Cloudflare cache-purge endpoint template.
pub const CLOUDFLARE_PURGE_API_URL: &str =
    "https://api.cloudflare.com/client/v4/zones/{zone_id}/purge_cache";

/// Upstream for metadata (GitHub Releases API).
pub const METADATA_UPSTREAM: (&str, u16) = ("api.github.com", 443);

/// Upstream for binary asset downloads.
pub const BINARY_UPSTREAM: (&str, u16) = ("github.com", 443);

/// User-Agent sent to all upstreams.  Built from `Cargo.toml` version.
pub const PROXY_USER_AGENT: &str =
    concat!("tlgrm-update-proxy/", env!("CARGO_PKG_VERSION"));

/// Headers stripped from binary responses to conceal the storage backend.
pub const STRIPPED_UPSTREAM_HEADERS: &[&str] = &[
    "X-GitHub-Request-Id",
    "X-Served-By",
    "X-Cache",
    "X-Timer",
    "X-Fastly-Request-ID",
    "Via",
    "Server",
];

/// Disk-cache filename (appended to `Config::cache_dir`).
pub const CACHE_FILENAME: &str = "current.json";

// ---------------------------------------------------------------------------
// Runtime configuration (loaded from environment variables)
// ---------------------------------------------------------------------------

/// All runtime-tuneable values.  Every field is populated from an environment
/// variable or its documented default.
#[derive(Debug, Clone)]
pub struct Config {
    /// `LISTEN_ADDR` — TCP bind address.  Default `0.0.0.0:8080`.
    pub listen_addr: String,

    /// `GITHUB_REPO` — GitHub `owner/repo`.  Default `CelestialTech/tlgrm`.
    pub github_repo: String,

    /// `GITHUB_TOKEN` — optional PAT for authenticated requests (5 000 req/hr
    /// instead of 60).
    pub github_token: Option<String>,

    /// `PUBLIC_DOMAIN` — public-facing domain used when constructing
    /// Cloudflare purge URLs.  Default `updates.71grm.site`.
    pub public_domain: String,

    /// `CACHE_DIR` — directory for the on-disk metadata cache.
    /// Default `/data/cache`.
    pub cache_dir: String,

    /// `CACHE_TTL_SECS` — in-memory + disk metadata cache lifetime.
    /// Default 3 600 (1 hour).
    pub cache_ttl: Duration,

    /// `CF_EDGE_CACHE_SECS` — `s-maxage` on binary responses (Cloudflare edge
    /// TTL).  Default 604 800 (7 days).
    pub cf_edge_cache_secs: u64,

    /// `CLIENT_BINARY_CACHE_SECS` — `max-age` on binary responses.
    /// Default 86 400 (1 day).
    pub client_binary_cache_secs: u64,

    /// `METADATA_CLIENT_CACHE_SECS` — `max-age` on metadata responses.
    /// Default 300 (5 minutes).
    pub metadata_client_cache_secs: u64,

    /// `WEBHOOK_SECRET` — HMAC-SHA256 secret for GitHub webhook signature
    /// verification.
    pub webhook_secret: Option<String>,

    /// `CLOUDFLARE_ZONE_ID` — required for edge cache purge on release
    /// deletion.
    pub cloudflare_zone_id: Option<String>,

    /// `CLOUDFLARE_API_TOKEN` — CF token with Cache Purge permission.
    pub cloudflare_api_token: Option<String>,

    /// Full path to the disk-cache JSON file (derived:
    /// `{cache_dir}/{CACHE_FILENAME}`).
    pub cache_file_path: String,

    /// `SITE_ROOT` — filesystem directory for static website files.
    /// Default `/var/www/71grm.site/`.
    pub site_root: String,

    /// `SITE_ENABLED` — enable the static-file landing page route.
    /// Default `true`.
    pub site_enabled: bool,
}

impl Config {
    /// Build a `Config` from environment variables, applying defaults where a
    /// variable is unset or empty.
    pub fn from_env() -> Self {
        let cache_dir = env_or("CACHE_DIR", "/data/cache");
        let cache_file_path = format!("{}/{}", cache_dir, CACHE_FILENAME);

        Config {
            listen_addr: env_or("LISTEN_ADDR", "0.0.0.0:8080"),
            github_repo: env_or("GITHUB_REPO", "CelestialTech/tlgrm"),
            github_token: env_opt("GITHUB_TOKEN"),
            public_domain: env_or("PUBLIC_DOMAIN", "updates.71grm.site"),
            cache_dir,
            cache_ttl: Duration::from_secs(env_u64("CACHE_TTL_SECS", 3600)),
            cf_edge_cache_secs: env_u64("CF_EDGE_CACHE_SECS", 604800),
            client_binary_cache_secs: env_u64("CLIENT_BINARY_CACHE_SECS", 86400),
            metadata_client_cache_secs: env_u64("METADATA_CLIENT_CACHE_SECS", 300),
            webhook_secret: env_opt("WEBHOOK_SECRET"),
            cloudflare_zone_id: env_opt("CLOUDFLARE_ZONE_ID"),
            cloudflare_api_token: env_opt("CLOUDFLARE_API_TOKEN"),
            cache_file_path,
            site_root: env_or("SITE_ROOT", "/var/www/71grm.site/"),
            site_enabled: env_bool("SITE_ENABLED", true),
        }
    }

    /// Resolved GitHub Releases API path (e.g. `/repos/CelestialTech/tlgrm/releases/latest`).
    pub fn releases_api_path(&self) -> String {
        GITHUB_RELEASES_API_PATH.replace("{repo}", &self.github_repo)
    }

    /// Resolved asset download path for a given tag and filename.
    pub fn asset_download_path(&self, tag: &str, filename: &str) -> String {
        GITHUB_ASSET_URL_TEMPLATE
            .replace("{repo}", &self.github_repo)
            .replace("{tag}", tag)
            .replace("{filename}", filename)
    }

    /// Cloudflare purge API URL (returns `None` if zone ID is not configured).
    pub fn cloudflare_purge_url(&self) -> Option<String> {
        self.cloudflare_zone_id.as_ref().map(|zid| {
            CLOUDFLARE_PURGE_API_URL.replace("{zone_id}", zid)
        })
    }

    /// `Cache-Control` header value for binary responses.
    pub fn binary_cache_control(&self) -> String {
        format!(
            "public, s-maxage={}, max-age={}",
            self.cf_edge_cache_secs, self.client_binary_cache_secs
        )
    }

    /// `Cache-Control` header value for metadata responses.
    pub fn metadata_cache_control(&self) -> String {
        format!("public, max-age={}", self.metadata_client_cache_secs)
    }
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

fn env_or(key: &str, default: &str) -> String {
    std::env::var(key)
        .ok()
        .filter(|v| !v.is_empty())
        .unwrap_or_else(|| default.to_string())
}

fn env_opt(key: &str) -> Option<String> {
    std::env::var(key).ok().filter(|v| !v.is_empty())
}

fn env_u64(key: &str, default: u64) -> u64 {
    std::env::var(key)
        .ok()
        .and_then(|v| v.parse::<u64>().ok())
        .unwrap_or(default)
}

fn env_bool(key: &str, default: bool) -> bool {
    match std::env::var(key).ok().filter(|v| !v.is_empty()) {
        Some(v) => matches!(v.as_str(), "1" | "true" | "yes" | "on"),
        None => default,
    }
}

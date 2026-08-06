mod cache;
mod config;
mod proxy;
mod transform;
mod webhook;

use std::sync::Arc;

use pingora::server::Server;
use tokio::sync::RwLock;

use cache::{disk_cache_to_manifest, load_disk_cache, CachedManifest};
use config::Config;
use proxy::TlgrmUpdateProxy;

fn main() {
    env_logger::init();

    let config = Config::from_env();
    log::info!("Starting {} with config:", config::PROXY_USER_AGENT);
    log::info!("  LISTEN_ADDR          = {}", config.listen_addr);
    log::info!("  GITHUB_REPO          = {}", config.github_repo);
    log::info!("  PUBLIC_DOMAIN        = {}", config.public_domain);
    log::info!("  CACHE_DIR            = {}", config.cache_dir);
    log::info!("  CACHE_TTL_SECS       = {}", config.cache_ttl.as_secs());
    log::info!("  CF_EDGE_CACHE_SECS   = {}", config.cf_edge_cache_secs);
    log::info!("  CLIENT_BINARY_CACHE  = {}", config.client_binary_cache_secs);
    log::info!("  METADATA_CLIENT_CACHE= {}", config.metadata_client_cache_secs);
    log::info!(
        "  GITHUB_TOKEN         = {}",
        if config.github_token.is_some() { "set" } else { "unset" }
    );
    log::info!(
        "  WEBHOOK_SECRET       = {}",
        if config.webhook_secret.is_some() { "set" } else { "unset" }
    );
    log::info!(
        "  CLOUDFLARE_ZONE_ID   = {}",
        if config.cloudflare_zone_id.is_some() { "set" } else { "unset" }
    );

    // Load disk cache on startup to avoid a cold-start GitHub API hit.
    let initial_cache: Option<CachedManifest> =
        load_disk_cache(&config).map(disk_cache_to_manifest);

    if initial_cache.is_some() {
        log::info!("Warm start: disk cache loaded successfully");
    } else {
        log::info!("Cold start: no usable disk cache");
    }

    let listen_addr = config.listen_addr.clone();

    let proxy = TlgrmUpdateProxy {
        cache: Arc::new(RwLock::new(initial_cache)),
        config: Arc::new(config),
    };

    let mut server = Server::new(None).unwrap();
    server.bootstrap();

    let mut service =
        pingora::proxy::http_proxy_service(&server.configuration, proxy);
    service.add_tcp(&listen_addr);

    server.add_service(service);

    log::info!("Listening on {}", listen_addr);
    server.run_forever();
}

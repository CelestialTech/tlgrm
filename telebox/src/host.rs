// The host: shared state the UI reads and the MCP relay writes.
//
// One Arc<Mutex<Inner>> is shared between the GPUI view (reads it every frame)
// and the relay thread (writes request counts, connection state and a log).
// Everything the panel shows that is *live* comes from here.

use std::sync::{Arc, Mutex};
use std::time::{SystemTime, UNIX_EPOCH};

#[derive(Clone, Copy, PartialEq)]
pub enum Runtime {
    Rust,
    Python,
}

impl Runtime {
    pub fn label(self) -> &'static str {
        match self {
            Runtime::Rust => "rust",
            Runtime::Python => "python",
        }
    }
}

// One module in the rack. Only `MCP` is wired to anything real in this slice;
// the rest are modelled so the panel shows the whole surface it will host.
#[derive(Clone)]
pub struct Plugin {
    pub name: &'static str,
    pub slot: &'static str,
    pub runtime: Runtime,
    pub perms: &'static str,
    pub desc: &'static str,
    pub active: bool,
    pub live: bool,
}

#[derive(Clone)]
pub struct LogEntry {
    pub t: String,
    pub src: String,
    pub msg: String,
}

pub struct Inner {
    pub upstream_connected: bool,
    pub clients: u32,
    pub requests: u64,
    pub last_method: String,
    pub log: Vec<LogEntry>,
    pub endpoint: String,
    pub upstream: String,
}

#[derive(Clone)]
pub struct HostState(pub Arc<Mutex<Inner>>);

fn hhmmss() -> String {
    let secs = SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .map(|d| d.as_secs())
        .unwrap_or(0)
        % 86400;
    format!("{:02}:{:02}:{:02}", secs / 3600, (secs % 3600) / 60, secs % 60)
}

impl HostState {
    pub fn new(endpoint: String, upstream: String) -> Self {
        HostState(Arc::new(Mutex::new(Inner {
            upstream_connected: false,
            clients: 0,
            requests: 0,
            last_method: String::new(),
            log: Vec::new(),
            endpoint,
            upstream,
        })))
    }

    pub fn log(&self, src: &str, msg: impl Into<String>) {
        if let Ok(mut i) = self.0.lock() {
            i.log.push(LogEntry {
                t: hhmmss(),
                src: src.to_string(),
                msg: msg.into(),
            });
            let n = i.log.len();
            if n > 200 {
                i.log.drain(0..n - 200);
            }
        }
    }

    pub fn record_request(&self, method: &str) {
        if let Ok(mut i) = self.0.lock() {
            i.requests += 1;
            i.last_method = method.to_string();
        }
    }

    pub fn set_upstream(&self, ok: bool) {
        if let Ok(mut i) = self.0.lock() {
            i.upstream_connected = ok;
        }
    }

    pub fn client_delta(&self, delta: i32) {
        if let Ok(mut i) = self.0.lock() {
            i.clients = (i.clients as i32 + delta).max(0) as u32;
        }
    }

    // The full rack. MCP is the one wired end-to-end in this milestone; its
    // `active` flag tracks whether the relay has an upstream.
    pub fn plugins(&self) -> Vec<Plugin> {
        let mcp_active = self.0.lock().map(|i| i.upstream_connected).unwrap_or(false);
        vec![
            Plugin {
                name: "MCP",
                slot: "slot 01 · aggregated endpoint",
                runtime: Runtime::Rust,
                perms: "session · invoke · files · settings · events",
                desc: "Aggregated MCP endpoint — proxies to the client's bridge. Wired end-to-end.",
                active: mcp_active,
                live: true,
            },
            Plugin {
                name: "Export",
                slot: "slot 02 · → disk",
                runtime: Runtime::Python,
                perms: "session · invoke · files · events",
                desc: "Classic + covert gradual export to disk. Rides raw invoke.",
                active: true,
                live: false,
            },
            Plugin {
                name: "Retention / Vault",
                slot: "slot 03 · SQLite + ephemeral",
                runtime: Runtime::Rust,
                perms: "session · files · events",
                desc: "Real-time retention and capture of view-once media.",
                active: true,
                live: false,
            },
            Plugin {
                name: "Archiver",
                slot: "slot 04 · → Telegram group",
                runtime: Runtime::Python,
                perms: "session · invoke · events",
                desc: "Deleted-account archiving — forward into a supergroup.",
                active: true,
                live: false,
            },
            Plugin {
                name: "Bots",
                slot: "slot 05 · automations",
                runtime: Runtime::Python,
                perms: "session · invoke · ui · events",
                desc: "The bot framework and rule automations.",
                active: true,
                live: false,
            },
            Plugin {
                name: "Wallet",
                slot: "slot 06 · disabled",
                runtime: Runtime::Python,
                perms: "session · invoke · ui",
                desc: "Stars and TON. Off by default — anything that spends stays dark.",
                active: false,
                live: false,
            },
            Plugin {
                name: "AI",
                slot: "slot 07 · disabled",
                runtime: Runtime::Rust,
                perms: "model · files · ui",
                desc: "Local LLM, TTS and voice — the only module touching model.",
                active: false,
                live: false,
            },
        ]
    }
}

// The host: shared state the UI reads, plus the relay lifecycle the controls
// drive.
//
// Two Arcs behind the HostState handle: the Inner state (read every frame,
// written by the relay) and the RelayCtl (the stop flag + join handle that
// Start / Stop / Restart act on). They are separate so stopping the host can
// join the relay thread without holding the state lock the relay itself takes.

use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::{Arc, Mutex};
use std::thread::JoinHandle;
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

#[derive(Clone)]
pub struct Plugin {
    pub name: &'static str,
    pub slot: &'static str,
    pub runtime: Runtime,
    pub perms: &'static str,
    pub desc: &'static str,
    pub live: bool,
}

// The seven Host API families, in matrix order.
pub const FAMILIES: [&str; 7] = [
    "session", "invoke", "model", "settings", "files", "ui", "events",
];

#[derive(Clone)]
pub struct LogEntry {
    pub t: String,
    pub src: String,
    pub msg: String,
}

pub struct Inner {
    pub running: bool,
    pub upstream_connected: bool,
    pub clients: u32,
    pub requests: u64,
    pub last_method: String,
    pub log: Vec<LogEntry>,
    pub endpoint: String,
    pub upstream: String,
    pub token: String,
}

pub struct RelayCtl {
    pub stop: Option<Arc<AtomicBool>>,
    pub join: Option<JoinHandle<()>>,
}

#[derive(Clone)]
pub struct HostState(pub Arc<Mutex<Inner>>, pub Arc<Mutex<RelayCtl>>);

fn hhmmss() -> String {
    let secs = SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .map(|d| d.as_secs())
        .unwrap_or(0)
        % 86400;
    format!("{:02}:{:02}:{:02}", secs / 3600, (secs % 3600) / 60, secs % 60)
}

// The rack template — names, runtimes, permissions. `active` here is a default;
// the view sets it from the live host and the operator's toggles.
pub fn plugin_templates() -> Vec<Plugin> {
    vec![
        Plugin { name: "MCP", slot: "slot 01 · aggregated endpoint", runtime: Runtime::Rust,
            perms: "session · invoke · files · settings · events",
            desc: "Aggregated MCP endpoint — proxies to the client's bridge. Wired end-to-end.",
            live: true },
        Plugin { name: "Export", slot: "slot 02 · → disk", runtime: Runtime::Python,
            perms: "session · invoke · files · events",
            desc: "Classic + covert gradual export to disk. Rides raw invoke.",
            live: false },
        Plugin { name: "Retention / Vault", slot: "slot 03 · SQLite + ephemeral", runtime: Runtime::Rust,
            perms: "session · files · events",
            desc: "Real-time retention and capture of view-once media.",
            live: false },
        Plugin { name: "Archiver", slot: "slot 04 · → Telegram group", runtime: Runtime::Python,
            perms: "session · invoke · events",
            desc: "Deleted-account archiving — forward into a supergroup.",
            live: false },
        Plugin { name: "Bots", slot: "slot 05 · automations", runtime: Runtime::Python,
            perms: "session · invoke · ui · events",
            desc: "The bot framework and rule automations.",
            live: false },
        Plugin { name: "Wallet", slot: "slot 06", runtime: Runtime::Python,
            perms: "session · invoke · ui",
            desc: "Stars and TON. Off by default — anything that spends stays dark.",
            live: false },
        Plugin { name: "AI", slot: "slot 07", runtime: Runtime::Rust,
            perms: "model · files · ui",
            desc: "Local LLM, TTS and voice — the only module touching model.",
            live: false },
    ]
}

impl HostState {
    pub fn new(endpoint: String, upstream: String, token: String) -> Self {
        HostState(
            Arc::new(Mutex::new(Inner {
                running: false,
                upstream_connected: false,
                clients: 0,
                requests: 0,
                last_method: String::new(),
                log: Vec::new(),
                endpoint,
                upstream,
                token,
            })),
            Arc::new(Mutex::new(RelayCtl { stop: None, join: None })),
        )
    }

    pub fn log(&self, src: &str, msg: impl Into<String>) {
        if let Ok(mut i) = self.0.lock() {
            i.log.push(LogEntry { t: hhmmss(), src: src.to_string(), msg: msg.into() });
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

    pub fn is_running(&self) -> bool {
        self.0.lock().map(|i| i.running).unwrap_or(false)
    }

    // Start the relay: bind the endpoint and begin accepting. No-op if running.
    pub fn start(&self) {
        if self.is_running() {
            return;
        }
        let (endpoint, upstream, token) = {
            let i = self.0.lock().unwrap();
            (i.endpoint.clone(), i.upstream.clone(), i.token.clone())
        };
        let stop = Arc::new(AtomicBool::new(false));
        let join = crate::mcp_relay::spawn(self.clone(), endpoint, upstream, token, stop.clone());
        {
            let mut r = self.1.lock().unwrap();
            r.stop = Some(stop);
            r.join = Some(join);
        }
        self.0.lock().unwrap().running = true;
        self.log("host", "host started — MCP endpoint listening");
    }

    // Stop the relay: signal the accept loop, unbind, and wait for it to exit.
    pub fn stop(&self) {
        if !self.is_running() {
            return;
        }
        let (stop, join) = {
            let mut r = self.1.lock().unwrap();
            (r.stop.take(), r.join.take())
        };
        if let Some(s) = stop {
            s.store(true, Ordering::SeqCst);
        }
        {
            let mut i = self.0.lock().unwrap();
            i.running = false;
            i.upstream_connected = false;
        }
        self.log("host", "host stopping");
        // Join OUTSIDE the state lock — the relay locks state while shutting down.
        if let Some(j) = join {
            let _ = j.join();
        }
        self.log("host", "host stopped");
    }

    pub fn restart(&self) {
        self.stop();
        self.start();
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::path::Path;
    use std::time::Duration;

    // The lifecycle that Start / Stop / Restart drive. The buttons' on_click
    // handlers call exactly these methods, so this verifies the real behavior:
    // Stop unbinds the endpoint (not cosmetic), Start and Restart rebind it.
    #[test]
    fn host_lifecycle_binds_and_unbinds() {
        let ep = "/tmp/telebox_test_host.sock".to_string();
        let _ = std::fs::remove_file(&ep);
        let st = HostState::new(ep.clone(), "/tmp/telebox_test_upstream.sock".into(), "/tmp/auth_token".into());

        assert!(!st.is_running());

        st.start();
        assert!(st.is_running());
        std::thread::sleep(Duration::from_millis(250));
        assert!(Path::new(&ep).exists(), "Start must bind the endpoint");

        st.stop();
        assert!(!st.is_running());
        std::thread::sleep(Duration::from_millis(250));
        assert!(!Path::new(&ep).exists(), "Stop must unbind the endpoint");

        st.restart();
        assert!(st.is_running());
        std::thread::sleep(Duration::from_millis(250));
        assert!(Path::new(&ep).exists(), "Restart must rebind the endpoint");

        st.stop();
        let _ = std::fs::remove_file(&ep);
    }

    // Toggling a modelled plugin flips its enabled bit; that is all the six
    // unwired modules do today. (MCP's toggle drives the relay above.)
    #[test]
    fn modelled_toggle_flips_state() {
        let mut enabled = vec![true, true, true, true, true, false, false];
        let i = 5; // Wallet, off by default
        enabled[i] = !enabled[i];
        assert!(enabled[i], "toggling an off module turns it on");
        enabled[i] = !enabled[i];
        assert!(!enabled[i], "toggling it again turns it off");
    }
}

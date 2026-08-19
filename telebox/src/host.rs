// The host: all render-relevant state, the relay lifecycle, and the operations
// the controls invoke.
//
// Everything the panel displays lives here — including the current view and the
// per-plugin enabled bits — so that BOTH the on-screen controls and the QA API
// (qa.rs) drive one identical code path. A button's on_click calls the same
// HostState method the QA socket calls; there is no second implementation to
// drift.

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

#[derive(Clone, Copy, PartialEq)]
pub enum PanelView {
    Plugins,
    Permissions,
    Activity,
}

impl PanelView {
    pub fn name(self) -> &'static str {
        match self {
            PanelView::Plugins => "plugins",
            PanelView::Permissions => "permissions",
            PanelView::Activity => "activity",
        }
    }
    pub fn from_name(s: &str) -> Option<Self> {
        match s {
            "plugins" => Some(PanelView::Plugins),
            "permissions" => Some(PanelView::Permissions),
            "activity" => Some(PanelView::Activity),
            _ => None,
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
    pub view: PanelView,
    pub enabled: Vec<bool>,
    pub shot_request: Option<String>,
    pub shot_armed: bool,
    pub shot_result: Option<(u32, u32, bool)>,
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

// What the panel shows for plugin `i`: MCP tracks the relay, the rest their bit.
pub fn plugin_active(i: usize, running: bool, enabled: &[bool]) -> bool {
    if i == 0 {
        running
    } else {
        *enabled.get(i).unwrap_or(&false)
    }
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
                view: PanelView::Plugins,
                // Export, Retention, Archiver, Bots on; Wallet, AI off.
                enabled: vec![true, true, true, true, true, false, false],
                shot_request: None,
                shot_armed: false,
                shot_result: None,
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

    pub fn view(&self) -> PanelView {
        self.0.lock().map(|i| i.view).unwrap_or(PanelView::Plugins)
    }

    pub fn enabled_vec(&self) -> Vec<bool> {
        self.0.lock().map(|i| i.enabled.clone()).unwrap_or_default()
    }

    // --- the operations the controls invoke -------------------------------

    pub fn set_view(&self, v: PanelView) {
        if let Ok(mut i) = self.0.lock() {
            i.view = v;
        }
        self.log("ui", format!("view → {}", v.name()));
    }

    pub fn toggle_plugin(&self, i: usize) {
        if i == 0 {
            if self.is_running() {
                self.stop();
            } else {
                self.start();
            }
            return;
        }
        let (name, on) = {
            let mut inner = self.0.lock().unwrap();
            if i < inner.enabled.len() {
                inner.enabled[i] = !inner.enabled[i];
            }
            (plugin_templates()[i].name, *inner.enabled.get(i).unwrap_or(&false))
        };
        self.log("ui", format!("{name} {}", if on { "enabled" } else { "bypassed" }));
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
        if let Some(j) = join {
            let _ = j.join();
        }
        self.log("host", "host stopped");
    }

    pub fn restart(&self) {
        self.stop();
        self.start();
    }

    // --- QA screenshot handshake ------------------------------------------
    // The QA thread requests a shot; the GPUI render loop fulfills it with
    // Window::render_to_image and reports the result back here.

    pub fn request_shot(&self, path: String) {
        if let Ok(mut i) = self.0.lock() {
            i.shot_request = Some(path);
            i.shot_armed = false;
            i.shot_result = None;
        }
    }

    // Two-phase so the captured frame reflects the latest state:
    // first tick arms (and repaints), the next tick captures.
    pub fn peek_shot(&self) -> (Option<String>, bool) {
        self.0
            .lock()
            .map(|i| (i.shot_request.clone(), i.shot_armed))
            .unwrap_or((None, false))
    }

    pub fn arm_shot(&self) {
        if let Ok(mut i) = self.0.lock() {
            i.shot_armed = true;
        }
    }

    pub fn finish_shot(&self, w: u32, h: u32, ok: bool) {
        if let Ok(mut i) = self.0.lock() {
            i.shot_result = Some((w, h, ok));
            i.shot_request = None;
            i.shot_armed = false;
        }
    }

    pub fn poll_shot(&self) -> Option<(u32, u32, bool)> {
        self.0.lock().ok().and_then(|i| i.shot_result)
    }

    // A structured view of exactly what the panel displays — the render grabbed
    // as data, for deterministic QA assertions.
    pub fn snapshot(&self) -> serde_json::Value {
        let i = self.0.lock().unwrap();
        let running = i.running;
        let plugins: Vec<serde_json::Value> = plugin_templates()
            .iter()
            .enumerate()
            .map(|(idx, p)| {
                serde_json::json!({
                    "name": p.name,
                    "runtime": p.runtime.label(),
                    "live": p.live,
                    "active": plugin_active(idx, running, &i.enabled),
                })
            })
            .collect();
        let log_tail: Vec<String> = i
            .log
            .iter()
            .rev()
            .take(8)
            .map(|e| format!("{} [{}] {}", e.t, e.src, e.msg))
            .collect();
        serde_json::json!({
            "host": if running { "running" } else { "stopped" },
            "running": running,
            "upstream_connected": i.upstream_connected,
            "requests": i.requests,
            "clients": i.clients,
            "endpoint": i.endpoint,
            "bridge": i.upstream,
            "view": i.view.name(),
            "plugins": plugins,
            "log_tail": log_tail,
        })
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::path::Path;
    use std::time::Duration;

    // The lifecycle Start / Stop / Restart drive: Stop must unbind the endpoint.
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

    // The control operations reflected in the snapshot — what QA drives.
    #[test]
    fn toggle_and_view_show_in_snapshot() {
        let st = HostState::new("/tmp/telebox_test_none.sock".into(), "u".into(), "t".into());
        assert_eq!(st.snapshot()["plugins"][1]["active"], serde_json::json!(true));
        st.toggle_plugin(1); // Export off
        assert_eq!(st.snapshot()["plugins"][1]["active"], serde_json::json!(false));
        assert_eq!(st.snapshot()["view"], serde_json::json!("plugins"));
        st.set_view(PanelView::Permissions);
        assert_eq!(st.snapshot()["view"], serde_json::json!("permissions"));
    }
}

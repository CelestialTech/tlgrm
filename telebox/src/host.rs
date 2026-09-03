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
    // The device subtitle under the name (what the plugin *is*), and the
    // capability families it is exercising right now (a subset of `perms`) —
    // these light the patch-bay's "active" jacks in the device panel.
    pub kind: &'static str,
    pub active: &'static str,
}

pub const FAMILIES: [&str; 7] = [
    "session", "invoke", "model", "settings", "files", "ui", "events",
];

// --- live retention data (polled from the client's MCP over the relay) -------
// The Retention device panel renders these, so what it shows is the real
// message_versions store — not a mock.
#[derive(Clone, Default)]
pub struct RetentionStats {
    pub available: bool,
    pub total_versions: i64,
    pub messages_tracked: i64,
    pub edits: i64,
    pub deletions: i64,
}
#[derive(Clone)]
pub struct TrackedMsg {
    pub chat_id: i64,
    pub message_id: i64,
    pub versions: i64,
    pub latest_kind: String,
    pub latest_content: String,
}
#[derive(Clone)]
pub struct MsgVersion {
    pub version: i64,
    pub kind: String,
    pub content: String,
    pub captured_at: i64,
}
#[derive(Clone, Default)]
pub struct Retention {
    pub stats: RetentionStats,
    pub tracked: Vec<TrackedMsg>,
    pub selected: Option<(i64, i64)>,
    pub chain: Vec<MsgVersion>,
}

// --- generic device panels (Export / Archiver / Bots / Wallet / AI / MCP) -----
// Every non-Retention device shows the SAME shape, filled by real tools over the
// relay: a key/value readout, an optional list of clickable rows (chats, bots,
// or the tool catalog), and the result line of the last action a button fired.
// One mechanism, six panels — the poller (relay.rs) fills it, the render draws
// it, and the buttons enqueue actions that run against the client's real MCP.
#[derive(Clone)]
pub struct PanelRow {
    pub id: i64,        // chat id / bot id (0 when the row is keyed by string)
    pub sid: String,    // string key — a tool name, a bot id
    pub title: String,
    pub sub: String,
    pub on: bool,       // running/enabled state (bots), else false
}
#[derive(Clone, Default)]
pub struct PanelData {
    pub readout: Vec<(String, String)>, // label → value, from a real read tool
    pub rows: Vec<PanelRow>,            // clickable list, from a real list tool
    pub result: String,                 // outcome of the last action button
    pub loaded: bool,                   // did the readout ever fetch successfully
}
// An export RUN — exists ONLY while an export is actually in progress. When
// there is no run, this is None (never a phantom "idle 0/6"). Ontology: the
// Export plugin is either IDLE (pick a chat) or RUNNING (this job), never both.
#[derive(Clone)]
pub struct ExportRun {
    pub chat: String,
    pub done: i64,
    pub total: i64,
    pub state: String,
}

// A queued tool call: a button click parks one here; the poller runs it and
// writes the outcome back into that plugin's PanelData.result.
#[derive(Clone)]
pub struct PendingAction {
    pub plugin: usize,
    pub tool: String, // an MCP tool name, or "tools/list" for the raw method
    pub args: serde_json::Value,
    pub note: String, // human label, echoed while the call is in flight
}

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
    pub selected: usize,
    pub enabled: Vec<bool>,
    pub retention: Retention,
    pub retention_selected: Option<(i64, i64)>,
    // Ephemeral capture switches (self_destruct, view_once, vanishing): the
    // truth read back from the client, plus a pending desired state a click
    // sets and the poller applies via configure_ephemeral_capture.
    pub capture: Option<(bool, bool, bool)>,
    pub capture_pending: Option<(bool, bool, bool)>,
    // Generic device panels, indexed by plugin. `panels[i]` is what device i
    // renders; `panel_sel[i]` is the row it has selected. `action` is a single
    // in-flight tool call a button queued; `busy` gates a second click.
    pub panels: Vec<PanelData>,
    pub panel_sel: Vec<Option<usize>>,
    pub action: Option<PendingAction>,
    pub busy: bool,
    // Export plugin ontology: a live search string, the chat the user has chosen
    // to export (by id, survives filtering), and the run in progress (if any).
    pub export_search: String,
    pub export_target: Option<(i64, String)>,
    pub export_run: Option<ExportRun>,
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
            live: true,
            kind: "Aggregated tool socket · JSON-RPC", active: "invoke" },
        Plugin { name: "Export", slot: "slot 02 · → disk", runtime: Runtime::Python,
            perms: "session · invoke · files · events",
            desc: "Classic + covert gradual export to disk. Rides raw invoke.",
            live: false,
            kind: "Gradual engine · messages.getHistory", active: "invoke · files" },
        Plugin { name: "Retention", slot: "slot 03 · SQLite + ephemeral", runtime: Runtime::Rust,
            perms: "session · files · events",
            desc: "Real-time retention and capture of view-once media.",
            live: false,
            kind: "Retention · view-once capture", active: "" },
        Plugin { name: "Archiver", slot: "slot 04 · SQLite + mirror group", runtime: Runtime::Python,
            perms: "session · invoke · events",
            desc: "Archive ANY chat's history — to a local SQLite archive or a mirror group. Deleted-account sweep and view-once capture are modes, not the whole job.",
            live: false,
            kind: "Saves any chat's messages & media to a local database you can keep and search", active: "invoke" },
        Plugin { name: "Bots", slot: "slot 05 · automations", runtime: Runtime::Python,
            perms: "session · invoke · ui · events",
            desc: "The bot framework and rule automations.",
            live: false,
            kind: "Automations · rule engine", active: "" },
        Plugin { name: "Wallet", slot: "slot 06", runtime: Runtime::Python,
            perms: "session · invoke · ui",
            desc: "Stars and TON. Off by default — anything that spends stays dark.",
            live: false,
            kind: "Stars · TON payments", active: "" },
        Plugin { name: "AI", slot: "slot 07", runtime: Runtime::Rust,
            perms: "model · files · ui",
            desc: "Local LLM, TTS and voice — the only module touching model.",
            live: false,
            kind: "Local LLM · TTS · voice", active: "" },
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
                // Open on the Export device — the "how do I export a chat" surface.
                selected: 1,
                // Export, Retention, Archiver, Bots on; Wallet, AI off.
                enabled: vec![true, true, true, true, true, false, false],
                retention: Retention::default(),
                retention_selected: None,
                capture: None,
                capture_pending: None,
                panels: vec![PanelData::default(); plugin_templates().len()],
                panel_sel: vec![None; plugin_templates().len()],
                action: None,
                busy: false,
                export_search: String::new(),
                export_target: None,
                export_run: None,
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

    pub fn selected(&self) -> usize {
        self.0.lock().map(|i| i.selected).unwrap_or(0)
    }

    // Focus a device in the rack — the stage loads that plugin's control surface.
    pub fn select(&self, i: usize) {
        let name = {
            let mut inner = self.0.lock().unwrap();
            inner.selected = i;
            plugin_templates().get(i).map(|p| p.name).unwrap_or("")
        };
        self.log("ui", format!("device → {name}"));
    }

    // --- live retention data ---------------------------------------------
    pub fn set_retention(&self, r: Retention) {
        if let Ok(mut i) = self.0.lock() {
            i.retention = r;
        }
    }
    pub fn retention(&self) -> Retention {
        self.0.lock().map(|i| i.retention.clone()).unwrap_or_default()
    }
    pub fn retention_selected(&self) -> Option<(i64, i64)> {
        self.0.lock().ok().and_then(|i| i.retention_selected)
    }
    // Pick a tracked message to inspect; the poller fetches its version chain.
    pub fn select_tracked(&self, chat_id: i64, message_id: i64) {
        if let Ok(mut i) = self.0.lock() {
            i.retention_selected = Some((chat_id, message_id));
        }
    }

    // --- ephemeral capture switches --------------------------------------
    // The displayed state is the pending desired value if a click is in flight,
    // else the last truth read from the client.
    pub fn capture(&self) -> (bool, bool, bool) {
        self.0
            .lock()
            .ok()
            .and_then(|i| i.capture_pending.or(i.capture))
            .unwrap_or((true, true, true))
    }
    // Set by the poller from the client's real flags; never clobbers a pending click.
    pub fn set_capture_truth(&self, c: (bool, bool, bool)) {
        if let Ok(mut i) = self.0.lock() {
            i.capture = Some(c);
        }
    }
    // A click flips one switch and queues the new triple for the poller to apply.
    pub fn toggle_capture(&self, which: usize) {
        if let Ok(mut i) = self.0.lock() {
            let mut n = i.capture_pending.or(i.capture).unwrap_or((true, true, true));
            match which {
                0 => n.0 = !n.0,
                1 => n.1 = !n.1,
                _ => n.2 = !n.2,
            }
            i.capture_pending = Some(n);
        }
    }
    // The poller consumes a pending desired state to push to the client.
    pub fn take_capture_pending(&self) -> Option<(bool, bool, bool)> {
        self.0.lock().ok().and_then(|mut i| i.capture_pending.take())
    }

    // --- generic device panels -------------------------------------------
    pub fn panel(&self, i: usize) -> PanelData {
        self.0.lock().ok().and_then(|s| s.panels.get(i).cloned()).unwrap_or_default()
    }
    // The poller refreshes the readout and row list from real tools; it must not
    // clobber the last action result or the row selection, which the click owns.
    pub fn set_panel_readout(&self, i: usize, readout: Vec<(String, String)>, rows: Vec<PanelRow>, loaded: bool) {
        if let Ok(mut s) = self.0.lock() {
            if let Some(p) = s.panels.get_mut(i) {
                p.readout = readout;
                p.rows = rows;
                p.loaded = loaded;
            }
        }
    }
    pub fn panel_row_sel(&self, i: usize) -> Option<usize> {
        self.0.lock().ok().and_then(|s| s.panel_sel.get(i).copied().flatten())
    }
    pub fn select_panel_row(&self, i: usize, row: usize) {
        if let Ok(mut s) = self.0.lock() {
            if let Some(sel) = s.panel_sel.get_mut(i) {
                *sel = Some(row);
            }
        }
        self.log("ui", format!("panel {i} row → {row}"));
    }
    pub fn busy(&self) -> bool {
        self.0.lock().map(|s| s.busy).unwrap_or(false)
    }
    // Queue one tool call; the poller runs it and writes the outcome back.
    pub fn enqueue(&self, plugin: usize, tool: &str, args: serde_json::Value, note: String) {
        if let Ok(mut s) = self.0.lock() {
            if s.busy {
                return; // one action in flight at a time
            }
            s.busy = true;
            if let Some(p) = s.panels.get_mut(plugin) {
                p.result = format!("… {note}");
            }
            s.action = Some(PendingAction { plugin, tool: tool.to_string(), args, note });
        }
    }
    pub fn take_action(&self) -> Option<PendingAction> {
        self.0.lock().ok().and_then(|mut s| s.action.take())
    }
    pub fn set_result(&self, plugin: usize, msg: String) {
        if let Ok(mut s) = self.0.lock() {
            s.busy = false;
            if let Some(p) = s.panels.get_mut(plugin) {
                p.result = msg;
            }
        }
    }

    // --- Export plugin: search, chosen target, live run --------------------
    pub fn export_search(&self) -> String {
        self.0.lock().map(|i| i.export_search.clone()).unwrap_or_default()
    }
    pub fn set_export_search(&self, s: String) {
        if let Ok(mut i) = self.0.lock() { i.export_search = s; }
    }
    pub fn export_search_push(&self, s: &str) {
        if let Ok(mut i) = self.0.lock() { i.export_search.push_str(s); }
    }
    pub fn export_search_backspace(&self) {
        if let Ok(mut i) = self.0.lock() { i.export_search.pop(); }
    }
    pub fn export_search_clear(&self) {
        if let Ok(mut i) = self.0.lock() { i.export_search.clear(); }
    }
    pub fn export_target(&self) -> Option<(i64, String)> {
        self.0.lock().ok().and_then(|i| i.export_target.clone())
    }
    pub fn set_export_target(&self, id: i64, title: String) {
        if let Ok(mut i) = self.0.lock() { i.export_target = Some((id, title)); }
    }
    pub fn export_run(&self) -> Option<ExportRun> {
        self.0.lock().ok().and_then(|i| i.export_run.clone())
    }
    pub fn set_export_run(&self, r: Option<ExportRun>) {
        if let Ok(mut i) = self.0.lock() { i.export_run = r; }
    }

    // The one primary action for device `i`, built from its current selection.
    // BOTH the on-screen button and the QA socket call this, so there is a
    // single code path per plugin and no second implementation to drift.
    pub fn primary_action(&self, i: usize) {
        let d = self.panel(i);
        let row = self.panel_row_sel(i).and_then(|r| d.rows.get(r).cloned());
        match i {
            // MCP: a canary invoke proving the relay round-trips a tools/call.
            0 => self.enqueue(0, "list_chats", serde_json::json!({}), "invoke · list_chats".into()),
            // Export: the HEADLESS gradual engine (GradualArchiver) — never the
            // client's native export window. If a run is live, the action cancels
            // it; otherwise it starts an export for the chosen chat (by id, so it
            // is unaffected by search filtering).
            1 => {
                if self.export_run().is_some() {
                    self.enqueue(1, "cancel_gradual_export", serde_json::json!({}), "cancel export".into());
                } else if let Some((id, title)) = self.export_target() {
                    self.enqueue(1, "start_gradual_export", serde_json::json!({ "chat_id": id }),
                        format!("export {title}"));
                } else {
                    self.set_result(1, "pick a chat to export first".into());
                }
            }
            // Archiver: archive the picked chat's history into the SQLite store.
            3 => match row {
                Some(r) => self.enqueue(3, "archive_chat", serde_json::json!({ "chat_id": r.id, "limit": 1000 }),
                    format!("archive {}", r.title)),
                None => self.set_result(3, "pick a chat first".into()),
            },
            // Bots: start or stop the picked bot, by its current run state.
            4 => match row {
                Some(r) if r.on => self.enqueue(4, "stop_bot", serde_json::json!({ "bot_id": r.sid }),
                    format!("stop {}", r.title)),
                Some(r) => self.enqueue(4, "start_bot", serde_json::json!({ "bot_id": r.sid }),
                    format!("start {}", r.title)),
                None => self.set_result(4, "pick a bot first".into()),
            },
            // AI: prove the local voice pipeline by synthesizing a sample.
            6 => self.enqueue(6, "text_to_speech",
                serde_json::json!({ "text": "TeleBox voice check. The local text to speech pipeline is live." }),
                "synthesize sample".into()),
            _ => {}
        }
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
        // The on-stage device panel, grabbed as data for QA assertions.
        let selp = i.selected;
        let panel = i.panels.get(selp).cloned().unwrap_or_default();
        let panel_readout: Vec<serde_json::Value> =
            panel.readout.iter().map(|(k, v)| serde_json::json!([k, v])).collect();
        // Export search reach — computed here so the QA snapshot can assert the filter.
        let export_ql = i.export_search.to_lowercase();
        let export_total = i.panels.get(1).map(|p| p.rows.len()).unwrap_or(0);
        let export_matches = i.panels.get(1).map(|p| {
            p.rows.iter().filter(|r| export_ql.is_empty() || r.title.to_lowercase().contains(&export_ql)).count()
        }).unwrap_or(0);
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
            "selected_plugin": selp,
            "panel": {
                "result": panel.result,
                "rows": panel.rows.len(),
                "row_sel": i.panel_sel.get(selp).copied().flatten(),
                "readout": panel_readout,
                "loaded": panel.loaded,
                "busy": i.busy,
            },
            "export": {
                "mode": if i.export_run.is_some() { "running" } else { "idle" },
                "search": i.export_search,
                "chats_total": export_total,
                "matches": export_matches,
                "target": i.export_target.as_ref().map(|(id, t)| serde_json::json!({ "id": id, "title": t })),
                "run": i.export_run.as_ref().map(|r| serde_json::json!({
                    "chat": r.chat, "done": r.done, "total": r.total, "state": r.state })),
            },
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

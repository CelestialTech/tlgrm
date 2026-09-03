// The host: all render-relevant state, the relay lifecycle, and the operations
// the controls invoke.
//
// Everything the panel displays lives here — including the current view and the
// per-plugin enabled bits — so that BOTH the on-screen controls and the QA API
// (qa.rs) drive one identical code path. A button's on_click calls the same
// HostState method the QA socket calls; there is no second implementation to
// drift.

use serde_json::Value;
use std::collections::{HashMap, HashSet};
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
    pub done: i64,     // messages written so far
    pub total: i64,    // the chat's REAL total (from get_chat_history's count)
    pub bytes: i64,    // real attachment bytes seen so far
    pub state: String, // exporting / done / cancelled / error
    pub path: String,  // where it's being written
}

// One editable field of a bot's configuration. Bots are built-in automations
// (registered in C++, not user-created), so the UI can't add/remove them — but
// their config is tunable via configure_bot, and this is one such setting typed
// so the panel can render the right control (toggle vs stepper).
#[derive(Clone)]
pub enum BotCfgVal {
    Bool(bool),
    Int(i64),
    Float(f64),
    Str(String),
}

// One voice/audio message found in a chat, offered as a transcription target.
#[derive(Clone, Default)]
pub struct VoiceMsg {
    pub id: i64,    // message id
    pub date: i64,  // unix seconds
    pub bytes: i64, // media size
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
    // Set to ask the Rust export engine thread to stop; it clears it on start.
    pub export_cancel: bool,
    // Pause the engine (it holds between pages until cleared); and whether a run
    // downloads media files or just records their sizes.
    pub export_pause: bool,
    pub export_with_media: bool,
    // Archiver: the same find-a-chat ontology as Export (Item picked by search),
    // driving archive_chat into the local SQLite store.
    pub archiver_search: String,
    pub archiver_target: Option<(i64, String)>,
    // The archive STORE contents — the chats actually archived (list_archived_chats),
    // so the panel shows what you've saved, not just a count.
    pub archive_store: Vec<PanelRow>,
    // Bots: the selected bot (id) and its detail lines (info + activity stats),
    // filled by the poller from get_bot_info.
    pub bots_selected: Option<String>,
    pub bots_detail: Vec<(String, String)>,
    // Editable config of the selected bot (from get_bot_info.config), plus the
    // identity lines (permissions/tags) and the edit/apply state.
    pub bots_config: Vec<(String, BotCfgVal)>,
    pub bots_config_loaded_for: Option<String>,
    pub bots_config_dirty: bool,
    pub bots_configure_pending: Option<(String, Value)>,
    pub bots_configure_busy: bool,
    pub bots_config_result: String,
    // MCP domain-tree UI: which nodes are expanded (domain name, or
    // "domain\u{1f}sub"), the in-place filter, and the selected tool.
    pub mcp_expanded: HashSet<String>,
    pub mcp_search: String,
    pub mcp_selected: Option<String>,
    // Live tool info from tools/list: name -> (description, params summary).
    pub mcp_tool_info: HashMap<String, (String, String)>,
    // Arg form for the selected tool: param name -> typed value, and which
    // field is currently receiving keystrokes. Lets read tools that need a
    // param (get_chat_history's chat_id) be filled in and invoked from here.
    pub mcp_args: HashMap<String, String>,
    pub mcp_active_arg: Option<String>,
    // A safe read-only invoke the user asked for; the poller runs it.
    pub mcp_invoke_pending: Option<String>,
    pub mcp_invoke_args: Value,
    pub mcp_invoke_result: String,
    pub mcp_invoke_busy: bool,
    // AI transcription flow (G4): pick a chat, list its voice/audio messages,
    // transcribe the chosen one via the client's transcribe_voice_message.
    pub ai_search: String,
    pub ai_chat: Option<(i64, String)>,
    pub ai_voice: Vec<VoiceMsg>,
    pub ai_voice_loaded_for: Option<i64>,
    pub ai_voice_sel: Option<i64>,
    pub ai_transcript: String,
    pub ai_transcribe_pending: Option<(i64, i64)>,
    pub ai_transcribe_busy: bool,
    // AI: send a synthesized voice message to the picked chat (send_voice_reply).
    pub ai_vm_text: String,
    pub ai_vm_pending: Option<(i64, String)>,
    pub ai_vm_busy: bool,
    pub ai_vm_result: String,
    // Bots: send a command to the selected bot (send_bot_command).
    pub bots_command: String,
    pub bots_command_pending: Option<(String, String)>,
    pub bots_command_busy: bool,
    pub bots_command_result: String,
    // Wallet: search transactions (search_transactions) + owned gifts (get_profile_gifts).
    pub wallet_query: String,
    pub wallet_search_pending: bool,
    pub wallet_search_busy: bool,
    pub wallet_hits: Vec<(String, String, bool)>, // (title, sub, income)
    pub wallet_gifts: Vec<(String, String)>,      // (title, sub)
    pub wallet_gifts_loaded: bool,
    // Archiver: search archived message content (search_archive).
    pub archive_query: String,
    pub archive_search_pending: bool,
    pub archive_search_busy: bool,
    pub archive_hits: Vec<(String, String)>, // (chat/who, snippet)
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
                export_cancel: false,
                export_pause: false,
                export_with_media: true,
                archiver_search: String::new(),
                archiver_target: None,
                archive_store: Vec::new(),
                bots_selected: None,
                bots_detail: Vec::new(),
                bots_config: Vec::new(),
                bots_config_loaded_for: None,
                bots_config_dirty: false,
                bots_configure_pending: None,
                bots_configure_busy: false,
                bots_config_result: String::new(),
                mcp_expanded: HashSet::new(),
                mcp_search: String::new(),
                mcp_selected: None,
                mcp_tool_info: HashMap::new(),
                mcp_args: HashMap::new(),
                mcp_active_arg: None,
                mcp_invoke_pending: None,
                mcp_invoke_args: Value::Null,
                mcp_invoke_result: String::new(),
                ai_search: String::new(),
                ai_chat: None,
                ai_voice: Vec::new(),
                ai_voice_loaded_for: None,
                ai_voice_sel: None,
                ai_transcript: String::new(),
                ai_transcribe_pending: None,
                ai_transcribe_busy: false,
                ai_vm_text: String::new(),
                ai_vm_pending: None,
                ai_vm_busy: false,
                ai_vm_result: String::new(),
                bots_command: String::new(),
                bots_command_pending: None,
                bots_command_busy: false,
                bots_command_result: String::new(),
                wallet_query: String::new(),
                wallet_search_pending: false,
                wallet_search_busy: false,
                wallet_hits: Vec::new(),
                wallet_gifts: Vec::new(),
                wallet_gifts_loaded: false,
                archive_query: String::new(),
                archive_search_pending: false,
                archive_search_busy: false,
                archive_hits: Vec::new(),
                mcp_invoke_busy: false,
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
    pub fn request_export_cancel(&self) {
        if let Ok(mut i) = self.0.lock() { i.export_cancel = true; }
    }
    pub fn clear_export_cancel(&self) {
        if let Ok(mut i) = self.0.lock() { i.export_cancel = false; }
    }
    pub fn export_cancel_requested(&self) -> bool {
        self.0.lock().map(|i| i.export_cancel).unwrap_or(false)
    }
    // The relay endpoint the export engine pages get_chat_history over: the
    // client's MCP socket and the auth-token path.
    pub fn relay_creds(&self) -> (String, String) {
        self.0.lock().map(|i| (i.upstream.clone(), i.token.clone())).unwrap_or_default()
    }

    // --- Archiver: search + chosen target (parallels Export) ---------------
    pub fn archiver_search(&self) -> String {
        self.0.lock().map(|i| i.archiver_search.clone()).unwrap_or_default()
    }
    pub fn archiver_search_push(&self, s: &str) {
        if let Ok(mut i) = self.0.lock() { i.archiver_search.push_str(s); }
    }
    pub fn archiver_search_backspace(&self) {
        if let Ok(mut i) = self.0.lock() { i.archiver_search.pop(); }
    }
    pub fn archiver_search_clear(&self) {
        if let Ok(mut i) = self.0.lock() { i.archiver_search.clear(); }
    }
    pub fn archiver_target(&self) -> Option<(i64, String)> {
        self.0.lock().ok().and_then(|i| i.archiver_target.clone())
    }
    pub fn set_archiver_target(&self, id: i64, title: String) {
        if let Ok(mut i) = self.0.lock() { i.archiver_target = Some((id, title)); }
    }
    pub fn archive_store(&self) -> Vec<PanelRow> {
        self.0.lock().map(|i| i.archive_store.clone()).unwrap_or_default()
    }
    pub fn set_archive_store(&self, rows: Vec<PanelRow>) {
        if let Ok(mut i) = self.0.lock() { i.archive_store = rows; }
    }
    // --- Archiver: search the archived content (search_archive) --------------
    pub fn archive_query(&self) -> String {
        self.0.lock().map(|i| i.archive_query.clone()).unwrap_or_default()
    }
    pub fn archive_query_push(&self, s: &str) {
        if let Ok(mut i) = self.0.lock() { i.archive_query.push_str(s); }
    }
    pub fn archive_query_backspace(&self) {
        if let Ok(mut i) = self.0.lock() { i.archive_query.pop(); }
    }
    pub fn archive_query_clear(&self) {
        if let Ok(mut i) = self.0.lock() { i.archive_query.clear(); }
    }
    pub fn archive_hits(&self) -> Vec<(String, String)> {
        self.0.lock().map(|i| i.archive_hits.clone()).unwrap_or_default()
    }
    pub fn archive_search_busy(&self) -> bool {
        self.0.lock().map(|i| i.archive_search_busy).unwrap_or(false)
    }
    pub fn request_archive_search(&self) {
        if let Ok(mut i) = self.0.lock() {
            if i.archive_query.trim().is_empty() || i.archive_search_busy { return; }
            i.archive_search_busy = true;
            i.archive_search_pending = true;
        }
    }
    pub fn take_archive_search(&self) -> Option<String> {
        self.0.lock().ok().and_then(|mut i| {
            if i.archive_search_pending { i.archive_search_pending = false; Some(i.archive_query.clone()) } else { None }
        })
    }
    pub fn set_archive_hits(&self, hits: Vec<(String, String)>) {
        if let Ok(mut i) = self.0.lock() { i.archive_hits = hits; i.archive_search_busy = false; }
    }

    // --- Wallet: search transactions + owned gifts --------------------------
    pub fn wallet_query(&self) -> String {
        self.0.lock().map(|i| i.wallet_query.clone()).unwrap_or_default()
    }
    pub fn wallet_query_push(&self, s: &str) {
        if let Ok(mut i) = self.0.lock() { i.wallet_query.push_str(s); }
    }
    pub fn wallet_query_backspace(&self) {
        if let Ok(mut i) = self.0.lock() { i.wallet_query.pop(); }
    }
    pub fn wallet_query_clear(&self) {
        if let Ok(mut i) = self.0.lock() { i.wallet_query.clear(); }
    }
    pub fn wallet_hits(&self) -> Vec<(String, String, bool)> {
        self.0.lock().map(|i| i.wallet_hits.clone()).unwrap_or_default()
    }
    pub fn wallet_search_busy(&self) -> bool {
        self.0.lock().map(|i| i.wallet_search_busy).unwrap_or(false)
    }
    pub fn request_wallet_search(&self) {
        if let Ok(mut i) = self.0.lock() {
            if i.wallet_query.trim().is_empty() || i.wallet_search_busy { return; }
            i.wallet_search_busy = true;
            i.wallet_search_pending = true;
        }
    }
    pub fn take_wallet_search(&self) -> Option<String> {
        self.0.lock().ok().and_then(|mut i| {
            if i.wallet_search_pending { i.wallet_search_pending = false; Some(i.wallet_query.clone()) } else { None }
        })
    }
    pub fn set_wallet_hits(&self, hits: Vec<(String, String, bool)>) {
        if let Ok(mut i) = self.0.lock() { i.wallet_hits = hits; i.wallet_search_busy = false; }
    }
    pub fn wallet_gifts(&self) -> Vec<(String, String)> {
        self.0.lock().map(|i| i.wallet_gifts.clone()).unwrap_or_default()
    }
    pub fn wallet_gifts_loaded(&self) -> bool {
        self.0.lock().map(|i| i.wallet_gifts_loaded).unwrap_or(false)
    }
    pub fn set_wallet_gifts(&self, gifts: Vec<(String, String)>) {
        if let Ok(mut i) = self.0.lock() { i.wallet_gifts = gifts; i.wallet_gifts_loaded = true; }
    }

    // --- Export: pause/resume + media toggle --------------------------------
    pub fn export_pause_requested(&self) -> bool {
        self.0.lock().map(|i| i.export_pause).unwrap_or(false)
    }
    pub fn toggle_export_pause(&self) {
        if let Ok(mut i) = self.0.lock() { i.export_pause = !i.export_pause; }
    }
    pub fn clear_export_pause(&self) {
        if let Ok(mut i) = self.0.lock() { i.export_pause = false; }
    }
    pub fn export_with_media(&self) -> bool {
        self.0.lock().map(|i| i.export_with_media).unwrap_or(true)
    }
    pub fn toggle_export_media(&self) {
        if let Ok(mut i) = self.0.lock() { i.export_with_media = !i.export_with_media; }
    }

    // --- Bots: send a command (send_bot_command) ----------------------------
    pub fn bots_command(&self) -> String {
        self.0.lock().map(|i| i.bots_command.clone()).unwrap_or_default()
    }
    pub fn bots_command_push(&self, s: &str) {
        if let Ok(mut i) = self.0.lock() { i.bots_command.push_str(s); }
    }
    pub fn bots_command_backspace(&self) {
        if let Ok(mut i) = self.0.lock() { i.bots_command.pop(); }
    }
    pub fn bots_command_clear(&self) {
        if let Ok(mut i) = self.0.lock() { i.bots_command.clear(); }
    }
    pub fn bots_command_busy(&self) -> bool {
        self.0.lock().map(|i| i.bots_command_busy).unwrap_or(false)
    }
    pub fn bots_command_result(&self) -> String {
        self.0.lock().map(|i| i.bots_command_result.clone()).unwrap_or_default()
    }
    pub fn request_bot_command(&self) {
        if let Ok(mut i) = self.0.lock() {
            let cmd = i.bots_command.trim().to_string();
            let Some(bot) = i.bots_selected.clone() else { return };
            if cmd.is_empty() || i.bots_command_busy { return; }
            i.bots_command_busy = true;
            i.bots_command_result = format!("… /{cmd}");
            i.bots_command_pending = Some((bot, cmd));
        }
    }
    pub fn take_bot_command(&self) -> Option<(String, String)> {
        self.0.lock().ok().and_then(|mut i| i.bots_command_pending.take())
    }
    pub fn set_bot_command_result(&self, r: String) {
        if let Ok(mut i) = self.0.lock() {
            i.bots_command_busy = false;
            i.bots_command_result = r;
            i.bots_command.clear();
        }
    }

    // --- AI: send a voice message to the picked chat (send_voice_reply) ------
    pub fn ai_vm_text(&self) -> String {
        self.0.lock().map(|i| i.ai_vm_text.clone()).unwrap_or_default()
    }
    pub fn ai_vm_push(&self, s: &str) {
        if let Ok(mut i) = self.0.lock() { i.ai_vm_text.push_str(s); }
    }
    pub fn ai_vm_backspace(&self) {
        if let Ok(mut i) = self.0.lock() { i.ai_vm_text.pop(); }
    }
    pub fn ai_vm_clear(&self) {
        if let Ok(mut i) = self.0.lock() { i.ai_vm_text.clear(); }
    }
    pub fn ai_vm_busy(&self) -> bool {
        self.0.lock().map(|i| i.ai_vm_busy).unwrap_or(false)
    }
    pub fn ai_vm_result(&self) -> String {
        self.0.lock().map(|i| i.ai_vm_result.clone()).unwrap_or_default()
    }
    pub fn request_send_vm(&self) {
        if let Ok(mut i) = self.0.lock() {
            let text = i.ai_vm_text.trim().to_string();
            let Some((cid, _)) = i.ai_chat.clone() else { return };
            if text.is_empty() || i.ai_vm_busy { return; }
            i.ai_vm_busy = true;
            i.ai_vm_result = "… sending voice message".into();
            i.ai_vm_pending = Some((cid, text));
        }
    }
    pub fn take_send_vm(&self) -> Option<(i64, String)> {
        self.0.lock().ok().and_then(|mut i| i.ai_vm_pending.take())
    }
    pub fn set_vm_result(&self, r: String) {
        if let Ok(mut i) = self.0.lock() {
            i.ai_vm_busy = false;
            i.ai_vm_result = r;
            i.ai_vm_text.clear();
        }
    }

    // --- Bots: selection + detail ------------------------------------------
    pub fn bots_selected(&self) -> Option<String> {
        self.0.lock().ok().and_then(|i| i.bots_selected.clone())
    }
    pub fn set_bots_selected(&self, id: String) {
        if let Ok(mut i) = self.0.lock() {
            i.bots_selected = Some(id);
            i.bots_detail.clear(); // the old detail was a different bot
            i.bots_config.clear(); // and a different config
            i.bots_config_loaded_for = None;
            i.bots_config_dirty = false;
            i.bots_config_result.clear();
        }
    }
    pub fn bots_detail(&self) -> Vec<(String, String)> {
        self.0.lock().map(|i| i.bots_detail.clone()).unwrap_or_default()
    }
    pub fn set_bots_detail(&self, lines: Vec<(String, String)>) {
        if let Ok(mut i) = self.0.lock() { i.bots_detail = lines; }
    }

    // --- Bots: editable config (configure_bot) -----------------------------
    pub fn bots_config(&self) -> Vec<(String, BotCfgVal)> {
        self.0.lock().map(|i| i.bots_config.clone()).unwrap_or_default()
    }
    pub fn bots_config_loaded_for(&self) -> Option<String> {
        self.0.lock().ok().and_then(|i| i.bots_config_loaded_for.clone())
    }
    pub fn bots_config_dirty(&self) -> bool {
        self.0.lock().map(|i| i.bots_config_dirty).unwrap_or(false)
    }
    pub fn bots_configure_busy(&self) -> bool {
        self.0.lock().map(|i| i.bots_configure_busy).unwrap_or(false)
    }
    pub fn bots_config_result(&self) -> String {
        self.0.lock().map(|i| i.bots_config_result.clone()).unwrap_or_default()
    }
    // Load config from get_bot_info — but never clobber a live edit (dirty).
    pub fn set_bots_config(&self, bot_id: String, fields: Vec<(String, BotCfgVal)>) {
        if let Ok(mut i) = self.0.lock() {
            if i.bots_config_dirty && i.bots_config_loaded_for.as_deref() == Some(bot_id.as_str()) {
                return; // keep the user's unsaved edits
            }
            i.bots_config = fields;
            i.bots_config_loaded_for = Some(bot_id);
            i.bots_config_dirty = false;
        }
    }
    pub fn bots_toggle_cfg(&self, key: &str) {
        if let Ok(mut i) = self.0.lock() {
            for (k, v) in i.bots_config.iter_mut() {
                if k == key {
                    if let BotCfgVal::Bool(b) = v { *b = !*b; i.bots_config_dirty = true; }
                    break;
                }
            }
        }
    }
    // Step a numeric config field by `delta` (Int rounds; both clamped >= 0).
    pub fn bots_step_cfg(&self, key: &str, delta: f64) {
        if let Ok(mut i) = self.0.lock() {
            for (k, v) in i.bots_config.iter_mut() {
                if k == key {
                    match v {
                        BotCfgVal::Int(n) => { *n = (*n + delta.round() as i64).max(0); i.bots_config_dirty = true; }
                        BotCfgVal::Float(f) => { *f = (*f + delta).max(0.0); i.bots_config_dirty = true; }
                        _ => {}
                    }
                    break;
                }
            }
        }
    }
    // Build the config JSON from the edited fields and queue configure_bot.
    pub fn request_configure_bot(&self) {
        if let Ok(mut i) = self.0.lock() {
            if i.bots_configure_busy { return; }
            let Some(id) = i.bots_selected.clone() else { return };
            let mut m = serde_json::Map::new();
            for (k, v) in &i.bots_config {
                let jv = match v {
                    BotCfgVal::Bool(b) => Value::from(*b),
                    BotCfgVal::Int(n) => Value::from(*n),
                    BotCfgVal::Float(f) => Value::from(*f),
                    BotCfgVal::Str(s) => Value::from(s.clone()),
                };
                m.insert(k.clone(), jv);
            }
            i.bots_configure_busy = true;
            i.bots_config_result = "… applying config".into();
            i.bots_configure_pending = Some((id, Value::Object(m)));
        }
    }
    pub fn take_configure_bot(&self) -> Option<(String, Value)> {
        self.0.lock().ok().and_then(|mut i| i.bots_configure_pending.take())
    }
    // Record the outcome; on success clear dirty and force a fresh reload so the
    // panel shows the values the bot actually accepted.
    pub fn set_configure_result(&self, ok: bool, msg: String) {
        if let Ok(mut i) = self.0.lock() {
            i.bots_configure_busy = false;
            i.bots_config_result = msg;
            if ok {
                i.bots_config_dirty = false;
                i.bots_config_loaded_for = None; // refetch config next tick
            }
        }
    }

    // --- MCP domain tree ---------------------------------------------------
    pub fn mcp_expanded(&self, key: &str) -> bool {
        self.0.lock().map(|i| i.mcp_expanded.contains(key)).unwrap_or(false)
    }
    pub fn mcp_toggle(&self, key: String) {
        if let Ok(mut i) = self.0.lock() {
            if !i.mcp_expanded.remove(&key) {
                i.mcp_expanded.insert(key);
            }
        }
    }
    pub fn mcp_search(&self) -> String {
        self.0.lock().map(|i| i.mcp_search.clone()).unwrap_or_default()
    }
    pub fn mcp_search_push(&self, s: &str) {
        if let Ok(mut i) = self.0.lock() { i.mcp_search.push_str(s); }
    }
    pub fn mcp_search_backspace(&self) {
        if let Ok(mut i) = self.0.lock() { i.mcp_search.pop(); }
    }
    pub fn mcp_search_clear(&self) {
        if let Ok(mut i) = self.0.lock() { i.mcp_search.clear(); }
    }
    pub fn mcp_selected(&self) -> Option<String> {
        self.0.lock().ok().and_then(|i| i.mcp_selected.clone())
    }
    pub fn mcp_select(&self, tool: String) {
        if let Ok(mut i) = self.0.lock() {
            i.mcp_selected = Some(tool);
            i.mcp_invoke_result.clear(); // the last invoke was for a different tool
            i.mcp_args.clear(); // a different tool has different params
            i.mcp_active_arg = None;
        }
    }
    // --- arg form (G1) --------------------------------------------------------
    pub fn mcp_arg_value(&self, name: &str) -> String {
        self.0.lock().ok().and_then(|i| i.mcp_args.get(name).cloned()).unwrap_or_default()
    }
    pub fn mcp_active_arg(&self) -> Option<String> {
        self.0.lock().ok().and_then(|i| i.mcp_active_arg.clone())
    }
    pub fn mcp_set_active_arg(&self, name: Option<String>) {
        if let Ok(mut i) = self.0.lock() {
            if let Some(ref n) = name {
                i.mcp_args.entry(n.clone()).or_default(); // ensure a slot exists
            }
            i.mcp_active_arg = name;
        }
    }
    pub fn mcp_arg_push(&self, s: &str) {
        if let Ok(mut i) = self.0.lock() {
            if let Some(n) = i.mcp_active_arg.clone() {
                i.mcp_args.entry(n).or_default().push_str(s);
            }
        }
    }
    pub fn mcp_arg_backspace(&self) {
        if let Ok(mut i) = self.0.lock() {
            if let Some(n) = i.mcp_active_arg.clone() {
                i.mcp_args.entry(n).or_default().pop();
            }
        }
    }
    // Directly set an arg (QA drives this deterministically).
    pub fn mcp_set_arg(&self, name: &str, value: &str) {
        if let Ok(mut i) = self.0.lock() {
            i.mcp_args.insert(name.to_string(), value.to_string());
        }
    }
    pub fn set_mcp_tool_info(&self, m: HashMap<String, (String, String)>) {
        if let Ok(mut i) = self.0.lock() { i.mcp_tool_info = m; }
    }
    pub fn mcp_has_tools(&self) -> bool {
        self.0.lock().map(|i| !i.mcp_tool_info.is_empty()).unwrap_or(false)
    }
    pub fn mcp_tool_info(&self, name: &str) -> Option<(String, String)> {
        self.0.lock().ok().and_then(|i| i.mcp_tool_info.get(name).cloned())
    }
    // Only read-only tools are one-click invokable here; a mutating tool is the
    // job of its owning plugin, not a raw tree click (a stray send_/delete_ would
    // act on the real account).
    pub fn is_read_tool(name: &str) -> bool {
        name.starts_with("get_")
            || name.starts_with("list_")
            || name.starts_with("search_")
            || name.starts_with("count_")
    }
    pub fn request_mcp_invoke(&self, tool: String) {
        if !Self::is_read_tool(&tool) {
            self.set_mcp_invoke_result(format!(
                "✕ {tool} can change state — invoke it from its plugin, not here. Read-only tools (get_/list_/search_) only."));
            return;
        }
        if let Ok(mut i) = self.0.lock() {
            if i.mcp_invoke_busy {
                return;
            }
            // Build typed args from the form: parse ints and bools, keep the
            // rest as strings; skip blank fields so an optional param is omitted.
            let mut m = serde_json::Map::new();
            for (k, v) in &i.mcp_args {
                let v = v.trim();
                if v.is_empty() {
                    continue;
                }
                let val = if let Ok(n) = v.parse::<i64>() {
                    Value::from(n)
                } else if v == "true" || v == "false" {
                    Value::from(v == "true")
                } else {
                    Value::from(v)
                };
                m.insert(k.clone(), val);
            }
            i.mcp_invoke_args = Value::Object(m);
            i.mcp_invoke_busy = true;
            i.mcp_invoke_result = format!("… invoking {tool}");
            i.mcp_invoke_pending = Some(tool);
        }
    }
    // Returns the pending tool and the typed args built from the form.
    pub fn take_mcp_invoke(&self) -> Option<(String, Value)> {
        self.0.lock().ok().and_then(|mut i| {
            i.mcp_invoke_pending.take().map(|t| {
                let a = std::mem::replace(&mut i.mcp_invoke_args, Value::Null);
                (t, if a.is_null() { serde_json::json!({}) } else { a })
            })
        })
    }
    pub fn set_mcp_invoke_result(&self, r: String) {
        if let Ok(mut i) = self.0.lock() {
            i.mcp_invoke_busy = false;
            i.mcp_invoke_result = r;
        }
    }
    pub fn mcp_invoke_result(&self) -> String {
        self.0.lock().map(|i| i.mcp_invoke_result.clone()).unwrap_or_default()
    }
    pub fn mcp_invoke_busy(&self) -> bool {
        self.0.lock().map(|i| i.mcp_invoke_busy).unwrap_or(false)
    }

    // --- AI transcription flow (G4) ------------------------------------------
    pub fn ai_search(&self) -> String {
        self.0.lock().map(|i| i.ai_search.clone()).unwrap_or_default()
    }
    pub fn ai_search_push(&self, s: &str) {
        if let Ok(mut i) = self.0.lock() { i.ai_search.push_str(s); }
    }
    pub fn ai_search_backspace(&self) {
        if let Ok(mut i) = self.0.lock() { i.ai_search.pop(); }
    }
    pub fn ai_search_clear(&self) {
        if let Ok(mut i) = self.0.lock() { i.ai_search.clear(); }
    }
    pub fn ai_chat(&self) -> Option<(i64, String)> {
        self.0.lock().ok().and_then(|i| i.ai_chat.clone())
    }
    // Pick (or clear) the chat to transcribe from — resets everything downstream
    // so a new chat's voice list is fetched fresh.
    pub fn set_ai_chat(&self, c: Option<(i64, String)>) {
        if let Ok(mut i) = self.0.lock() {
            i.ai_chat = c;
            i.ai_voice.clear();
            i.ai_voice_loaded_for = None;
            i.ai_voice_sel = None;
            i.ai_transcript.clear();
            i.ai_search.clear();
        }
    }
    pub fn ai_voice(&self) -> Vec<VoiceMsg> {
        self.0.lock().map(|i| i.ai_voice.clone()).unwrap_or_default()
    }
    pub fn ai_voice_loaded_for(&self) -> Option<i64> {
        self.0.lock().ok().and_then(|i| i.ai_voice_loaded_for)
    }
    // Record the voice messages found for a chat (fetched once per selection).
    pub fn set_ai_voice(&self, chat_id: i64, v: Vec<VoiceMsg>) {
        if let Ok(mut i) = self.0.lock() {
            i.ai_voice = v;
            i.ai_voice_loaded_for = Some(chat_id);
        }
    }
    pub fn ai_voice_sel(&self) -> Option<i64> {
        self.0.lock().ok().and_then(|i| i.ai_voice_sel)
    }
    pub fn set_ai_voice_sel(&self, id: Option<i64>) {
        if let Ok(mut i) = self.0.lock() { i.ai_voice_sel = id; }
    }
    pub fn ai_transcript(&self) -> String {
        self.0.lock().map(|i| i.ai_transcript.clone()).unwrap_or_default()
    }
    pub fn ai_transcribe_busy(&self) -> bool {
        self.0.lock().map(|i| i.ai_transcribe_busy).unwrap_or(false)
    }
    // Ask the poller to transcribe one voice message; guarded against re-entry.
    pub fn request_transcribe(&self, chat_id: i64, msg_id: i64) {
        if let Ok(mut i) = self.0.lock() {
            if i.ai_transcribe_busy {
                return;
            }
            i.ai_transcribe_busy = true;
            i.ai_transcript = format!("… transcribing message {msg_id}");
            i.ai_transcribe_pending = Some((chat_id, msg_id));
        }
    }
    pub fn take_transcribe(&self) -> Option<(i64, i64)> {
        self.0.lock().ok().and_then(|mut i| i.ai_transcribe_pending.take())
    }
    pub fn set_ai_transcript(&self, r: String) {
        if let Ok(mut i) = self.0.lock() {
            i.ai_transcribe_busy = false;
            i.ai_transcript = r;
        }
    }

    // The one primary action for device `i`, built from its current selection.
    // BOTH the on-screen button and the QA socket call this, so there is a
    // single code path per plugin and no second implementation to drift.
    pub fn primary_action(&self, i: usize) {
        let d = self.panel(i);
        match i {
            // MCP: a canary invoke proving the relay round-trips a tools/call.
            0 => self.enqueue(0, "list_chats", serde_json::json!({}), "invoke · list_chats".into()),
            // Export: the RUST engine (export_engine.rs), which pages the client's
            // get_chat_history API by its REAL count and writes to disk — Tlgrm is
            // never asked to export. A live run cancels; otherwise start the chosen
            // chat. No message cap here: this is a full export from the UI.
            1 => {
                if self.export_run().is_some() {
                    self.request_export_cancel();
                    self.set_result(1, "cancelling export…".into());
                } else if let Some((id, title)) = self.export_target() {
                    let (sock, token) = self.relay_creds();
                    crate::export_engine::spawn(self.clone(), sock, token, id, title, None);
                } else {
                    self.set_result(1, "pick a chat to export first".into());
                }
            }
            // Archiver: archive the chosen chat's history into the SQLite store
            // (by id, so search filtering never changes what gets archived).
            3 => match self.archiver_target() {
                Some((id, title)) => self.enqueue(3, "archive_chat",
                    serde_json::json!({ "chat_id": id, "limit": 500 }),
                    format!("archive {title}")),
                None => self.set_result(3, "pick a chat to archive first".into()),
            },
            // Bots: start or stop the SELECTED bot, by its current run state.
            4 => {
                match self.bots_selected().and_then(|s| d.rows.iter().find(|r| r.sid == s).cloned()) {
                    Some(r) if r.on => self.enqueue(4, "stop_bot", serde_json::json!({ "bot_id": r.sid }),
                        format!("stop {}", r.title)),
                    Some(r) => self.enqueue(4, "start_bot", serde_json::json!({ "bot_id": r.sid }),
                        format!("start {}", r.title)),
                    None => self.set_result(4, "select a bot first".into()),
                }
                // Clear the detail so the poller refetches it with the new state.
                self.set_bots_detail(Vec::new());
            }
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
        let arc_ql = i.archiver_search.to_lowercase();
        let arc_matches = i.panels.get(3).map(|p| {
            p.rows.iter().filter(|r| arc_ql.is_empty() || r.title.to_lowercase().contains(&arc_ql)).count()
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
            "mcp": {
                "selected": i.mcp_selected,
                "invoke_busy": i.mcp_invoke_busy,
                "invoke_result": i.mcp_invoke_result,
                "tools_known": i.mcp_tool_info.len(),
                "active_arg": i.mcp_active_arg,
                "args": i.mcp_args.iter().map(|(k, v)| serde_json::json!([k, v])).collect::<Vec<_>>(),
                // Live endpoint traffic (G2): newest-first recent calls + per-minute rate.
                "rate_1m": crate::retention::call_rate(60_000),
                "recent": crate::retention::recent_calls().iter().take(12)
                    .map(|(t, ts, ok)| serde_json::json!({ "tool": t, "ts": ts, "ok": ok }))
                    .collect::<Vec<_>>(),
            },
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
                "paused": i.export_pause,
                "with_media": i.export_with_media,
                "run": i.export_run.as_ref().map(|r| serde_json::json!({
                    "chat": r.chat, "done": r.done, "total": r.total,
                    "bytes": r.bytes, "state": r.state, "path": r.path })),
            },
            "archiver": {
                "search": i.archiver_search,
                "matches": arc_matches,
                "target": i.archiver_target.as_ref().map(|(id, t)| serde_json::json!({ "id": id, "title": t })),
                "store_count": i.archive_store.len(),
                "store": i.archive_store.iter().take(20).map(|r| serde_json::json!({ "id": r.id, "title": r.title, "sub": r.sub })).collect::<Vec<_>>(),
                "query": i.archive_query,
                "search_busy": i.archive_search_busy,
                "hits": i.archive_hits.iter().map(|(a, b)| serde_json::json!([a, b])).collect::<Vec<_>>(),
            },
            "bots": {
                "selected": i.bots_selected,
                "detail": i.bots_detail.iter().map(|(k, v)| serde_json::json!([k, v])).collect::<Vec<_>>(),
                "config": i.bots_config.iter().map(|(k, v)| {
                    let val = match v {
                        BotCfgVal::Bool(b) => serde_json::json!(b),
                        BotCfgVal::Int(n) => serde_json::json!(n),
                        BotCfgVal::Float(f) => serde_json::json!(f),
                        BotCfgVal::Str(s) => serde_json::json!(s),
                    };
                    serde_json::json!([k, val])
                }).collect::<Vec<_>>(),
                "config_dirty": i.bots_config_dirty,
                "configure_busy": i.bots_configure_busy,
                "config_result": i.bots_config_result,
                "command": i.bots_command,
                "command_busy": i.bots_command_busy,
                "command_result": i.bots_command_result,
            },
            "ai": {
                "search": i.ai_search,
                "chat": i.ai_chat.as_ref().map(|(id, t)| serde_json::json!({ "id": id, "title": t })),
                "voice_count": i.ai_voice.len(),
                "voice": i.ai_voice.iter().map(|m| serde_json::json!({ "id": m.id, "date": m.date, "bytes": m.bytes })).collect::<Vec<_>>(),
                "voice_sel": i.ai_voice_sel,
                "transcribe_busy": i.ai_transcribe_busy,
                "transcript": i.ai_transcript,
                "vm_text": i.ai_vm_text,
                "vm_busy": i.ai_vm_busy,
                "vm_result": i.ai_vm_result,
            },
            "wallet": {
                "query": i.wallet_query,
                "search_busy": i.wallet_search_busy,
                "hits": i.wallet_hits.iter().map(|(t, s, inc)| serde_json::json!({ "title": t, "sub": s, "income": inc })).collect::<Vec<_>>(),
                "gifts_loaded": i.wallet_gifts_loaded,
                "gifts": i.wallet_gifts.iter().map(|(t, s)| serde_json::json!([t, s])).collect::<Vec<_>>(),
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

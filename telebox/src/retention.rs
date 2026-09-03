// The relay driver — TeleBox as an MCP client to the Tlgrm bridge.
//
// Every device panel shows the client's REAL state, not a mock. This one thread
// is the whole mechanism: each tick it (1) runs any action a button queued, then
// (2) refreshes just the device that is on stage — the Retention version store,
// or one of the generic panels (Export / Archiver / Bots / Wallet / AI / MCP),
// each filled from real tools over the relay. Only the visible panel is polled,
// so load is one or two calls per tick regardless of how many devices exist.
//
// The client's MCP bridge is flaky about serving several calls on one
// connection, so each call gets its OWN short-lived connection (initialize + one
// request), retried a few times with a small gap.

use std::collections::{HashMap, VecDeque};
use std::io::{BufRead, BufReader, Write};
use std::os::unix::net::UnixStream;
use std::sync::Mutex;
use std::thread;
use std::time::{Duration, SystemTime, UNIX_EPOCH};

use serde_json::{json, Value};

// --- live endpoint traffic (the MCP monitor) ---------------------------------
// Every relay call TeleBox makes to the aggregated endpoint is recorded here —
// tool name, unix-ms, ok — newest first, capped. The MCP panel renders this as
// the endpoint's live traffic + a per-minute rate. This is real usage, not a
// mock: the poller, actions and invokes all flow through call/call_slow.
static RECENT: Mutex<VecDeque<(String, i64, bool)>> = Mutex::new(VecDeque::new());

// The one gate that removes bridge contention. The client's MCP bridge handles
// exactly one tool call at a time — its re-entrancy guard rejects a second, and
// the single-call socket drops concurrent connections. TeleBox drives it from
// several threads at once (the UI poller, the export engine, queued actions and
// invokes), so without serialization two of them collide and the loser gets
// "client did not answer". Every client call acquires this lock for the whole
// connect+initialize+call+read, turning collisions into an orderly queue —
// exactly one TeleBox↔client conversation is ever in flight.
//
// ponytail: global bridge lock — a slow call (a media download) makes the UI
// poller wait behind it; upgrade to a priority queue only if refresh latency
// during long exports becomes a real problem. (POSD P8: complexity pulled down
// into the bridge so no caller has to reason about concurrency.)
static BRIDGE: Mutex<()> = Mutex::new(());

// Acquire the bridge gate, recovering a poisoned lock (a caller that panicked
// mid-call left no shared invariant broken — the next call is still safe).
fn bridge_gate() -> std::sync::MutexGuard<'static, ()> {
    BRIDGE.lock().unwrap_or_else(|e| e.into_inner())
}

// The client bridge opens a fresh connection per call and needs a moment to
// retire the previous one; back-to-back connects churn the socket and it drops
// a call. Under the gate (so this is already serialized) enforce a minimum gap
// between the START of consecutive calls: a burst is smoothed to a safe rate,
// while normal paced traffic (poller every ~2s, export throttled 300ms/page)
// never waits, since it is already spaced wider than the gap.
// ponytail: fixed 150ms floor — tune only if a real workload needs a tighter
// or looser cadence than the socket's settle time.
static LAST_START_MS: std::sync::atomic::AtomicU64 = std::sync::atomic::AtomicU64::new(0);
const MIN_GAP_MS: u64 = 150;

fn throttle() {
    use std::sync::atomic::Ordering;
    let last = LAST_START_MS.load(Ordering::Relaxed);
    let now = now_ms() as u64;
    if last > 0 {
        let elapsed = now.saturating_sub(last);
        if elapsed < MIN_GAP_MS {
            thread::sleep(Duration::from_millis(MIN_GAP_MS - elapsed));
        }
    }
    LAST_START_MS.store(now_ms() as u64, Ordering::Relaxed);
}

fn now_ms() -> i64 {
    SystemTime::now().duration_since(UNIX_EPOCH).map(|d| d.as_millis() as i64).unwrap_or(0)
}
fn record(name: &str, ok: bool) {
    if let Ok(mut q) = RECENT.lock() {
        q.push_front((name.to_string(), now_ms(), ok));
        while q.len() > 60 {
            q.pop_back();
        }
    }
}
// (tool, unix_ms, ok), newest first.
pub(crate) fn recent_calls() -> Vec<(String, i64, bool)> {
    RECENT.lock().map(|q| q.iter().cloned().collect()).unwrap_or_default()
}
// Calls in the last `window_ms` — the live rate.
pub(crate) fn call_rate(window_ms: i64) -> usize {
    let cutoff = now_ms() - window_ms;
    RECENT.lock().map(|q| q.iter().filter(|(_, ts, _)| *ts >= cutoff).count()).unwrap_or(0)
}

use crate::host::{HostState, MsgVersion, PanelRow, RetentionStats, TrackedMsg, VoiceMsg};

// One initialize + one tools/call over a fresh connection. The tool returns its
// payload as a JSON string in result.content[0].text, so unwrap and re-parse.
fn call_once(sock: &str, token: &str, name: &str, args: &Value) -> Option<Value> {
    let s = UnixStream::connect(sock).ok()?;
    s.set_read_timeout(Some(Duration::from_secs(5))).ok()?;
    let mut w = s.try_clone().ok()?;
    let mut r = BufReader::new(s);

    let init = json!({"jsonrpc":"2.0","id":1,"method":"initialize",
        "params":{"auth_token":token}});
    writeln!(w, "{init}").ok()?;
    let mut line = String::new();
    r.read_line(&mut line).ok()?;

    let call = json!({"jsonrpc":"2.0","id":2,"method":"tools/call",
        "params":{"name":name,"arguments":args}});
    writeln!(w, "{call}").ok()?;
    line.clear();
    r.read_line(&mut line).ok()?;
    let v: Value = serde_json::from_str(&line).ok()?;
    let text = v.get("result")?.get("content")?.get(0)?.get("text")?.as_str()?;
    serde_json::from_str(text).ok()
}

// call_once with retries + a gap between attempts, to ride out the bridge's
// occasional refusal of a rapid reconnect. Shared with the export engine.
pub(crate) fn call(sock: &str, token: &str, name: &str, args: Value) -> Option<Value> {
    let _gate = bridge_gate(); // serialize with every other client call
    throttle(); // and keep a safe gap between connects
    let mut out = None;
    for attempt in 0..4 {
        if attempt > 0 {
            thread::sleep(Duration::from_millis(180));
        }
        if let Some(v) = call_once(sock, token, name, &args) {
            out = Some(v);
            break;
        }
    }
    record(name, out.is_some());
    out
}

// A single call that tolerates a slow tool. An action button can trigger real
// work — synthesizing speech, archiving a large chat — that runs well past the
// 5s read window a poll uses, so the outcome is worth waiting for. One long
// attempt, then one retry.
pub(crate) fn call_slow(sock: &str, token: &str, name: &str, args: &Value) -> Option<Value> {
    let _gate = bridge_gate(); // serialize with every other client call
    throttle(); // and keep a safe gap between connects
    // Up to 4 attempts with exponential backoff (0.4s, 0.8s, 1.6s), so a
    // transient refusal on the flaky single-call bridge recovers on its own
    // instead of surfacing as "no response" for the user to retry by hand.
    for attempt in 0..4 {
        if attempt > 0 {
            thread::sleep(Duration::from_millis(200 * (1u64 << attempt)));
        }
        let Ok(s) = UnixStream::connect(sock) else { continue };
        let _ = s.set_read_timeout(Some(Duration::from_secs(60)));
        let Ok(mut w) = s.try_clone() else { continue };
        let mut r = BufReader::new(s);
        let init = json!({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"auth_token":token}});
        if writeln!(w, "{init}").is_err() { continue; }
        let mut line = String::new();
        if r.read_line(&mut line).is_err() { continue; }
        let callm = json!({"jsonrpc":"2.0","id":2,"method":"tools/call",
            "params":{"name":name,"arguments":args}});
        if writeln!(w, "{callm}").is_err() { continue; }
        line.clear();
        if r.read_line(&mut line).is_err() { continue; }
        if let Ok(v) = serde_json::from_str::<Value>(&line) {
            if let Some(text) = v.get("result").and_then(|x| x.get("content"))
                .and_then(|x| x.get(0)).and_then(|x| x.get("text")).and_then(Value::as_str)
            {
                if let Ok(parsed) = serde_json::from_str::<Value>(text) {
                    record(name, true);
                    return Some(parsed);
                }
            }
        }
    }
    record(name, false);
    None
}

// Summarize a tool's inputSchema into "chat_id* · limit · offset_id" (required
// params get a trailing *). Empty when the tool takes no arguments.
fn params_of(t: &Value) -> String {
    let Some(schema) = t.get("inputSchema") else { return String::new() };
    let req: Vec<&str> = schema
        .get("required")
        .and_then(|r| r.as_array())
        .map(|a| a.iter().filter_map(Value::as_str).collect())
        .unwrap_or_default();
    match schema.get("properties").and_then(|p| p.as_object()) {
        Some(props) => props
            .keys()
            .map(|k| if req.contains(&k.as_str()) { format!("{k}*") } else { k.clone() })
            .collect::<Vec<_>>()
            .join(" · "),
        None => String::new(),
    }
}

// The raw tools/list method: name -> (description, params summary) for every tool.
fn list_tools_once(sock: &str, token: &str) -> Option<Vec<(String, String, String)>> {
    let s = UnixStream::connect(sock).ok()?;
    s.set_read_timeout(Some(Duration::from_secs(5))).ok()?;
    let mut w = s.try_clone().ok()?;
    let mut r = BufReader::new(s);

    let init = json!({"jsonrpc":"2.0","id":1,"method":"initialize",
        "params":{"auth_token":token}});
    writeln!(w, "{init}").ok()?;
    let mut line = String::new();
    r.read_line(&mut line).ok()?;

    let req = json!({"jsonrpc":"2.0","id":2,"method":"tools/list"});
    writeln!(w, "{req}").ok()?;
    line.clear();
    r.read_line(&mut line).ok()?;
    let v: Value = serde_json::from_str(&line).ok()?;
    let arr = v.get("result")?.get("tools")?.as_array()?;
    Some(
        arr.iter()
            .filter_map(|t| {
                let name = t.get("name")?.as_str()?.to_string();
                let desc = t.get("description").and_then(Value::as_str).unwrap_or("").to_string();
                Some((name, desc, params_of(t)))
            })
            .collect(),
    )
}
fn list_tools(sock: &str, token: &str) -> Option<Vec<(String, String, String)>> {
    let _gate = bridge_gate(); // serialize with every other client call
    throttle(); // and keep a safe gap between connects
    let mut out = None;
    for attempt in 0..4 {
        if attempt > 0 {
            thread::sleep(Duration::from_millis(180));
        }
        if let Some(v) = list_tools_once(sock, token) {
            out = Some(v);
            break;
        }
    }
    record("tools/list", out.is_some());
    out
}

pub(crate) fn i64_of(v: &Value, k: &str) -> i64 {
    v.get(k).and_then(Value::as_i64).unwrap_or(0)
}
pub(crate) fn str_of(v: &Value, k: &str) -> String {
    v.get(k).and_then(Value::as_str).unwrap_or("").to_string()
}
// A field that may arrive as a string or a number, always rendered as a string.
fn id_str(v: &Value, k: &str) -> String {
    match v.get(k) {
        Some(Value::String(s)) => s.clone(),
        Some(Value::Number(n)) => n.to_string(),
        _ => String::new(),
    }
}
fn human_bytes(n: i64) -> String {
    if n >= 1 << 20 {
        format!("{:.1} MB", n as f64 / (1u64 << 20) as f64)
    } else if n >= 1 << 10 {
        format!("{:.1} KB", n as f64 / 1024.0)
    } else {
        format!("{n} B")
    }
}

// The client's live chat list → clickable rows (used by Export and Archiver).
// `limit` caps how many are returned; Export pulls the full set so its search
// can reach every chat, not just a 40-row window.
fn chat_rows(sock: &str, token: &str, limit: usize) -> Vec<PanelRow> {
    if let Some(s) = call(sock, token, "list_chats", json!({})) {
        if let Some(arr) = s.get("chats").and_then(Value::as_array) {
            return arr
                .iter()
                .take(limit)
                .map(|c| PanelRow {
                    id: i64_of(c, "id"),
                    sid: String::new(),
                    title: str_of(c, "name"),
                    sub: str_of(c, "type"),
                    on: false,
                })
                .collect();
        }
    }
    Vec::new()
}

// Refresh the on-stage generic device from real tools.
fn refresh_panel(state: &HostState, sock: &str, token: &str, idx: usize) {
    match idx {
        // MCP — the aggregated endpoint: the tree is static (taxonomy), so the
        // poller's job is to keep each tool's live description + params so the
        // detail card can show what a selected tool does and takes.
        0 => {
            // The tool catalog is static — fetch it ONCE, then stop, so it never
            // competes with an invoke on the flaky single-call bridge.
            if !state.mcp_has_tools() {
                if let Some(tools) = list_tools(sock, token) {
                    let mut info = HashMap::new();
                    for (name, desc, params) in tools {
                        info.insert(name, (desc, params));
                    }
                    state.set_mcp_tool_info(info);
                }
            }
        }
        // Export — the HEADLESS gradual engine (GradualArchiver, never the
        // client's Export::Controller). Ontology: set export_run to Some ONLY
        // while a run truly exists; otherwise None (no phantom "idle 0/6").
        1 => {
            // The Rust export engine owns export_run. While a run is active, stop
            // the heavy list_chats refresh so it doesn't starve the engine's
            // get_chat_history calls on the flaky single-call bridge.
            if state.export_run().is_some() {
                return;
            }
            let rows = chat_rows(sock, token, 2000);
            state.set_panel_readout(1, Vec::new(), rows, true);
        }
        // Archiver — archive ANY chat to the SQLite store: real stats + a chat.
        3 => {
            let mut readout = Vec::new();
            if let Some(s) = call(sock, token, "get_archive_stats", json!({})) {
                if s.get("error").is_some() {
                    readout.push(("archiver".into(), str_of(&s, "error")));
                } else {
                    // The archive STORE — what you've saved. (Ephemeral capture is
                    // a Retention concern, deliberately not shown here.)
                    readout.push(("messages archived".into(), i64_of(&s, "total_messages").to_string()));
                    readout.push(("chats".into(), i64_of(&s, "total_chats").to_string()));
                    readout.push(("db size".into(), human_bytes(i64_of(&s, "database_size_bytes"))));
                }
            }
            // The STORE contents — the chats actually archived — so the panel
            // shows what you've saved, not just a count. Derived server-side from
            // the archived messages, with titles for chats recorded on archive.
            if let Some(a) = call(sock, token, "list_archived_chats", json!({})) {
                let store = a.get("chats").and_then(Value::as_array).map(|arr| {
                    arr.iter().map(|c| {
                        let id = i64_of(c, "chat_id");
                        let title = str_of(c, "title");
                        let mc = i64_of(c, "message_count");
                        let ty = str_of(c, "type");
                        PanelRow {
                            id,
                            sid: String::new(),
                            title: if title.is_empty() { format!("chat {id}") } else { title },
                            sub: format!("{mc} msg{}", if ty.is_empty() { String::new() } else { format!(" · {ty}") }),
                            on: false,
                        }
                    }).collect::<Vec<_>>()
                }).unwrap_or_default();
                state.set_archive_store(store);
            }
            // Full chat set so the Archiver's search reaches every chat.
            let rows = chat_rows(sock, token, 2000);
            state.set_panel_readout(3, readout, rows, true);
        }
        // Bots — the automation framework: list bots, start/stop the picked one.
        4 => {
            if let Some(s) = call(sock, token, "list_bots", json!({ "include_disabled": true })) {
                let rows = s
                    .get("bots")
                    .and_then(Value::as_array)
                    .map(|arr| {
                        arr.iter()
                            .map(|b| PanelRow {
                                id: 0,
                                sid: id_str(b, "id"),
                                title: str_of(b, "name"),
                                sub: str_of(b, "description"),
                                on: b.get("is_running").and_then(Value::as_bool).unwrap_or(false),
                            })
                            .collect::<Vec<_>>()
                    })
                    .unwrap_or_default();
                let readout = vec![
                    ("bots registered".into(), i64_of(&s, "total_count").to_string()),
                    ("running".into(), rows.iter().filter(|r| r.on).count().to_string()),
                ];
                state.set_panel_readout(4, readout, rows, true);
            }
            // The selected bot's detail + editable config — fetched ONCE per
            // selection (and once more after a configure, which clears
            // bots_config_loaded_for), so it doesn't compete with the toggle or
            // apply action on the flaky single-call bridge.
            if let Some(botid) = state.bots_selected() {
                let need = state.bots_detail().is_empty()
                    || state.bots_config_loaded_for().as_deref() != Some(botid.as_str());
                if need {
                    if let Some(info) = call(sock, token, "get_bot_info", json!({ "bot_id": botid })) {
                        let mut lines = Vec::new();
                        if info.get("error").is_some() {
                            lines.push(("bot".into(), str_of(&info, "error")));
                        } else {
                            let ver = str_of(&info, "version");
                            if !ver.is_empty() { lines.push(("version".into(), ver)); }
                            let auth = str_of(&info, "author");
                            if !auth.is_empty() { lines.push(("author".into(), auth)); }
                            lines.push(("state".into(),
                                if info.get("is_running").and_then(Value::as_bool) == Some(true) { "running".into() } else { "stopped".into() }));
                            // What the bot may do, and how it's classified.
                            if let Some(perms) = info.get("required_permissions").and_then(Value::as_array) {
                                let p: Vec<&str> = perms.iter().filter_map(Value::as_str).collect();
                                if !p.is_empty() { lines.push(("permissions".into(), p.join(", "))); }
                            }
                            if let Some(tags) = info.get("tags").and_then(Value::as_array) {
                                let t: Vec<&str> = tags.iter().filter_map(Value::as_str).collect();
                                if !t.is_empty() { lines.push(("tags".into(), t.join(", "))); }
                            }
                            if let Some(st) = info.get("statistics") {
                                lines.push(("messages".into(), i64_of(st, "messages_processed").to_string()));
                                lines.push(("commands".into(), i64_of(st, "commands_executed").to_string()));
                                lines.push(("errors".into(), i64_of(st, "errors_occurred").to_string()));
                                let avg = st.get("avg_execution_ms").and_then(Value::as_f64).unwrap_or(0.0);
                                lines.push(("avg ms".into(), format!("{avg:.1}")));
                            }
                            // The editable config -> typed fields for the panel.
                            if let Some(cfg) = info.get("config").and_then(Value::as_object) {
                                let mut fields = Vec::new();
                                for (k, v) in cfg {
                                    let tv = if let Some(b) = v.as_bool() {
                                        crate::host::BotCfgVal::Bool(b)
                                    } else if v.is_i64() || v.is_u64() {
                                        crate::host::BotCfgVal::Int(v.as_i64().unwrap_or(0))
                                    } else if v.is_f64() {
                                        crate::host::BotCfgVal::Float(v.as_f64().unwrap_or(0.0))
                                    } else {
                                        crate::host::BotCfgVal::Str(v.as_str().unwrap_or("").to_string())
                                    };
                                    fields.push((k.clone(), tv));
                                }
                                state.set_bots_config(botid.clone(), fields);
                            }
                        }
                        state.set_bots_detail(lines);
                    }
                }
            }
        }
        // Wallet — Stars / TON: the real live balance. Spending stays dark.
        5 => {
            // Wallet is read-only: the Store is the live Stars balance + the
            // local transaction records. Spending stays dark by design.
            let mut readout = Vec::new();
            if let Some(s) = call(sock, token, "get_wallet_balance", json!({})) {
                if s.get("loaded").and_then(Value::as_bool) == Some(true) {
                    readout.push(("stars_balance".into(), i64_of(&s, "stars_balance").to_string()));
                } else {
                    let e = str_of(&s, "error");
                    readout.push(("wallet".into(), if e.is_empty() { "unavailable".into() } else { e }));
                }
            }
            let rows = call(sock, token, "get_transactions", json!({ "limit": 50 }))
                .and_then(|t| t.get("transactions").and_then(Value::as_array).cloned())
                .map(|arr| {
                    arr.iter()
                        .map(|x| {
                            let amount = x.get("amount").and_then(Value::as_f64).unwrap_or(0.0);
                            let desc = str_of(x, "description");
                            PanelRow {
                                id: amount as i64,
                                sid: str_of(x, "category"),
                                title: if desc.is_empty() { str_of(x, "category") } else { desc },
                                sub: format!("{} · {:+} ★", str_of(x, "date"), amount),
                                on: amount >= 0.0, // income vs spend
                            }
                        })
                        .collect()
                })
                .unwrap_or_default();
            state.set_panel_readout(5, readout, rows, true);
            // The owned-gift portfolio — fetched ONCE (get_profile_gifts), so it
            // doesn't compete with a transaction search on the single-call bridge.
            if !state.wallet_gifts_loaded() {
                if let Some(g) = call(sock, token, "get_profile_gifts", json!({})) {
                    let gifts = g.get("gifts").and_then(Value::as_array).map(|arr| {
                        arr.iter().map(|x| {
                            let title = {
                                let t = str_of(x, "title");
                                if !t.is_empty() { t } else { str_of(x, "name") }
                            };
                            let stars = i64_of(x, "stars");
                            (if title.is_empty() { "gift".into() } else { title },
                             if stars > 0 { format!("{stars} ★") } else { str_of(x, "date") })
                        }).collect::<Vec<_>>()
                    }).unwrap_or_default();
                    state.set_wallet_gifts(gifts);
                }
            }
        }
        // AI — local LLM / TTS / voice: probe the TTS service state ONCE (the
        // empty-text probe), then stop, so it doesn't starve the Speak action on
        // the single-call bridge.
        6 => {
            // (a) Probe the TTS service + load the chat set for the transcription
            // picker — ONCE (readout empty), so it never starves the Speak or
            // Transcribe actions on the single-call bridge.
            if state.panel(6).readout.is_empty() {
                let status = match call(sock, token, "text_to_speech", json!({ "text": "" })) {
                    Some(s) => {
                        let e = str_of(&s, "error");
                        if e.contains("not initialized") {
                            "offline".into()
                        } else {
                            // "Missing text" (our empty probe) means the service is up.
                            "ready".into()
                        }
                    }
                    None => "no response".into(),
                };
                let readout = vec![
                    ("engine".into(), "local TTS · Piper / espeak / coqui".into()),
                    ("service".into(), status),
                ];
                let rows = chat_rows(sock, token, 2000); // chats to pick from
                state.set_panel_readout(6, readout, rows, true);
            }
            // (b) A chat is picked but its voice list isn't loaded yet: page its
            // recent history once and keep the audio/voice documents.
            if let Some((chat_id, _)) = state.ai_chat() {
                if state.ai_voice_loaded_for() != Some(chat_id) {
                    if let Some(p) = call_slow(sock, token, "get_chat_history",
                        &json!({ "chat_id": chat_id, "limit": 100 }))
                    {
                        let mut voice = Vec::new();
                        if let Some(arr) = p.get("messages").and_then(Value::as_array) {
                            for m in arr {
                                let mt = str_of(m, "media_type");
                                let mime = str_of(m, "media_mime");
                                if mt == "document" && mime.contains("audio") {
                                    voice.push(VoiceMsg {
                                        id: i64_of(m, "id"),
                                        date: i64_of(m, "date"),
                                        bytes: i64_of(m, "media_size"),
                                    });
                                }
                            }
                        }
                        state.set_ai_voice(chat_id, voice);
                    }
                }
            }
        }
        _ => {}
    }
}

// Run a queued action against the client's MCP and write a human outcome line.
fn run_action(state: &HostState, sock: &str, token: &str) {
    let Some(a) = state.take_action() else { return };
    let res = call_slow(sock, token, &a.tool, &a.args);
    let msg = match res {
        None => format!("{} — no response from client", a.note),
        Some(v) => summarize(&a.tool, &v),
    };
    state.set_result(a.plugin, msg);
}

// Run a user-requested READ-ONLY tool invoke from the MCP tree and show a
// compact result. The host guards this to get_/list_/search_ tools, so calling
// with no arguments is safe (worst case: an honest "missing X" error).
fn run_mcp_invoke(state: &HostState, sock: &str, token: &str) {
    let Some((tool, args)) = state.take_mcp_invoke() else { return };
    let res = call_slow(sock, token, &tool, &args);
    let msg = match res {
        None => format!("✕ {tool}: no response from client"),
        Some(v) => {
            if let Some(e) = v.get("error").and_then(Value::as_str) {
                format!("✕ {e}")
            } else if v.get("success").and_then(Value::as_bool) == Some(false) {
                format!("✕ {}", str_of(&v, "message"))
            } else {
                let raw = serde_json::to_string(&v).unwrap_or_default();
                let snip: String = raw.chars().take(300).collect();
                let ell = if raw.chars().count() > 300 { "…" } else { "" };
                format!("✓ {snip}{ell}")
            }
        }
    };
    state.set_mcp_invoke_result(msg.clone());
    state.push_mcp_history(&tool, &msg);
}

// Transcribe one voice/audio message the user picked in the AI panel. Shows the
// real transcript text, or the engine's honest state (no model, empty) — never
// a fabricated success.
fn run_transcribe(state: &HostState, sock: &str, token: &str) {
    let Some((chat_id, msg_id)) = state.take_transcribe() else { return };
    // The engine transcribes from the client's local cache, so the file has to
    // be downloaded first (otherwise "file not found in local cache"). Fetch it
    // with download_media, then transcribe. Both calls serialize on the gate.
    let dl = call_slow(sock, token, "download_media",
        &json!({ "chat_id": chat_id, "message_id": msg_id }));
    if let Some(d) = &dl {
        if d.get("success").and_then(Value::as_bool) == Some(false) {
            state.set_ai_transcript(format!("✕ download: {}",
                if str_of(d, "error").is_empty() { str_of(d, "message") } else { str_of(d, "error") }));
            return;
        }
    }
    let res = call_slow(sock, token, "transcribe_voice_message",
        &json!({ "chat_id": chat_id, "message_id": msg_id }));
    let msg = match res {
        None => "✕ transcribe: no response from client".to_string(),
        Some(v) => {
            if let Some(e) = v.get("error").and_then(Value::as_str) {
                format!("✕ {e}")
            } else if v.get("success").and_then(Value::as_bool) == Some(false) {
                let m = str_of(&v, "message");
                format!("✕ {}", if m.is_empty() { str_of(&v, "error") } else { m })
            } else {
                // The transcript may be under text / transcription / result.
                let t = ["text", "transcription", "transcript", "result"]
                    .iter()
                    .map(|k| str_of(&v, k))
                    .find(|s| !s.is_empty())
                    .unwrap_or_default();
                if t.is_empty() {
                    "✓ transcribed (no text returned by the engine)".to_string()
                } else {
                    format!("✓ {t}")
                }
            }
        }
    };
    state.set_ai_transcript(msg);
}

// Apply the edited config to a bot via configure_bot. On success the poller
// re-fetches get_bot_info (bots_config_loaded_for was cleared), so the panel
// then shows exactly what the bot accepted.
fn run_bot_configure(state: &HostState, sock: &str, token: &str) {
    let Some((bot_id, config)) = state.take_configure_bot() else { return };
    let res = call_slow(sock, token, "configure_bot",
        &json!({ "bot_id": bot_id, "config": config }));
    match res {
        None => state.set_configure_result(false, "✕ configure: no response from client".into()),
        Some(v) => {
            if let Some(e) = v.get("error").and_then(Value::as_str) {
                state.set_configure_result(false, format!("✕ {e}"));
            } else if v.get("success").and_then(Value::as_bool) == Some(false) {
                let m = str_of(&v, "message");
                state.set_configure_result(false, format!("✕ {}", if m.is_empty() { str_of(&v, "error") } else { m }));
            } else {
                state.set_configure_result(true, "✓ config applied".into());
            }
        }
    }
}

// Search archived message content (search_archive) -> hits (who · snippet).
fn run_archive_search(state: &HostState, sock: &str, token: &str) {
    let Some(q) = state.take_archive_search() else { return };
    let res = call_slow(sock, token, "search_archive", &json!({ "query": q, "limit": 50 }));
    let hits = res
        .and_then(|v| v.get("results").and_then(Value::as_array).cloned())
        .map(|arr| arr.iter().map(|r| {
            let who = {
                let u = str_of(r, "username");
                let n = str_of(r, "first_name");
                if !n.is_empty() { n } else if !u.is_empty() { u } else { format!("chat {}", i64_of(r, "chat_id")) }
            };
            (who, str_of(r, "content"))
        }).collect::<Vec<_>>())
        .unwrap_or_default();
    state.set_archive_hits(hits);
}

// Search Stars/TON transactions (search_transactions) -> matching rows.
fn run_wallet_search(state: &HostState, sock: &str, token: &str) {
    let Some(q) = state.take_wallet_search() else { return };
    let res = call_slow(sock, token, "search_transactions", &json!({ "query": q, "limit": 50 }));
    let hits = res
        .and_then(|v| v.get("transactions").and_then(Value::as_array).cloned())
        .map(|arr| arr.iter().map(|x| {
            let amount = x.get("amount").and_then(Value::as_f64).unwrap_or(0.0);
            let desc = str_of(x, "description");
            let title = if desc.is_empty() { str_of(x, "category") } else { desc };
            (title, format!("{} · {:+} ★", str_of(x, "date"), amount), amount >= 0.0)
        }).collect::<Vec<_>>())
        .unwrap_or_default();
    state.set_wallet_hits(hits);
}

// Send a command to the selected bot (send_bot_command).
fn run_bot_command(state: &HostState, sock: &str, token: &str) {
    let Some((bot_id, command)) = state.take_bot_command() else { return };
    let res = call_slow(sock, token, "send_bot_command",
        &json!({ "bot_id": bot_id, "command": command }));
    let msg = match res {
        None => "✕ command: no response from client".to_string(),
        Some(v) => {
            if let Some(e) = v.get("error").and_then(Value::as_str) { format!("✕ {e}") }
            else if v.get("success").and_then(Value::as_bool) == Some(false) {
                let m = str_of(&v, "message"); format!("✕ {}", if m.is_empty() { str_of(&v, "error") } else { m })
            } else {
                let m = str_of(&v, "message");
                if m.is_empty() { "✓ command sent".into() } else { format!("✓ {m}") }
            }
        }
    };
    state.set_bot_command_result(msg);
}

// Send a synthesized voice message to the picked chat (send_voice_reply).
fn run_send_vm(state: &HostState, sock: &str, token: &str) {
    let Some((chat_id, text)) = state.take_send_vm() else { return };
    let res = call_slow(sock, token, "send_voice_reply",
        &json!({ "chat_id": chat_id, "text": text }));
    let msg = match res {
        None => "✕ voice message: no response from client".to_string(),
        Some(v) => {
            if let Some(e) = v.get("error").and_then(Value::as_str) { format!("✕ {e}") }
            else if v.get("success").and_then(Value::as_bool) == Some(false) {
                let m = str_of(&v, "message"); format!("✕ {}", if m.is_empty() { str_of(&v, "error") } else { m })
            } else { "✓ voice message sent".into() }
        }
    };
    state.set_vm_result(msg);
}

// Turn a tool's raw JSON into one honest line for the panel's result field.
fn summarize(tool: &str, v: &Value) -> String {
    if let Some(e) = v.get("error").and_then(Value::as_str) {
        return format!("✕ {e}");
    }
    if v.get("success").and_then(Value::as_bool) == Some(false) {
        return format!("✕ {}", str_of(v, "message"));
    }
    match tool {
        "archive_chat" => format!(
            "✓ archived {} message(s) from chat {}",
            i64_of(v, "archived_count"),
            i64_of(v, "chat_id")
        ),
        "start_gradual_export" => "✓ headless export started — progress shows here, no Tlgrm window".into(),
        "cancel_gradual_export" => "✓ export cancelled".into(),
        "list_chats" => format!("✓ list_chats → {} chats", i64_of(v, "count")),
        "start_bot" | "stop_bot" => {
            let m = str_of(v, "message");
            if m.is_empty() { "✓ done".into() } else { format!("✓ {m}") }
        }
        "text_to_speech" => {
            let m = str_of(v, "message");
            if m.is_empty() { "✓ synthesized".into() } else { format!("✓ {m}") }
        }
        _ => "✓ done".into(),
    }
}

// --- Retention (device 2): the live git-style version store -------------------
fn refresh_retention(state: &HostState, sock: &str, token: &str) {
    // Start from the last-good snapshot; only overwrite what fetches.
    let mut ret = state.retention();

    match call(sock, token, "get_retention_stats", json!({})) {
        Some(s) => {
            ret.stats = RetentionStats {
                available: s.get("available").and_then(Value::as_bool).unwrap_or(false),
                total_versions: i64_of(&s, "total_versions"),
                messages_tracked: i64_of(&s, "messages_tracked"),
                edits: i64_of(&s, "edits"),
                deletions: i64_of(&s, "deletions"),
            };
        }
        None => {
            // A transient poll miss (the bridge is flaky under load, and we
            // already retry). Keep the last-good snapshot rather than flicker
            // the panel to "not reporting" while the archiver is up.
            return;
        }
    }

    if let Some(l) = call(sock, token, "list_message_history", json!({ "limit": 20 })) {
        if let Some(arr) = l.get("messages").and_then(Value::as_array) {
            ret.tracked = arr
                .iter()
                .map(|x| TrackedMsg {
                    chat_id: i64_of(x, "chat_id"),
                    message_id: i64_of(x, "message_id"),
                    versions: i64_of(x, "versions"),
                    latest_kind: str_of(x, "latest_kind"),
                    latest_content: str_of(x, "latest_content"),
                })
                .collect();
        }
    }

    let sel = state
        .retention_selected()
        .or_else(|| ret.tracked.first().map(|t| (t.chat_id, t.message_id)));
    if let Some((cid, mid)) = sel {
        if ret.selected != Some((cid, mid)) {
            ret.chain.clear();
        }
        ret.selected = Some((cid, mid));
        if let Some(h) = call(
            sock,
            token,
            "get_message_history",
            json!({ "chat_id": cid, "message_id": mid }),
        ) {
            if let Some(arr) = h.get("versions").and_then(Value::as_array) {
                ret.chain = arr
                    .iter()
                    .map(|v| MsgVersion {
                        version: i64_of(v, "version"),
                        kind: str_of(v, "kind"),
                        content: str_of(v, "content"),
                        captured_at: i64_of(v, "captured_at"),
                    })
                    .collect();
            }
        }
    }

    // Ephemeral capture switches: apply a pending click, then read the real
    // per-type flags back so the panel shows the client's true state.
    if let Some((sd, vo, va)) = state.take_capture_pending() {
        let _ = call(
            sock,
            token,
            "configure_ephemeral_capture",
            json!({ "capture_self_destruct": sd, "capture_view_once": vo, "capture_vanishing": va }),
        );
    }
    if let Some(es) = call(sock, token, "get_ephemeral_stats", json!({})) {
        if let (Some(sd), Some(vo), Some(va)) = (
            es.get("capture_self_destruct").and_then(Value::as_bool),
            es.get("capture_view_once").and_then(Value::as_bool),
            es.get("capture_vanishing").and_then(Value::as_bool),
        ) {
            state.set_capture_truth((sd, vo, va));
        }
    }

    state.set_retention(ret);
}

pub fn spawn(state: HostState, sock: String, token_path: String) {
    thread::spawn(move || loop {
        thread::sleep(Duration::from_millis(2000));
        let token = std::fs::read_to_string(&token_path)
            .unwrap_or_default()
            .trim()
            .to_string();
        if token.is_empty() {
            continue;
        }

        // 1. Run any action a button queued (highest priority — the user is
        //    waiting on its result line).
        run_action(&state, &sock, &token);
        run_mcp_invoke(&state, &sock, &token);
        run_transcribe(&state, &sock, &token);
        run_bot_configure(&state, &sock, &token);
        run_archive_search(&state, &sock, &token);
        run_wallet_search(&state, &sock, &token);
        run_bot_command(&state, &sock, &token);
        run_send_vm(&state, &sock, &token);

        // 2. Refresh only the device on stage.
        match state.selected() {
            2 => refresh_retention(&state, &sock, &token),
            other => refresh_panel(&state, &sock, &token, other),
        }
    });
}

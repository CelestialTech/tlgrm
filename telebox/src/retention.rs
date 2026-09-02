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

use std::io::{BufRead, BufReader, Write};
use std::os::unix::net::UnixStream;
use std::thread;
use std::time::Duration;

use serde_json::{json, Value};

use crate::host::{HostState, MsgVersion, PanelRow, RetentionStats, TrackedMsg};

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
// occasional refusal of a rapid reconnect.
fn call(sock: &str, token: &str, name: &str, args: Value) -> Option<Value> {
    for attempt in 0..4 {
        if attempt > 0 {
            thread::sleep(Duration::from_millis(180));
        }
        if let Some(v) = call_once(sock, token, name, &args) {
            return Some(v);
        }
    }
    None
}

// A single call that tolerates a slow tool. An action button can trigger real
// work — synthesizing speech, archiving a large chat — that runs well past the
// 5s read window a poll uses, so the outcome is worth waiting for. One long
// attempt, then one retry.
fn call_slow(sock: &str, token: &str, name: &str, args: &Value) -> Option<Value> {
    for attempt in 0..2 {
        if attempt > 0 {
            thread::sleep(Duration::from_millis(250));
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
                    return Some(parsed);
                }
            }
        }
    }
    None
}

// The raw tools/list method (not a tools/call): returns result.tools[] directly.
fn list_tools_once(sock: &str, token: &str) -> Option<Vec<(String, String)>> {
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
                Some((name, desc))
            })
            .collect(),
    )
}
fn list_tools(sock: &str, token: &str) -> Option<Vec<(String, String)>> {
    for attempt in 0..4 {
        if attempt > 0 {
            thread::sleep(Duration::from_millis(180));
        }
        if let Some(v) = list_tools_once(sock, token) {
            return Some(v);
        }
    }
    None
}

fn i64_of(v: &Value, k: &str) -> i64 {
    v.get(k).and_then(Value::as_i64).unwrap_or(0)
}
fn str_of(v: &Value, k: &str) -> String {
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
fn chat_rows(sock: &str, token: &str) -> Vec<PanelRow> {
    if let Some(s) = call(sock, token, "list_chats", json!({})) {
        if let Some(arr) = s.get("chats").and_then(Value::as_array) {
            return arr
                .iter()
                .take(40)
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
        // MCP — the aggregated endpoint: browse the real tool catalog.
        0 => {
            if let Some(tools) = list_tools(sock, token) {
                let readout = vec![
                    ("endpoint".into(), "/tmp/tlgrm_mcp.sock".into()),
                    ("tools advertised".into(), tools.len().to_string()),
                    ("transport".into(), "JSON-RPC · unix socket".into()),
                ];
                let rows = tools
                    .into_iter()
                    .take(400)
                    .map(|(n, d)| PanelRow { id: 0, sid: n.clone(), title: n, sub: d, on: false })
                    .collect();
                state.set_panel_readout(0, readout, rows, true);
            }
        }
        // Export — the HEADLESS gradual engine to disk: live progress + a chat
        // to export. get_gradual_export_status reads GradualArchiver, NOT the
        // client's native Export::Controller, so nothing pops up in Tlgrm.
        1 => {
            let mut readout = Vec::new();
            if let Some(s) = call(sock, token, "get_gradual_export_status", json!({})) {
                let st = str_of(&s, "state");
                readout.push(("state".into(), if st.is_empty() { "idle".into() } else { st }));
                let title = str_of(&s, "chat_title");
                if !title.is_empty() {
                    readout.push(("chat".into(), title));
                }
                let (arch, total) = (i64_of(&s, "archived_messages"), i64_of(&s, "total_messages"));
                if arch > 0 || total > 0 {
                    readout.push(("progress".into(), format!("{arch} / {total}")));
                }
                let batches = i64_of(&s, "batches_completed");
                if batches > 0 {
                    readout.push(("batches".into(), batches.to_string()));
                }
            }
            let rows = chat_rows(sock, token);
            readout.push(("chats loaded".into(), rows.len().to_string()));
            state.set_panel_readout(1, readout, rows, true);
        }
        // Archiver — archive ANY chat to the SQLite store: real stats + a chat.
        3 => {
            let mut readout = Vec::new();
            if let Some(s) = call(sock, token, "get_archive_stats", json!({})) {
                if s.get("error").is_some() {
                    readout.push(("archiver".into(), str_of(&s, "error")));
                } else {
                    readout.push(("messages archived".into(), i64_of(&s, "total_messages").to_string()));
                    readout.push(("chats".into(), i64_of(&s, "total_chats").to_string()));
                    readout.push(("ephemeral captured".into(), i64_of(&s, "ephemeral_captured").to_string()));
                    readout.push(("db size".into(), human_bytes(i64_of(&s, "database_size_bytes"))));
                }
            }
            let rows = chat_rows(sock, token);
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
        }
        // Wallet — Stars / TON: the real live balance. Spending stays dark.
        5 => {
            let mut readout = Vec::new();
            if let Some(s) = call(sock, token, "get_wallet_balance", json!({})) {
                if s.get("loaded").and_then(Value::as_bool) == Some(true) {
                    readout.push(("stars balance".into(), i64_of(&s, "stars_balance").to_string()));
                    readout.push(("source".into(), "payments.getStarsStatus · live".into()));
                } else {
                    let e = str_of(&s, "error");
                    readout.push(("wallet".into(), if e.is_empty() { "unavailable".into() } else { e }));
                }
            }
            state.set_panel_readout(5, readout, Vec::new(), true);
        }
        // AI — local LLM / TTS / voice: probe the real TTS service state.
        6 => {
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
            state.set_panel_readout(6, readout, Vec::new(), true);
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

        // 2. Refresh only the device on stage.
        match state.selected() {
            2 => refresh_retention(&state, &sock, &token),
            other => refresh_panel(&state, &sock, &token, other),
        }
    });
}

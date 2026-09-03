// The QA API — drive the app and grab the rendered view without a screen.
//
// A local socket that speaks newline JSON. Every command runs the SAME
// HostState operation the on-screen control runs, then returns a snapshot of
// what the panel now shows. `shot` renders the live window to a real PNG via
// the GPUI render loop (host.request_shot → render_to_image), so pixels can be
// grabbed even when the OS screen is locked.
//
//   {"cmd":"snapshot"}              -> full render state
//   {"cmd":"start"|"stop"|"restart"} -> lifecycle, then snapshot
//   {"cmd":"toggle","i":1}         -> flip plugin i, then snapshot
//   {"cmd":"view","name":"permissions"} -> switch view, then snapshot
//   {"cmd":"shot","path":"/tmp/x.png"}  -> rendered PNG, returns {w,h,ok}

use std::fs::Permissions;
use std::io::{BufRead, BufReader, Write};
use std::os::unix::fs::PermissionsExt;
use std::os::unix::net::{UnixListener, UnixStream};
use std::thread;
use std::time::Duration;

use crate::host::{HostState, PanelView};

pub fn spawn(state: HostState, path: String) {
    thread::spawn(move || {
        let _ = std::fs::remove_file(&path);
        let listener = match UnixListener::bind(&path) {
            Ok(l) => l,
            Err(e) => {
                state.log("qa", format!("QA endpoint bind failed: {e}"));
                return;
            }
        };
        let _ = std::fs::set_permissions(&path, Permissions::from_mode(0o600));
        state.log("qa", format!("QA endpoint on {path}"));
        for conn in listener.incoming() {
            let Ok(s) = conn else { continue };
            let state = state.clone();
            thread::spawn(move || handle(state, s));
        }
    });
}

fn handle(state: HostState, s: UnixStream) {
    let Ok(rclone) = s.try_clone() else { return };
    let reader = BufReader::new(rclone);
    let mut w = s;
    for line in reader.lines() {
        let Ok(line) = line else { break };
        if line.trim().is_empty() {
            continue;
        }
        let v: serde_json::Value = match serde_json::from_str(&line) {
            Ok(v) => v,
            Err(e) => {
                let _ = writeln!(w, "{}", serde_json::json!({ "error": format!("bad json: {e}") }));
                let _ = w.flush();
                continue;
            }
        };
        let cmd = v.get("cmd").and_then(|c| c.as_str()).unwrap_or("");
        let resp = match cmd {
            "snapshot" => state.snapshot(),
            "start" => {
                state.start();
                state.snapshot()
            }
            "stop" => {
                state.stop();
                state.snapshot()
            }
            "restart" => {
                state.restart();
                state.snapshot()
            }
            "toggle" => {
                let i = v.get("i").and_then(|x| x.as_u64()).unwrap_or(0) as usize;
                state.toggle_plugin(i);
                state.snapshot()
            }
            "select" => {
                let i = v.get("i").and_then(|x| x.as_u64()).unwrap_or(0) as usize;
                state.select(i);
                state.snapshot()
            }
            "toggle_capture" => {
                // Exercise the exact path a capture-switch click takes.
                let which = v.get("which").and_then(|x| x.as_u64()).unwrap_or(0) as usize;
                state.toggle_capture(which);
                state.snapshot()
            }
            "panel_row" => {
                // Pick a row in device i's list (chat / bot / tool), as a click would.
                let i = v.get("i").and_then(|x| x.as_u64()).unwrap_or(0) as usize;
                let row = v.get("row").and_then(|x| x.as_u64()).unwrap_or(0) as usize;
                state.select_panel_row(i, row);
                state.snapshot()
            }
            "action" => {
                // Fire device i's primary action — the exact path its button takes.
                let i = v.get("i").and_then(|x| x.as_u64()).unwrap_or(0) as usize;
                state.primary_action(i);
                state.snapshot()
            }
            "search" => {
                // Set the Export chat-search string (the same state a keystroke sets).
                let t = v.get("text").and_then(|x| x.as_str()).unwrap_or("").to_string();
                state.set_export_search(t);
                state.snapshot()
            }
            "pick_export" => {
                // Choose the export target chat by id (the same as a row click).
                let id = v.get("id").and_then(|x| x.as_i64()).unwrap_or(0);
                let title = v.get("title").and_then(|x| x.as_str()).unwrap_or("").to_string();
                state.set_export_target(id, title);
                state.snapshot()
            }
            "export_engine" => {
                // Start the Rust export engine directly — for tests, with a small
                // `max` cap and a scratch `dir`, so a run is never a heavy export.
                let id = v.get("chat_id").and_then(|x| x.as_i64()).unwrap_or(0);
                let title = v.get("title").and_then(|x| x.as_str()).unwrap_or("test").to_string();
                let max = v.get("max").and_then(|x| x.as_i64());
                let (sock, token) = state.relay_creds();
                match v.get("dir").and_then(|x| x.as_str()) {
                    Some(d) => crate::export_engine::spawn_to(
                        state.clone(), sock, token, id, title, max, std::path::PathBuf::from(d)),
                    None => crate::export_engine::spawn(state.clone(), sock, token, id, title, max),
                }
                state.snapshot()
            }
            "view" => {
                if let Some(pv) = v
                    .get("name")
                    .and_then(|x| x.as_str())
                    .and_then(PanelView::from_name)
                {
                    state.set_view(pv);
                }
                state.snapshot()
            }
            "shot" => {
                let path = v
                    .get("path")
                    .and_then(|x| x.as_str())
                    .unwrap_or("/tmp/telebox_shot.png")
                    .to_string();
                state.request_shot(path.clone());
                // Wait for the GPUI render loop to fulfill it (~up to 3s).
                let mut result = None;
                for _ in 0..60 {
                    if let Some(r) = state.poll_shot() {
                        result = Some(r);
                        break;
                    }
                    thread::sleep(Duration::from_millis(50));
                }
                match result {
                    Some((w2, h2, ok)) => {
                        serde_json::json!({ "cmd": "shot", "path": path, "w": w2, "h": h2, "ok": ok })
                    }
                    None => serde_json::json!({ "cmd": "shot", "ok": false, "error": "timeout" }),
                }
            }
            other => serde_json::json!({ "error": format!("unknown cmd: {other}") }),
        };
        if writeln!(w, "{resp}").is_err() {
            break;
        }
        let _ = w.flush();
    }
}

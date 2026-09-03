// The TeleBox export engine — Rust, headless, on top of the client's raw
// get_chat_history API.
//
// This is the architecture the user asked for: the export LOGIC (paging,
// progress, writing, cancellation) lives HERE in TeleBox, and Tlgrm only serves
// one raw page of messages.getHistory (get_chat_history). Because it pages by
// the chat's REAL server `count`, it never truncates the way the old C++
// local-cache estimate did; it writes every message to a JSONL file and tracks
// real message + attachment-byte progress into HostState::export_run.

use std::fs;
use std::io::Write;
use std::path::PathBuf;
use std::thread;
use std::time::{Duration, SystemTime, UNIX_EPOCH};

use serde_json::json;

use crate::host::{ExportRun, HostState};
use crate::retention::{call_slow, i64_of, str_of};

const PAGE: i64 = 100; // messages per get_chat_history call
const THROTTLE_MS: u64 = 300; // gentle gap between pages

fn sanitize(s: &str) -> String {
    let cleaned: String = s
        .chars()
        .map(|c| if c.is_alphanumeric() || c == ' ' || c == '_' || c == '-' { c } else { '_' })
        .collect();
    cleaned.trim().replace(' ', "_")
}

fn default_out_dir(title: &str) -> PathBuf {
    let home = std::env::var("HOME").unwrap_or_else(|_| "/tmp".into());
    let ts = SystemTime::now().duration_since(UNIX_EPOCH).map(|d| d.as_secs()).unwrap_or(0);
    let mut p = PathBuf::from(home);
    p.push("Downloads");
    p.push("TeleBox Exports");
    p.push(format!("{}-{}", sanitize(title), ts));
    p
}

fn mb(bytes: i64) -> String {
    format!("{:.1} MB", bytes as f64 / (1024.0 * 1024.0))
}

// A full export to the default Downloads location (the on-screen button). A real
// export fetches the actual media files, so `download_media` is on.
pub fn spawn(state: HostState, sock: String, token_path: String, chat_id: i64, title: String, max_messages: Option<i64>) {
    let dir = default_out_dir(&title);
    spawn_to(state, sock, token_path, chat_id, title, max_messages, true, dir);
}

// The engine proper, with an explicit output dir (QA tests use a scratch dir and
// a small `max_messages` cap so a run is never heavy). When `download_media` is
// set, each media message's file is fetched to <out_dir>/media via the client's
// download_media tool and recorded in the JSONL as media_file + media_bytes.
pub fn spawn_to(
    state: HostState,
    sock: String,
    token_path: String,
    chat_id: i64,
    title: String,
    max_messages: Option<i64>,
    download_media: bool,
    out_dir: PathBuf,
) {
    thread::spawn(move || {
        state.clear_export_cancel();
        let token = fs::read_to_string(&token_path).unwrap_or_default().trim().to_string();
        let path_str = out_dir.display().to_string();

        if let Err(e) = fs::create_dir_all(&out_dir) {
            state.set_export_run(None);
            state.set_result(1, format!("✕ export: cannot create {path_str}: {e}"));
            return;
        }
        let mut file = match fs::File::create(out_dir.join("messages.jsonl")) {
            Ok(f) => f,
            Err(e) => {
                state.set_export_run(None);
                state.set_result(1, format!("✕ export: cannot write file: {e}"));
                return;
            }
        };
        state.log("export", format!("export {title} → {path_str}"));

        let media_dir = out_dir.join("media");
        let mut offset = 0i64;
        let (mut done, mut total, mut bytes) = (0i64, 0i64, 0i64);
        let mut media_files = 0i64;
        let running = |done, total, bytes| ExportRun {
            chat: title.clone(),
            done,
            total,
            bytes,
            state: "exporting".into(),
            path: path_str.clone(),
        };
        state.set_export_run(Some(running(0, 0, 0)));

        loop {
            if state.export_cancel_requested() {
                let _ = file.flush();
                state.set_export_run(None);
                state.set_result(1, format!("export cancelled — {done} of {total} messages saved to {path_str}"));
                state.log("export", "export cancelled");
                return;
            }

            let page = call_slow(&sock, &token, "get_chat_history",
                &json!({ "chat_id": chat_id, "offset_id": offset, "limit": PAGE }));
            let Some(p) = page else {
                let _ = file.flush();
                state.set_export_run(None);
                state.set_result(1, format!("✕ export: client did not answer ({done} saved)"));
                return;
            };
            if p.get("success").and_then(|v| v.as_bool()) != Some(true) {
                let _ = file.flush();
                state.set_export_run(None);
                state.set_result(1, format!("✕ export: {}", str_of(&p, "error")));
                return;
            }
            if total == 0 {
                total = i64_of(&p, "count"); // the chat's REAL total — set once
            }

            let msgs = p.get("messages").and_then(|v| v.as_array()).cloned().unwrap_or_default();
            for m in &msgs {
                let mut record = m.clone();
                // Attachment bytes: the get_chat_history estimate by default,
                // replaced by the real downloaded size when we fetch the file.
                let mut msg_bytes = i64_of(m, "media_size");
                if download_media && m.get("media_type").is_some() {
                    let mid = i64_of(m, "id");
                    let dl = call_slow(&sock, &token, "download_media", &json!({
                        "chat_id": chat_id,
                        "message_id": mid,
                        "out_dir": media_dir.display().to_string(),
                    }));
                    match dl {
                        Some(d) if d.get("success").and_then(|v| v.as_bool()) == Some(true) => {
                            let fbytes = i64_of(&d, "bytes");
                            msg_bytes = fbytes; // real, not the estimate
                            media_files += 1;
                            if let Some(o) = record.as_object_mut() {
                                o.insert("media_file".into(), json!(str_of(&d, "path")));
                                o.insert("media_bytes".into(), json!(fbytes));
                            }
                        }
                        Some(d) => {
                            if let Some(o) = record.as_object_mut() {
                                o.insert("media_error".into(), json!(str_of(&d, "error")));
                            }
                        }
                        None => {
                            if let Some(o) = record.as_object_mut() {
                                o.insert("media_error".into(), json!("client did not answer"));
                            }
                        }
                    }
                }
                let _ = writeln!(file, "{}", serde_json::to_string(&record).unwrap_or_default());
                bytes += msg_bytes;
                done += 1;
                state.set_export_run(Some(running(done, total, bytes)));
                if let Some(cap) = max_messages {
                    if done >= cap {
                        let _ = file.flush();
                        state.set_export_run(None);
                        state.set_result(1, format!(
                            "✓ exported {done} messages · {media_files} media file(s) ({}) → {path_str} (test cap {cap})", mb(bytes)));
                        state.log("export", format!("export capped at {cap} → {path_str}"));
                        return;
                    }
                }
            }

            let next = i64_of(&p, "next_offset_id");
            let has_more = p.get("has_more").and_then(|v| v.as_bool()).unwrap_or(false);
            state.set_export_run(Some(running(done, total, bytes)));

            if !has_more || next == 0 || msgs.is_empty() {
                let _ = file.flush();
                state.set_export_run(None);
                state.set_result(1, format!(
                    "✓ exported {done} of {total} messages · {media_files} media file(s) ({}) → {path_str}", mb(bytes)));
                state.log("export", format!("export complete: {done} messages, {media_files} media → {path_str}"));
                return;
            }
            offset = next;
            thread::sleep(Duration::from_millis(THROTTLE_MS));
        }
    });
}

// The MCP plugin, wired for real.
//
// TeleBox listens on its own aggregated MCP socket and proxies every
// connection to the client's existing bridge socket. It owns the endpoint:
// downstream clients (Claude, tools) connect to TeleBox, and TeleBox injects
// the bridge's auth token into the `initialize` so callers never handle it.
//
// No client changes — this rides the socket the Tlgrm client already serves.
// A pure byte relay would work; we parse the downstream line only to count
// requests for the panel and to inject the token on initialize.

use std::fs::Permissions;
use std::io::{BufRead, BufReader, Write};
use std::os::unix::fs::PermissionsExt;
use std::os::unix::net::{UnixListener, UnixStream};
use std::thread;

use crate::host::HostState;

pub fn spawn(state: HostState, listen: String, upstream: String, token_path: String) {
    thread::spawn(move || {
        let _ = std::fs::remove_file(&listen);
        let listener = match UnixListener::bind(&listen) {
            Ok(l) => l,
            Err(e) => {
                state.log("host", format!("MCP endpoint bind failed: {e}"));
                return;
            }
        };
        let _ = std::fs::set_permissions(&listen, Permissions::from_mode(0o600));
        state.log("host", format!("MCP endpoint listening on {listen}"));

        for conn in listener.incoming() {
            let Ok(down) = conn else { continue };
            let state = state.clone();
            let upstream = upstream.clone();
            let token_path = token_path.clone();
            thread::spawn(move || handle(state, down, upstream, token_path));
        }
    });
}

fn handle(state: HostState, down: UnixStream, upstream: String, token_path: String) {
    let up = match UnixStream::connect(&upstream) {
        Ok(s) => s,
        Err(e) => {
            state.set_upstream(false);
            state.log("mcp", format!("upstream connect failed: {e}"));
            return;
        }
    };
    state.set_upstream(true);
    state.client_delta(1);
    state.log("mcp", "client connected — proxying to bridge");

    let token = std::fs::read_to_string(&token_path)
        .unwrap_or_default()
        .trim()
        .to_string();

    // upstream → downstream: raw copy, its own thread.
    let mut up_read = match up.try_clone() {
        Ok(s) => s,
        Err(_) => return,
    };
    let mut down_write = match down.try_clone() {
        Ok(s) => s,
        Err(_) => return,
    };
    let pump = thread::spawn(move || {
        let _ = std::io::copy(&mut up_read, &mut down_write);
    });

    // downstream → upstream: line by line, parse to count + inject token.
    let reader = BufReader::new(down);
    let mut up_write = up;
    for line in reader.lines() {
        let Ok(line) = line else { break };
        if line.trim().is_empty() {
            continue;
        }
        let mut out = line.clone();
        if let Ok(mut v) = serde_json::from_str::<serde_json::Value>(&line) {
            if let Some(method) = v.get("method").and_then(|m| m.as_str()).map(String::from) {
                state.record_request(&method);
                if method == "initialize" {
                    match v.get_mut("params").and_then(|p| p.as_object_mut()) {
                        Some(params) => {
                            params.insert(
                                "auth_token".into(),
                                serde_json::Value::String(token.clone()),
                            );
                        }
                        None => {
                            v["params"] = serde_json::json!({ "auth_token": token });
                        }
                    }
                    out = v.to_string();
                    state.log("mcp", "initialize → injected host token");
                } else {
                    state.log("mcp", format!("→ {method}"));
                }
            }
        }
        if writeln!(up_write, "{out}").is_err() {
            break;
        }
        let _ = up_write.flush();
    }

    let _ = pump.join();
    state.client_delta(-1);
    state.log("mcp", "client disconnected");
}

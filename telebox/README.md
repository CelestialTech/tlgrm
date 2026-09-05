# TeleBox

The plugin-hosted controller for the Tlgrm client — a separate macOS app on
**GPUI** (Rust, GPU-accelerated, from Zed; no webview). It owns the aggregated
MCP endpoint that Claude and other clients connect to, and hosts the local-only
surface as a rack of **device panels**, each driving its domain through the
client's real MCP tools. Design and rationale: `../PROPOSAL_REFACTOR.md`;
ontology: `ONTOLOGY.md`; the gap-closing ledger: `GATES.md`.

The governing rule: **every feature is preserved and relocated to a plugin —
nothing is discarded, and TeleBox has its own UI for every use case** (it never
drives the client's native windows).

## Status — all seven devices wired end-to-end

Each panel does its domain's real work against the live client (not a readout),
and every action is verified end-to-end through the QA socket.

| Device | What it does | Key tools |
|---|---|---|
| **MCP** | The aggregated tool socket: a collapsible tool tree across **14 domains** (the live endpoint serves 396 tools); select a tool to see its description + params; fill params (scalars, and array/object via JSON) and **Invoke** read tools; a **live-traffic monitor** (recent calls + per-minute rate) and a **recent-invokes history**. Mutating tools are guarded — run from their own plugin. | `tools/list`, any `get_/list_/search_/count_` |
| **Export** | A **headless** Rust engine (`export_engine.rs`) that pages `get_chat_history` → JSONL and downloads each media file, tracking real message + byte progress. Pause/Resume, Cancel, and a media on/off toggle. **No native Tlgrm export window.** | `get_chat_history`, `download_media` |
| **Retention** | The git-style message-version store: version/edit/deletion stats; three **ephemeral-capture** toggles (self-destruct / view-once / vanishing); the tracked-message list; and a version chain with a per-version **diff** (− removed / + added). | `get_retention_stats`, `list_message_history`, `configure_ephemeral_capture` |
| **Archiver** | Archive any chat to a local SQLite store, **browse** what you've archived (`list_archived_chats`), and **search** the archived message content (`search_archive`), plus store stats. | `archive_chat`, `list_archived_chats`, `search_archive`, `get_archive_stats` |
| **Bots** | The automation framework: list + inspect bots (identity, permissions, tags, live stats), Start/Stop, **edit config** (typed toggles + steppers → `configure_bot`), and **send a command** (`send_bot_command`). | `list_bots`, `get_bot_info`, `start_bot`/`stop_bot`, `configure_bot`, `send_bot_command` |
| **Wallet** | Stars / TON: live balance, the transaction list + **search** (`search_transactions`), and the owned-**gift portfolio** (`get_profile_gifts`). Spending (`send_stars`/`send_star_gift`) stays dark by design. | `get_wallet_balance`, `get_transactions`, `search_transactions`, `get_profile_gifts` |
| **AI** | Local voice: speak a TTS sample; a transcription flow (pick a chat → its voice messages → download + `transcribe_voice_message`); and **send a synthesized voice message** to the picked chat (`send_voice_reply`). | `text_to_speech`, `download_media`, `transcribe_voice_message`, `send_voice_reply` |

Every control is a thin wrapper over a `HostState` method — the same method the
QA API calls — so there is one implementation, not two that can drift.

## Architecture — one serialized bridge

TeleBox is an MCP **client** to the Tlgrm bridge (`/tmp/tlgrm_mcp.sock`), and
also re-hosts the aggregated endpoint (`/tmp/telebox_host.sock`) for downstream
clients, injecting the bridge's auth token so callers never handle it.

The client bridge serves **one tool call at a time** — its re-entrancy guard
rejects a second, and the single-call socket drops concurrent connections. Since
TeleBox drives it from several threads (the UI poller, the export engine, queued
actions), all client I/O is **serialized through one gate** (`retention.rs`
`BRIDGE`) with a 150 ms minimum spacing (`throttle`) so bursts don't churn the
socket, plus a 4-attempt backoff retry. This is what removed the intermittent
"client did not answer". Only the on-stage panel is polled, so load stays at one
or two calls per tick.

The client's **native export auto-resume is disabled** — TeleBox exports
headlessly, and the client must never open its native export window on startup.
See `../SELF_GATES.md` (G-AR) and the gate memory.

## QA API — drive the app and grab the render without a screen

`qa.rs` opens `/tmp/telebox_qa.sock`, speaking newline JSON. It drives the app
directly and renders the window to a PNG **offscreen**, so verification works
even with the screen locked (a locked screen blanks `screencapture`). It also
underpins the end-to-end tests that prove each device.

```
{"cmd":"snapshot"}                         -> full render state as JSON
{"cmd":"select","i":<0..6>}                -> put device i on stage
{"cmd":"action","i":<n>}                   -> fire device n's primary action
{"cmd":"mcp_select","tool":"..."}          -> select an MCP tool
{"cmd":"mcp_arg","name":"...","value":".."}-> fill an arg-form field
{"cmd":"mcp_invoke","tool":"..."}          -> invoke the selected read tool
{"cmd":"export_engine","chat_id":..,"max":..,"media":..,"dir":".."} -> run the export engine (capped)
{"cmd":"export_pause"} / {"cmd":"export_media"}                     -> toggle pause / media
{"cmd":"select_bot","id":".."} / {"cmd":"bots_toggle"|"bots_step"|"bots_configure"} -> bot config editor
{"cmd":"bot_command","command":".."}       -> send a bot command
{"cmd":"archive_search"|"wallet_search","query":".."}              -> search the archive / transactions
{"cmd":"pick_ai_chat","chat_id":..} / {"cmd":"select_voice","message_id":..} / {"cmd":"transcribe"}
{"cmd":"send_vm","text":".."}              -> send a voice message to the picked chat
{"cmd":"shot","path":"/tmp/x.png"}         -> render the live window to a PNG {w,h,ok}
```

`snapshot` grabs the rendered view *as data* (per-device state, the on-stage
panel, the log tail) — what QA asserts on. `shot` grabs it as *pixels* via
GPUI's `Window::render_to_image` (a real Metal offscreen render). Note: driving
a panel via the QA socket updates state but does not force a repaint of an
unfocused window — macOS pauses that — so a background screen-capture may show a
stale frame; the QA snapshot is the authority.

## Layout

| Path | What |
|---|---|
| `src/main.rs` | App entry — the GPUI window and every device panel's render. |
| `src/host.rs` | `HostState`: shared state, the device registry, and the one method behind each control. |
| `src/retention.rs` | The relay driver — the poller loop, the serialized `call`/`call_slow` bridge (gate + throttle + traffic recorder), and every action runner. |
| `src/export_engine.rs` | The headless Rust export engine (paging + media download + pause/cancel). |
| `src/mcp_relay.rs` | The aggregated MCP endpoint proxying to the client bridge. |
| `src/mcp_tree.rs` + `src/mcp_taxonomy.json` | The 14-domain / 58-subdomain taxonomy behind the MCP tree. |
| `src/qa.rs` | The QA socket — drive controls and render the window to a PNG offscreen. |
| `ONTOLOGY.md` | How every panel derives from Service / Item / Job / Store / Setting. |
| `GATES.md` | The gap-closing ledger (each gate proven live). |

## Build & run

```bash
cargo build --release      # from telebox/
cp target/release/telebox TeleBox.app/Contents/MacOS/telebox   # deploy into the bundle
```

The devices drive a **running Tlgrm client** (they need `/tmp/tlgrm_mcp.sock`);
the app still builds and runs without one — panels show `Upstream: —` until a
client is up. The client build lives in `../tdesktop/build` (Ninja):
`ninja Telegram`, then relaunch from `build/Tlgrm.app` — see
`reference_client_build_tree` in memory.

### Two gotchas, both load-bearing

- **Toolchain.** `gpui` at v1.15.1 uses stdlib features stabilized in **1.95.0**;
  an older stable fails to compile `gpui_util`. `rust-toolchain.toml` pins it,
  matching Zed's own pin. `gpui` and `gpui_platform` must share the same tag.
- **Fonts.** `gpui_platform` needs the **`font-kit`** feature on macOS or text
  renders as nothing. It is set in `Cargo.toml`; do not drop it.

The first build compiles GPUI from source and is slow (~minutes); incremental
builds are seconds. Bump the pinned Zed revision deliberately — its API moves
between commits.

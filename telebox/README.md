# TeleBox

The plugin-hosted controller for the Tlgrm client. A separate macOS app on
**GPUI** (Rust, GPU-accelerated, from Zed — no webview), hosting the local-only
surface as plugins. Design and rationale live in `../PROPOSAL_REFACTOR.md`; the
approved control-panel design is in `design/` (`plugins-console.html`).

The governing rule, from the proposal: **every feature is preserved and
relocated to a plugin — nothing is discarded.** TeleBox owns the aggregated MCP
endpoint that Claude and other clients connect to.

## Status

Milestone **M0 — first vertical slice: done and verified.** One plugin wired
end-to-end through a real native app, to prove the architecture.

- **App shell** — the GPUI control panel: host status bar, the plugin rack, a
  live activity log. Native macOS window, no webview.
- **MCP plugin, real** — TeleBox listens on its own aggregated MCP socket
  (`/tmp/telebox_host.sock`) and proxies every connection to the client's
  existing bridge (`/tmp/tlgrm_mcp.sock`), owning the endpoint: it injects the
  bridge's auth token into `initialize`, so downstream callers never handle it.
  No client changes — it rides the socket the client already serves.

Verified: a client pointed at TeleBox's socket, sending a deliberately wrong
token, gets `tools/list` = the full surface and real `tools/call` results
proxied through — while the panel's **MCP requests** counter and **Upstream:
connected** light track it live.

Everything else in the rack (Export, Retention/Vault, Archiver, Wallet, Bots,
AI) is modelled in the UI but not yet wired — that is later milestones.

### Controls

The panel is interactive:

- **Host Start / Stop / Restart** — really bind and unbind the endpoint.
- **Per-plugin bypass toggles** — MCP's drives the relay; the other six flip
  modelled state (they are not wired yet).
- **Left rail** — switches between Plugins, Permissions (the Host API grant
  matrix) and Activity (the live log).

Every control is a thin wrapper over a `HostState` method — the same method the
QA API calls — so there is one implementation, not two that can drift.

## QA API — drive the app and grab the render without a screen

`qa.rs` opens a second socket, `/tmp/telebox_qa.sock`, speaking newline JSON.
It exists because the usual way to verify a GUI — screenshot the screen, click
coordinates — fails when the machine's screen is locked (a locked screen blanks
screencapture and won't route synthetic clicks). This drives the app directly
and renders the window to a PNG **offscreen**, so QA works headless.

```
{"cmd":"snapshot"}                    -> full render state as JSON
{"cmd":"start"|"stop"|"restart"}      -> lifecycle, then snapshot
{"cmd":"toggle","i":1}                -> flip plugin i, then snapshot
{"cmd":"view","name":"permissions"}   -> switch view, then snapshot
{"cmd":"shot","path":"/tmp/x.png"}    -> render the live window to a PNG {w,h,ok}
```

`snapshot` grabs the rendered view *as data* — running state, per-plugin
active/enabled, current view, log tail — which is what QA asserts on. `shot`
grabs it as *pixels*: the render loop fulfills it with GPUI's
`Window::render_to_image` (a real Metal offscreen render), captured two-phase so
the image reflects the latest state rather than the previous frame.

**This is tested and passing.** `qa/smoke.py` drives the whole surface —
start/stop/restart, every toggle (including MCP's, which drives the real relay),
view switching — asserting the snapshot each step, then captures PNGs of the
Plugins and Permissions views. It was run with the screen **locked** and every
control verified from the resulting renders. `cargo test` additionally asserts
the lifecycle at the unit level (Stop unbinds the socket; Start/Restart rebind).

> `render_to_image` is gated behind gpui's `test-support` feature, so
> `Cargo.toml` enables it. That pulls test-only deps into the build; gating the
> QA surface behind a dedicated `qa` cargo feature for production builds is a
> follow-up.

```bash
cargo run &                 # launch the app (window may be offscreen if locked)
python3 qa/smoke.py         # drive it, assert, and write /tmp/telebox_qa_shot*.png
```

## Layout

| Path | What |
|---|---|
| `Cargo.toml` | The app crate. `gpui` + `gpui_platform` are pinned git deps on the Zed repo. |
| `rust-toolchain.toml` | Pins Rust 1.95.0 — the toolchain Zed v1.15.1 needs. |
| `src/main.rs` | App entry — the GPUI window and the control panel. |
| `src/host.rs` | The host: shared state and the plugin registry. |
| `src/mcp_relay.rs` | The MCP plugin — aggregated endpoint proxying to the client bridge. |
| `src/qa.rs` | The QA socket — drive controls and render the window to a PNG offscreen. |
| `qa/smoke.py` | The QA driver: exercises every control and captures renders. |
| `design/` | The approved control-panel designs (HTML). |

## Build & run

```bash
cargo run          # from telebox/
```

The MCP plugin proxies to a **running Tlgrm client** (it needs
`/tmp/tlgrm_mcp.sock`); the app still builds and runs without one — the panel
just shows `Upstream: —` until a client is up.

### Two gotchas, both load-bearing

- **Toolchain.** `gpui` at v1.15.1 uses stdlib features stabilized in **1.95.0**
  (e.g. `slice_as_array`); an older stable fails to compile `gpui_util`.
  `rust-toolchain.toml` pins it, matching Zed's own pin. `gpui` and
  `gpui_platform` must share the same tag.
- **Fonts.** `gpui_platform` needs the **`font-kit`** feature on macOS or text
  renders as nothing — the layout draws but every label is invisible. It is set
  in `Cargo.toml`; do not drop it.

The first build compiles GPUI from source and is slow (~minutes); incremental
builds are seconds. Bump the pinned Zed revision deliberately, not casually —
its API moves between commits.

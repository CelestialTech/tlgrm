# TeleBox

The plugin-hosted controller for the Tlgrm client. A separate macOS app on
**GPUI** (Rust, GPU-accelerated, from Zed — no webview), hosting the local-only
surface as plugins. Design and rationale live in `../PROPOSAL_REFACTOR.md`; the
approved control-panel design is in `design/` (`plugins-console.html`).

The governing rule, from the proposal: **every feature is preserved and
relocated to a plugin — nothing is discarded.** TeleBox owns the aggregated MCP
endpoint that Claude and other clients connect to.

## Status

Milestone **M0 — first vertical slice**. Not the whole host yet: one plugin
wired end-to-end to prove the architecture.

- **App shell** — the GPUI control panel: host status, the plugin rack, a live
  activity log.
- **MCP plugin, real** — TeleBox listens on its own aggregated MCP socket and
  proxies to the client's existing MCP bridge, re-exposing the full tool
  surface. No client changes: it rides the socket the client already serves.

Everything else in the rack (Export, Retention/Vault, Archiver, Wallet, Bots,
AI) is modelled in the UI but not yet wired — that is later milestones.

## Layout

| Path | What |
|---|---|
| `Cargo.toml` | The app crate. `gpui` is a pinned git dependency on the Zed repo. |
| `src/main.rs` | App entry — the GPUI window and the control panel. |
| `src/host.rs` | The host: plugin registry and lifecycle. |
| `src/mcp_relay.rs` | The MCP plugin — aggregated endpoint proxying to the client bridge. |
| `design/` | The approved control-panel designs (HTML). |

## Build & run

```bash
cargo run          # from telebox/
```

The first build compiles GPUI from source and is slow. `gpui` is pinned to a
specific Zed revision in `Cargo.toml` — bump it deliberately, not casually,
because its API moves between commits.

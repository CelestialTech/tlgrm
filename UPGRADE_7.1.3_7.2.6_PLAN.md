# Upstream port: Tlgrm 7.1.3a → Telegram Desktop 7.2.6

Assessed 2026-09-04 against `upstream` = `telegramdesktop/tdesktop` (fetch only;
never pushed). Companion to the prior `UPGRADE_7.0.9_7.1.3_PLAN.md`.

## TL;DR

- **Latest mainstream:** v7.2.6 (7.2.6 beta, 04.09.26). Fork is on **7.1.3a**.
- **TL API layer: 229 → 229 — UNCHANGED.** No scheme/protocol changes; the MCP
  server, all MCP tools, and every raw-invoke path are unaffected. This is a
  feature/UI/build port, not an API port.
- **Real conflict surface: 5 files** (upstream changed 314, the fork customized
  151, the overlap is 11, and an in-memory 3-way merge auto-resolves all but 5).
  The other ~300 upstream files — which carry the new features — merge cleanly.
- **The one real decision:** upstream's entire 7.2 line is a **v2 update system +
  signing-key rotation**, a large rewrite of the two files the fork customizes to
  point updates at its own infrastructure. Keep the fork's updater, or adopt v2
  and re-point it? (See "Decision" below — this gates execution of 2 of 5 files.)

## What the fork gains (features, from the changelog)

**7.2.6 (beta, today):**
- Image-editor **text tool** — fonts, alignment, styles.
- **Drop folders / file sets** into a chat as an archive.
- **Edit GIFs** before sending, with caption.
- **Rate finished calls with stars** in the call panel.
- Custom emoji from **video stickers**; video-editor enhancements; tooltip fixes.

**7.1.4:**
- Wider rich-message bubbles for wide content.
- Choose fitting quality for **video autoplay** in channels.
- Disable gift messages for recipients who charge for messages.
- Many fixes: call panel, profile info, chat-list menu, self-destructing media,
  video encoding, sponsored messages, TON transactions, stealth mode.

**7.1.5:** Linux crash fix.  **7.2.0–7.2.5:** the v2 update system + key rotation
(infrastructure, not user features — see Decision).

These land via the ~300 cleanly-merging files (e.g. `photo_editor/`,
`send_gif_with_caption_box.*`, `star_gift_box.*`, `transfer_gift_box.*`).

## Conflict resolution — the 5 files

| File | fork Δ | upstream Δ | Nature / strategy |
|---|---|---|---|
| `Telegram/build/version` | small | small | **Mechanical.** Bump to 7.2.6; keep the fork's `a`-suffix scheme → `7.2.6a`, AppVersion `700200601`. |
| `Telegram/SourceFiles/core/version.h` | small | small | **Mechanical.** Same bump; keep `AppVersionOriginal 7.2.6`. |
| `Telegram/SourceFiles/window/main_window.cpp` | +1 | small | **Trivial branding.** Keep the fork's `setTitle("Tlgrm" …)`. |
| `Telegram/SourceFiles/core/update_checker.cpp` | +26/-15 | **+644/-136** | **Load-bearing (Decision).** Fork redirects the feed to `updates71grm` (MTProto) + `updates.71grm.site` (HTTP), `/current` with no `AutoUpdateVersion()` suffix. Upstream rewrote the whole updater for v2. |
| `Telegram/SourceFiles/_other/packer.cpp` | +13/-6 | **+522/-5** | **Load-bearing (Decision).** Upstream rewrote the packer for v2 signing; the fork's packer feeds the release pipeline (`create_dmg.sh` + `tools/publish_update.py`). |

## Decision — the v2 update system

Upstream 7.2 replaced its update mechanism (v2 manifest, rotated then revoked
signing keys, new packer). The fork deliberately runs its **own** update system
(HTTP via ironforge → `updates.71grm.site`, MTProto via `@updates71grm`), and the
two conflicting files are exactly where the fork points updates at its own
servers. Two ways to resolve them:

- **A — Keep the fork's updater (recommended default).** Take "ours" for the
  update-specific hunks in `update_checker.cpp` / `packer.cpp`, ignore upstream's
  v2 rewrite. The working release pipeline is untouched; lowest risk. Cost: the
  fork's updater stays on its current (v1) shape — which already works.
- **B — Adopt upstream's v2 updater, re-pointed at the fork's servers.** Re-point
  v2's manifest/host at `updates.71grm.site`/`@updates71grm`, re-issue signing
  keys, and re-verify the entire pipeline (`create_dmg.sh`, `publish_update.py`,
  the MTProto channel, the HTTP host). Larger, riskier; only worth it if v2 buys
  something the fork needs.

This is a real fork-infrastructure call (it touches the release/update pipeline,
which is load-bearing). **A is the safe default** and is assumed below unless the
owner chooses B.

## Execution (once the Decision is set)

1. Branch from the current fork HEAD (`upgrade-v7.2.6`), never on `master`.
2. `git merge v7.2.6` — expect the 5 conflicts above; ~300 files merge clean.
3. Resolve: mechanical bumps (version/version.h), keep `main_window.cpp` title;
   apply the chosen strategy (A: keep fork updater) to `update_checker.cpp` +
   `packer.cpp`. Re-run `tools/check_mcp_tools.py` (should stay green — no TL
   change).
4. Universal build (`-destination 'generic/platform=macOS'`) + strip, per
   `reference_universal_build_and_strip`.
5. **Verify — gates:**
   - MCP server up; `tools/list` count ≥ prior (no tool lost).
   - The fork's feature families (export/archiver/retention/bots/wallet/AI +
     TeleBox) still function against the live client.
   - Update system: `update_checker` still targets `updates.71grm.site` /
     `@updates71grm` (grep the built binary), and the release pipeline
     (`create_dmg.sh` + `publish_update.py`) still runs.
   - New features present: image-editor text tool, GIF-with-caption, call-rating.
6. Sign + notarize (macos-codesign skill), verify with `spctl`.

## Risk

Low on the API side (layer unchanged) and for the ~300 clean files. The only
real risk is the update-system reconciliation (Decision) — a wrong resolution
breaks the fork's updater / release pipeline. Strategy A contains that risk by
preserving the fork's current updater verbatim.

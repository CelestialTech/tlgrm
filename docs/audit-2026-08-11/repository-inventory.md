# Non-submodule tree inventory — /Users/pasha/xCode/tlgrm

Repo at AppVersion 7000009 (7.0.9). Everything below excludes `tdesktop/` (the
fork itself) and `.git/`. "tracked" = appears in `git ls-files`.

## Top-level directories

| Path | What it is | State | Relationship |
|---|---|---|---|
| `cloudflare-worker/` | TypeScript Worker implementing the update protocol from GitHub release assets (`src/worker.ts`, `wrangler.toml`, `setup.sh`). Tracked. | **Standby / current-as-fallback.** Its custom-domain binding was removed when ironforge took the hostname. | Alternative origin to `update-server/`. Only one can own `updates.71grm.site`. |
| `update-server/` | Rust/axum static musl binary `tlgrm-updates`, serves `/current` + packages from `/srv/tlgrm-updates` on ironforge behind a Cloudflare tunnel. `alpine/` holds the OpenRC provisioning. Tracked. | **CURRENT — this is what serves `updates.71grm.site`.** | Confirmed by `docs/UPDATE_SYSTEM.md` and `tools/publish_update.py` docstring. Supersedes `update-proxy/` in practice. |
| `update-server/target/` | Cargo build tree incl. a 938 KB `tlgrm-updates` release binary and 178 dep dirs. | Dead weight, local only. | Ignored by `update-server/.gitignore` (`target/`). |
| `update-proxy/` | Rust/Pingora `tlgrm-update-proxy` — a *third* implementation of the same protocol, proxying GitHub Releases, plus webhook cache invalidation and static site serving. Tracked. | **Dead / not deployed** — its own README says "Source only — not deployed". | Superseded by `update-server/`. Actively misleading: its README opens "The **Tlgrm auto-update server**". Itself supersedes an older nginx+Lua variant on branch `aus-wip-6.9.6`. |
| `tools/` | `publish_update.py` (the release pipeline), three MCP test scripts, `site-preview/`. Tracked. | Current, but was invisible from the README until this audit. | See per-file table. |
| `tools/site-preview/` | Headless-Chromium 3D asset authoring for the site: `render-object.mjs`, `viewport.mjs`, `cdp.sh`, `preview.html`, `vault-sketch.html`, `node_modules/`. Tracked — **including 1690 files of `node_modules`**. | Niche tooling; the vendored `node_modules` is dead weight. | Serves `site/`. Referenced only from `update-proxy/README.md` "Related". |
| `site/` | 71grm.site landing page (`dist/`, `src/`, `models/`, README). **Entirely untracked**; `dist/assets/{fonts,img,models,textures}` are empty directories. | Half-built, undiscoverable, absent from a fresh clone. | Consumed by `update-proxy` (`SITE_ROOT`) — which is itself not deployed. |
| `pythonMCP/` | Python MCP server: AI/ML (FAISS, Whisper), Prometheus metrics, IPC bridge to the C++ server. Tracked. | Unmaintained — last commit 2026-02-20 while the C++ surface moved to 7.0.9. | **Not** a duplicate of the C++ MCP server; complementary by design (README: "working alongside the C++ server"). Carries two rival configs: `config.example.toml` and `config.toml.example`. |
| `docs/` | 19 documents. `UPDATE_SYSTEM.md` (Aug, authoritative), `FUTURE_FEATURES.md`, `FEATURES.md`, `BUILD_GUIDE.md`, `gradual_export_plan.md` are live. | Mixed. | The `BOT_*.md` set, `CLAUDE_NOTES.md`, `ARCHITECTURE_DECISION.md`, `NEXT_STEPS.md`, `MONETIZATION_STRATEGY.md`, `HYBRID_UI_IMPLEMENTATION.md`, `IMPLEMENTATION_SUMMARY.md`, `MIGRATION_HISTORY.md`, `QUICK_START.md` are all Nov 2025 and predate the 7.0.x port — stale. |
| `DesktopPrivate/` | Signing + identity headers: `packer_private.h` (RSA private keys, 4 PRIVATE KEY blocks), `alpha_private.h`, `custom_api_id.h`. | **Correctly protected.** `.gitignore:199 DesktopPrivate/` with `!DesktopPrivate/custom_api_id.h` at :207. `git ls-files` returns only `custom_api_id.h` (public tdesktop pair, ApiId 2040). Neither key file has ever been committed on any ref. | Without `packer_private.h` no update can ever be signed again — it exists only on this build machine and is in no backup path in the repo. |
| `dmg_build/` | DMG inputs (`dmg_background.png`, `README_EN/RU.txt`, `scripts/`, `initiate.pkg`) **plus 1.1 GB of output**: DMGs for 6.3.3, 6.5.0, 6.9.3, 6.9.4, 6.9.5, 6.9.6, 7.0.9 and `tarmacupd7000009`/`tmacupd7000009`. | Inputs current; outputs are dead weight. | Gitignored (`dmg_build/`), local only. Everything below 7.0.9 is superseded. |
| `Libraries/` | Built third-party dependencies. | Build cache. | Ignored (`Libraries/*/`, keeping only README.md). |
| `ThirdParty/` | Same pattern. | Build cache. | Ignored (`ThirdParty/*/`). |
| `.codegraph/` | 627 MB code-index cache: `codegraph.db` (625 MB), `-shm`, live `daemon.pid`/`daemon.sock`/`daemon.log`. | Regenerable cache, untracked, self-ignored. | Deleting costs one reindex. |
| `.claude/` | Claude Code session config. Ignored (`.claude/`). | Local. | — |

## Loose files at root

| Path | What it is | State |
|---|---|---|
| `create_dmg.sh` | Minimal DMG: app + background + Applications link, `create-dmg`, version read from Info.plist. Tracked, executable. | **CURRENT.** Named in `README.md:373` and in the user's standing instructions. |
| `create_beautiful_dmg.sh` | Superset of the above: 1024x680 window, README_EN/README_RU icons, **and it builds `initiate.pkg` from `~/tdata.zip`** — a live Telegram session — installing it into the recipient's home. Tracked, executable. | **Superseded and dangerous.** Not mentioned anywhere in README.md. Only difference of substance from `create_dmg.sh` is the session-injection pkg and the extra icons. |
| `DMG_README.md` | Describes the DMG layout. Tracked. | Documents the *beautiful* variant's layout, so it describes the superseded script. |
| `Makefile` | Build/convenience targets. Tracked. | Current. |
| `apply-patches.sh` | Applies `tdesktop/patches/` onto the submodule. Tracked. | Current, part of the upgrade workflow. |
| `README.md` | 60 KB primary entry point. Tracked, modified in working tree. | Current. Until this audit it named neither `tools/publish_update.py` nor `create_dmg.sh`; a fix landed at lines 372–374 mid-audit. Still silent on `cloudflare-worker/`, `update-proxy/`, `tools/mcp_*.py`, `site/`. |
| `ARCHITECTURE.md` | System architecture. Tracked, Nov 2025. | Predates the 7.0.x port — verify before trusting. |
| `BUILD_STANDARDS.md` | Build conventions. Tracked, Aug 2026. | Current. |
| `BUILD_BENCHMARKS.md` | Build timing numbers, Nov 2025. Tracked. | Historical. |
| `RELEASE_NOTES_7.0.7.md` | Release notes for 7.0.7. Tracked. | Stale — repo is at 7.0.9, no 7.0.8/7.0.9 equivalent exists. |
| `UPGRADE_6.9.6_7.0.7_PLAN.md` | Port plan, renamed from `..._7.0.6_PLAN.md`. Tracked. | Completed work — the port landed and the base advanced past it. Historical record. |
| `IMPOSSIBLE_TO_POSSIBLE_7.0.6.md` | Narrative of the 7.0.6 port. Tracked. | Historical, superseded by the 7.0.9 state. |
| `WHATS_NEW_7.0.6_MCP.md` | 35 KB of 7.0.6 MCP changes. Tracked. | Historical; two minor versions behind. |
| `DELETED_ACCOUNT_ARCHIVING.md` | Feature spec. **Untracked.** | Live feature (has its own memory entries) but absent from git — undiscoverable to anyone else. |
| `test_mcp.py` | Root MCP test. Tracked, executable. | **Dead.** Spawns `out/Release/Telegram.app/Contents/MacOS/Telegram` — wrong directory *and* wrong binary name post-rename. Superseded by `tools/mcp_smoke_test.py`. |
| `sweep_results.json` | 31 KB of `mcp_test_suite.py` output (`OK_OTHER`/`REFUSED` records). **Untracked.** | Generated garbage left at root. Nothing reads it. |
| `.env` | `TELEGRAM_API_ID/HASH`, `TELEGRAM_BOT_TOKEN`, `ALLOWED_CHATS/USERS`, `OPENAI_API_KEY`. **Untracked, mode 0600.** | Correctly ignored (`.gitignore:96`). Real secrets, safe. No consumer found outside `pythonMCP`. |
| `.gitignore` | 207 lines. Tracked. | Current; the `DesktopPrivate/` rule and its `custom_api_id.h` exception are both documented inline. |
| `.DS_Store` | Finder metadata. | Ignored. |

## The three MCP test scripts — coverage and overlap

| Script | Covers | Overlap |
|---|---|---|
| `tools/mcp_smoke_test.py` (4.8 KB) | Read-only tools called for real; mutating tools called only with arguments that must fail validation. | Narrowest. Subsumed by `mcp_test_suite.py` for the read half. |
| `tools/mcp_test_suite.py` (5.3 KB) | Every advertised tool called with no arguments, classifying LIES / CRASH / SLOW / SCHEMA. Skips ~50 destructive substrings. | Superset of the smoke test's read half; produces `sweep_results.json`. |
| `tools/mcp_fixture_test.py` (9.6 KB) | The destructive half the other two skip. Creates its own channel + supergroup, enforces scoping through `guard()`, deletes fixtures in a `finally`. | **No overlap** — this is the complement. All three together are one coherent suite; none is redundant. |

All three hardcode `SOCK, TOKEN = "/tmp/tlgrm_mcp.sock", "/tmp/auth_token"`, which
disagrees with `tools/publish_update.py`'s discovery via
`~/Library/Preferences/tlgrm/mcp_socket_path`.

## Where undocumented tooling should be documented

| Tool | Belongs in |
|---|---|
| `tools/publish_update.py` | `README.md` release section (landed mid-audit at :373) **and** `docs/UPDATE_SYSTEM.md` "Publishing a release" (already present there — the README was the gap). |
| `tools/mcp_smoke_test.py`, `mcp_test_suite.py`, `mcp_fixture_test.py` | `README.md` MCP section — a "Testing the MCP surface" subsection naming all three and what each covers. |
| `create_dmg.sh` vs `create_beautiful_dmg.sh` | `DMG_README.md`, which currently documents only the superseded variant. |
| `cloudflare-worker/` | `docs/UPDATE_SYSTEM.md` names it; `README.md` does not mention it at all. |
| `update-server/alpine/provision.sh` | `README.md` deployment section; today it is reachable only by opening `update-server/README.md` and following a relative link. |
| `site/` + `tools/site-preview/` | Nothing in `README.md` or `docs/` mentions either; only `update-proxy/README.md` (the dead component) links them. |

# Markdown documentation rot audit — agent w2

Repo: `/Users/pasha/xCode/tlgrm` (Tlgrm, macOS-only Telegram Desktop fork)
Audited: 2026-08 · Scope: every `.md` outside `ThirdParty/`, `Libraries/`, `cmake/external/`, `node_modules`, `.venv`, `.pytest_cache`.

## Ground truth established from code (not from docs)

| Fact | Value | Verified at |
|---|---|---|
| Version | **7.0.9** / `AppVersion 7000009` | `tdesktop/Telegram/build/version`, `tdesktop/Telegram/SourceFiles/core/version.h:25-26` |
| MCP tool count | **355** (348 `Tool{` literals, all unique names, + 7 `tool.name =` appends) | `tdesktop/Telegram/SourceFiles/mcp/mcp_tool_registry.cpp` |
| MCP IPC socket | **`/tmp/tlgrm_mcp.sock`** | `core/application.cpp:492`, `mcp/mcp_bridge.cpp:53` |
| `--mcp` flag | Only selects the **stdio** transport; the IPC bridge is always on | `core/application.cpp:444-447` |
| Architectures | **`x86_64;arm64` universal** | `tdesktop/cmake/validate_special_target.cmake:26` |
| HTTP update path | **`/current`** (no generation suffix) | `core/update_checker.cpp:719,727` |
| HTTP update host | **`https://updates.71grm.site`**, served by Rust `update-server/` on ironforge.local | `storage/localstorage.cpp:557` |
| MTProto feed | **`updates71grm`** | `core/update_checker.cpp:1010` |

## Verdicts

Legend: **CRITICAL** = following the doc causes a wrong action · **STALE** = materially misleading or outdated · **CLEAN** = no contradiction found · *(upstream)* = vendored Telegram Desktop doc, stale only relative to this fork's macOS-only scope.

### Root

| File | Verdict | Note |
|---|---|---|
| `README.md` | **CRITICAL** | "339 tools" ×7 (:15,:87,:113,:1060,:1062,:1596,:1743,:1753) — real count 355. Version/base "7.0.7" (:59,:1749,:1750) — real 7.0.9. Socket `/tmp/tlgrm_mcp.sock` and the `-destination` build note are correct. |
| `BUILD_STANDARDS.md` | **CRITICAL** | :60 points at `build_with_extracted_api.sh`, which does not exist. The universal-binary rule itself is correct and matches cmake. |
| `UPGRADE_6.9.6_7.0.7_PLAN.md` | **CRITICAL** | :113 claims feed `tlgrmfeed4` + path `/current4` — both wrong (`/current`, `updates71grm`). Title/body say "→ 7.0.6" while filename says 7.0.7 and tree is 7.0.9. :50 cites a "339-entry QHash". |
| `ARCHITECTURE.md` | **CRITICAL** | Socket documented as `/tmp/telegram_mcp.sock` at :105, :263, :424, :480 — never existed; real path is `/tmp/tlgrm_mcp.sock`. |
| `RELEASE_NOTES_7.0.7.md` | **STALE** | Accurate for the 7.0.7 release it describes (update channel, universal-binary fix, `@updates71grm`, `updates.71grm.site` all correct); superseded by 7.0.9 with no successor notes. |
| `DMG_README.md` | **STALE** | :51, :79 frame `--mcp` as required to get MCP; the IPC bridge is unconditional. Hardware line (:65) is correct now that the build is universal. |
| `WHATS_NEW_7.0.6_MCP.md` | **STALE** | Historical port-analysis doc frozen at 7.0.6 vs a 6.9.6 fork; still self-labelled as the current state. Value is archival. |
| `IMPOSSIBLE_TO_POSSIBLE_7.0.6.md` | **STALE** | Same vintage; references the retired worktree `/Users/pasha/xCode/tlgrm-fork-6.9.6`. |
| `BUILD_BENCHMARKS.md` | **STALE** | Pre-7.x timings; :38 carries a "msys (Windows only)" row in a macOS-only project. |
| `DELETED_ACCOUNT_ARCHIVING.md` | **CLEAN** | No version/arch/socket/URL claims that the code contradicts. |

### `docs/`

| File | Verdict | Note |
|---|---|---|
| `docs/NEXT_STEPS.md` | **CRITICAL** | :95 `-DCMAKE_OSX_ARCHITECTURES=arm64` forces a single-arch build, violating `BUILD_STANDARDS.md`. :223 documents a Linux data path. |
| `docs/BUILD_GUIDE.md` | **CRITICAL** | :117-120 `xcodebuild -scheme` with no `-destination` ⇒ silent arm64-only. :110-114 hand-rolls `cmake -G Xcode ..`, bypassing `Telegram/configure.sh` and the API credentials. |
| `docs/CLAUDE_NOTES.md` | **CRITICAL** | :327 same `-destination`-less xcodebuild. :72-76 describes MCP as gated on `--mcp`, which is no longer true. |
| `docs/MIGRATION_HISTORY.md` | **CRITICAL** | :104, :174, :201 socket `/tmp/telegram_mcp.sock`; :309 `-destination`-less build. |
| `docs/UPDATE_SYSTEM.md` | **STALE** | Protocol description is accurate. Every example and the "verified end to end" record are pinned at 7000007 (:29, :56, :132) vs 7000009; :40-43 still offers the Cloudflare Worker as a live alternative origin. |
| `docs/FEATURES.md` | **STALE** | Advertises "MCP Tools Reference (45+ Tools)" (:11) against 355. |
| `docs/README.md` | **STALE** | Bot-framework index claiming "✅ COMPLETE … Ready for compilation, 95% build confidence" — a status snapshot from before the framework shipped. |
| `docs/QUICK_START.md` | **STALE** | Bot-framework fast track; build section inherits the pre-universal build flow. |
| `docs/IMPLEMENTATION_SUMMARY.md` | **STALE** | :172 launches with `--mcp` as the way to get MCP. |
| `docs/ARCHITECTURE_DECISION.md` | **CLEAN** | Decision record; time-stamped rationale, no contradicted claims. |
| `docs/BOT_ARCHITECTURE.md` | **CLEAN** | Design doc, no version/path claims. |
| `docs/BOT_FRAMEWORK_STATUS.md` | **STALE** | Status snapshot, superseded by shipped code. |
| `docs/BOT_USE_CASES.md` | **CLEAN** | Scenario catalogue, no environment claims. |
| `docs/HYBRID_UI_IMPLEMENTATION.md` | **CLEAN** | |
| `docs/MONETIZATION_STRATEGY.md` | **CLEAN** | Business doc, out of technical scope. |
| `docs/FUTURE_FEATURES.md` | **CLEAN** | Correctly uses `/tmp/tlgrm_mcp.sock` (:74, :291). |
| `docs/gradual_export_plan.md` | **CLEAN** | Matches the single gradual-export system in `export_api_wrap.cpp`. |

### `update-server/`, `update-proxy/`, `cloudflare-worker/`, `site/`, `tools/`

| File | Verdict | Note |
|---|---|---|
| `update-server/README.md` | **STALE** | Endpoint/env docs correct. :118-127 presents `cloudflare-worker/` as a co-equal, still-choosable origin; it was superseded (it had been bound to `updates.tlgrm.app`, a domain not owned — see `RELEASE_NOTES_7.0.7.md:147`). |
| `update-proxy/README.md` | **CRITICAL** | Opens "**The Tlgrm auto-update server**" — but `update-server/` is what actually serves `updates.71grm.site`. Two directories claim the same role; a reader deploys the wrong one. Its endpoint table (:13) also lists `/current` twice alongside `/current2`. |
| `update-server/alpine/README.md` | **CLEAN** | Host-provisioning notes; cloudflared usage here is the tunnel, not the Worker. |
| `site/README.md` | **CLEAN** | Unusually honest — documents its own `dist/`↔`src/` drift. Ties itself to `../update-proxy` (`SITE_ENABLED`), which inherits the update-proxy/update-server ambiguity above. |
| `tools/site-preview/README.md` | **CLEAN** | Accurately describes the relocation out of `update-proxy/`. |
| `tools/site-preview/.claude/skills/viewport.md` | **CLEAN** | |

### `pythonMCP/`

| File | Verdict | Note |
|---|---|---|
| `pythonMCP/README.md` | **CRITICAL** | :143, :165, :586 configure `IPC_SOCKET_PATH=/tmp/telegram_mcp.sock`. Real path `/tmp/tlgrm_mcp.sock`; `pythonMCP/src/ipc_bridge.py:36` independently defaults to `/tmp/tdesktop_mcp.sock`. Three paths, none matching the client. |
| `pythonMCP/IMPLEMENTATION_SUMMARY.md` | **STALE** | Point-in-time snapshot. |
| `pythonMCP/GAP_ANALYSIS.md` | **STALE** | Gap list predates the current tool surface. |
| `pythonMCP/FORMATTING_UPGRADE.md` | **CLEAN** | Untracked working doc, self-contained. |
| `pythonMCP/VALIDATION_USAGE.md` | **CLEAN** | |
| `pythonMCP/VALIDATION_QUICK_REFERENCE.md` | **CLEAN** | |
| `pythonMCP/PROMETHEUS_IMPLEMENTATION.md` | **CLEAN** | |
| `pythonMCP/METRICS_REFERENCE.md` | **CLEAN** | |
| `pythonMCP/docs/cache.md` | **CLEAN** | |
| `pythonMCP/docs/monitoring.md` | **CLEAN** | |

### `tdesktop/` (submodule — fork + vendored upstream)

| File | Verdict | Note |
|---|---|---|
| `tdesktop/AGENTS.md` | **CRITICAL** | 750-line agent contract written for "Codex on Windows + WSL" (:5), Linux Docker builds (:52), Windows CRLF normalization (:166). Wrong platform end to end for a macOS-only fork. The `Q_OS_LINUX` / `Platform::IsLinux()` guidance (:296-322) is correct upstream lore and worth keeping. |
| `tdesktop/CLAUDE.md` | **CLEAN** | 3 lines. |
| `tdesktop/REVIEW.md` | **CLEAN** | Code-review lore; the platform-split section (:559-573) is accurate. |
| `tdesktop/README.md` | **STALE** *(upstream)* | Upstream's own README — Windows/Linux download links, upstream CI badges. Harmless as a vendored file, misleading if read as this fork's README. |
| `tdesktop/.github/CONTRIBUTING.md` | **CLEAN** *(upstream)* | |
| `tdesktop/docs/building-mac.md` | **CLEAN** *(upstream)* | |
| `tdesktop/docs/building-mas.md` | **CLEAN** *(upstream)* | Mac App Store path, unused here (no sandboxing in this project). |
| `tdesktop/docs/building-win.md` | **STALE** *(upstream)* | Windows build, unsupported here. |
| `tdesktop/docs/building-linux.md` | **STALE** *(upstream)* | Linux/Docker build, unsupported here. |
| `tdesktop/docs/api_credentials.md` | **CLEAN** *(upstream)* | |
| `tdesktop/Telegram/lib_spellcheck/README.md` | **CLEAN** | Empty (0 lines). |
| `tdesktop/Telegram/SourceFiles/mcp/REFACTORED.md` | **CLEAN** | Refactor log; no count/version claims that conflict. |
| `tdesktop/.claude/ai-workflow-adapter.md` | **CLEAN** | |
| `tdesktop/.claude/commands/continue.md` | **CLEAN** | |
| `tdesktop/.claude/commands/reflect.md` | **CLEAN** | |
| `tdesktop/.claude/commands/release.md` | **CLEAN** | |
| `tdesktop/.claude/commands/icon.md` | **CLEAN** | |
| `tdesktop/.claude/commands/process-inbox.md` | **CLEAN** | |
| `tdesktop/.claude/commands/perform-task.md` | **CLEAN** | |
| `tdesktop/.agents/shared/build-lock-recovery.md` | **STALE** | Titled "Autonomous **Windows** Build-Lock Recovery" — no applicable scenario on macOS. |
| `tdesktop/.agents/shared/test-loop.md` | **STALE** | Windows file-lock and path-scoped-kill branches (:109, :204, :473, :504) are dead weight here. |
| `tdesktop/.agents/skills/perform-task/SKILL.md` | **STALE** | Mandates a Windows normalization phase (:102, :125). |
| `tdesktop/.agents/skills/perform-task/references/pipeline.md` | **STALE** | Build drivers are native-Windows CMake and WSL Docker (:54-55, :70); neither applies. |
| `tdesktop/.agents/skills/perform-task/references/phase-prompts.md` | **STALE** | Phase 7 is "Native-Windows Text Normalization" (:1022). |
| `tdesktop/.agents/skills/perform-task/references/computer-use-testing.md` | **STALE** | "Computer Use runs in the foreground on **Windows**" (:77). |
| `tdesktop/.agents/skills/process-inbox/SKILL.md` | **STALE** | Windows/BOM line-ending rules (:229). |
| `tdesktop/.agents/skills/continue/SKILL.md` | **CLEAN** | |

## Cross-cutting patterns

1. **Three different MCP socket paths in the tree.** Client: `/tmp/tlgrm_mcp.sock`. Docs: `/tmp/telegram_mcp.sock`. Python bridge code: `/tmp/tdesktop_mcp.sock`. Only the first is real; the Python client cannot connect as shipped.
2. **The universal-binary rule is documented once and contradicted three times.** `BUILD_STANDARDS.md` states it correctly and `README.md` repeats it correctly, but `docs/BUILD_GUIDE.md`, `docs/CLAUDE_NOTES.md`, `docs/MIGRATION_HISTORY.md` all print the exact command the standard warns against, and `docs/NEXT_STEPS.md` hardcodes `arm64`.
3. **The tool count is a moving number nobody re-greps.** 45+ → 326 → 339 → 355 across docs; no doc currently states 355.
4. **The Windows/WSL agent corpus under `tdesktop/.agents/` and `tdesktop/AGENTS.md`** (~3,500 lines) targets a platform this fork does not build for. It is the single largest block of inapplicable instruction in the repo, and unlike the vendored upstream `docs/building-*.md`, it is *addressed to agents* and will be acted upon.
5. **`update-proxy/` vs `update-server/`** — two Rust crates, both introduced as the auto-update server, one of them not the one in production.

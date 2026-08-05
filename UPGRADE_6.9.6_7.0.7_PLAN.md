# Tlgrm Upgrade Plan — 6.9.6 → 7.0.6

**Milestone:** [Upgrade 6.9.6 → 7.0.6](https://github.com/CelestialTech/tlgrm/milestone/1) (#1)
**From:** Tlgrm 6.9.6 (`AppVersion 6009006`), forked from Telegram Desktop **v6.5.1** (`6b0fc1bef6`), MTProto **layer 222**
**To:** Telegram Desktop **v7.0.6** (`fccb2672b0`), MTProto **layer 228**
**Started:** 2026-07-29 · **Last updated:** 2026-07-31

Companion documents: `WHATS_NEW_7.0.6_MCP.md` (new capabilities + MCP design), `IMPOSSIBLE_TO_POSSIBLE_7.0.6.md` (the 18 impossible-verdict tools).

---

## 0. Corrections to the record

Recorded here because several were load-bearing and repeated downstream before being caught:

| Claim | Reality |
|---|---|
| Fork base is 6.3 / `aadc81279a` (per old `CLAUDE.md`) | **v6.5.1 / `6b0fc1bef6`** |
| Layer delta 223 → 229 | **222 → 228** |
| `channels.editCreator` was removed | **Moved** to `messages.editChatCreator`. Ownership transfer IS implementable. |
| Message effects are new in 228 | Already present at **222** — usable today, no upgrade needed |
| `test_uninstall.cpp` is the only new upstream test | That file **does not exist** at 7.0.6 |
| Fork is at api.tl parity with 7.0.6 | False — artifact of a mid-analysis branch switch |
| Markdown route makes rich messages trivial via `InputRichFile.id` | `inputRichMessageMarkdown`/`HTML`/`InputRichFile` are **never constructed anywhere** in tdesktop. Use `Iv::SerializeInputRichMessage` instead. |

The old fork `CLAUDE.md` is additionally wrong on `-fno-exceptions` (not set anywhere; `bot_manager.cpp` uses 16 `try`/`catch`), on nested `QJsonObject` initializer lists (the registry uses 550 of them), on `--mcp` gating (MCP always starts; the flag only picks stdio over IPC), and on every tool count. **Do not restore it.** Upstream now ships `AGENTS.md` + `.agents/skills/`; follow those conventions and re-derive our notes from code.

---

## 1. Scale

| | |
|---|---|
| Upstream delta | 2,854 commits · 1,980 files · **+284,741 / −34,654** · 425 new files |
| TL delta | 2,295 → 2,449 names: **156 new, 2 moved, 68 modified** |
| New TL namespaces | `communities.*` (9), `ephemeral.*` (4), `aicompose.*` (7) |
| Largest new subsystem | `iv/` — 13 → 105 files, **+87,144 lines (~30% of all new code)** |
| Fork delta to port | 80 commits · 65 `mcp/` files · ~35 invasive upstream touchpoints |

---

## 2. Strategy

**Rebase, don't merge.** We moved to a clean upstream v7.0.6 base and re-apply the fork on top, file by file, in risk order. This is why several fork defects vanished for free — the `.gitmodules` / `prepare.py` SSH-rewrite damage (~71 sites, which broke clean-machine builds) simply did not come along.

**Port in dependency order:** build system → MCP tree → export → branding → auto-update → tools.

**Delete as part of porting.** Where upstream absorbed a feature or the fork's code was never reachable, the correct port is removal (`ponytail` rung 2). Confirmed dead and *not* ported: `mcp/message_types.h`, `mcp/schema.sql`, `mcp/sql/`, `mcp/mcp_integration.txt`, `export_view_operations_log.{cpp,h}` (246 lines, never in any build), the bot-framework UI trio (`settings_bots.*`, `bot_config_box.*`, `bot_statistics_widget.*` — 725 lines, only reference was a dead `#include`), `Tlgrm.icns` (unreferenced), both `*.backup/` icon dirs, `verify_datas_build.sh` (fails by construction now), and `patches/` + `apply-patches.sh`.

**`patches/` must be deleted, not used.** `apply-patches.sh:116-134` does `rm -rf Telegram/SourceFiles/mcp` and replaces 69 live files with 6 obsolete ones frozen at Telegram 6.3 (24-branch if/else dispatch, zero tool registrations, vs the live 339-entry QHash). Both patches already fail `git apply --check`. The authoritative record is the `6b0fc1be..HEAD` diff.

---

## 3. Status

### Done

| Item | Detail |
|---|---|
| Base branch | `upgrade-v7.0.6` @ `fccb2672b0` |
| Submodules | all 15 synced, incl. 4 new: `MicroTeX`, `TooManyCooks`, `cmark-gfm`, `lib_translate` |
| Work preserved | `aus-wip-6.9.6` @ `4a2beeb669` (the previously-uncommitted AUS work) |
| Fork worktree | `/Users/pasha/xCode/tlgrm-fork-6.9.6` for side-by-side reference |
| CodeGraph | fork, fork-root, and upstream-7.0.6 indexes; `tdesktop` re-indexed post-switch |
| MCP tree | 65 files ported (24 h / 41 cpp), 4 dead artifacts excluded |
| `messages.summarizeText` | layer-228 `tone:flags.2?string` — 5th arg added |
| `Qt::Sql` | `find_package` in the fork's **own** CMakeLists + link entry |
| CMake source list | 65 `mcp/` entries, guarded by existence + duplicate checks |
| Host hooks | `application.h`, `application.cpp` (4 hunks), `launcher.cpp`, `sandbox.cpp` |
| Export | 17 of 22 files — 15 zero-drift cherry-picks (drift-guarded), `MessagesSlice::serverCount`, `export_output_html.cpp` (3 hunks) |
| Toolchain | installed missing `automake`, `autoconf`, `wget`, `meson`, `nasm` |

**Qt::Sql design decision.** The fork carried a forked `cmake` submodule solely to add `Sql` to `find_package(Qt6 COMPONENTS …)`. That commit's target file still exists upstream, so it *could* be replayed — but instead we call `find_package(Qt${QT_VERSION_MAJOR} COMPONENTS Sql REQUIRED)` in the fork's own `Telegram/CMakeLists.txt`. Same effect, and it **permanently retires a vendored submodule fork** that would otherwise need re-rebasing at every future upgrade.

**Deliberate divergence — `gManyInstance`.** The fork set it unconditionally, which disabled `tg://` handoff to a running instance for *every* launch and allowed two processes to open one `tdata` concurrently (a plausible corruption path). Ported **gated behind `--mcp`**, with an in-code comment stating the divergence and how to revert.

### In flight

| Owner | Work |
|---|---|
| `wheif` | libheif x86_64 link failure — **blocks all compilation** |
| `wapi` | `export_api_wrap.{h,cpp}` re-implementation |
| `wset` | `export_view_settings.cpp` |
| `wbrand` | branding + the `AppIcon`→`Icon` build-chain break |
| `waus` | AUS port + `update_checker.cpp:1647` compile error |

### Not started

Configure + first build · the 333-tool program · rich-message tooling · test coverage.

---

## 4. Build requirements new in 7.0.6

| Requirement | Note |
|---|---|
| **Qt 6.2.13 → 6.11.1** (macOS) | Nine minor versions. **The dominant risk** — only a build surfaces the fallout. Verified in `build/qt_version.py`; the `6.10.2` figure elsewhere is the Windows path. |
| **Swift 6 toolchain** | New, for `lib_translate` on-device translation. Escape hatch `DESKTOP_APP_DISABLE_SWIFT6` — but disabling it removes the macOS system translation provider, which our translation tools must then report honestly. |
| **`qsb`** (Qt Shader Baker) | Hard `FATAL_ERROR` if missing — `Telegram/shaders/` now exists. |
| **python3 at build time** | `generate_models.cmake`, `.obj`→`.binobj`. A stray project venv on PATH was masking the real interpreter; strip it. |
| autotools | `autoreconf` missing caused a 3/26 failure. Installed. |
| Codesigning | Swift stdlib copy adds nested dylibs in `Contents/Frameworks` → bottom-up signing required. |

---

## 5. Risk register

| # | Risk | Severity | Mitigation |
|---|---|---|---|
| 1 | **`AppVersion` vs tdata version gate** | **Critical — silent session loss** | `storage_file_utilities.cpp:537` rejects any tdata file whose embedded version exceeds `AppVersion`; a rejected `key_data` regenerates the local key and **destroys the session**. On the 7.0.6 base `AppVersion` is already `7000006` — **never lower it**, and keep `Telegram/build/version` in lockstep. Migration is one-way. |
| 2 | Qt 6.2→6.11 fallout across ~40k lines of Qt-heavy fork code | High | Only a build reveals it. Budget real time here. |
| 3 | Bundle-name ↔ updater-path coupling | High — silent broken updates | `output_name` (CMake), `updater_osx.m`, and `update_checker.cpp`'s `tupdates/temp/Tlgrm.app/…` must agree, or an update downloads, verifies, and does nothing. |
| 4 | Update feed/endpoint naming | High | `AutoUpdateVersion()==4` ⇒ channel `tlgrmfeed4`, path `{prefix}/current4`. A mismatch means a permanent silent no-check. |
| 5 | `export_api_wrap.cpp` re-implementation | High | Upstream refactored 1,210 lines; `loadMessagesFiles` split, `processFileLoad` doubled, `loadMessageFileDone` re-signed. A verbatim re-apply **silently disables resume for message attachments**. |
| 6 | 7.0.6 tightened assertions in the export file path | High | New `Expects` in `loadNextMessageFile`/`loadMessageFileDone` can be violated across the fork's pacing-timer gap. Must be re-audited, not assumed. |
| 7 | `main_account.cpp` userId inference | High | Fork overrides the authoritative mtp-auth userId and mutates on-disk state. Re-verify call order before re-applying. |
| 8 | macOS About menu has no target file | Medium | Menu construction moved to a new `global_menu_mac.mm`; upstream also switched to a hardcoded literal. Easy to silently miss. |
| 9 | App icon asset renamed `AppIcon`→`Icon` | Medium | Breaks the documented post-build `iconutil` step. |
| 10 | `Export::Settings` field order = TDF binary layout | Medium | `storage_account.cpp` streams new fields behind version guards with a hand-computed size; reorder and the settings file corrupts. |
| 11 | Upstream `CTRL:`/`DATA:` verb on the single-instance socket | Low | Design overlap with our MCP IPC bridge; different socket, so no hard collision. |

---

## 6. The MCP surface program

**Current state: 333 advertised tools — 147 real (44%), 186 stubs (56%).** Stubs return `success: true` over locally-fabricated SQLite that nothing syncs from Telegram. `grep -cE 'TODO|FIXME|XXX|HACK'` across `mcp/*.cpp` returns **zero** — the fiction is unmarked.

**Root cause: no mechanism makes the gap visible.** A tool is declared in three hand-synced places (header, `registerTools()` metadata, `_toolHandlers` map) with no compile-time link. That is exactly how one duplicate registration, five silently-overwritten handlers, and one callable-but-unadvertised tool got in.

### The fix (ponytail gate: rung 6 held — a table and one function, not a new module)

1. **One `constexpr` table** carrying name, schema, handler, and a **truthfulness field** — `Backing::Mtproto` / `LocalOnly` / `Unimplemented`.
2. **`Unimplemented` returns an error, never `success: true`.** This single rule prevents the 186-stub outcome recurring.
3. **`LocalOnly` is named as such** in the tool name and description.
4. **Debug assert** on table-name uniqueness and handler coverage.
5. **New defect class to sweep:** counter columns that are `SELECT`ed but never `UPDATE`d — always `0`, always `success`. At least three exist. Assert every counter column has a writer.

### Triage (216 of 333 verdicted)

| Family | Verdicted | ALREADY_REAL | IMPLEMENTABLE | PARTIAL | IMPOSSIBLE | DELETE |
|---|---|---|---|---|---|---|
| settings | 118 | 18 | 32 | 13 | 9 | 46 |
| stars | 45 | 1 | 23 | 6 | 7 | 8 |
| business + premium | 53 | 9 | 18 | 26 | 4 | 15 |
| wallet + residual | 9 of 39 | 2 | 2 | 7 | 0 | 0 |

Remaining: 23 wallet tools. *(Worker summaries disagreed with their own tables in two cases; the tables are authoritative and were re-tallied.)*

**Of 18 IMPOSSIBLE verdicts, only 5 are genuinely impossible** — and not because an endpoint is missing, but because **the protocol places authority elsewhere**: auctions are Telegram-run (every term server-set), paid-reaction count belongs to the sender, paid-media price is write-once at creation. The other 13 are recoverable through four patterns: local time-series over real server snapshots, a client-side watcher/differ, honest `LocalOnly` labelling, and simply writing a counter we already own. Build two small pieces of shared infrastructure — a scheduled snapshot store and a watcher/differ — and eight tools become implementable at once.

### Highest-value new work

`send_rich_message` / `edit_rich_message`. Rich messages are sendable, editable, draftable content, and `messages.editMessage#b106e66c` carries `rich_message`, so **send-then-progressively-edit yields a native streaming-reveal animation**. Authoring goes through `Iv::SerializeInputRichMessage` (21 emittable block kinds; 6 kinds hard-fail the whole message; validate before submit). Pre-check `RichMessageLimits` from appConfig and the two premium gates — `rich_message_posting` defaults to `"disabled"`, and non-flatten-safe pages are refused outright.

---

## 7. Test debt

The fork's most intricate feature has **zero tests for resume**. No test writes a partial export and restarts it; `resume_state.json` round-trip, `MTPInputPeer` reconstruction, progress seeding, directory reuse, and the `.dl_` scheme are all unverified. There are no unit tests on the pure functions (`SanitizeForFilesystem`, `NormalizePath`, the resume serializer, the pacing thresholds), and the existing suite is non-hermetic — it needs a live session and a specific channel.

**Before trusting the re-implemented `export_api_wrap.cpp`:** extract `SanitizeForFilesystem`, `NormalizePath`, and the `resume_state.json` (de)serializer into unit-testable form and pin them against 6.5.1 behaviour first, so the 7.0.6 re-implementation has a fixed contract to satisfy.

Upstream is no better here: the ~120k-line markdown/rich-editor engine ships with no dedicated test coverage.

---

## 8. Sequence

1. **Unblock the build** — libheif x86_64 link failure. *(in flight)*
2. Complete the dependency tree through Qt 6.11.1.
3. Finish export: `export_api_wrap.{h,cpp}`, `export_view_settings.cpp`. *(in flight)*
4. Branding + AUS. *(in flight)*
5. **First configure + build.** Expect Qt fallout; iterate.
6. Verify session loading with real `tdata` — risk #1 is silent, so test it explicitly.
7. Verify the MCP bridge starts and `tools/list` responds.
8. Land the tool-table refactor (§6) **before** touching individual tools.
9. Execute the 333-tool program: implement, relabel, or delete — no stubs returning success.
10. Add rich-message tooling.
11. Sign + notarize (bottom-up, now including the Swift stdlib dylibs).
12. Re-run `complexity-audit` on the rebase diff; `adversarial-review` before release.

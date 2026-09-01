# Tlgrm Upgrade Plan — 7.0.9 → 7.1.3

**From:** Tlgrm 7.0.9a (`AppVersion 700000901`), forked from Telegram Desktop **v7.0.9** (`a1e89e1f64`, 2026-08-05), MTProto **layer 228**
**To:** Telegram Desktop **v7.1.3** (`956e93e386`), MTProto **layer 229**
**Planned:** 2026-09-01 · base fetched into `tdesktop` from `upstream` (telegramdesktop/tdesktop)

Follows the methodology of the prior port [`UPGRADE_6.9.6_7.0.7_PLAN.md`](UPGRADE_6.9.6_7.0.7_PLAN.md)
(v6.5.1→v7.0.6, layer 222→228). Every number below is measured from the two
tags in the live `tdesktop` repo, not estimated.

---

## 0. Corrections to the record

| Assumption | Reality (measured 2026-09-01) |
|---|---|
| "146 → 150" (as recalled) | The prior port was **6.9.6 → 7.0.6**, base layer **222 → 228**. This follow-on is **7.0.9 → 7.1.3**, layer **228 → 229**. No 146/150 artifact exists in the tree; the phrase is shorthand for "the incremental upstream port we ran before." |
| A minor bump means a small upstream diff | The version bump is minor but the diff is **886 commits / 910 files / +81,359 −13,571**. The bump is small *for the fork* because almost none of it collides — not because upstream did little. |
| We are behind by one release | Behind by **four**: v7.1.0, v7.1.1, v7.1.2, v7.1.3. |
| The scheme must be re-derived | The fork already carries at least one layer-229 constructor (`3e003fb090` — `user.linked_community_id from layer 229`). Guard against **double-application**, don't re-add it. |

---

## 1. Scale

| | |
|---|---|
| Upstream delta (v7.0.9→v7.1.3) | **886 commits · 910 files · +81,359 / −13,571** |
| TL delta | api.tl **3087 → 3117 lines** (+30), layer **228 → 229** — a handful of new constructors, near-parity |
| New top-level subsystems | **none** (`Telegram/SourceFiles/` gained no new directory — unlike the prior port's +87k-line `iv/`) |
| Qt toolchain | **unchanged** — pin identical at both tags in `Telegram/build/prepare/prepare.py` |
| Submodule changes | **one, cosmetic** — `cmark-gfm` url `github/…` → `desktop-app/…` (mirror), no new modules |
| Fork surface to re-apply | **79 non-`mcp/` edited files** + the additive `mcp/` tree; **14 both-touched**, of which a real 3-way merge shows only **3 textually conflict** |
| Merge simulation (`git merge-tree`, in-memory) | **3 real conflicts** — `build/version`, `core/version.h`, `core/application.cpp`; the other **11 auto-merge textually** (semantic audit still required) |

**Read:** the two risks that dominated the last port — the nine-minor **Qt 6.2→6.11**
jump and a giant new subsystem — are **both absent here**. This is a
reconciliation of 14 files, not a re-platforming.

---

## 2. Strategy

**Rebase onto v7.1.3, don't merge.** Move `tlgrm-desktop` to a clean v7.1.3 base
and re-apply the fork on top, file by file, in risk order. Same as the prior port
— it is why fork defects (SSH-rewritten `.gitmodules`, dead `patches/`) did not
survive last time and must not be reintroduced.

**Dependency order:** build system → `mcp/` tree (additive) → export graft →
branding → auto-update → version stamp.

**Delete as part of porting** (`ponytail` rung 2): re-apply nothing upstream
absorbed or the fork left dead. Re-confirm the prior port's kill-list is still
dead at this base before assuming it.

**The additive `mcp/` tree ports for free.** It is a new directory upstream never
touches; it moves verbatim. The only build coupling is the source list in
`Telegram/CMakeLists.txt` (a conflict file — §3) and the `find_package(... Sql)`
line the fork already owns in its own CMakeLists.

---

## 3. The conflict surface — 14 files, ranked

The only files both upstream (7.0.9→7.1.3) and the fork (7.0.9→HEAD) changed.
Everything else in the fork re-applies without reconciliation. Upstream churn is
`+added/−deleted` at the two tags.

**Measured, not assumed:** an in-memory 3-way merge of the fork against v7.1.3
(`git merge-tree --write-tree HEAD v7.1.3`, base v7.0.9 — writes nothing) shows
that **only 3 of these 14 textually conflict** — `build/version`,
`core/version.h`, and `core/application.cpp`. The other **11 auto-merge**
(marked ⋯ below). Caveat: a clean text-merge is **not** a correctness guarantee
— git resolves non-overlapping hunks even when they are semantically
incompatible, so the ⋯ files (above all `export_output_html.cpp`'s +463 rewrite
and the export cluster) still need a **semantic** audit, just not manual
conflict-marker surgery.

| # | File | Upstream churn | Fork feature here | Action | Risk |
|---|---|---|---|---|---|
| 1 ⋯ | `export/output/export_output_html.cpp` | **+463 / −13** | gradual-export HTML writer | **Auto-merges** (hooks and upstream's rewrite touch different regions). No conflict surgery — but the +463 rewrite demands a **semantic render-diff** to confirm the gradual-export hooks still fire. | Med (semantic) |
| 2 ⚠ | `core/application.cpp` | +108 / −5 | MCP bridge start (`:492`) + host hooks | **Real conflict.** Re-apply the host hooks onto new init flow by hand; verify `_mcpBridge->start()` still lands after session init. | **High** |
| 3 | `window/main_window.cpp` | +85 / −10 | fork window hooks | Re-apply; inspect for moved construction. | Medium |
| 4 | `export/data/export_data_types.h` | +81 / −1 | fork export types | Mostly additive upstream → likely append-compatible; check field order (TDF layout, risk #10 prior). | Medium |
| 5 | `Telegram/CMakeLists.txt` | +79 / −1 | `mcp/` sources + `Qt::Sql` | Mechanical: re-add the guarded `mcp/` source list + Sql find_package. | Low (mechanical) |
| 6 | `build/prepare/prepare.py` | +12 / −36 | fork build prep | Re-apply fork hunks; **do not** reintroduce SSH url-rewrites. | Low |
| 7 | `core/application.h` | +18 / −2 | `_mcpBridge` member + accessor | Re-declare the member/hook. | Low |
| 8 | `iv/iv_instance.h` | +20 / 0 | rich-message touchpoint | Re-apply if the fork's IV hook is still used; else drop. | Low |
| 9 | `core/sandbox.cpp` | +8 / 0 | single-instance / `--mcp` gating | Re-apply the `gManyInstance`-behind-`--mcp` divergence (documented in code). | Low |
| 10 | `export/export_api_wrap.cpp` | **+9 / −3** | **the gradual-export engine** | Small upstream drift — the fork's core export logic largely survives. **Was High(#5) last port; now Low.** Still re-audit the resume path. | Low |
| 11 | `export/view/export_view_panel_controller.cpp` | +3 / 0 | export panel | Trivial re-apply. | Low |
| 12 | `storage/localstorage.cpp` | +1 / −1 | fork storage hook | One-line re-apply. | Low |
| 13 ⚠ | `Telegram/build/version` | +5 / −5 | version stamp | **Real conflict — and that's good:** it forces the AppVersion decision instead of silently taking one side. Resolve per risk #1. | Trivial edit / **Critical if wrong** |
| 14 ⚠ | `core/version.h` | +2 / −2 | version constants | **Real conflict.** Keep in lockstep with #13. | Trivial edit / **Critical if wrong** |

*(⚠ = one of the 3 real textual conflicts; ⋯ = auto-merges, semantic audit only. Rows without a mark auto-merge and are low-risk.)*

The prior port's companion audit `docs/audit-2026-08-11/fork-divergence.md`
covers the same divergence at the 7.0.6 base; the table above supersedes it for
this delta (computed from HEAD, 2026-09-01).

---

## 4. Build requirements new in 7.1.x

Near-empty — the good news of a same-toolchain minor bump.

| Requirement | Note |
|---|---|
| Qt | **No change.** Identical pin at v7.0.9 and v7.1.3. The dominant risk of the prior port does not recur. |
| Submodules | Only `cmark-gfm`'s url moved to the `desktop-app` mirror; no new module to init. Re-point and `submodule sync`. |
| New subsystem deps | None — no new top-level source directory. |
| Everything else | The 7.0.6-era requirements (qsb, python3 at build, Swift 6 for `lib_translate`, autotools) already hold on the 7.0.9 base; nothing new to install. |

---

## 5. Risk register

| # | Risk | Severity | Mitigation |
|---|---|---|---|
| 1 | **AppVersion / tdata version gate** | **Critical — silent session loss** | The fork carries an inflated `AppVersion 700000901` (7.0.9a); upstream v7.1.3 is `7001003`. The tdata gate rejects any data file whose embedded version exceeds the running `AppVersion` and **regenerates the local key, destroying the session**. Re-stamp `build/version` + `core/version.h` in the **fork's** numbering (increment past 700000901), **never** adopt upstream's smaller 7001003. Migration is one-way — verify by loading a real `tdata` after the first build. |
| 2 | **`application.cpp` host-hook conflict** (the one real code conflict) | High | The merge sim's only substantive textual conflict. +108 lines of new init flow around `_mcpBridge->start("/tmp/tlgrm_mcp.sock")`. Resolve by hand; re-verify the bridge starts *after* session availability; smoke-test `tools/list` responds. |
| 3 | **`export_output_html.cpp` semantic drift** | Med (not a conflict) | Upstream reworked +463/−13 but it **auto-merges** — which is the trap: git splices the fork's HTML hooks and upstream's rewrite without either failing. Do **not** trust the clean merge. Render one real gradual export and diff the HTML against a 7.0.9 export to confirm the hooks still fire. |
| 4 | Bundle-name ↔ updater-path coupling | High — silent broken updates | Unchanged mechanism from prior #3: `output_name` (CMake), `updater_osx.m`, and `update_checker.cpp`'s `tupdates/temp/Tlgrm.app/…` must agree. Re-verify after branding re-stamp. |
| 5 | Update feed/endpoint naming | High | `AutoUpdateVersion()` → channel `tlgrmfeed4` / path `current4`. A mismatch is a permanent silent no-check. Both HTTP (ironforge) and MTProto (@updates71grm) feeds must be re-pointed. |
| 6 | Export resume assertions | Medium (down from High) | `export_api_wrap.cpp` barely moved (+9/−3), but re-audit the `Expects` in the file-load path against the fork's pacing-timer gap — a new assert can fire across the jitter window. |
| ~ | **Qt fallout** | **Not a risk this port** | Toolchain unchanged. |
| ~ | **TL scheme reconciliation** | **Near-zero** | +30 api.tl lines, one 229 constructor already in the fork. Regenerate scheme, guard the pre-adopted constructor against double-application. |

---

## 6. Sequence

1. Branch `upgrade-v7.1.3` off v7.1.3 in `tdesktop` (`tlgrm-desktop` lineage); `submodule sync` (cmark-gfm mirror).
2. Re-apply the additive `mcp/` tree verbatim; re-add the guarded source list to `Telegram/CMakeLists.txt` (#5) + `Qt::Sql`.
3. Re-apply the 14 conflict files in the §3 order — **export cluster (#1,#4,#10,#11) with the most care**, host hooks (#2,#3,#7,#9) next.
4. **Re-stamp version in the fork's numbering** (risk #1) — increment past 700000901; keep `version` and `version.h` in lockstep.
5. Branding + AUS re-point (risks #4, #5).
6. **First configure + build** — same toolchain, so expect *far* less fallout than 7.0.6; iterate.
7. **Load a real `tdata`** and confirm the session survives (risk #1 is silent — test it explicitly).
8. Verify the MCP bridge starts and `tools/list` responds (risk #3).
9. **Render one real gradual export** and diff the HTML against a 7.0.9 export (risk #2).
10. Sign + notarize bottom-up (macos-codesign skill); `spctl` verify.
11. `adversarial-review` on the rebase diff before release.

---

## 7. Strategic note — this may be the last manual export re-graft

Risk #2 (`export_output_html.cpp`) exists **only because the gradual-export
feature is still welded into the client**. Under the TeleBox refactor
([`PROPOSAL_REFACTOR.md`](PROPOSAL_REFACTOR.md), [`docs/M1_HOST_API.md`](docs/M1_HOST_API.md)),
Export→disk relocates to a TeleBox plugin driving `host.invoke messages.getHistory`
(M2), after which the client's `export/*` files **revert to stock**. Once that
lands, every future upstream port stops conflicting on the export cluster — the
plugin owns the HTML writer, and the client just tracks upstream. If the M1/M2
timeline is near, it is worth weighing whether to relocate Export *before* the
next port rather than re-grafting +463 lines again.

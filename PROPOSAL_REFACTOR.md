# Refactoring proposal

**Status (2026-08-30):** no longer a pure proposal. P1, P3, P4 and P5 shipped;
P2 (TeleBox) is reframed and its first milestone **M0 is built** — a native
GPUI/Rust controller re-hosting the MCP surface end-to-end, at `telebox/` on the
`telebox` branch. The per-item scoreboard at the bottom is the current record;
M1–M6 of P2 (client Host API, relocating the remaining feature families,
cutover) are not started. What follows is the original argument, kept intact
because the reasoning — including the two big proposals the minimalism gate
rejected — is the useful part.

**Requirements update (2026-09-01):** four late requirements — independent-product
branch discipline (never push to upstream), the dual-path update system
(Telegram + web), **lockstep versioning** of the client + TeleBox under one feed,
and the agent surface (extend AGENTS.md + a minimal TLGRM.md skill) — are designed,
Ponytail-gated and POSD-shaped, in
[`docs/DESIGN_PRODUCT_UPDATE_AGENTS.md`](docs/DESIGN_PRODUCT_UPDATE_AGENTS.md). The
verdict there is mostly *don't build* — three of the four already exist. The
`upgrade-v7.1.3` port (7.0.9→7.1.3) is built and runtime-verified
([`UPGRADE_7.0.9_7.1.3_PLAN.md`](UPGRADE_7.0.9_7.1.3_PLAN.md)).

Grounded in the codebase as of 16 August 2026 (Tlgrm 7.0.9a) and the audit
under [`docs/audit-2026-08-11/`](docs/audit-2026-08-11/). Where a claim comes
from a measurement, the measurement is given. Where it comes from an incident,
the incident is named.

Two protocols are applied, in order:

1. **Ponytail (Phase 0, minimalism ladder)** — before designing anything, check
   whether it needs to exist. Stop at the first rung that holds.
2. **POSD** — for what survives the gate, name the complexity symptom (change
   amplification, cognitive load, unknown unknowns) and the principle at stake.

The gate was applied after the first draft was written, and **it rejected or
shrank three of the five proposals**. Those results are kept in place rather
than quietly deleted, because the reasoning is the useful part.

---

## The shape of the problem

| | |
|---|---|
| MCP subsystem | 43,692 lines across 71 files |
| Tools | 355, of which **229 (65%) are `LocalOnly`** — they read this client's own SQLite, not Telegram |
| Backed by Telegram | 81 `Mtproto` + 41 `LiveSession` = 122 (34%) |
| Largest single file | `mcp_tool_registry.cpp`, 3,415 lines of literal schema |
| Declaration sites per tool | 4, all hand-maintained |
| `publish_update.py` | 11 command-line flags |

---

## P1 — Four hand-maintained declaration sites per tool

**Symptom (POSD: change amplification).** One logical change — add a tool —
requires four edits: the `Tool{}` literal in `mcp_tool_registry.cpp`, the
declaration in `mcp_server.h`, the binding in `initializeToolHandlers()`, and
the entry in `mcp_tool_backing.h`. Nothing in the type system relates them.

**Evidence this is not theoretical.** Seven tools were once advertised twice.
Eight were callable but never advertised. 155 arguments were read but never
declared; 72 declared but never read. Every one was found by writing a script
to diff the sites — never by the compiler.

### Ponytail gate

- **Rung 1 — does it need to exist?** Yes for *something*: the drift is
  concrete and recurring, not speculative.
- **Rung 2 — already in this codebase?** **This rung holds, partially.**
  `VerifyToolBackings()` already cross-checks the handler map against the
  backing table at startup, and the `Server` constructor already checks for
  duplicates and unbacked tools. The machinery exists; it runs at the wrong
  time (runtime, on whoever happens to launch) and covers two of the four
  sites.

**Gate verdict: build the smaller thing first.** Promote the existing checks to
a build step that fails the build, and extend them to all four sites plus
schema-versus-implementation arguments. The comparison logic already exists —
in the throwaway scripts written during the schema fix. That is an afternoon,
and it converts every historical defect in this area into a compile error.

**Done:** `tools/check_mcp_tools.py` runs PRE_BUILD on the Telegram target and
fails the build on any of: a tool advertised twice, callable but never
advertised, advertised but unbound, a missing or out-of-order backing entry, an
argument read but never declared, an argument declared but never read, a
required argument nothing reads, or a parameter with no description. Verified
by injecting each defect and watching it fail with the specific tool and
argument named — a check that cannot fail the build is decoration.

**The code generator is deferred, not adopted.** Generating all four sites from
one `TOOL(...)/ARG(...)` declaration is the deeper fix, and would make drift
*impossible* rather than *detected*. But with a build-time check in place the
residual cost is four edits, and 355 tools is a large mechanical rewrite to buy
that. Revisit when tool additions are frequent enough for those edits to be a
real tax — the surface went 326 → 339 → 355 over months, which is not that.

```
// ponytail: build-time cross-check instead of generated declarations —
// revisit if the tool surface starts changing weekly
```

---

## P2 — Telebox: what is actually in the box

**The first draft proposed** splitting the tool surface into `telegram.*` and
`local.*` namespaces. The minimalism gate rejected that, correctly: the
`backing` field already tells a caller which store answered, and renaming 355
tools is a breaking change for information the caller already has.

**But the gate answered the wrong question.** The point was never labelling.
229 of 355 tools do not touch Telegram, and a chunk of those are not a
Telegram client feature at all — they are a separate application that happens
to live in the same process. That application now has a name: **Telebox**.

### `LocalOnly` is not the seam

Backing says *which store answered*. The seam is *what the tool is about*, and
the two do not coincide. Splitting on `backing == LocalOnly` would take 229
tools; splitting on subject matter gives a different set.

| | Tools | Example | Where it belongs |
|---|---|---|---|
| **About Telegram data, stored locally** | ~33 | `archive_chat`, `search_archive`, `get_chat_activity`, `semantic_search`, the gradual-export and deleted-account tools | the client — this *is* what the fork is for |
| **Not about Telegram at all** | ~150 | `set_wallet_budget`, `configure_ai_chatbot`, `clone_voice`, `backtest_strategy`, `create_gift_auction` | Telebox |
| **Local bookkeeping of a Telegram concept** | ~45 | `buy_gift`, `get_star_reactions`, `browse_gift_marketplace` | needs deciding per tool |

The middle category is unambiguous. The third is the one that needs judgement:
a tool named after a real Telegram feature but implemented against a local
store is either a ledger *for* that feature or a simulation *of* it, and which
one it is has to be read per tool. Nothing should be moved on the strength of
its name.

The concentration is helpful: 182 of the 229 live in five files
(`mcp_settings_tools.cpp` 77, `mcp_stars_tools.cpp` 36,
`mcp_business_tools.cpp` 31, `mcp_wallet_tools.cpp` 20,
`mcp_premium_tools.cpp` 18), and those five are almost entirely category two.
The tail — archive, gradual export, deleted accounts, analytics, search,
system — is almost entirely category one.

### Decided

- **Telebox stays a subproject** inside this repository for now, on the
  `telebox` branch. The client half of the separation is `tlgrm-desktop` in
  the submodule.
- **It may become separate later.** Nothing done now should assume either
  outcome; the useful work is finding the seam, not moving directories.
- **No namespace rename.** That was the rejected proposal and it stays
  rejected. If a split happens it is a real separation, not a prefix.

### Next, in order

1. **Classify the ~45 ambiguous tools** by reading each implementation — is it
   a ledger for a Telegram feature, or a self-contained simulation? This is
   reading, not refactoring, and it decides the size of the box.
2. **Find what the two halves share.** The MCP server, the socket, the auth
   token, the SQLite handle, the audit log. Whatever is shared is the real
   interface between them, and it is currently implicit.
3. **Only then** consider moving code. A separation designed before its seam is
   known is how the first draft ended up proposing a rename.

## P3 — The release pipeline is four tools, two docs and a skill

**Symptom (POSD: temporal decomposition exposed to the caller).** Cutting a
release touches `create_dmg.sh`, `tools/publish_update.py`, the
`macos-codesign` skill's `sign.sh` and `notarize.sh`, `gh release create`, and
a manual `scp`. The steps are strictly ordered and the order lives in prose in
`AGENTS.md`.

**Evidence.** Every ordering hazard in that document is there because it was
hit: stripping after the DMG shipped a 1.56 GB app; signing after packaging
shipped an ad-hoc build Gatekeeper refused on download; packing before signing
put an unsigned bundle inside the update packages. Three releases, three
orderings, three repairs.

### Ponytail gate

- **Rung 1 — needed?** Yes; three incidents, one root cause.
- **Rung 2 — exists?** Partially. `publish_update.py` already sequences
  pack → HTTP → MTProto, and already refuses a non-universal bundle and a
  signature downgrade.
- **Rung 6 — can this be one well-named function?** **This rung holds.**

**Gate verdict: one script, not a framework.** The first draft proposed
`release.py` with `--dry-run` and `--from <step>` resume semantics. Resume is
speculative: no release has needed restarting from the middle, and the one case
that did occur — a package posted without its feed JSON — is already covered by
`publish_update.py --post-id`. Build the straight line:

```bash
tools/release.py 7.0.9b     # build → strip → sign → dmg → notarize →
                            # verify → pack → publish → gh release
```

No flags beyond the version until a second use case appears. Each step keeps
its current implementation; the only new thing is one program that knows they
are ordered and refuses to run them out of order.

```
// ponytail: idempotent steps, no --from — each step skips when its output
// already exists, which covers resume without a resume mechanism
```

**Revised during implementation.** The gate rejected `--dry-run` as
speculative. Building the script proved otherwise: there is no other way to
test an orchestrator whose last three steps publish to a channel, an origin and
a GitHub release. It is in. `--from` stayed out — idempotent steps turned out
to cover the same need more simply, which is the better answer than either
option in the first draft.

---

## P4 — `pythonMCP/`'s relationship to the C++ server is undocumented

**Symptom (POSD: unknown unknowns).** The repository contains a second MCP
implementation in Python — 29 source files, 49 tracked. Its README documented
the IPC socket as `/tmp/telegram_mcp.sock`, a path the client has never bound,
so nobody has run it against this client recently enough to notice.

Beside it sits an **untracked, un-gitignored `.mypy_cache/` of 18,524 files**.
Not in the repository, but present in every `find`, and one `git add -A` away
from being committed. (The first draft of this proposal counted it as project
files and claimed pythonMCP was 18,626 files — which is itself the argument for
ignoring it.)

**No abstraction is proposed, so the ladder does not apply.** Two actions:
gitignore the cache, and record in one line at the top of that README whether
the Python server is superseded, complementary, or dead. Any of the three beats
making a reader reverse-engineer intent from a stale config example.

I do not have the information to say which. That is a question for whoever
wrote it.

---

## P5 — Superseded things still present themselves as current

- `cloudflare-worker/` — implements the update protocol from GitHub release
  assets. Nothing has routed through it since ironforge took the hostname.
- `create_beautiful_dmg.sh` sits beside `create_dmg.sh` with no statement of
  which is canonical, and can embed a logged-in Telegram session (now behind
  `INCLUDE_TDATA=1`).
- `dmg_build/` holds DMGs back to 6.3.3 and packages for versions no longer
  served.

**No abstraction proposed.** One header line per file saying what it is and
whether it is current. The cheapest item here, and close to the highest ratio
of confusion removed per minute spent.

---

## What is explicitly not proposed

- **Rewriting the export subsystem.** It works, it has 90 tests, and it is the
  fork's reason for existing.
- **Touching `cmake/`, `lib_ui` or any shared submodule.** The version plumbing
  showed the right shape: adapt in our own files, leave upstream's alone.
- **Reorganising by file size.** `mcp_settings_tools.cpp` is 2,983 lines, but
  splitting by line count moves complexity without reducing it.

---

## Suggested order

| | Item | Status |
|---|---|---|
| 1 | **P5** — label superseded things | **done** — `create_dmg.sh`, `create_beautiful_dmg.sh`, `cloudflare-worker/src/worker.ts` |
| 2 | **P4** — gitignore the cache, decide on pythonMCP | **done** — caches ignored; pythonMCP declared superseded |
| 3 | **P3** — one release script | **done** — `tools/release.py` |
| 4 | **P1** — build-time cross-check | **done** — `tools/check_mcp_tools.py`, PRE_BUILD |
| 5 | **P2** — Telebox | **reframed** — the seam is subject matter, not backing; see above |

**P2 reframed (2026-08-17):** local-only tools are part of the product — but
part of *which* product is the question, and the answer is Telebox for most of
them. The `backing` field distinguishes stores, not subjects.

**Also delivered alongside these:** resumable update downloads. The server
ignored the `Range` header the updater sends on every request, so an
interrupted 110 MB download restarted from zero. Not in the original five —
it came out of the audit's constants table and was prioritised on request.

Nothing in this list is a large refactor, which is the gate working: the two
big rewrites in the first draft — a tool-declaration generator and a 355-tool
namespace split — did not survive contact with it.

When one of these is approved, `/refactor-posd` is the execution vehicle:
AUDIT → PLAN → EXECUTE → VERIFY → DOCUMENT, with the delta recorded in
`REFACTORED.md`.

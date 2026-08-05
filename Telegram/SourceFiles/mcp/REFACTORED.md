# Refactoring Log

## Session: 2026-08-05 — `Telegram/SourceFiles/mcp/` tool surface

### Context

The MCP server advertises a large tool surface to any connected client. A tool
was declared in three hand-synced places — a signature in `mcp_server.h`, a
schema entry in `registerTools()`, and a lambda in the `_toolHandlers` map —
with nothing linking them. Nothing recorded whether a tool had a data source at
all, so tools that invented their answer returned `success: true`
indistinguishably from tools that queried Telegram. There were zero
`TODO`/`FIXME` markers across ~41k lines: the gap was not merely unfixed, it was
unmarked.

Measured before the change: 335 callable tools, 327 advertised, 294 distinct
implementations. Of the callable tools, 87 reached Telegram (56 MTProto, 31 live
session), 9 were pure computation, 219 read only a local SQLite database that
nothing syncs from Telegram, and 20 had no data source whatsoever — 39 of the
unbacked ones reported success.

### Issues Addressed

#### P1-1: registry — INFO-LEAK
**Principle violated:** POSD #5 — Information hiding
**Before:** the fact "tool X exists" lived in three files that had to be edited
in step, with no compile-time or startup link between them.
**After:** `mcp_tool_backing.h` holds one `constexpr` table naming every tool and
what backs it; `VerifyToolBackings()` runs at startup and asserts the table, the
handler map, and the advertised list agree.
**Why this is better:** a single logical change — adding a tool — previously had
three independently forgettable edit sites, and each omission failed silently in
a different way. Drift is now detected where it happens rather than surfacing as
a client seeing a tool it cannot call.
**Caller impact:** none at the call site; server startup now fails loudly on a
drifted table instead of serving an inconsistent surface.

#### P1-2: implementations — UNKNOWN-UNKNOWN
**Principle violated:** POSD #10 — Define errors out of existence (inverted: an
error was being defined *into* nonexistence by reporting success)
**Before:** 39 tools with no data source set `success: true` and returned
plausible-looking values built from literals. No part of the interface — name,
schema, description, or response — distinguished them from working tools.
**After:** `callTool()` consults the table before dispatching. `Unimplemented`
returns an explicit error and never reaches the handler. Every other response
carries a `backing` field, and `tools/list` appends the backing to each
description.
**Why this is better:** the caller can no longer be misled, and the property is
enforced centrally rather than depending on 335 implementations each choosing to
be honest. This is the single rule that prevents the condition recurring.
**Caller impact:** 20 tools now return an error where they previously returned
fabricated success. This is a deliberate behaviour change — the previous
behaviour was the defect.

#### P1-3: `_toolHandlers` — CONJOINED-METHODS
**Principle violated:** POSD #16 — Code should be obvious
**Before:** five tools in the tag family had their handler assigned twice, the
second assignment silently overwriting the first. Both assignments named the
same function, so behaviour was unaffected — but nothing would have caught it
had they differed.
**After:** redundant assignments removed; the startup check now rejects
duplicates.
**Caller impact:** none.

#### P1-4: registry — OVEREXPOSED
**Principle violated:** POSD #5 — Information hiding
**Before:** eight tools were callable but never advertised — seven
deleted-account tools and `create_giveaway`. No client could discover them, and
`tools/list` understated the surface.
**After:** all eight advertised, with schemas derived from the arguments each
implementation actually reads rather than from its name.
**Caller impact:** eight tools became discoverable.

#### P2-2: `start_gradual_export` — duplicate advertisement
**Principle violated:** POSD #15 — Consistency
**Before:** advertised twice with different descriptions and different defaults
(3000/15000 ms vs 2000/5000 ms). The second omitted `export_format` and
`export_path`, which the implementation does read.
**After:** the accurate entry kept, the inaccurate duplicate and its stale
section comment removed.
**Caller impact:** clients now see the two parameters the schema had been hiding.

### Issues Deferred

- **P2-1 PASS-THROUGH (36 alias names)** — deferred. Removing them is a breaking
  API change for any client already calling them; it needs a deprecation pass,
  not a silent deletion.
- **P2-3 GENERIC-CONTAINER** — deferred. Typing 335 tool signatures is a larger
  project than this pass; the `backing` field addresses the harmful part (not
  knowing where a value came from) without it.
- **P3-1** `mcp_settings_tools.cpp` holds 79 unrelated tools — deferred, purely
  organisational.
- **219 LocalOnly tools** — *not* a defect to fix here, but the real remaining
  work. Giving them genuine MTProto backing requires per-tool API research and
  is a separate program. They are now labelled rather than silently misleading,
  which is what makes that program possible to scope.

### Complexity Delta

| Metric | Before | After |
|---|---|---|
| Overall verdict | CRITICAL | MEDIUM |
| Advertised / callable / recorded | 327 / 335 / — | 335 / 335 / 335 |
| Duplicate registrations | 6 | 0 |
| Invisible tools | 8 | 0 |
| Unbacked tools reporting success | 39 | 0 |
| Tools declaring their data source | 0 | 335 |
| Change-amplification sites | 3, unchecked | 3, checked at startup |

### Principles Applied

- **#4 Deep modules:** one table plus three small functions replace a fact that
  was previously spread across three files and 21 implementation units.
- **#5 Information hiding:** "what backs this tool" now has exactly one
  authority.
- **#8 Pull complexity downward:** callers no longer need out-of-band knowledge
  to tell a Telegram result from a local one; the server states it.
- **#10 Define errors out of existence:** an unimplemented tool cannot report
  success, so the class of "silently fabricated result" is gone by construction.
- **#16 Code should be obvious:** the table is the inventory. Reading it tells
  you the surface, which reading three files previously did not.

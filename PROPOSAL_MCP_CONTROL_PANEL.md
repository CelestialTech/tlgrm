# Proposal — an MCP Control Panel, with a Claude-powered DBA agent

Status: draft, `telebox` branch. Not scheduled. Companion to
`PROPOSAL_REFACTOR.md` and the record in `docs/MCP_STAR_GIFTS.md`.

## Why this exists

The embedded MCP server is invisible. `Core::Application` constructs it and
calls `_mcpServer->start(transport)` and `_mcpBridge->start(...)` during launch
(`core/application.cpp:455,492`), and from that point there is **no way, inside
the app, to see that it is running, stop it, restart it, or change a single
setting**. The transport is chosen by the presence of the `--mcp` flag; the
socket path is a string literal (`/tmp/tlgrm_mcp.sock`); the auth token is a
UUID written to a file next to the socket (`mcp_bridge.cpp:63`) that a user
never sees. 352 tools and ~50 local database tables sit behind that socket with
no dashboard, no log a person can read, and no off switch.

That invisibility is not a cosmetic gap. This whole cleanup pass — the twenty
star-gift, marketplace and wallet tools in `docs/MCP_STAR_GIFTS.md` that
reported local rows as Telegram truth — happened **because no one could see the
local database.** `wallet_spending` filled with purchases that never happened;
`auctions` and `marketplace_listings` held records no server ever saw; nothing
in the product ever showed those rows to a human who could ask "why is this
here?" The observability that would have caught it did not exist.

This proposal builds that observability, and makes it the centrepiece rather
than an afterthought: a control panel that starts and stops the server,
configures it visually, browses the tool surface, streams live activity — and
gives the local database a **Claude-powered DBA agent** whose first job is to
answer exactly the question that went unasked for twenty tools: *which of these
tables is real, and which is telling me a story?*

## What exists today (grounded)

| Concern | Today | Where |
|---|---|---|
| Lifecycle | Auto-start at launch, no stop/restart | `core/application.cpp:444–495` |
| Transport | `Stdio` if `--mcp`, else `IPC` | `core/application.cpp:452` |
| Socket | Literal `/tmp/tlgrm_mcp.sock`, or `<cache>/mcp/bridge.sock` (0700 dir, 0600 socket) | `mcp_bridge.cpp:47,102` |
| Auth | Auto UUID, written to `<dir>/auth_token`, required in `initialize` | `mcp_bridge.cpp:63,295` |
| Tool surface | 352 tools, four declaration sites, backing enum | `mcp_tool_backing.h` |
| Backing gate | `callTool()` refuses success for `Unimplemented` | `mcp_server_complete.cpp` |
| Local DB | ~50 tables, `QSQLITE` connection `telegram_mcp_archive` | `chat_archiver.cpp:53` |
| Audit log | `AuditLogger` already writes to the same DB | `audit_logger.cpp:22` |
| Settings host | Native Qt `Section` subclasses | `SourceFiles/settings/` |

Two things worth noting from that table. First, the machinery a control panel
needs mostly **already exists** — a backing table that knows what each tool is,
an audit logger that already records calls, a settings framework built to hold
new sections. Second, the one genuinely new capability is the DBA agent; the
rest is surfacing state the server already holds.

## The panel

A new native settings section, `MCP Server`, sitting alongside Advanced and
Experimental. Five views down a left rail. Nothing here is a web view or a
bolted-on window — it is the same `Ui::` widgets the rest of Settings uses, so
it inherits the app's theme, scaling and accessibility for free (ponytail
rung 2: reuse what exists).

### 1. Status & lifecycle

The header every view shares: a status pill (**running** / **stopped** /
**starting**), and **Start / Stop / Restart**. Below it, the facts the server
already knows but never showed — uptime, transport, socket path, connected
clients, requests in the last minute, and the last error if any. A small
stacked bar breaks the 352 tools down by backing, so the shape of the surface
is legible at a glance: how much is really Telegram-backed (`Mtproto`,
`LiveSession`), how much is local, how much is deliberately `Unimplemented`.

The stop switch matters on its own. A headless server you cannot stop is a
security surface with no lock on it; giving it an off switch and a visible
running state is the smallest honest version of this whole proposal.

### 2. Configuration

Everything currently frozen at compile time or launch time, made editable:

- **Transport** — IPC / stdio, with a note on what each is for.
- **Socket path** — editable, with the permissions shown (0600) so the security
  posture is visible rather than assumed.
- **Auth token** — view, **copy**, and **rotate**. Rotation is a real operation
  today (regenerate the UUID, rewrite the token file, drop existing sessions);
  it just has no button.
- **Autostart** — whether the server comes up with the app at all. Off by
  default would be a defensible posture; today it is unconditionally on.
- **Tool enablement by backing** — toggle whole classes. "Disable everything
  that only writes locally" becomes one switch, not a code change.

### 3. Tool catalog

The 352 tools, browsable and searchable, each with its backing as a colour-coded
chip. Filter to `Unimplemented` to see exactly what is stubbed and why; filter
to `LocalOnly` to see what reads the local DB rather than Telegram. Each row can
show its schema and its last-called time. This is the four-site contract from
`check_mcp_tools.py`, made something a person can read instead of a build-time
assertion.

### 4. Activity

The live JSON-RPC stream — method, tool, duration, result size, error — the
same events `AuditLogger` already persists. Read-only, filterable, the thing you
watch when a client misbehaves.

### 5. The DBA agent — the centrepiece

A Claude conversation scoped to one job: administering the local
`telegram_mcp_archive` database. It is not a general chat; its system prompt
pins it to the live schema and to DBA duties, and it holds the same rule the
tools now hold — **report what the database contains, never invent.**

**What it can do**

- **Answer in natural language over the schema.** "How big is each table?"
  "Which tables were written this week?" It composes SQL, *shows the SQL before
  running it*, and runs it read-only.
- **Health checks — the reason it exists.** "Which tables have no live writer?"
  is a query the agent can actually answer by cross-referencing table names
  against the tool bodies and the backing table. Run against today's tree it
  would surface `auctions`, `marketplace_listings`, and the orphaned
  `wallet_spending` writers — the fabrication, found by the tool built to find
  it. Referential gaps (`collection_items` pointing at absent `gift_collections`)
  and stale rows fall out the same way.
- **Explain, in plain language.** "`wallet_spending` is empty because its only
  writers — `buy_gift`, `send_stars`, `withdraw_earnings` — were changed to
  record nothing; the table is now vestigial." The agent reads the finding back
  the way this document reads it.
- **Maintain, behind a gate.** `VACUUM`, `ANALYZE`, `schema_version`
  migrations, pruning vestigial tables. Every mutation is **read-only by
  default and confirmed by preview**: the agent shows the statement and the
  exact row count it will touch, and nothing runs until a human clicks through.

**Guardrails (non-negotiable)**

- **Scoped to the archive DB only.** It cannot reach `tdata` or the Telegram
  session store. The connection it holds is `telegram_mcp_archive` and nothing
  else.
- **Read-only by default.** `SELECT` / `PRAGMA` / `EXPLAIN` run freely; anything
  that writes is gated behind the confirm-preview step above.
- **Every statement is audited.** It writes through the existing `AuditLogger`,
  so the DBA agent's own actions are as visible as the tool calls it inspects.
- **It never fabricates.** Same rule as `AGENTS.md`: a DBA agent that guessed at
  a row count would be the very failure this panel is meant to end.

**How it reaches Claude.** Through the account the user configures in the panel;
the app assembles a system prompt from the live schema and streams query
results back as context. The agent proposes, the app executes against the local
DB, the result returns to the agent — the model never touches the database
directly, which is what keeps the guardrails enforceable in code rather than in
a prompt.

## Design discipline

Gated through the ponytail ladder before anything is built:

- **Rung 1 (does it need to exist).** The lifecycle, config, catalog and
  activity views surface state the server already holds — they pass because the
  alternative is the status quo that hid twenty fabricated tools. The DBA agent
  is the one genuinely new module, and it earns rung 7: nothing existing answers
  "which of my tables is real."
- **Rung 2 (reuse).** The panel is `Ui::` widgets in the existing Settings
  framework; activity rides the existing `AuditLogger`; the catalog is the
  existing backing table rendered. Almost nothing here is new plumbing.
- **POSD.** The DBA agent is a deep module: a wide, messy job (schema
  introspection, NL-to-SQL, integrity analysis, gated maintenance) behind a
  narrow interface (a conversation and a confirm button). The complexity is
  pulled down into the app, not pushed onto the user.

## Phasing

1. **Status & Stop.** The status pill and a working stop/restart. Smallest
   honest slice; ships the security win alone.
2. **Configuration & Catalog.** Editable transport/socket/auth/autostart, and
   the browsable tool surface.
3. **Activity.** The live log over `AuditLogger`.
4. **DBA agent, read-only.** Schema browser, NL queries, health checks. No
   writes yet — this alone would have caught the fabrication.
5. **DBA agent, gated writes.** Confirmed maintenance and pruning.

Each phase is usable on its own, and the ordering front-loads the observability
that was missing when it was most needed.

## Not in scope

- Remote control. The panel is local; the server stays bound to a local socket.
- Editing the Telegram session store. The DBA agent sees the MCP archive DB and
  nothing else, by construction.
- Multi-account server fan-out. One app, one server, one panel.

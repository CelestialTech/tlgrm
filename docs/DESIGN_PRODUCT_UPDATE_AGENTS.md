# Design consolidation — product model, update system, agent surface

> Status (2026-09-01): design. Folds four late requirements onto the TeleBox
> refactor ([`../PROPOSAL_REFACTOR.md`](../PROPOSAL_REFACTOR.md),
> [`M1_HOST_API.md`](M1_HOST_API.md)), gated by **Ponytail** (minimalism ladder)
> and shaped by **POSD**. The recurring verdict: most of this already exists —
> the design work is deciding what NOT to build.

## Requirements captured

| # | Requirement (user, 2026-09-01) |
|---|---|
| R1 | Continue as a **separate branch / independent product** from upstream tdesktop. |
| R2 | Keep an **independent update system** — copying upstream's principles — delivered on the **same two paths**: via Telegram itself, and via the web. |
| R3 | Consider **publishing both tools at once under the same versioning**. (Advice requested.) |
| R4 | An **AGENTS.md** that says what the system is and how agents use it; a **TLGRM.md skill** for agents to talk to TeleBox. |

---

## R1 — Independent product, separate branch

**Ponytail rung 2 (exists).** Already true: parent repo on `telebox`, client on
`tlgrm-desktop` (+ the port branch `upgrade-v7.1.3`); `upstream` (telegramdesktop)
is fetch-only and **never** a push target. Identity (branding, the `aus`
auto-update family) stays with the client and does not move to a plugin
([[reference-fork-feature-provenance]]).

**No build.** Formalize the rule in `AGENTS.md` (§R4): upstream is a source to
*merge from*, never push to; releases go to `CelestialTech/tlgrm` only.

## R2 — Independent dual-path update system

**Ponytail rung 2 (exists), verified in code.** The system is built and
documented ([`RELEASE_PIPELINE.md`](RELEASE_PIPELINE.md), [[reference-update-system]]):

- **Via Telegram:** `tools/publish_update.py` posts the package + a feed JSON to
  `@updates71grm`; the client's `MtpChecker` reads history `limit=1`.
- **Via web:** the same script `scp`s packages to `ironforge`, served at
  `updates.71grm.site`; the client's HTTP checker polls it. Both run in parallel
  in `Updater::start()`; either can supply an update.

This already *copies upstream's principles* (Packer-signed payloads, per-arch
platform keys, a feed the client polls). **Do not rebuild it.** The only change
R3 forces is **what the feed names** (below).

**One real gap surfaced during the port:** `DesktopPrivate/packer_private.h` (the
RSA signing key) is **absent from this checkout**. Without it Packer cannot sign
update payloads, so `publish_update.py` cannot produce a shippable update — the
build + DMG are unaffected. Restoring that key from backup is a **human
prerequisite** before any release; it cannot be regenerated without orphaning
every installed client (the public key is compiled into `config.h`).

## R3 — Versioning of the two tools (the advice)

**Recommendation: lockstep — one version, one feed, both tools together —
plus a cheap compatibility guard.** Design-it-twice (POSD #11):

**Alternative A — lockstep (recommended).** One `AppVersion` (the fork's
`base*100+index`, e.g. `700100301`/7.1.3a) applies to the *release*, not to each
binary. The existing dual-path feed publishes **one version** whose feed JSON
names **two artifacts** — the client package and the TeleBox package.
*Hides:* cross-tool compatibility — a matched pair is compatible by construction.
*Weakness:* any change to either tool re-releases both.

**Alternative B — independent semver per tool.** Client and TeleBox version and
update on separate tracks.
*Hides:* nothing — it *exposes* a compatibility matrix to the user.
*Weakness:* version skew in the wild; a TeleBox calling a `host.*` shape the
installed client lacks. During the volatile M1–M5 phase (the Host API contract
is still moving) this is a constant breakage source.

**Pick A.** It matches upstream's one-version/one-feed principle (R2), and lockstep
*eliminates* skew while the contract is unstable.

**The refinement that keeps A from being a trap (POSD #8, pull complexity down;
Ponytail rung 6 — one integer + one check, not a subsystem):** the client's Host
API carries a `HOST_API_VERSION` integer; TeleBox reads it at socket-connect
(`initialize`) and refuses/warns on mismatch. This makes lockstep *provably* safe
now and preserves the option to decouple cadences later with no redesign. It
extends the M1 Host API design — added to [`M1_HOST_API.md`](M1_HOST_API.md) as a
`session.hostApiVersion` primitive.

**Consequences for the pipeline (small, additive):** `publish_update.py`'s feed
JSON gains a second artifact entry; the DMG (`create_dmg.sh`) may stage both apps
or ship two DMGs — a later M6/cutover detail, not now.

## R4 — Agent surface

### AGENTS.md — **extend, don't create** (Ponytail rung 2)

`AGENTS.md` exists (239 lines: Versioning, "the tooling already exists", Cutting a
release, Layout, Standing rules). It is a *developer/agent-working-in-the-repo*
guide. Add one section, **"What this is for an operating agent"**: the product is
a Telegram client that exposes its capabilities as an MCP tool surface (352 tools
over a local socket, [[reference-mcp-server]]); how to discover the socket
(`~/Library/Preferences/tlgrm/mcp_socket_path` + `auth_token`); the
`initialize`→`tools/list`→`tools/call` handshake; and the governing rule *a tool
reports what happened, never what it wishes had happened*. Nothing new is built —
it documents the existing bridge.

### TLGRM.md skill — **minimal, reusing the existing channels** (Ponytail rung 1→2)

*Does a new mechanism need to exist?* **No.** Two channels to reach the fork
already exist: the client's **MCP bridge** (the 352-tool endpoint) and TeleBox's
**relay** (`mcp_relay.rs`, which re-hosts that same surface) plus its **QA
control socket** (`qa.rs`). A skill that invents a third protocol would be pure
complexity (rung 1 — reject).

*Is even a documentation skill premature?* Partly. TeleBox is **not yet the
production endpoint** — the client's own bridge is, until cutover (M6). So the
skill's *content* today is "talk to the client's MCP endpoint"; it grows a
TeleBox section only when TeleBox owns `mcp_socket_path`.

**Design:** a thin `TLGRM.md` skill whose body is the discover→initialize→call
recipe against the **existing** MCP socket (the same one `publish_update.py`
already uses — reuse that discovery, don't re-specify it), with a stubbed
"post-cutover: same protocol, TeleBox's aggregated socket" note. It documents;
it does not implement. Deferring the TeleBox half until M6 is the honest call.

---

## What NOT to build (the Ponytail wins)

- **No** new update system — the dual-path one exists (R2).
- **No** new AGENTS.md — extend the existing 239-line one (R4).
- **No** new agent↔TeleBox protocol — reuse the MCP endpoint + QA socket (R4).
- **No** per-tool version tracks — lockstep, one feed (R3).
- The only genuinely new code is the **`HOST_API_VERSION` integer + connect-time
  check** (R3) — and it is deliberately one integer, not a versioning module.

## POSD notes

- **Deep module (#4):** the update system stays a deep module — the feed JSON is
  the whole interface; adding a second artifact name does not widen it.
- **Information hiding (#5):** lockstep hides cross-tool compatibility from the
  user entirely; the `HOST_API_VERSION` check keeps that guarantee if cadences
  ever split.
- **Pull complexity down (#8):** compatibility is enforced once, at connect, not
  scattered across every `host.*` call.

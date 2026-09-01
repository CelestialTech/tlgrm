# M1 — The Client Host API

> Status (2026-09-01): **design, grounded in the live client.** This is the
> first slice of P2/M1 in [`PROPOSAL_REFACTOR.md`](../PROPOSAL_REFACTOR.md).
> M0 (the native TeleBox controller re-hosting the MCP surface) is built; M1
> gives TeleBox plugins a *primitive* surface to call back into the client, so
> the fork's features can leave the client and the client can revert toward
> stock. No C++ is written yet — this doc is the contract the C++ implements.

Grounded by reading the live tree on the `tlgrm-desktop` branch:
`Telegram/SourceFiles/mcp/mcp_bridge.{h,cpp}`, `mcp/mcp_server.h`, and
`core/application.cpp:492`.

---

## 1. What M1 is, and why it exists

Today the fork's features live **inside** the client as ~352 MCP tools
(`mcp_tool_registry.cpp`, 3424 lines) reached over a Unix socket. Each tool is
feature-specific and knows about Telegram internals *and* about what the feature
wants to do with them. That is why the client cannot revert to stock: the
features are welded to it.

M1 inverts the dependency. It exposes a **small, general-purpose, feature-free**
surface — seven primitive families — that a TeleBox plugin can drive to
accomplish anything a current tool does, without the client knowing which
feature is asking. Once every fork feature is expressed in terms of these
primitives (M2–M5), the feature code moves out to a plugin and the corresponding
client file reverts to stock (the decoupling principle from
`reference-fork-feature-provenance`: *preserve every feature, relocate it —
a client file reverts to stock only because its feature moved out*).

The north-star acceptance test for M1: **the Export feature can run entirely
from a TeleBox plugin, driving only Host-API primitives, with the client's
export C++ deleted** — because Export→disk is just "page history + write files,"
and paging history is `invoke messages.getHistory`. If `invoke` + `model.*` +
`files.*` can carry Export, they can carry the rest.

## 2. Ground truth — reuse the transport, don't reinvent it (ponytail rung 2)

The socket, the auth handshake, and the JSON-RPC envelope **already exist** in
`MCP::Bridge` and are correct. M1 adds methods, not a second server.

| Concern | Where it lives now | M1 stance |
|---|---|---|
| Unix socket lifecycle | `Bridge::start()` on `/tmp/tlgrm_mcp.sock` (`application.cpp:492`) | reuse unchanged |
| Peer credential check + auth token | `Bridge::verifyPeerCredentials`, `generateAuthToken`, token file | reuse unchanged |
| JSON-RPC envelope | `{jsonrpc,id,method,params}` → `{result}` / `{error:{code,message}}` (`mcp_bridge.cpp:319-385`) | reuse unchanged |
| Method dispatch | `handleCommand` if/else ladder; bare `method` falls through to `Server::callTool(method, params)` (`mcp_bridge.cpp:363-365`) | **extend** with the `host.*` families |
| Session binding | `Server::setSession(Main::Session*)`, `hasSession()` (`mcp_server.h:109,116`) | backs `session.*` |

Because `handleCommand` already routes any unrecognized `method` to a single
dispatch function, the Host API needs **one** new routing arm, not a rewrite.

## 3. The seven families

Namespaced `host.<family>.<verb>` on the existing wire. Every method is
feature-agnostic: it names a Telegram capability, never a fork feature.

| Family | Purpose | Representative methods | Backed in client by |
|---|---|---|---|
| `session` | who am I / connection state | `whoami`, `state`, `waitOnline` | `Server::setSession/hasSession` + `Main::Session` |
| `invoke` | **raw MTProto passthrough** (the keystone) | `invoke{ method, params }` → serialized TL result | new thin wrapper over `Session::api().request(MTP…)` |
| `model` | read the local data model | `dialogs`, `history{peer,offset,limit}`, `peer`, `message` | the existing `get_dialogs`/`get_messages`/`search_local` handlers + local DB |
| `settings` | read/write client settings | `get`, `set`, `list` | `mcp_settings_tools.cpp` |
| `files` | download/produce media & files | `download{ location }`, `path`, `thumb` | `mcp_download_tools.cpp` |
| `ui` | drive the client UI | `openChat`, `compose`, `notify` | `mcp_rich_message_tools.cpp` and friends |
| `events` | subscribe to client events | `subscribe{ kind }`, `poll`, `unsubscribe` (`kind` ∈ newMessage, edit, delete, mediaExpiring) | the existing ephemeral/pre-delete capture hooks |

`invoke` is the only genuinely **new** primitive (ponytail rung 7 — everything
else reuses an existing handler). It is deliberately the most general: given
`invoke`, a plugin can perform any MTProto call the account can, which is why
the covert gradual export ("plain `messages.getHistory`, no takeout" —
`reference-gradual-export-takeout-bypass`) reduces to a loop over
`host.invoke`. Guarding it is section 6.

## 4. Design it twice (POSD #11) — where do the primitives live?

The one structural decision: `host.*` handlers as a **new dispatcher**, or as
**more methods on `Server`**.

**Alternative A — a parallel `MCP::HostApi` object.**
`Bridge` gets a `setHostApi(HostApi*)` beside `setServer(Server*)`; `host.*`
methods route to it. `HostApi` holds the `Main::Session*` and the primitive
implementations; the 352 tools stay in `Server`, untouched.
*Hides:* the primitive layer from the tool registry entirely.
*Weakness:* two objects both needing the session and both wrapping MTProto;
some duplication until the tools are deleted in M2–M5.

**Alternative B — primitives as methods on the existing `Server`.**
Add `hostInvoke`, `hostHistory`, … next to the `toolXxx` methods; `handleCommand`
routes `host.*` into `Server::callHost(family, verb, params)`.
*Hides:* nothing new; reuses the session binding and MTProto plumbing the tools
already use.
*Weakness:* piles the new general-purpose surface into the 933-line/3424-line
God-object we are trying to dissolve — it grows the thing M1 exists to shrink.

**Recommendation: Alternative A.** M1's *point* is a layer the client can keep
after the feature tools are gone. If the primitives live inside `Server`
(Alt B), deleting the tools in M2–M5 means surgically extracting the primitives
back out — we would build the coupling we are paying M1 to remove. `HostApi` as
its own module is the deep, feature-free abstraction (POSD #4, #7: different
layer, different abstraction); the temporary duplication (Alt A's weakness) is
deleted *for free* as each feature family relocates. This is POSD #8, pull
complexity downward: the plugin says `host.invoke messages.getHistory`, and the
messy TL serialization lives once, in `HostApi`.

## 5. Relocation map — what M1 unblocks

From `reference-fork-feature-provenance`: 5 families, only two move.

| Family | Moves? | Expressed via | Client file at end |
|---|---|---|---|
| `mcp` (352 tools) | → TeleBox | each tool re-implemented in a plugin over `host.*` | `mcp/` deleted; `HostApi` kept |
| `export` (gradual→disk) | → TeleBox Export plugin | `host.invoke messages.getHistory` loop + `host.files` + local writers | export C++ deleted |
| `aus` (auto-update) | **stays** | — client identity | unchanged |
| `branding` | **stays** | — client identity | unchanged |
| `build` | **stays** | — client identity | unchanged |

The archive/export space is three *purposes* (Export→disk, Retention/Vault,
Archiver→Telegram) over one engine; each becomes its own plugin, and each is
carried by `invoke` + `model` + `files` + `events`. M1 ships the primitives;
M2–M5 relocate one plugin at a time and delete its client code.

## 6. `invoke` safety — a tool reports what happened

The governing MCP rule (*a tool reports what happened; it never records what it
wishes had happened*) applies doubly to a raw passthrough. `host.invoke`:

- returns the **actual** TL result or the **actual** RPC error, never a
  synthesized success;
- is gated by the same auth token as every other method (§2) — no new trust
  boundary;
- does **no** rate-limit smoothing of its own. The covert pacing (3–15 s
  jitter, active-hours, ≤5000/day) is a *plugin* policy in TeleBox, not a client
  behavior — the client primitive stays honest and dumb, the plugin owns the
  strategy. This keeps the reverted client free of any feature-specific conduct.

## 7. What M1 does **not** do (non-goals)

- No feature logic. No export loop, no archiver, no retention DB in the client —
  those are M2–M5 plugin work.
- No new socket, no new auth model, no protocol version bump beyond adding
  methods.
- No client file reverts yet — a family reverts only when its feature has
  actually moved out (M2+). M1 is purely additive.

## 8. Verification plan (how M1 gets proven, not claimed)

1. **Unit, headless:** a `host.invoke ping`-class round-trip and a
   `host.model.dialogs` call over the socket, asserted against a logged-in test
   session — no UI needed.
2. **End-to-end, headless via M0:** drive it through the built TeleBox
   controller's QA socket (`telebox/qa/smoke.py`, `render_to_image`) so it is
   verifiable with the Mac screen locked — the same path M0 already proved.
3. **Acceptance:** a throwaway script that pages one test chat's history purely
   through `host.invoke messages.getHistory` and writes it to disk — proving
   Export can leave the client. Test peer `768828198`
   (`project-deleted-account-test-peer`).

Only when (3) passes is M1 "done." Until then this document is the design, and
the scoreboard in `PROPOSAL_REFACTOR.md` shows M1 as **not started (design
landed)**.

---

*Branch discipline: M1 C++ lands on `tlgrm-desktop` (the `tdesktop/` client
repo); this design doc lives with the parent's other refactor docs on the
`telebox` branch. Never on a shipping branch (`master`, `upgrade-v7.0.9`).*

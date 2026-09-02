# Plan — close the 14 layer-229 MCP gaps (POSD + Ponytail)

> Grounded in the live tool-writing mechanism (`mcp_settings_tools.cpp:2992`
> `toolTransferGift`, `mcp_tool_registry.cpp` `registerTools`, `mcp_tool_backing.h`).
> Backlog source: [`FEATURE_MCP_API_MAP_7.1.3.md`](FEATURE_MCP_API_MAP_7.1.3.md).
>
> **Progress (2026-09-02):** 6 tools shipped, 4 gaps closed (G1 read+delete, G2,
> G5, G10) — 3 read tools live-verified against a real account; the mutating ones
> compile-verified + advertised, live-fire gated on the operator. The honest-await
> template (this doc) is proven at compile *and* runtime. Surface 352 → 358. The
> per-tool status table lives in `FEATURE_MCP_API_MAP_7.1.3.md`.

## The load-bearing design decision (POSD #4, #8, #10)

The MCP bridge is **synchronous** (`handleCommand` returns a `QJsonObject`); MTProto
is **async**. The existing MTProto tools resolve this by **fire-and-forget**:
`toolTransferGift` does `.send()` and returns `"status":"submitted"`, with
`.done`/`.fail` only logging. **That reports what it wished, not what happened —
the exact violation the governing rule (AGENTS.md) forbids.**

**Every new tool here must report the real server result.** That means the
handler blocks on the async reply. The wait mechanism **already exists** — a
nested `QEventLoop` in `mcp_extra_tools.cpp` / `mcp_server_complete.cpp`
(Ponytail **rung 2 — reuse, do not invent**).

**Design-it-twice on the shared primitive:**
- **A (recommended) — extract the existing nested-loop into one helper**
  `awaitRequest<TLRequest>(request) → Expected<TLResponse, MTP::Error>`. Every new
  tool becomes: parse args → build request → `awaitRequest` → map TL→JSON. The
  event-loop complexity lives **once** (POSD #8, pull complexity down; #4, a deep
  helper hiding the hardest part). *Weakness:* nested loops risk re-entrancy — must
  guard against being called while another await is in flight on the same socket.
- **B — each tool inlines its own `QEventLoop`.** *Weakness:* copy-pasted event-loop
  boilerplate across ~8 tools; the re-entrancy guard gets forgotten in some. Rejected.

**Recommendation: A… corrected to reuse-the-idiom (Ponytail, on grounding).**
Inspecting the code, the honest-await pattern is already an inline idiom, proven in
`toolRequestMessageSummary` (`mcp_extra_tools.cpp:26`): a `QEventLoop`, `.done`
mapping the *typed* response into `result`, `.fail` capturing the error, a 15s
`QTimer::singleShot` guard, then `loop.exec()`. Because each MTP request returns a
**different** typed response, a generic `awaitRequest<Req,Resp>` helper adds type
gymnastics for little gain — the inline idiom is *more* minimal and matches the
existing 3 uses (POSD #15 consistency, Ponytail rung 2 taken literally: reuse the
pattern, don't invent an abstraction). So **Wave 0 builds no helper**; it just
establishes this idiom + a `resolvePeer`/admin-rights mapper as the shared shape.

**Concrete template (G1 `get_welcome_messages`, execution-ready):**
```cpp
QJsonObject Server::toolGetWelcomeMessages(const QJsonObject &args) {
    if (!_session) return jsonError("No active session");
    const auto peer = resolvePeer(args.value("chat_id").toVariant().toLongLong());
    if (!peer) return jsonError("Peer not found");
    QJsonObject result; QEventLoop loop;
    _session->api().request(MTPephemeral_GetWelcomeMessages(
        peer->input(), MTP_long(0) /*hash=0 → always full list*/
    )).done([&](const MTPephemeral_WelcomeMessages &r) {
        r.match([&](const MTPDephemeral_welcomeMessages &d) {
            QJsonArray msgs;
            for (const auto &m : d.vmessages().v) { const auto &md = m.data();
                msgs.append(QJsonObject{{"id", md.vid().v},
                    {"message", qs(md.vmessage())}, {"date", qint64(md.vdate().v)}}); }
            result = QJsonObject{{"success", true}, {"count", msgs.size()},
                                 {"welcome_messages", msgs}};
        }, [&](const MTPDephemeral_welcomeMessagesNotModified &) {
            result = QJsonObject{{"success", true}, {"not_modified", true}}; });
        loop.quit();
    }).fail([&](const MTP::Error &e) {
        result = QJsonObject{{"success", false}, {"error", e.type()}}; loop.quit();
    }).send();
    QTimer::singleShot(15000, &loop, &QEventLoop::quit);
    loop.exec();
    return result; // reports the REAL ephemeral.WelcomeMessages, never "submitted"
}
```
Delete/send/editAdmin follow the same shape over `Bool`/`Updates` responses. This
is the drop-in template every wave-1..4 tool copies.

## Ponytail gate per gap — new tool vs. extend vs. wire-stub

| Item | Ponytail verdict | Backing |
|---|---|---|
| G1 welcome messages (get/delete/deleteAll/send) | **rung 7** — new, 4 RPCs, greenfield | `Mtproto` |
| G2 `set_admin_rights` (`channels.editAdmin`) | **rung 7** — new; 0 admin tools exist | `Mtproto` |
| G3 resale **buy** | **rung 2** — wire the existing `toolBuyGift` stub (`mcp_stars_tools.cpp:592`) | `Unimplemented`→`Mtproto` |
| G4 draft streaming (`set_typing`) | **rung 7** — new; 0 typing tools | `Mtproto` |
| G5 decode `has_welcome_messages` etc. | **rung 2** — extend `get_chat_info`, data already fetched | (no new tool) |
| G6 ephemeral bot callback | **rung 7** — new | `Mtproto` |
| G7 edit ephemeral message | **rung 7** — new | `Mtproto` |
| G8 forward-from-ephemeral | **rung 2** — extend `forward`/`batch_forward` with a flag | (existing tool) |
| G9 reply-markup on send | **rung 2** — extend send tools with a `reply_markup` arg | (existing tools) |
| G10 Instant-View page blocks | **rung 7** — new `get_web_page` | `Mtproto` |

Net: **6 genuinely new tools**, **4 extensions/wirings of existing surface**. The
Ponytail win is that a third of the "gaps" are one-field extensions, not new tools.

## The four declaration sites (every new tool touches all four)

1. `mcp_server.h` — `QJsonObject toolXxx(const QJsonObject&);` decl.
2. `mcp_tool_registry.cpp registerTools()` — the `Tool{name, description, schema}`.
3. `initializeToolHandlers()` — the `_toolHandlers` name→method map.
4. `mcp_tool_backing.h` — the sorted `{"name", Backing::Mtproto}` row.

`VerifyToolBackings` (asserts at server start) + `tools/check_mcp_tools.py` (build
time) enforce that all four agree. **This is change-amplification (POSD #2)** — the
plan does NOT refactor it (out of scope), but it dictates the wave structure below:
sites 2–4 are three shared files, so parallel authors collide there.

## Implementation waves (partitioned to avoid the shared-file collision)

Each wave: write handlers → **coordinator** batches the 4-site registration →
build (ninja arm64 `build/` for dev speed) → verify against a running client.

- **Wave 0 — shared infra:** `awaitRequest` helper + a `resolveChannel`/admin-rights
  JSON→`ChatAdminRights` mapper. Nothing ships without this.
- **Wave 1 — welcome-messages (G1) + `get_chat_info` decode (G5):** the headline
  229 feature; one domain, one handler file (`mcp_ephemeral`/`mcp_archive`).
- **Wave 2 — admin (G2) + forward-from-ephemeral (G8):** `channels.editAdmin` +
  the forward flag; unlocks stories/direct-messages/linked-peers too.
- **Wave 3 — resale buy (G3) + ephemeral callback (G6) + ephemeral edit (G7).**
- **Wave 4 — composition: reply-markup (G9) + Instant-View (G10) + draft
  streaming (G4).**

Handlers within a wave live in **different** domain files, so they can be written
in parallel (leashed-swarm, per the standing rule); the **coordinator alone**
edits the three shared registration files, once per wave, eliminating the collision.

## Verification (per wave — no tool declared done on a submit)

Build → launch the client with an **isolated `-workdir`** + real signed-in session
(publishing-style discovery), then over the MCP socket: `tools/list` shows the new
names, and `tools/call` each new tool and assert the **real TL response** comes
back (not `"submitted"`). For mutating tools (editAdmin, buy, delete-welcome) test
against the test peer `768828198` where safe. `check_mcp_tools.py` must stay green.

## Definition of done

All 10 items backed by a real TL call that **returns the server's actual answer**;
four declaration sites agree; `check_mcp_tools.py` green; each verified live. The
352-tool surface grows to ~358–360 real tools with **zero new `Unimplemented`
stubs and zero fire-and-forget dishonesty.**

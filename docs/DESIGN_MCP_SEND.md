# Design: the maximal MCP send surface (POSD / Design-It-Twice)

## Decision date
2026-09-02

## What we're building
Revamp the Tlgrm MCP **send** tools from minimal plain-text emitters to the full
expressive power the client actually has: `send_message` and `send_document` gain
complete text formatting (raw `entities` — the precise maximum — plus a
`parse_mode` markdown/HTML convenience), and the full send-option surface
(`reply_to`, `silent`, `schedule_date`, link-preview control, `spoiler`); and a
new `send_rich_message` posts structured article content over
`Iv::SerializeInputRichMessage`. The complexity — entity parsing/validation and
MTProto `SendOptions`/`SendAction`/`MessageToSend` construction — is pulled down
behind a small, deep helper.

## Requirements (Phase 1, extracted)
- **Does:** express any user-sendable message (formatted text, formatted-caption
  media, structured rich article) through the client, honestly.
- **Callers:** MCP agents over the tool socket.
- **Receives:** `chat_id`; `text`/`caption`; formatting (`entities[]` OR
  `parse_mode`); options (`reply_to_message_id`, `silent`, `schedule_date`,
  `link_preview`, `spoiler`); `file_path` (document); `blocks[]` (rich).
- **Returns:** the real outcome — the applied entity count + queue status (send is
  async; "queued" is what actually happened), or the real error.
- **Hardest decisions:** (a) markdown/HTML → validated MTProto entities without
  reinventing the parser; (b) three send *kinds* vs one tool; (c) honest result
  from an async send.
- **Hidden:** entity JSON→`EntityInText`→MTP mapping + offset validation; the
  `SendOptions`/`SendAction`/`MessageToSend`/`WebPageDraft` construction; the
  `RichPage`→`SerializeInputRichMessage` path; async upload.
- **Errors:** entity offset/length out of range; unknown entity type; unparseable
  markdown; chat/file not found; file too large; rich block hard-fail; **bot-only
  features (`reply_markup` inline keyboards) on a user session — surfaced, not faked.**

## Design A — three focused tools over a shared deep formatting/options core
- **`resolveFormatting(text, parse_mode?, entities?) → TextWithEntities`** — the
  deep core. Accepts raw entities (validated, full power) or parses
  markdown/HTML, always runs auto-link detection, clamps/rejects bad offsets.
- **`buildSendOptions(args) → {SendOptions, FullReplyTo, WebPageDraft}`** — maps
  silent/schedule/spoiler/reply/link-preview to the MTProto option structs.
- `toolSendMessage`, `toolSendDocument`, `toolSendRichMessage` — each thin: parse
  its own distinct inputs, call the two helpers, hand off to `api()`.

## Design B — one unified `send(kind, …)` tool
A single polymorphic tool with a `kind` ∈ {text, document, rich} and the union of
all params. One entry point; dispatches internally.
*Weakness:* the param union is mode-dependent (`file_path` only for document,
`blocks` only for rich) → configuration proliferation + unknown-unknowns; the
caller must know which params apply to which kind. A shallow-wide interface.

## Module Depth Test
| Criterion | A: `resolveFormatting` core | B: unified `send` |
|---|---|---|
| Interface simplicity | 2 (text + one formatting choice) | 0 (mode-dependent param maze) |
| Implementation power | 2 (all entity/markdown work) | 2 |
| Information hiding | 2 (all MTProto entity detail) | 1 (leaks mode coupling) |
| Error absorption | 2 (offsets clamped, types validated) | 1 |
| Generality | 2 (every send kind reuses it) | 1 (special-cased by kind) |
| **Total** | **10 (deep)** | **5 (shallow-wide)** |

## Phase 5 (key principle verdicts)
- **#4 Deep modules:** A puts the depth in `resolveFormatting` (small interface,
  all entity power). B's tool interface ≈ its implementation complexity — shallow.
  **A STRONG / B WEAK.**
- **#9 Better together/apart:** the three send *kinds* have genuinely different
  inputs (string vs file vs blocks) → keep apart (obvious); formatting + options
  are shared → pull together into helpers. A honors both; B forces unlike things
  together. **A STRONG / B WEAK.**
- **#10 Define errors out of existence:** A validates entities once, centrally
  (bad offsets clamped, unknown types rejected with the real reason) so no tool
  re-checks. **A STRONG.**
- **#14 Naming / #16 Obviousness:** `send_message`/`send_document`/`send_rich_message`
  each read exactly as they do; B's `send(kind=…)` hides intent behind a discriminator.
  **A STRONG / B WEAK.**

## Chosen design: A — three tools + a deep formatting/options core
**Why:** the depth belongs in the *formatting*, not the *dispatch*. A concentrates
all entity/markdown/option complexity in two shared, deeply-tested helpers and
keeps each tool obvious; B spreads a mode-dependent param union across one
shallow interface. The `entities[]` path gives callers the exact MTProto maximum
(every entity type, incl. `custom_emoji`, `pre` with language, `blockquote`,
`spoiler`), and `parse_mode` is convenience layered on the same core.

**Tradeoffs accepted:** three tool registrations instead of one (mechanical); a
caller wanting to switch kinds calls a different tool (but that is the honest
shape of the operation).

**Red flags cleared:** config-proliferation (B's union) — avoided; shallow module
(B) — avoided; special-case-heavy (per-kind validation) — centralized in the core.

**Remaining risk:** `reply_markup` inline keyboards are **bot-only**; a user
session cannot attach them. The tools accept the param but **honestly report it
as unsupported on a user account** rather than silently dropping it (governing
rule). Rich-block coverage starts with the core block kinds (heading, paragraph,
blockquote, code, divider, media) and grows; unsupported blocks fail with the
real reason, never a fabricated success.

## Interface comments (Phase 7 — the deep core)
```
// Resolve a caller's text + formatting choice into a validated TextWithEntities
// ready for the send path. The caller supplies EITHER a precise `entities` array
// (exact type/offset/length — the full MTProto surface) OR a `parse_mode`
// ("markdown"/"html") to parse from the text; auto-links/mentions are always
// detected. Callers never see EntityType, offset clamping, or MTP mapping.
// Returns the text with a validated, in-range entity set; entities that fall
// outside the text are dropped (defined out of existence), unknown types error.
TextWithEntities resolveFormatting(text, parse_mode?, entities[]?);

// Map the caller's send options to the client's MTProto option structs. Hides
// SendOptions/FullReplyTo/WebPageDraft. reply_markup is accepted but, on a user
// session, reported unsupported rather than dropped.
SendResolution buildSend(history, args);  // {SendAction, WebPageDraft, notes[]}
```

## Information hiding map
| Module | Hidden decision |
|---|---|
| `resolveFormatting` | entity JSON↔`EntityInText`↔MTP, markdown/HTML parsing, offset validation |
| `buildSend` | `SendOptions`/`SendAction`/`WebPageDraft` construction, reply resolution |
| `toolSendRichMessage` | `RichPage` assembly + `SerializeInputRichMessage` (Draft/FinalSubmit, block hard-fails) |
| the three tools | that any of the above exists — each takes plain JSON |

## Temporal-decomposition watch points
Do not split `resolveFormatting` into parse→validate→map stages exposed to
callers; it is one information-hiding unit. Do not order the tools by "prepare /
send / confirm" — structure is by *what* (kind of message), not *when*.

# What's New in 7.0.6 — and What It Means for the Tlgrm MCP Surface

**Base:** Telegram Desktop v6.5.1 (`6b0fc1bef6`, MTProto **layer 222**)
**Target:** Telegram Desktop v7.0.6 (`fccb2672b0`, MTProto **layer 228**)
**Our fork:** Tlgrm 6.9.6 (`AppVersion 6009006`) — 80 commits on the 6.5.1 base, still at layer 222
**Date:** 2026-07-29

## How this document was verified

Every TL constructor below was extracted by diffing the two schemes directly:

```
OLD=<worktree aus-wip-6.9.6>/Telegram/SourceFiles/mtproto/scheme/api.tl   # layer 222
NEW=<upgrade-v7.0.6>/Telegram/SourceFiles/mtproto/scheme/api.tl           # layer 228
comm -13 <(rg '= RichText;' $OLD | sed 's/#.*//' | sort) \
         <(rg '= RichText;' $NEW | sed 's/#.*//' | sort)
```

Constructor names carry their CRC hashes so any claim here is checkable in one grep. Where a C++ wrapper is named it was located in the 7.0.6 tree; where no wrapper is cited, **the MTProto method is the only verified entry point and the C++ path is still to be confirmed.** Claims that could not be verified are marked `[unverified]` rather than smoothed over.

Three corrections to earlier analysis in this project, recorded because they change conclusions:

1. The layer delta is **222 → 228**, not 223 → 229.
2. `channels.editCreator` was **not removed** — it moved namespace to `messages.editChatCreator#`. Group-ownership transfer is therefore implementable, contrary to an earlier report.
3. Message effects (`effect:flags.18?long` on `messages.sendMessage`) are **not new in 228** — they already exist at layer 222. Our fork can use them today.

---

## 1. The delta in numbers

| | |
|---|---|
| Commits | 2,854 |
| Files touched | 1,980 (`+284,741 / −34,654`) |
| New files | 425 |
| TL names | 2,295 → 2,449 — **156 new, 2 moved, 68 modified** |
| New TL methods | 54, across **three brand-new namespaces** |
| New TL types | 102 |
| Largest subsystem | `iv/` (Instant View / markdown / rich messages): 13 → 105 files, **+87,144 lines ≈ 30% of all new code** |

The headline for us is not the volume — it's that **the single largest new subsystem is a new *message content type*.** Rich messages are not a viewer feature; they are sendable, editable, draftable, translatable, and exportable content. That makes them the highest-value MCP target in the release.

---

## 2. New API namespaces

### `communities.*` — 9 methods
`create`, `getJoinedCommunities`, `getParticipantJoinedChats`, `getPeerLinkRequests`, `togglePeerLink`, `togglePeerLinkRequestApproval`, `toggleAllPeerLinkRequestApproval`, `toggleParticipantBanned`, `toggleCommunityCollapsedInDialogs`

New types: `community`, `communityFull`, `communityPeer`, `dialogCommunity`. `messages.searchGlobal` gained a `community` parameter.

### `ephemeral.*` — 4 methods
```
ephemeral.sendMessage#68cbd09f flags:# peer:InputPeer receiver_id:InputUser
    query_id:flags.0?long message:string entities:flags.1?Vector<MessageEntity>
    media:flags.2?InputMedia reply_markup:flags.3?ReplyMarkup
    rich_message:flags.4?InputRichMessage random_id:long
    reply_to:flags.5?InputReplyTo = Updates;
ephemeral.deleteMessage#a3c0d511 peer:InputPeer receiver_id:InputUser id:int = Bool;
ephemeral.reportMessage#8704f2bf peer:InputPeer id:int option:bytes message:string = ReportResult;
ephemeral.getCallbackAnswer#3fa464c8 flags:# peer:InputPeer id:int data:flags.1?bytes = messages.BotCallbackAnswer;
```
Note `receiver_id:InputUser` — an ephemeral message is addressed to **one recipient inside a shared chat**. It is visible only to them. New type `EphemeralMessage`, plus `data/components/ephemeral_messages.*` (2 files, +915).

### `aicompose.*` — 7 methods
`createTone`, `deleteTone`, `getTone`, `getToneExample`, `getTones`, `saveTone`, `updateTone` — a server-side registry of named writing styles, shareable by deep link. New `AiComposeTone` family.

### New `messages.*` methods — 20
`getRichMessage`, `translateRichMessage`, `composeMessageWithAI`, `composeRichMessageWithAI`, `addPollAnswer`, `deletePollAnswer`, `getUnreadPollVotes`, `readPollVotes`, `deleteParticipantReaction`, `deleteParticipantReactions`, `editChatCreator`, `editChatParticipantRank`, `getFutureChatCreatorAfterLeave`, `getPersonalChannelHistory`, `reportReadMetrics`, `reportMusicListen`, `requestChatJoinWebView`, `setBotGuestChatResult`, `checkUrlAuthMatchCode`, `declineUrlAuth`

### New `bots.*` methods — 8
`createBot`, `checkUsername`, `exportBotToken`, `getAccessSettings`, `editAccessSettings`, `requestWebViewButton`, `getRequestedWebViewButton`, `setJoinChatResults`

---

## 3. Feature → use case → MCP design

### 3.1 Rich messages — the flagship

**What it is.** A message can now carry a structured document instead of (or alongside) plain text: headings, tables, math, collapsible sections, blockquote groups, collages, slideshows, and media, rendered natively by the client.

**Three authoring routes, all sender-side:**

```
inputRichMessage#e4c449fc      flags:# rtl:flags.0?true noautolink:flags.1?true
    blocks:Vector<PageBlock> photos:flags.2?Vector<InputPhoto>
    documents:flags.3?Vector<InputDocument> users:flags.4?Vector<InputUser>
inputRichMessageHTML#dacb836a  flags:# rtl noautolink html:string
    files:flags.2?Vector<InputRichFile>
inputRichMessageMarkdown#4b572c flags:# rtl noautolink markdown:string
    files:flags.2?Vector<InputRichFile>
```

Delivered through the ordinary send path — `messages.sendMessage` gained `rich_message:flags.23?InputRichMessage`. Also on `messages.saveDraft`, `draftMessage`, and `ephemeral.sendMessage`.

**CORRECTION (verified against the 7.0.6 client).** An earlier draft of this document claimed the markdown/HTML routes make this trivial because `inputRichFilePhoto#9b00622b id:string photo:InputPhoto` lets markdown reference attachments by name. **That claim was speculation and is not supported by the client.** `MTP_inputRichMessageMarkdown`, `MTP_inputRichMessageHTML`, `InputRichFile`, `inputRichFilePhoto` and `inputRichFileDocument` are **never constructed anywhere in the tdesktop repo** — the only occurrences are the TL scheme lines themselves (`api.tl:2234-2235`) plus generated scheme output. The convention for referencing an `InputRichFile.id` from a markdown body is therefore **undiscoverable from this corpus**. Those constructors may well work server-side, but we would be guessing at an undocumented contract.

**The actual sanctioned path — and it is still good.** There is exactly one place in the entire repo that builds a rich message:

```
Iv::SerializeInputRichMessage(session, page, mode)
    iv/iv_rich_message_serializer.cpp:1701   (the sole MTP_inputRichMessage construction site, :1754)
```

It takes a `RichPage` model and emits `inputRichMessage` with `blocks:Vector<PageBlock>`, auto-collecting `photos`/`documents`/`users` via `CollectPhoto`/`CollectDocument`/`CollectMentionUser`, and auto-detecting RTL via `DetermineRichPageRtl`. The caller supplies none of those side tables.

And markdown authoring still works — via a **local importer**, not the TL route. Its dialect is concrete and verifiable:

- cmark-gfm options (`iv/markdown/iv_markdown_parse_finalize.cpp:755-758`): `CMARK_OPT_DEFAULT | SOURCEPOS | FOOTNOTES | STRIKETHROUGH_DOUBLE_TILDE`
- exactly 5 mandatory extensions, a missing one aborts the parse (`iv_markdown_parse_convert.cpp:629-635`): `table`, `strikethrough`, `autolink`, `tagfilter`, `tasklist` — note the `footnote` *extension* is not enabled; footnotes come from the core option
- a non-cmark math pass, `ExtractMathRegions` (`iv_markdown_parse_finalize.cpp:790`), lifting `$…$` / `$$…$$` before conversion
- import limits (`iv_markdown_parse.cpp:16-23`): 4 MiB source, 100,000 cmark nodes, nesting 128, 64 KiB per formula, 10,000 formulas

So the shape of the tool is unchanged — **markdown in, rich message out** — but the route is `markdown → RichPage → Iv::SerializeInputRichMessage`, entirely inside the client, with no guessing about server contracts.

**Use cases**
- Post a formatted report — headings, a real table, a formula — as one message instead of a screenshot or a wall of asterisks.
- Convert our existing export output (already markdown/HTML capable) back *into* Telegram as a rich message.
- Long-form answers from an assistant, with structure preserved rather than flattened.
- Math and code with syntax highlighting rendered natively rather than as images.

**Proposed tools**

| Tool | Args | Backing |
|---|---|---|
| `send_rich_message` | `peer`, `markdown` \| `html`, `files[]`, `rtl?`, `reply_to?`, `silent?`, `schedule_date?` | `messages.sendMessage` + `inputRichMessageMarkdown` / `…HTML` |
| `edit_rich_message` | `peer`, `message_id`, `markdown` \| `html`, `files[]` | `messages.editMessage#b106e66c` — carries `rich_message:flags.23?InputRichMessage` (verified) |
| `get_rich_message` | `peer`, `message_id` | `messages.getRichMessage#501569cf` |
| `save_rich_draft` | `peer`, `markdown`, `files[]` | `messages.saveDraft` + `rich_message:flags.9` |
| `translate_rich_message` | `peer`, `ids[]` \| `text[]`, `to_lang`, `tone?` | `messages.translateRichMessage#1a542004` |

**Rationale.** Markdown is the native currency of an MCP client — this is the one place where the impedance match between an LLM and Telegram is essentially perfect. One tool unlocks the entire 87k-line subsystem. Start with `send_rich_message`; the block-vector route (`inputRichMessage` with `Vector<PageBlock>`) is only needed for programmatic construction and can wait.

**Gating.** Rich formatting is premium-gated in the UI (14 upstream commits add the gate, a send-lock badge, and a "send without formatting" fallback). The tool must surface that honestly — detect and report, never silently downgrade.

### 3.2 Communities

**What it is.** A named grouping of up to ~20 chats presented as one chat-list row, with join requests, hidden member-only chats, admins who need not be members, and a community-scoped search.

**Use cases**
- Enumerate an org's chat topology in one call instead of walking the dialog list.
- Triage join requests in bulk — the reason `toggleAllPeerLinkRequestApproval` exists.
- Search scoped to a community rather than globally.

**Proposed tools**

| Tool | Backing |
|---|---|
| `list_communities` | `communities.getJoinedCommunities` |
| `get_community_chats` | `communities.getParticipantJoinedChats` |
| `list_community_join_requests` | `communities.getPeerLinkRequests` |
| `approve_community_request` / `decline_community_request` | `communities.togglePeerLinkRequestApproval` |
| `approve_all_community_requests` | `communities.toggleAllPeerLinkRequestApproval` |
| `add_chat_to_community` / `remove_chat_from_community` | `communities.togglePeerLink` |
| `ban_community_participant` | `communities.toggleParticipantBanned` |
| `search_in_community` | `messages.searchGlobal` + new `community` param |

**Rationale.** This is bulk administrative work over a list — precisely what an agent is good at and a human is slow at. Approval is also the highest-risk category here: `approve_all` acts on people. Both should require an explicit confirmation argument, and the tool must return the full list of affected users, not a count.

### 3.3 Ephemeral / invisible bot messages

**What it is.** A message in a shared chat visible to exactly one recipient (`receiver_id:InputUser`), unstored and unforwardable, with callback buttons.

**Use cases**
- Answer one person in a busy group without noise for everyone else.
- Per-user prompts, confirmations, and menus inside a shared chat.
- Free in paid-message chats (an upstream carve-out), which makes it the cheap channel for interactive flows.

**Proposed tools:** `send_ephemeral_message` (with optional `rich_message`), `delete_ephemeral_message`, `answer_ephemeral_callback`.

**Rationale.** Upstream restricts these deliberately — immediate text only, no replying to one's own, polls and multi-file blocked. Our tools should mirror those constraints as validation errors *before* the request, so a caller learns the rule from the tool rather than from a server error.

### 3.4 AI compose and cloud tones

**What it is.** Server-side text transformation — proofread, emojify, translate, restyle — with named, shareable tones.

```
messages.composeMessageWithAI#daecc589 flags:# proofread:flags.0?true
    emojify:flags.3?true text:TextWithEntities translate_to_lang:flags.1?string
    tone:flags.2?InputAiComposeTone = messages.ComposedMessageWithAI;
messages.composeRichMessageWithAI#8d7ae6af flags:# proofread emojify
    text:flags.4?InputRichMessage translate_to_lang:flags.1?string
    tone:flags.2?InputAiComposeTone = messages.ComposedRichMessageWithAI;
```

**Use cases:** proofread before sending; enforce a house style across a team via one shared tone; translate while preserving rich structure.

**Proposed tools:** `compose_with_ai`, `compose_rich_with_ai`, `list_ai_tones`, `create_ai_tone`, `update_ai_tone`, `delete_ai_tone`, `get_ai_tone_example`.

**Rationale — and the honest note.** An MCP client is itself an LLM; asking Telegram's server to rewrite text is usually redundant. What is *not* redundant is **tone management** (`aicompose.*`): tones are shared team assets with deep links, and managing them is real, non-duplicative work. Prioritise the tone CRUD; treat `composeMessageWithAI` as low priority. This is the `ponytail` rung-2 test applied honestly — where the capability already exists on our side of the wire, we should not wrap it just because an endpoint exists.

### 3.5 Enhanced polls

New: `messages.addPollAnswer`, `deletePollAnswer`, `getUnreadPollVotes`, `readPollVotes`, `stats.getPollStats`. `poll` and `pollAnswer` constructors both changed. Options can carry media (photos, stickers, video, audio, **geo-location**); polls gained descriptions, shuffling, revote control, deadlines, subscriber-only and country-based restrictions.

**Use cases:** create a rich poll from structured input; harvest results with statistics; monitor unread votes as an event stream.

**Proposed tools:** `create_rich_poll`, `add_poll_option`, `remove_poll_option`, `get_poll_stats`, `get_unread_poll_votes`, `mark_poll_votes_read`.

**Rationale.** `pollAnswer` changing shape means **any existing fork code that constructs a poll positionally will break at compile time on 7.0.6.** That is a port task, not just an opportunity — audit it before adding anything new.

### 3.6 Member tags, ranks, and ownership transfer

- `messages.editChatParticipantRank` — set a visible member tag
- `messages.editChatCreator` — **this is the relocated `channels.editCreator`**
- `messages.getFutureChatCreatorAfterLeave` — who inherits if the owner leaves

**This retires a known gap.** `transfer_group_ownership` is currently the fork's single honest "not implemented" tool. It is implementable on 7.0.6 — the method simply moved namespace. `messages.editChatCreator` requires 2FA password confirmation `[unverified: confirm the password parameter shape]`, so the tool must accept a password argument and fail cleanly without it rather than pretending.

**Proposed tools:** `set_member_tag`, `get_member_tags`, `transfer_chat_ownership`, `get_future_owner`.

### 3.7 Bot creation and management

`bots.createBot`, `checkUsername`, `exportBotToken`, `getAccessSettings`, `editAccessSettings`, `setJoinChatResults`.

**Use case:** create and configure a bot from a conversation instead of a chat with BotFather.

**Rationale, and a hard boundary.** `exportBotToken` returns a **credential**. It must never land in a tool result that gets logged, cached, or echoed into a transcript. Either omit this tool entirely, or return a redacted acknowledgement and write the token to a file with `0600`. Our audit logger currently records tool results — that path must be excluded before this tool exists at all. Note also that the bot-framework UI trio already in the fork (725 lines, `settings_bots.*`, `bot_config_box.*`, `bot_statistics_widget.*`) is **entirely unreachable dead code**; the correct move is to delete it and build against these real APIs instead.

### 3.8 Translation providers

New `lib_translate` submodule; `messages.translateText` (pre-existing) plus provider abstraction — macOS system translation, Crow Translate on Linux, custom URL templates, and whole-chat translation with parallel batches.

**Use cases:** translate a chat's history for analysis; translate outbound messages; on-device translation with no third-party egress.

**Proposed tools:** `translate_messages` (replacing the current stub), `translate_chat_history`, `list_translation_providers`, `set_translation_provider`.

**Note:** on-device translation is why 7.0.6 requires a **Swift 6 toolchain** on macOS (escape hatch: `DESKTOP_APP_DISABLE_SWIFT6`). If we disable Swift 6 to get the build green, the system-translation provider is gone and these tools must report that rather than fail obscurely.

### 3.9 Reaction moderation

`messages.deleteParticipantReaction`, `deleteParticipantReactions` — remove a specific person's reactions; plus group-level reaction restriction and **multiple favourite reactions**.

**Use case:** moderation cleanup without deleting messages.

**Proposed tools:** `delete_participant_reaction`, `delete_all_participant_reactions`, `set_favorite_reactions`, `restrict_group_reactions`.

### 3.10 Live photos

```
inputMediaUploadedPhoto#7d8375da flags:# spoiler:flags.2?true live_photo:flags.3?true
    file:InputFile stickers:flags.0?Vector<InputDocument> ttl_seconds:flags.1?int
    video:flags.3?InputDocument = InputMedia;
```
(Layer 222 had neither `live_photo` nor `video` — both constructors changed hash.)

A still image plus an attached motion video, sent as one unit. **Any fork code positionally constructing `inputMediaUploadedPhoto` breaks on 7.0.6.**

### 3.11 Metrics and personal channels

`messages.getPersonalChannelHistory`, `reportReadMetrics`, `reportMusicListen`.

`getPersonalChannelHistory` is worth a tool (`get_personal_channel_history`). The two `report*` methods are telemetry the client emits — **we should not wrap them.** Wrapping a telemetry emitter as an agent-callable tool means an agent can forge engagement data. Deliberate omission, recorded here so the decision is visible rather than an oversight.

---

## 4. What is now expressible visually — the animation question

The question was what animation effects become possible with rich text, custom stickers, custom emoji, and custom images. The important distinction is **sender-controllable** (we can produce it from a tool) versus **client-side only** (the recipient's client decides). Only the first column is actionable for us.

### 4.1 Animated custom emoji inside rich text — new

```
textCustomEmoji#a26156c0 document_id:long alt:string = RichText;
```

Before 228, custom emoji in text required a `messageEntityCustomEmoji` offset/length span over plain text. Now custom emoji are a **first-class node in the rich-text tree**, meaning an animated `.tgs` emoji can sit inside a heading, a table cell, a blockquote, a math caption, or a collapsible section's title. Combined with `pageBlockDetails` (below), you can put a spinning animated glyph in a section header that animates on expand.

Practical consequence: a status document whose section headers carry live animated state icons, updated by editing the message.

### 4.2 The `pageBlockThinking` block — new, and the most interesting one

```
pageBlockThinking#3c29a3e2 text:RichText = PageBlock;
```

Upstream added this for streaming bot replies: it renders as an **animated "thinking" block**. It is a plain `PageBlock`, so it goes straight into `inputRichMessage.blocks` — **we can send it.**

Combined with the gradient-reveal text animation that upstream built for gradual bot-reply streaming (6.7.5 is entirely that pipeline), this gives a genuinely novel sender-side pattern:

> send a rich message containing `pageBlockThinking` → progressively **edit** the message, replacing the thinking block with real content → the client animates each reveal natively.

**The edit half is confirmed:** `messages.editMessage#b106e66c` carries `rich_message:flags.23?InputRichMessage`, and `api/api_editing.cpp:93` sets `f_rich_message` conditionally on `withRichMessage`. So progressive editing works.

**The thinking-block half is subtler than it first appeared.** The serializer *does* emit `MTP_pageBlockThinking` (`iv_rich_message_serializer.cpp:1625`) from `BlockKind::Thinking` — but the only thing that ever *produces* that block kind is the **receive parser** (`iv/iv_rich_page.cpp:1218-1223`), and the editor explicitly refuses to create or edit one: `CanEditBlock` (`iv/editor/iv_editor_state.cpp:1464`) lists `BlockKind::Thinking` among its `return false` kinds, and there is no toolbar or insert action anywhere in `iv/editor/`. In stock behaviour the client emits a thinking block only when re-sending a page that already contained one.

**For us that is a distinction without a blocker:** we are writing C++ inside the client, not driving its editor. Constructing a `RichPage` containing a `Thinking` block programmatically and handing it to `Iv::SerializeInputRichMessage` will serialize it, because the serializer keys off `BlockKind`, not off editor provenance. Two caveats, both honest:

- **Server-side acceptance is unverified.** `pageBlockThinking#3c29a3e2 text:RichText` carries no flags, and there is no bot check anywhere on the desktop send path — but any restriction would live server-side and nothing in this corpus proves it is open to ordinary accounts. This needs one live test.
- If it turns out to be bot-only or rejected, the pattern degrades gracefully: progressive editing of ordinary blocks still animates the reveal. You lose the thinking indicator, not the effect.

### 4.3 Autoplaying looping video inside a document — pre-existing block, newly reachable

```
pageBlockVideo#7c8fe7b6 flags:# autoplay:flags.0?true loop:flags.1?true
    spoiler:flags.2?true video_id:long caption:PageCaption = PageBlock;
```

`autoplay` + `loop` + a caption, embedded mid-document. Effectively an inline GIF-style animation as a *block* in a structured message — a short screen recording under a heading, looping, with formatted caption. Previously this markup existed only for Instant View pages we could not author; now it is reachable through `inputRichMessage`.

### 4.4 Slideshows and collages with real transition animation

```
pageBlockSlideshow#31f9590 items:Vector<PageBlock> caption:PageCaption = PageBlock;
pageBlockCollage#65a0fa4d  items:Vector<PageBlock> caption:PageCaption = PageBlock;
```

7.0.5 added preload, easing animation, and horizontal-scroll/swipe slide switching; 7.0.6 added touch support with hit-testing at the touch position. So a slideshow we author gets navigation dots, eased transitions, and swipe — for free. `items` is `Vector<PageBlock>`, so slides can be **photos, autoplaying videos, or nested blocks**, not just images. A frame-by-frame animation built from photo slides is a legitimate construction.

### 4.5 Collapsible sections that animate

```
pageBlockDetails#76768bed flags:# open:flags.0?true blocks:Vector<PageBlock> title:RichText = PageBlock;
```

`title` is `RichText`, so it can contain `textCustomEmoji` — an animated glyph in the summary line. Expand/collapse is animated by the client. Nested `pageBlockDetails` gives progressive disclosure: a long report that opens one layer at a time.

### 4.6 Spoilers as an animated reveal

```
textSpoiler#4c2a5d62 text:RichText = RichText;
```
plus `spoiler` flags on `pageBlockPhoto`, `pageBlockVideo`, and `inputMediaPhoto`. Telegram renders spoilers as an animated particle cover that dissolves on tap. In rich text, spoiler is now a **tree node**, so a whole formatted subtree — a table, a formula, a nested block — can hide behind an animated cover. Quiz-and-reveal documents become natural.

### 4.7 Live-updating timestamps

```
textDate#a5b45e2b flags:# relative:flags.0?true short_time:flags.1?true
    long_time:flags.2?true short_date:flags.3?true long_date:flags.4?true
    day_of_week:flags.5?true text:RichText date:int = RichText;
messageEntityFormattedDate#904ac7c7  (same flag set, offset/length/date)
```

With `relative`, the client renders "3 minutes ago" and **keeps it current**. This is text that changes without an edit — the cheapest possible live element. A status message whose timestamps stay fresh forever, sent once.

### 4.8 Rendered diffs

```
textDiff#9686cb50 text:RichText old_text:RichText = RichText;
messageEntityDiffInsert#71777116 / messageEntityDiffDelete#652c1c5 /
messageEntityDiffReplace#c6c1e5a7 offset:int length:int old_text:string
```

Upstream uses these for the AI editor's coloured diffs.

**Correction: `textDiff` is never emitted by the sender** — `SerializeRichTextEntity` (`iv_rich_message_serializer.cpp:406-551`) has no branch producing it, so it is parse-only in stock behaviour. The three `messageEntityDiff*` entities are plain message entities and *are* usable on ordinary text. Authoring a `textDiff` node inside a rich message would require our own construction path outside the serializer's entity mapping, and server acceptance is unverified. Treat the entity route as available and the rich-text node as research.

### 4.9 Math typeset natively

```
textMath#9d2eac97   source:string = RichText;
pageBlockMath#59080c20 source:string = PageBlock;
```

LaTeX rendered by MicroTeX, inline or as a block. Not animation, but it removes the "render to PNG and upload" dance entirely.

### 4.10 Message effects — available now, not new

```
availableEffect#93c3e27e flags:# premium_required:flags.2?true id:long
    emoticon:string static_icon_id:flags.0?long effect_sticker_id:long
    effect_animation_id:flags.1?long = AvailableEffect;
messages.getAvailableEffects#dea20a39 hash:int = messages.AvailableEffects;
```

`effect:flags.18?long` is on `sendMessage`, `sendMedia`, `forwardMessages`, and `saveDraft` — **and it was already there at layer 222.** A full-screen animation on message arrival is available to our fork *today*, before any upgrade. `premium_required` must be checked per effect.

Proposed now, independent of the port: `list_message_effects`, and an `effect_id` argument on every send tool.

### 4.11 Live photos as motion stills — new
See §3.10. A still that animates into a short video on interaction, sent as one media unit.

### 4.12 Client-side only — do not promise these

The Thanos dissolution on delete (85 commits, compute-shader particles), the 3D premium covers (star/coin/diamond meshes), one-time-voice playback particles, and the birthday profile effect are all **rendered by the recipient's client from local assets**. A tool can *trigger* Thanos by deleting a message, but cannot author or vary it. Listing these as MCP capabilities would be a lie of exactly the kind that produced 186 stub tools.

### 4.13 Composition summary

| Effect | Sender-controllable? | Primitive |
|---|---|---|
| Animated custom emoji in structured text | Yes — new | `textCustomEmoji` |
| Native streaming "thinking" reveal | Yes — new | `pageBlockThinking` + progressive edit |
| Inline autoplay/loop video block | Yes — newly reachable | `pageBlockVideo` `autoplay` `loop` |
| Eased/swipeable slideshow | Yes — newly reachable | `pageBlockSlideshow` |
| Animated collapse/expand | Yes — newly reachable | `pageBlockDetails` |
| Animated spoiler over a subtree | Yes — new | `textSpoiler` |
| Self-updating relative timestamps | Yes — new | `textDate relative` |
| Coloured diff rendering | Yes — new | `textDiff` + diff entities |
| Full-screen arrival effect | Yes — **already available at 222** | `sendMessage effect` |
| Motion-still live photo | Yes — new | `live_photo` + `video` |
| Thanos dissolve, 3D covers, voice particles | **No** | client-local assets |

---

## 4bis. The authoring contract — what is provably sendable

Verified by reading `Iv::SerializeInputRichMessage` and its callees end to end.

**21 block kinds are emitted** (`iv_rich_message_serializer.cpp:1208-1655`): headings 1-6, paragraph, footer, divider, anchor, pullquote, blockquote, blockquote-with-children, ordered list, unordered list (both with per-item text-or-blocks and checkbox/checked), photo, video (autoplay/loop/spoiler), audio, math, table (bordered/striped + rows/cells), details (open), map (`inputPageBlockMap`, zoom > 0 required), preformatted code with language, thinking, slideshow, collage.

**Six kinds hard-fail the entire message** (`:1647-1653` → `FailedSerializeBlock()`): `Unsupported`, `AuthorDate`, `Embed`, `EmbedPost`, `Channel`, `RelatedArticles`. There is no `pageBlockTitle`, `Subtitle`, `Kicker`, or `Cover` path, and `pageBlockMap` is never sent — only the input variant.

**Failure is all-or-nothing.** `SerializeBlocks` (`:1659`) returns `std::nullopt` on the first bad block, dropping the whole message. Any tool must validate before submitting, not hope.

**Inline nodes the sender can emit** (`:425-551`): `textBold, textItalic, textUnderline, textStrike, textFixed, textSubscript, textSuperscript, textMarked, textSpoiler, textMention, textHashtag, textBotCommand, textCashtag, textAutoUrl, textAutoEmail, textAutoPhone, textBankCard, textEmail, textPhone, textUrl, textMentionName, textMath, textCustomEmoji, textDate` (with all six format flags), plus `textAnchor`, `textConcat`, `textPlain`. Nodes that silently degrade to their inner text: `Semibold, MediaTimestamp, Colorized, Pre, Blockquote, Invalid`.

**`textCustomEmoji` is not flattened anywhere.** One rich-text serializer (`SerializeRichTextWithAnchor`, `:615`) serves every block with no context restriction — headings (`:1214`), details title (`:1570`), table title (`:1554`), footer, code, thinking, quote text and captions, list items. An animated custom emoji in a heading or a collapsible section's title is confirmed. If the document can't be collected it degrades to plain text rather than failing.

**Limits — resolve from appConfig, never hardcode.** `RichMessageLimits` (`iv/iv_rich_page.h:244`) defaults: length 32,768 chars · 500 blocks · depth 16 · 50 media · 20 table columns. Overridden by `ResolveRichMessageLimits` (`iv_rich_page.cpp:2308`) from keys `rich_message_length_limit`, `rich_message_max_blocks`, `rich_message_max_depth`, `rich_message_max_media`, `rich_message_max_table_cols`. `ValidateRichMessage` (`:2329`) returns a typed `RichMessageLimitError`.

**Two premium gates, both client-side** (`iv/editor/iv_editor_session.cpp`):
1. `RichMessagePostingMode` (`:250-260`) reads appConfig `rich_message_posting` — `"enabled"` / `"premium"` / default **`"disabled"`**.
2. `CanUseRichMessages` (`:240-242`) is literally `return session->premium();`

The send gate (`:1046-1061`): if not permitted and the page is **not flatten-safe**, the send is **refused outright**. A page is not flatten-safe if any block, recursively, is `Table, Math, Photo, Video, Audio, GroupedMedia, or Map` (`iv_rich_page.cpp:2545`, `:2233`). Otherwise the user is offered "send without formatting". Error surface: `lng_article_premium_required` (`lang.strings:7443-7444`). **A tool that ignores this will silently get `false` back.**

Send plumbing: `f_rich_message` set unconditionally on send (`apiwrap.cpp:4424`), conditionally on edit (`api/api_editing.cpp:93`), and on draft (`apiwrap.cpp:2380`).

## 5. How these tools should be added

The existing surface is 333 advertised tools of which **147 are real and 186 are stubs** that return `success: true` over fabricated local SQLite. New tools must not join that population. From the `ponytail` gate (rung 6 held — a table and one function, not a new module):

1. **One declaration site.** A `constexpr` table carrying name, schema, handler, and a **truthfulness field** (`Backing::Mtproto` / `LocalOnly` / `Unimplemented`). Today a tool is declared in three hand-synced places — which is how one duplicate registration, five silently overwritten handlers, and one callable-but-unadvertised tool got in.
2. **`Unimplemented` returns an error, never `success: true`.** This one rule is what prevents the 186-stub outcome recurring.
3. **`LocalOnly` is named as such** in both the tool name and its description, so a caller can never mistake local bookkeeping for Telegram data.
4. **A debug assert** that table names are unique and that every entry has a handler. Cheap, and it makes the class of defect impossible.
5. **Deletion is part of the port.** Where 7.0.6 absorbed something we hand-built, the correct port is removal — `ponytail` rung 2.

---

## 6. Not implementable, and deliberately omitted

| Capability | Why |
|---|---|
| Fabricated financial data (`get_wallet_balance` hardcoding 0, invented USD/TON rates, `wallet_spending`/`star_reactions`/`gift_transfers` tables written only by our own tools) | No API backs these as written. Highest-harm category — a caller cannot distinguish invented money from real. Rebuild on the real credits API or delete. |
| `reportReadMetrics`, `reportMusicListen` | Telemetry emitters. Exposing them lets an agent forge engagement data. |
| `bots.exportBotToken` as a normal tool | Returns a credential; our audit logger records tool results. Needs redaction and `0600` file output, or omission. |
| TON wallet tools | `TonWallet` is dead code (zero references outside its own files), its `python3`+tonsdk dependency is absent on this machine, and it stores 24-word mnemonics in **plaintext** SQLite. Delete. |
| Thanos / 3D covers / voice particles | Client-side rendering only (§4.12). |

---

## 7. Open questions

1. ~~Does `messages.editMessage` carry `rich_message`?~~ **RESOLVED: yes** — `messages.editMessage#b106e66c`, `rich_message:flags.23?InputRichMessage`. The §4.2 streaming-reveal pattern is viable.
2. ~~Which `PageBlock` constructors are legal in `inputRichMessage.blocks`?~~ **RESOLVED: 21 kinds, enumerated in §4bis**, with six that hard-fail the whole message. Slideshow, collage, details, table, math, and map authoring are all confirmed.
3. ~~Can `textCustomEmoji` appear in headings and details titles?~~ **RESOLVED: yes, no flattening** (§4bis).
4. ~~Are there size limits?~~ **RESOLVED: yes** — `RichMessageLimits`, appConfig-overridable (§4bis).
5. ~~Where is the premium gate?~~ **RESOLVED: two gates**, and `rich_message_posting` defaults to `"disabled"` (§4bis).
6. **Is `pageBlockThinking` accepted from a non-bot sender?** Needs one live test — no client-side restriction exists, so any gate is server-side. Gates the §4.2 streaming animation.
7. **Do `inputRichMessageMarkdown` / `inputRichMessageHTML` work at all, and how does `InputRichFile.id` get referenced?** No client code exists; undiscoverable from the corpus. Only worth pursuing if the local importer route proves insufficient.
8. Exact password parameter shape for `messages.editChatCreator`.
9. Does disabling Swift 6 remove only the macOS system translation provider, or more?

Question 6 is the only remaining blocker on the highest-value capability, and it is answerable with a single live send rather than more code reading.

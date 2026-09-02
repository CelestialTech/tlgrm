# Feature → MCP → Tlgrm API map — v7.0.9 → v7.1.3 (MTProto layer 228 → 229)

Grounded in the live tree, not the task brief. Every row cites a real TL constructor
(`tdesktop/Telegram/SourceFiles/mtproto/scheme/api.tl`) and, for the MCP column, the
352-tool roster in `tdesktop/Telegram/SourceFiles/mcp/mcp_tool_backing.h` plus the
`Tool{}` definitions in `mcp_tool_registry.cpp` and their handlers.

## Ground-truth correction to the brief

The `git diff v7.0.9 v7.1.3 -- .../api.tl` shows the `// LAYER 228` marker becoming
`// LAYER 229`. Several flags the brief lists as "new" were **already present in layer
228** (they appear on the `-` side of the diff, unchanged):

- `chatAdminRights`: `post_stories`, `edit_stories`, `delete_stories`,
  `manage_direct_messages`, `manage_linked_peers`, `manage_ranks` were all in 228.
  **The only genuinely new admin right in 229 is `manage_welcome_messages` (flags.20).**
- `channelFull`: `paid_messages_available`, `send_paid_messages_stars`,
  `stargifts_available`, `stargifts_count`, `guard_bot_id`, `bot_verification` were all
  in 228. **The only new channelFull/chatFull flag in 229 is `has_welcome_messages`**
  (chatFull flags.21 / channelFull flags2.24).
- The rich-message draft actions (`sendMessageTextDraftAction`,
  `inputSendMessageRichMessageDraftAction`, `sendMessageRichMessageDraftAction`) existed
  in 228; 229 *changed* them (added `can_stop`/`keep_on_stop`) and **added
  `sendMessageStopDraftAction`**.
- `messageActionStarGiftUnique` and `inputInvoiceStarGiftResale` existed in 228; 229
  *changed* them (hash + fields). Only the enclosing **welcome-message RPC family** and
  the **keyboard-button type hierarchy** are wholly new type systems.

So the headline of layer 229 is the **welcome-messages family** and the **keyboard /
inline-button `ButtonType` refactor** — not stars/stories, which mostly predate it.

## The map

| # | New / changed feature (TL constructor) | MCP tool | Tlgrm API (TL method) |
|---|---|---|---|
| 1 | **Welcome messages** — read/list a channel's welcome messages (`ephemeral.welcomeMessages#104fc872`, `ephemeral.welcomeMessagesNotModified`) | **GAP** — no welcome tool. The ephemeral tools (`get_ephemeral_messages`, `get_ephemeral_stats`, `configure_ephemeral_capture`, all `Backing::LocalOnly`) read the local *capture* DB, not this RPC | `ephemeral.getWelcomeMessages#db9ac18d` |
| 2 | **Delete welcome message(s)** — new RPCs | **GAP** | `ephemeral.deleteWelcomeMessage#e882a9e1`, `ephemeral.deleteAllWelcomeMessages#734f9721` |
| 3 | **Send a welcome message** — `ephemeralMessage.welcome_template:flags.5?true` | **GAP** | `ephemeral.sendMessage#ba8d5f35` (`welcome:flags.7?true`) |
| 4 | **`has_welcome_messages` flag** on chatFull (flags.21) / channelFull (flags2.24) — surface that a peer has welcome messages | **GAP** — `get_chat_info` does not decode it (nor `stargifts_count` / `guard_bot_id` / `bot_verification`); only `get_paid_messages_stats` reads `send_paid_messages_stars`, in `mcp_stars_tools.cpp:762` | `channels.getFullChannel` / `messages.getFullChat` |
| 5 | **`manage_welcome_messages` admin right** — `chatAdminRights#5fb224d5 …manage_welcome_messages:flags.20?true` (only *new* admin flag in 229) | **GAP** — no promote/edit-admin tool exists anywhere in `mcp/` (grep for `editAdmin`/`admin_rights`/`promote` → 0 hits) | `channels.editAdmin#9a98ad68` |
| 6 | **Story admin rights** — `post_stories`/`edit_stories`/`delete_stories`, `manage_direct_messages`, `manage_linked_peers` (⚠ already in 228, not new in 229) | **GAP** — same missing `channels.editAdmin` tool | `channels.editAdmin#9a98ad68` |
| 7 | **Ephemeral bot callback query** — `updateEphemeralBotCallbackQuery#7c1079d6` (new update) | **GAP** — no callback tool (grep `callback_answer`/`getBotCallback`/`click_button` → 0 hits) | `ephemeral.getCallbackAnswer#3fa464c8`; cf. `messages.getBotCallbackAnswer#9342ca07` |
| 8 | **Edit an ephemeral message** — `ephemeral.editMessage#cf9c725b` (new RPC) | **GAP** — capture tools are read-only `LocalOnly` | `ephemeral.editMessage#cf9c725b` |
| 9 | **Ephemeral message capture** — `ephemeralMessage#dd27bee9`, `updateNewEphemeralMessage` | **COVERED (read)** — `get_ephemeral_messages`, `get_ephemeral_stats`, `configure_ephemeral_capture` (`mcp_bot_tools.cpp:543`, `mcp_archive_tools.cpp:816`) | local capture of `updateNewEphemeralMessage#20bcbba1` (no outbound RPC) |
| 10 | **Forward from ephemeral** — `messages.forwardMessages` `from_ephemeral:flags.25?true` | **GAP** — `forward` / `batch_forward` tools don't set `from_ephemeral` | `messages.forwardMessages#13704a7c` |
| 11 | **Unique/collectible star-gift resale — browse** — `messageActionStarGiftUnique#7e1c1187` (added `name_hidden`, `message`) | **COVERED** — `list_marketplace` / `browse_gift_marketplace` / `list_gift_for_sale` decode `starGiftUnique` (`mcp_stars_tools.cpp:516,537`) | `payments.getResaleStarGifts#7a5fa236` |
| 12 | **Resale — buy a resold unique gift** — `inputInvoiceStarGiftResale#e9b0c658` (added `show_name`, `message`) | **GAP** — `buy_gift` / `send_gift` / `send_stars` are `Backing::Unimplemented`; `toolBuyGift` is a deliberate stub that refuses (`mcp_stars_tools.cpp:592`) | `payments.getPaymentForm#37148dbb` + `payments.sendStarsForm#7998c914` over `inputInvoiceStarGiftResale` |
| 13 | **Resale — list / delist your gift** | **COVERED** — `list_gift_for_sale`, `delist_gift`, `cancel_listing` (`Backing::Mtproto`); update-listing handler `mcp_settings_tools.cpp:1674` | payments resale-price update (`toolUpdateListing`) |
| 14 | **Unique-gift auctions** — `starGiftAuction*` | **COVERED** — `get_auction_history` and auction reads (`mcp_stars_tools.cpp:375,420,456`) | `payments.getStarGiftActiveAuctions`, `…AuctionState`, `…AuctionAcquiredGifts` |
| 15 | **Craft / transfer unique gift** — `messageActionStarGiftUnique` `craft`/`transferred` flags | **COVERED** — `craft_star_gift` (`mcp_tool_registry.cpp:3289`), `transfer_gift` → `MTPpayments_TransferStarGift` (`mcp_settings_tools.cpp:3022`) | `payments.transferStarGift` |
| 16 | **`messageActionChatJoinedViaCommunity#4a8bfe80`** (new service action) | **PARTIAL** — no dedicated tool; the service message is readable via `read_messages`; join/link flow covered by community tools | service action (server-generated); join via `communities.togglePeerLinkRequestApproval` |
| 17 | **Communities — create / link / join-request review** (`community`, `communityFull`, `communityPeerRequest`) | **COVERED** — `create_community`, `add_chat_to_community`, `remove_chat_from_community`, `list_community_join_requests`, `review_community_join_request`, `set_community_collapsed` (`mcp_community_tools.cpp`) | `communities.create#a63859ec`, `communities.addPeerLink`, `communities.togglePeerLinkRequestApproval`, … |
| 18 | **Paid messages** — `send_paid_messages_stars` / `paid_messages_available` (⚠ flags already in 228) | **COVERED** — `configure_paid_messages` → `MTPchannels_UpdatePaidMessagesPrice` (`mcp_stars_tools.cpp:753`); `get_paid_messages_stats` | `channels.updatePaidMessagesPrice#4b12327b` |
| 19 | **Rich-message draft streaming / typing** — `sendMessageTextDraftAction#3630b85a`, `inputSendMessageRichMessageDraftAction#a937c7be`, `sendMessageRichMessageDraftAction#52564893`, **new** `sendMessageStopDraftAction#fbf902b0` | **GAP** — no typing/draft tool anywhere (`setTyping`/`DraftAction` → 0 hits in `mcp/`). `list_rich_messages` / `save_rich_message_html` only *read* stored `RichMessage`s | `messages.setTyping#58943ee2` (with the draft `SendMessageAction`) |
| 20 | **Keyboard / inline button refactor** — `keyboardButton#2f67a72f type:ButtonType`, new `ButtonType`/`InlineButtonType` hierarchies, `keyboardInlineButton`, `richButtonStyle`, `pageButton`, `textButton#afc79cd6`, `replyKeyboardMarkup`/`replyInlineMarkup` `force_reply` | **GAP** — no send-tool builds `reply_markup` (grep `reply_markup`/`inline_keyboard`/`KeyboardButton` in send/bot tools → 0 hits) | carried on `messages.sendMessage#fef48f62` `reply_markup:flags.2?ReplyMarkup` |
| 21 | **Instant-View page blocks** — `pageBlockBlockquote#66d1670b` (`collapsed`), `pageBlockTable` (`compact`), **new** `pageBlockButtonRow#6d640318`, **new** `pageBlockDocument#38fa3ba3` | **GAP** — no Instant-View tool (`getWebPage`/`instantView` → 0 hits); the `blockquote` handling in `export_html.cpp:332` is message-entity HTML, not IV `WebPage` blocks | `messages.getWebPage#8d9692a3` |
| 22 | **Firebase phone-number-verification login** — new `auth.firebasePnvIntent`, `auth.initFirebasePnvLogin`, `auth.finishFirebasePnvLogin`, `auth.firebasePnvSignUp` | **GAP (out of MCP scope)** — auth/login is a client-only flow, not a bot-surface tool | `auth.initFirebasePnvLogin#777df37a`, `auth.finishFirebasePnvLogin#2c85094c`, `auth.firebasePnvSignUp#783f6b56` |

## Summary

Counting the 22 feature rows above (grouping the genuinely-new-or-changed layer-229
constructor families):

- **Covered: 7** — ephemeral capture read (9), unique-gift resale browse (11), resale
  list/delist (13), gift auctions (14), craft/transfer (15), community
  create/link/join-review (17), paid-messages price (18).
- **Partial: 1** — `messageActionChatJoinedViaCommunity` (16): readable as a service
  message, no dedicated tool.
- **GAP: 14** — welcome messages (1–4), welcome/story admin rights (5–6), ephemeral bot
  callback (7), ephemeral edit (8), forward-from-ephemeral (10), resale **purchase**
  (12), rich-message draft streaming (19), keyboard/inline-button `reply_markup` (20),
  Instant-View page blocks (21), Firebase PNV auth (22).

**Coverage: 7 covered / 1 partial / 14 GAP.**

If Firebase PNV (out-of-scope client login) is excluded, it's **7 covered / 1 partial /
13 GAP** over 21 in-scope features.

### Highest-value GAPs to close

1. **Welcome-messages family (rows 1–5)** — the defining layer-229 feature and a clean
   greenfield: four new RPCs (`ephemeral.getWelcomeMessages`,
   `ephemeral.deleteWelcomeMessage`, `ephemeral.deleteAllWelcomeMessages`,
   `ephemeral.sendMessage welcome=true`) plus the `manage_welcome_messages` admin right.
   Zero MCP surface today. A `get/set/delete_welcome_messages` tool set is the single
   biggest coverage win.
2. **Star-gift resale *purchase* (row 12)** — read-side is fully covered but the
   buy-side (`inputInvoiceStarGiftResale` → `payments.getPaymentForm` +
   `payments.sendStarsForm`) is a deliberate `Unimplemented` stub. Wiring
   `toolBuyGift` to the real payment-form flow completes the resale loop.
3. **Rich-message draft streaming (row 19)** — the fork already reads `RichMessage`s;
   `sendMessageStopDraftAction` is new. A `set_typing` / `stream_draft` tool over
   `messages.setTyping` would expose the fork's own rich-message drafting to agents.
4. **`channels.editAdmin` tool (rows 5–6)** — one missing tool unlocks *all* admin
   rights (welcome + stories + direct-messages + linked-peers), an entire
   administration surface currently absent.
5. **Instant-View page blocks (row 21)** and **reply-markup/keyboard building (row
   20)** — lower priority (rendering/composition concerns), backed by
   `messages.getWebPage` and `messages.sendMessage.reply_markup` respectively.

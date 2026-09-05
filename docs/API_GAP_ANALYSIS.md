# MCP API gap analysis — what layer-229 functionality is possible but missing

**Method (2026-09-04):** a leashed swarm of 7 domain workers enumerated the TL
layer-229 method namespaces (messages 259, account 128, payments 65, channels 58,
phone 43, bots 38, stories 33, contacts 28, …) and cross-referenced the **365 live
MCP tools**, then an adversarial pass live-verified every claim against the bridge
+ source. 87 raw findings → the deduped, re-ranked map below.

**Adversarial verification:** every claimed-missing family confirms **0 matching
tools** in the live surface (polls, ban/kick, invite-links, drafts, callbacks,
mute/notify, send_photo/sticker/location, calls, boosts, comment-threads,
view-counts, todo, contacts). No false positives. Two nuances confirmed:
`get_user_info` reads **cached UserData only** (no `users.getFullUser`); join-
requests exist **community-scoped** only (general channel/group approval missing).
Correctly-covered areas (NOT gaps): `set_admin_rights`=editAdmin,
`transfer_group_ownership`=editCreator, setPrivacy, updateProfile, sessions,
reactions, forward/pin/schedule/translate/folders, and the whole star-gift /
subscription / auction / gift-marketplace tool set.

## Tier 1 — high value + EASY (plain MTP reads/sends; build these next)

These mirror patterns the bridge already has (e.g. `send_video` = PrepareMediaList +
`SendMediaType::Photo`; every `get_*` = one MTP round-trip via `awaitMtp`).

| Gap | TL method(s) | Note |
|---|---|---|
| **Media send family** | messages.sendMedia + inputMediaUploaded* / sendMultiMedia | `send_photo` (image as photo), `send_sticker`, `send_audio`, `send_gif`/animation, `send_location`, `send_venue`, `send_contact`, `send_dice`, **media album**. Same path as `send_video`. |
| **Polls / quizzes** | messages.sendMedia(inputMediaPoll), sendVote, getPollResults, getPollVotes | Create a poll, vote, read results/voters. Fully absent. |
| **Drafts** | messages.saveDraft, getAllDrafts, clearAllDrafts | Compose/stage a draft before sending. |
| **Mute / notifications** | account.updateNotifySettings, getNotifySettings | **Mute/unmute a chat** — no tool today. |
| **Story reads** | stories.getAllStories, getPeerStories, getStoriesByID, getStoriesViews, searchPosts | Read the stories feed + a peer's stories + own-story views. Easy reads. |
| **Contacts** | contacts.getContacts, addContact, deleteContacts, importContacts, resolvePhone | Resolve a phone→user, list/add/remove contacts. |
| **Full user profile** | users.getFullUser; photos.getUserPhotos | `get_user_info` is cache-only — no bio/common-chats/profile-photo history. |
| **Message intel** | messages.getMessagesViews, getMessageReadParticipants, getReplies/getDiscussionMessage, getMessageReactionsList, getUnreadMentions/Reactions | View counts, read receipts, **channel comment threads**, per-message reactor lists. |
| **Todo / checklist** | messages.appendTodoList, toggleTodoCompleted | New in layer 229 (7.2 checklist messages). |
| **Drive other bots** | messages.startBot, getBotCallbackAnswer, getInlineBotResults, sendInlineBotResult | `/start` a bot, **press an inline keyboard button**, query & send inline results (`@gif …`). High value for agent automation. |

## Tier 2 — high value, moderate effort

- **Group/channel moderation:** channels.editBanned (**ban/kick/restrict**),
  invite-link management (exportChatInvite, getChatInviteImporters, edit/delete),
  general join-request approval (messages.hideChatJoinRequest), config toggles
  (toggleSignatures/JoinToSend/SlowMode/Forum), channels.getAdminLog (the SERVER
  admin log — the fork's `get_audit_log` is LocalOnly), setDiscussionGroup,
  getSendAs, getChannelRecommendations.
- **Forum topics — writes:** createForumTopic/editForumTopic/updatePinnedForumTopic
  (currently read-only).
- **Boosts:** premium.getBoostsStatus/getMyBoosts/applyBoost/getUserBoosts.
- **Account & security:** account.getPassword/updatePasswordSettings (2FA),
  getContentSettings (18+), getGlobalPrivacySettings (default auto-delete TTL,
  read-time), getWebAuthorizations/resetWebAuthorization (revoke website/bot logins).
- **Payments:** checkGiftCode/applyGiftCode (redeem a gift/premium code),
  getGiveawayInfo, getPaymentReceipt, getPremiumGiftCodeOptions.
- **Dialogs:** markDialogUnread, toggleDialogPin, getPinnedDialogs.

## Tier 3 — hard (needs a client subsystem) or niche

- **1:1 calls** (phone.requestCall/accept/confirm/discard/setCallRating) and
  **group calls / video chats** (createGroupCall/join/leave/getGroupParticipants).
  The client has the tgcalls/webrtc subsystem, but exposing live audio via MCP is
  large and low-ROI for automation.
- **Bot WebApps** (messages.requestWebView/requestSimpleWebView) + **attach-menu
  bots** — complex UI/session surface.
- **Post a story** (stories.sendStory/editStory/deleteStories) — media upload +
  story mechanics.
- **Sticker-set creation** (stickers.createStickerSet/addStickerToSet).

## Tier 4 — low value, deprioritize

- Cosmetic account settings: emoji status, profile name-color, personal channel,
  birthday setter, wallpapers/chat themes, custom ringtones, account self-destruct
  TTL.
- **Fiat payment forms** (getPaymentForm/sendPaymentForm) — entering card details
  via an MCP tool is a safety concern; leave to the UI.
- Store/subscription internals: canPurchaseStore, getBankCardData,
  fulfillStarsSubscription, getStarGiftWithdrawalUrl, star affiliate/referral bots.
- Owned-bot admin (bots.setBotCommands/setBotInfo/setBotMenuButton) — @BotFather-
  style management, niche for this bridge.

## Bottom line

The 365-tool surface is deep on **read/search, business, wallet/gifts, retention,
and the fork's automation framework**, but three whole first-class Telegram
subsystems are entirely absent — **calls/video-chats, stories, and polls** — and
the everyday **send-anything** surface stops at documents/video (no photo, sticker,
audio, gif, location, contact, or album). The best ROI is Tier 1: it's all plain
MTP the client already supports, it completes the sending surface an automation
agent (argot, TeleBox, Claude) actually reaches for, and each item is the same
shape as tools already shipped.

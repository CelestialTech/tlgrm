# MCP Tool Contract Conventions — Reference and Deviations

Audit of the 355-tool MCP surface in
`/Users/pasha/xCode/tlgrm/tdesktop/Telegram/SourceFiles/mcp/`.
Machine-diffed across all four declaration sites plus every bound handler body.

## 1. Four-site consistency: CLEAN

| Site | Count |
|---|---|
| `mcp_tool_registry.cpp` advertised `Tool{}` entries | 355 |
| `mcp_server_complete.cpp` `_toolHandlers[]` bindings | 355 |
| `mcp_tool_backing.h` `kToolBackings` entries | 355 |
| Method definitions found for every bound handler | 355/355 |

Set differences are all empty: no advertised-but-unbound tool, no
bound-but-unadvertised tool, no missing or orphaned backing entry, and no
duplicate name in any of the three tables. The `mcp_tool_backing.h` header
comment describes historical drift (double-advertised, double-assigned,
8 callable-but-unadvertised); **that drift is gone**. Backing breakdown:
LocalOnly 229, Mtproto 81, LiveSession 41, PureCompute 4, Unimplemented 0.

Note: with zero `Unimplemented` entries, the `callTool()` gate at
`mcp_server_complete.cpp:133-137` that refuses success for unbacked tools is
currently inert. It protects nothing today; it only matters for future tools.

---

## 2. THE CONVENTION — what the surface SHOULD use

These are the rules the majority of the surface already follows. Every
deviation below is measured against them.

| Concept | Canonical key | Canonical JSON type | Rationale |
|---|---|---|---|
| Chat/peer identity | `chat_id` | **integer** (`qint64`) | 125 int sites vs 22 string sites |
| Message identity | `message_id` | **integer** (`qint64`) | 47 int sites vs 7 string sites |
| User identity | `user_id` | **integer** (`qint64`) | 12 int vs 4 string |
| Peer identity (deleted-account tools) | `peer_id` | **integer** | 11 int vs 2 string |
| Naming style | `snake_case` | — | ~99% of keys |
| Result envelope | `success` | **boolean** | 312 sites vs 14 `status:"success"` |
| Failure | `error` | string | 355 sites, uniform |
| Message text argument | `text` | string | `send_message`, `send_*` family |
| Row cap argument | `limit` | integer | uniform where declared |

**Rules that follow from the above:**

1. Never `QString::number()` an id into a response. Ids are `qint64`.
2. Never emit a bare `id` key alongside `chat_id`/`message_id` for the same
   value — pick the qualified name.
3. Never emit `status:"success"`; emit `success: true`.
4. Every argument the implementation reads MUST appear in `inputSchema`.
5. An alias tool (two names, one handler) MUST advertise the identical schema
   and identical backing as its twin.

---

## 3. DEVIATIONS

### 3a. Id fields typed as STRING (must be integer)

| Tool / site | File:line | Key |
|---|---|---|
| `read_messages` | mcp_core_tools.cpp:433 | `message_id` |
| `read_messages` (reply block) | mcp_core_tools.cpp:463 | `message_id` |
| `read_messages` (from user) | mcp_core_tools.cpp:444 | `id` |
| `search_messages` | mcp_core_tools.cpp:667, 675 | `message_id`, `id` |
| `list_chats` | mcp_core_tools.cpp:62 | `id` |
| `get_chat_info` | mcp_core_tools.cpp:300, 305, 372, 379 | `chat_id`, `id` |
| `get_user_info` | mcp_core_tools.cpp:737, 746, 751, 801 | `user_id`, `id` |
| all analytics tools | mcp_analytics_tools.cpp:21,27,90,102,148,210,218,270,325,416,475 | `chat_id`, `user_id` |
| deleted-account tools | mcp_deleted_account_tools.cpp:28, 268, 290 | `peer_id`, `id` |
| batch export JSON | batch_operations.cpp:694-698 | `messageId`, `chatId`, `fromId` |
| analytics engine | analytics.cpp:213,277,360,391,1372,1385 | `userId`, `chatId` |
| HTML/MD export | export_html.cpp:89,96; export_markdown.cpp:94,101 | `message_id`, `id` |
| gradual archiver | gradual_archiver.cpp:346,1125,1234 | `id` |

**Contrast (correct, integer):** `list_rich_messages`
(mcp_rich_message_tools.cpp:71 `entry["message_id"] = qint64(item->id.bare)`),
topics, `mcp_download_tools.cpp:81-82`, `mcp_batch_tools.cpp:106,166,226,286`,
`mcp_community_tools.cpp:21,104,175`, `mcp_archive_tools.cpp:865-866`.

A caller reading `message_id` cannot write one code path: `read_messages`
gives `"12345"`, `list_rich_messages` gives `12345`.

### 3b. camelCase keys (must be snake_case)

`batch_operations.cpp:694-699` — `messageId`, `chatId`, `fromId`, `fromName`.
`analytics.cpp` — `userId`, `chatId`, `activeUsers`, `messagesLastHour`,
`messagesPerDay`, `totalMessages`, `messageCount`, `averageLength`,
`chatTitle`, `userName`, `topUsers`, `topWords`, `textMessages`.
`message_scheduler.cpp` — `scheduleId`, `scheduledTime`, `recurrencePattern`,
`recurrenceData`, `retryCount`, `totalScheduled`.
`batch_operations.cpp` — `operationId`, `totalItems`, `processedItems`,
`successfulItems`, `errorMessage`, `startTime`.

### 3c. Envelope split — `status:"success"` instead of `success:true`

14 tools, all in `mcp_settings_tools.cpp`, return `{"status":"success"}` and
never set `success`; the other 268 set `success` and never set `status`:

`get_active_sessions` (:514), `get_blocked_users` (:453),
`get_privacy_settings` (:241), `get_profile_settings` (:43),
`get_security_settings` (:491), `update_about_privacy` (:427),
`update_auto_delete_period` (:583), `update_birthday_privacy` (:401),
`update_forwards_privacy` (:375), `update_last_seen_privacy` (:297),
`update_phone_number_privacy` (:349), `update_profile_bio` (:125),
`update_profile_name` (:84), `update_profile_photo_privacy` (:323).

A caller checking `result["success"]` sees `false`/absent on all 14 successes.

### 3d. Aliased handlers whose schemas diverge

Five tool names are bound to a handler shared with another name. Three
advertise a **different** schema than their twin, so identical behaviour is
documented two incompatible ways:

| Handler | Tools | Divergence |
|---|---|---|
| `toolGetMessageTags` (mcp_premium_tools.cpp:316) | `list_tags`, `get_message_tags` | `list_tags` declares **no** props; twin declares `chat_id`, `message_id` |
| `toolRemoveMessageTag` (mcp_premium_tools.cpp:356) | `delete_tag`, `remove_message_tag` | `delete_tag` declares only `tag`; impl needs `chat_id`+`message_id` |
| `toolSearchByTag` (mcp_premium_tools.cpp:382) | `get_tagged_messages`, `search_by_tag` | one declares `limit`+`tag`, the other `chat_id`+`tag`; impl reads `tag`+`limit` |
| `toolAddMessageTag` (mcp_premium_tools.cpp:281) | `tag_message`, `add_message_tag` | schemas agree; both omit `color`, which the impl reads |
| `toolIndexMessages` (mcp_search_tools.cpp:109) | `index_messages`, `semantic_index_messages` | schemas agree; both omit `rebuild`, which the impl reads |

### 3e. Schema/implementation drift — headline cases

| Tool | Advertised | Actually read | Effect |
|---|---|---|---|
| `configure_ad_filter` | `hide_promoted`, `hide_sponsored` | `enabled`, `keywords`, `exclude_chats` | Zero overlap. Schema-following call writes an empty config and returns `success:true` (mcp_premium_tools.cpp:446-461) |
| `backtest_strategy` | `strategy`, `start_date`, `end_date` | `strategy`, `days`, `initial_investment`, `gift_type` | Date window ignored; always last 30 days (mcp_settings_tools.cpp:1659-1663) |
| `get_miniapp_spending` | `app_id` | `miniapp_id` | Documented filter silently ignored; returns all rows (mcp_settings_tools.cpp:1097) |
| `set_chat_rules` | `chat_id`, `rules` | `rule_name`, `rule_type`, `conditions`, `actions` | `rules` payload never read (mcp_premium_tools.cpp:498) |
| `send_gift` | `gift_type`, `recipient_id`*, `stars_amount`* | `gift_id`, `message`, `anonymous` | Required advertised args do not drive the call (mcp_wallet_tools.cpp:280) |
| `simulate_rating_change` | `action`, `amount` | `additional_stars`, `additional_reactions` | Zero overlap (mcp_settings_tools.cpp:1898) |
| `convert_stars` | `direction`, `stars_amount` | `target`, `stars_amount` | Same concept, two names (mcp_wallet_tools.cpp:1182) |
| `generate_financial_report` | `start_date`, `end_date`, `format` | `period` | Zero overlap (mcp_settings_tools.cpp:1416) |
| `get_market_trends` | `period` | `days`, `gift_type` | Zero overlap (mcp_settings_tools.cpp:1627) |
| `get_earnings_chart` | `channel_id`, `period` | `days` | Zero overlap (mcp_settings_tools.cpp:2449) |
| `configure_voice_persona` | `provider`, `settings` | `speed`, `pitch` | Zero overlap (mcp_business_tools.cpp:954) |
| `configure_paid_messages` | `min_stars` | `chat_id`, `price` | Zero overlap (mcp_settings_tools.cpp:1016) |
| `update_gift_display` | `visible` | `chat_id` | (mcp_settings_tools.cpp:1973) |
| `text_to_video` | `avatar` | `voice`, `speed`, `preset` | (mcp_business_tools.cpp:1129) |

*`required` in the advertised schema.

**Aggregate: 135 of 355 tools (38%) drift. 88 read at least one argument that
tools/list never advertises — those parameters are unreachable to any caller
that trusts the schema.**

### 3f. `success: true` on a path that did no work

`get_transcription_status` (mcp_settings_tools.cpp:698-720) — if no stored
transcription exists it returns `success:true, status:"pending"` with a note.
Nothing was queued, nothing is pending; the tool never starts a transcription.
Also reads `message_id`, which is not advertised.

The `pause_*`/`resume_*`/`cancel_*`/`get_*_status` families in
`mcp_gradual_tools.cpp`, `mcp_deleted_account_tools.cpp`, and
`mcp_archive_tools.cpp` (19 tools) all return `success:true` from bodies that
touch no DB; most delegate to a live archiver object, so they are legitimate,
but they are the shape to watch.

### 3g. Backing classification notes

The 81 `Mtproto` tools were probed for an actual MTP request in the handler
body. 39 have none in-body; most legitimately delegate (`send_message` →
`Api::SendMessage`, `forward_message`, the batch family). Two patterns are
genuinely misleading:

- `get_privacy_settings` (mcp_settings_tools.cpp:241) and `get_active_sessions`
  (:514) fire an **async** `reload()` and then immediately read the *cached*
  value, returning before the MTP response arrives. The comment at :516 admits
  it: "may be empty if not yet loaded". Classified `Mtproto`, but the returned
  data is `LiveSession` (stale cache) on the first call.
- `test_greeting` (mcp_business_tools.cpp:273), `test_away` (:402),
  `send_subscriber_message` (mcp_stars_tools.cpp:1636),
  `share_achievement` (mcp_stars_tools.cpp:1430), `cancel_listing`
  (mcp_settings_tools.cpp:2978) are marked `Mtproto` and issue no request
  directly — each needs manual confirmation that its delegate does.

---

## 4. FULL DRIFT TABLE — tools reading undeclared arguments (88)

| Tool | File:line | Reads but never advertises | Advertises but never reads |
|---|---|---|---|
| `add_message_tag` | mcp_premium_tools.cpp:281 | color | — |
| `backtest_strategy` | mcp_settings_tools.cpp:1659 | days, gift_type, initial_investment | end_date, start_date |
| `batch_delete` | mcp_batch_tools.cpp:68 | revoke | — |
| `batch_pin` | mcp_batch_tools.cpp:188 | notify | — |
| `cancel_subscription` | mcp_settings_tools.cpp:2344 | channel_id | — |
| `clone_voice` | mcp_settings_tools.cpp:2777 | audio_sample | — |
| `configure_ad_filter` | mcp_premium_tools.cpp:446 | enabled, exclude_chats, keywords | hide_promoted, hide_sponsored |
| `configure_chatbot` | mcp_business_tools.cpp:640 | response_style, trigger_keywords | — |
| `configure_paid_messages` | mcp_settings_tools.cpp:1016 | chat_id, price | min_stars |
| `configure_voice_persona` | mcp_business_tools.cpp:954 | pitch, speed | provider, settings |
| `convert_stars` | mcp_wallet_tools.cpp:1182 | target | direction |
| `create_auto_reply_rule` | mcp_business_tools.cpp:1363 | name, priority | — |
| `create_crypto_payment` | mcp_settings_tools.cpp:1240 | action, comment, limit, mnemonics, recipient | — |
| `create_exclusive_content` | mcp_stars_tools.cpp:1538 | price | — |
| `create_gift_auction` | mcp_stars_tools.cpp:160 | duration_hours | — |
| `create_gift_collection` | mcp_stars_tools.cpp:13 | description, public | — |
| `create_paid_post` | mcp_stars_tools.cpp:823 | chat_id, preview | — |
| `create_task_from_message` | mcp_premium_tools.cpp:628 | due_date, priority, title | — |
| `delete_chat_rule` | mcp_settings_tools.cpp:901 | rule_name | — |
| `delete_message` | mcp_message_ops.cpp:80 | revoke | — |
| `delete_tag` | mcp_premium_tools.cpp:356 | chat_id, message_id | — |
| `detect_topics` | mcp_search_tools.cpp:239 | message_limit | — |
| `execute_chat_rules` | mcp_settings_tools.cpp:930 | chat_id, test_message | — |
| `export_analytics` | mcp_analytics_tools.cpp:391 | format | — |
| `export_chat` | mcp_archive_tools.cpp:58 | messages_written | — |
| `generate_financial_report` | mcp_settings_tools.cpp:1416 | period | end_date, format, start_date |
| `get_auction_history` | mcp_stars_tools.cpp:371 | limit, status | — |
| `get_earnings` | mcp_wallet_tools.cpp:657 | channel_id | period |
| `get_earnings_chart` | mcp_settings_tools.cpp:2449 | days | channel_id, period |
| `get_ephemeral_messages` | mcp_archive_tools.cpp:816 | type | — |
| `get_gift_history` | mcp_wallet_tools.cpp:353 | limit | — |
| `get_gift_price_history` | mcp_stars_tools.cpp:627 | days | — |
| `get_gift_transfer_history` | mcp_settings_tools.cpp:2250 | limit | gift_id |
| `get_leaderboard` | mcp_stars_tools.cpp:1361 | type | — |
| `get_market_trends` | mcp_settings_tools.cpp:1627 | days, gift_type | period |
| `get_miniapp_spending` | mcp_settings_tools.cpp:1096 | miniapp_id | app_id |
| `get_monetization_analytics` | mcp_wallet_tools.cpp:813 | chat_id | — |
| `get_reaction_analytics` | mcp_stars_tools.cpp:720 | period | — |
| `get_star_reactions` | mcp_stars_tools.cpp:682 | message_id | — |
| `get_stars_history` | mcp_wallet_tools.cpp:1245 | direction | — |
| `get_tag_suggestions` | mcp_premium_tools.cpp:418 | limit | — |
| `get_transactions` | mcp_wallet_tools.cpp:146 | type | — |
| `get_transcription_status` | mcp_settings_tools.cpp:698 | message_id | — |
| `get_translation_history` | mcp_premium_tools.cpp:237 | target_language | — |
| `get_trends` | mcp_analytics_tools.cpp:460 | days_back, metric | — |
| `index_messages` | mcp_search_tools.cpp:109 | rebuild | — |
| `list_auctions` | mcp_stars_tools.cpp:286 | limit | — |
| `list_gift_for_sale` | mcp_stars_tools.cpp:462 | category | — |
| `list_giveaways` | mcp_settings_tools.cpp:2479 | limit | — |
| `list_marketplace` | mcp_stars_tools.cpp:412 | limit | — |
| `list_quick_replies` | mcp_business_tools.cpp:48 | limit | — |
| `list_tags` | mcp_premium_tools.cpp:316 | chat_id, message_id | — |
| `list_tasks` | mcp_premium_tools.cpp:677 | limit | chat_id |
| `refund_content` | mcp_stars_tools.cpp:951 | reason | — |
| `reorder_profile_gifts` | mcp_settings_tools.cpp:2010 | chat_id | — |
| `request_stars` | mcp_wallet_tools.cpp:1104 | reason | — |
| `search_by_tag` | mcp_premium_tools.cpp:382 | limit | chat_id |
| `search_transactions` | mcp_settings_tools.cpp:1134 | category | — |
| `semantic_index_messages` | mcp_search_tools.cpp:109 | rebuild | — |
| `send_gift` | mcp_wallet_tools.cpp:280 | anonymous, gift_id, message | gift_type |
| `send_subscriber_message` | mcp_stars_tools.cpp:1636 | channel_id | — |
| `send_video_reply` | mcp_business_tools.cpp:1201 | preset, speed, voice | — |
| `send_voice_reply` | mcp_business_tools.cpp:1016 | persona | — |
| `set_auto_download_settings` | mcp_download_tools.cpp:237 | type | — |
| `set_away_message` | mcp_business_tools.cpp:326 | end_time, start_time | — |
| `set_budget_alert` | mcp_wallet_tools.cpp:954 | type | — |
| `set_chat_rules` | mcp_premium_tools.cpp:498 | actions, conditions, rule_name, rule_type | rules |
| `set_greeting_message` | mcp_business_tools.cpp:196 | delay_seconds, trigger_chats | — |
| `set_reaction_price` | mcp_stars_tools.cpp:743 | chat_id | — |
| `set_spending_budget` | mcp_wallet_tools.cpp:866 | weekly_limit | — |
| `simulate_rating_change` | mcp_settings_tools.cpp:1898 | additional_reactions, additional_stars | action, amount |
| `subscribe_to_channel` | mcp_wallet_tools.cpp:542 | tier | — |
| `tag_message` | mcp_premium_tools.cpp:281 | color | — |
| `test_greeting` | mcp_business_tools.cpp:273 | chat_id | — |
| `text_to_speech` | mcp_business_tools.cpp:887 | pitch, speed | — |
| `text_to_video` | mcp_business_tools.cpp:1129 | preset, speed, voice | avatar |
| `toggle_gift_notifications` | mcp_settings_tools.cpp:2048 | chat_id | — |
| `train_chatbot` | mcp_business_tools.cpp:698 | category, test_after_train | — |
| `translate_message` | mcp_premium_tools.cpp:126 | source_language | — |
| `unsubscribe_from_channel` | mcp_wallet_tools.cpp:578 | subscription_id | — |
| `update_auto_reply_rule` | mcp_business_tools.cpp:1428 | enabled, name, triggers | — |
| `update_gift_display` | mcp_settings_tools.cpp:1973 | chat_id | visible |
| `update_listing` | mcp_settings_tools.cpp:1588 | msg_id | — |
| `update_profile_phone` | mcp_settings_tools.cpp:178 | code, phone_code_hash | — |
| `update_quick_reply` | mcp_business_tools.cpp:87 | category, shortcut | — |
| `update_scheduled` | mcp_scheduler_tools.cpp:82 | new_pattern, new_time | — |
| `update_task` | mcp_premium_tools.cpp:724 | priority, title | — |
| `withdraw_earnings` | mcp_wallet_tools.cpp:720 | channel_id | — |
## 5. Tools advertising arguments they never read (47)

| Tool | File:line | Advertised but unread |
|---|---|---|
| `add_chat_to_community` | mcp_community_tools.cpp:154 | chat_id |
| `browse_gift_marketplace` | mcp_settings_tools.cpp:2174 | category, limit, sort_by |
| `cancel_listing` | mcp_settings_tools.cpp:2978 | listing_id |
| `configure_ai_chatbot` | mcp_settings_tools.cpp:736 | enabled, max_tokens, model, system_prompt |
| `configure_away_message` | mcp_settings_tools.cpp:818 | enabled, end_time, message, start_time |
| `configure_greeting` | mcp_settings_tools.cpp:788 | delay_seconds, enabled, message, only_first_message |
| `configure_video_avatar` | mcp_settings_tools.cpp:727 | file_path, name, provider, settings |
| `configure_wallet_alerts` | mcp_settings_tools.cpp:1217 | enabled, threshold_percentage |
| `create_auction_alert` | mcp_settings_tools.cpp:1530 | minutes_before |
| `create_chat_rule` | mcp_settings_tools.cpp:893 | actions, conditions, rule_name |
| `create_price_alert` | mcp_settings_tools.cpp:1655 | direction, gift_type, target_price |
| `create_task` | mcp_settings_tools.cpp:2684 | chat_id, due_date, message_id, title |
| `edit_quick_reply` | mcp_settings_tools.cpp:830 | category, id, shortcut, text |
| `generate_video_circle` | mcp_settings_tools.cpp:722 | preset, text |
| `generate_voice_message` | mcp_settings_tools.cpp:688 | preset, text |
| `get_auction_details` | mcp_settings_tools.cpp:1526 | auction_id |
| `get_community` | mcp_community_tools.cpp:79 | chat_id |
| `get_filtered_ads` | mcp_settings_tools.cpp:1064 | limit |
| `get_fragment_listings` | mcp_settings_tools.cpp:1584 | limit |
| `get_gift_investment_advice` | mcp_settings_tools.cpp:2100 | budget, risk_level |
| `get_miniapp_history` | mcp_settings_tools.cpp:1072 | app_id, limit |
| `get_paid_content_earnings` | mcp_settings_tools.cpp:2398 | channel_id |
| `get_reaction_stats` | mcp_settings_tools.cpp:1771 | channel_id |
| `get_star_gift_details` | mcp_settings_tools.cpp:2170 | gift_type |
| `get_star_reactions_received` | mcp_settings_tools.cpp:1775 | limit |
| `get_star_reactions_sent` | mcp_settings_tools.cpp:1800 | limit |
| `get_stars_leaderboard` | mcp_wallet_tools.cpp:1216 | limit |
| `get_unlocked_content` | mcp_settings_tools.cpp:2394 | limit |
| `get_voice_transcription` | mcp_premium_tools.cpp:95 | chat_id |
| `list_active_auctions` | mcp_settings_tools.cpp:1518 | limit |
| `list_community_join_requests` | mcp_community_tools.cpp:252 | chat_id |
| `list_deleted_accounts` | mcp_deleted_account_tools.cpp:11 | include_archive_folder |
| `list_gifts` | mcp_settings_tools.cpp:2855 | direction, limit |
| `list_star_gifts` | mcp_settings_tools.cpp:2166 | limit |
| `place_auction_bid` | mcp_settings_tools.cpp:1522 | auction_id, bid_amount |
| `queue_gradual_export` | mcp_gradual_tools.cpp:201 | priority |
| `remove_chat_from_community` | mcp_community_tools.cpp:199 | chat_id |
| `review_community_join_request` | mcp_community_tools.cpp:296 | chat_id |
| `send_quick_reply` | mcp_settings_tools.cpp:826 | chat_id, shortcut |
| `send_star_gift` | mcp_settings_tools.cpp:2917 | gift_id, message, recipient_id |
| `set_away_now` | mcp_settings_tools.cpp:2696 | duration_hours |
| `set_community_collapsed` | mcp_community_tools.cpp:230 | chat_id |
| `set_gradual_export_config` | mcp_gradual_tools.cpp:176 | active_hour_end, active_hour_start, batches_before_pause, burst_pause_ms, export_format, long_pause_ms, max_batch_size, max_delay_ms, max_messages_per_day, max_messages_per_hour, min_batch_size, min_delay_ms, respect_active_hours |
| `set_miniapp_budget` | mcp_settings_tools.cpp:1126 | daily_limit, miniapp_id, monthly_limit |
| `set_wallet_budget` | mcp_settings_tools.cpp:1213 | amount, category, period |
| `translate_messages` | mcp_settings_tools.cpp:679 | chat_id, message_id, target_language |
| `update_folder` | mcp_extra_tools.cpp:250 | exclude_archived, exclude_chat_ids, exclude_muted, exclude_read, include_bots, include_channels, include_chat_ids, include_contacts, include_groups, include_non_contacts |
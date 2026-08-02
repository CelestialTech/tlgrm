# The 18 "IMPOSSIBLE" MCP Tools — Itemized, Explained, and Routed to Possible

**Corpus:** fork MCP sources at layer 222 (`/Users/pasha/xCode/tlgrm-fork-6.9.6`) against upstream v7.0.6 at layer 228 (`/Users/pasha/xCode/tlgrm/tdesktop`).
**Date:** 2026-07-30

## Headline

Of 18 tools marked IMPOSSIBLE across the feasibility sweep, **only 5 are genuinely impossible** — the capability does not exist for any client, official included. The other **13 are recoverable**, and they collapse into four reusable patterns rather than thirteen separate problems.

| Outcome | Count | Tools |
|---|---|---|
| Truly impossible — delete or replace | 5 | `create_gift_auction`, `cancel_auction`, `set_reaction_price`, `set_content_price`, `get_ad_filter_stats` |
| Recoverable — local series over real server snapshots | 3 | `backtest_strategy`, `get_rating_history`, `get_price_predictions` |
| Recoverable — client-side watcher / differ | 3 | `create_auction_alert`, `get_auction_alerts`, `get_subscription_alerts` |
| Recoverable — honest `LocalOnly` label, already works | 2 | `set_chatbot_prompt`, `delete_chat_rule` |
| Recoverable — write the counter we already own | 1 | `get_auto_reply_stats` |
| Recoverable — swap for the real server-side control | 1 | `configure_ad_filter` |
| **Misclassified** — actually PARTIAL, not impossible | 1 | `refund_content` |
| Recoverable but heuristic — derived from history | 2 | `get_greeting_stats`, `get_away_stats` |

The most important structural observation: **"no API for X" was conflated with "X is impossible."** In a persistent desktop client with a scheduler and a database, the absence of a *server-side* history or alert endpoint does not make history or alerts impossible — it makes them *our* job. Three of the four recovery patterns below are just that realisation applied consistently.

---

## The four recovery patterns

### Pattern 1 — Local time series over real server snapshots
**Problem shape:** "layer 228 exposes only the current value, no time series."
**Route:** poll the real endpoint on a schedule, persist each snapshot with a timestamp, serve the series from local storage. The *data* is genuine Telegram data; only the *retention* is ours.
**Why it's sound here:** the fork already ships `message_scheduler.cpp` and a SQLite layer. This is retention, not fabrication — and it is honestly describable as "history since this client started observing."
**Critical distinction from the current stubs:** today these tables are populated by *the caller's own inputs* (e.g. `price_history`'s only writer is `toolListGiftForSale:494`, the user's own asking price). Pattern 1 replaces that with server-sourced values. Same table, entirely different epistemic status.

### Pattern 2 — Client-side watcher / differ
**Problem shape:** "no server-side alert or subscription API."
**Route:** snapshot real state on a schedule, diff against the previous snapshot, emit on change. Alerts become real because the *state* is real.
**Why it's sound here:** an always-on client is exactly the right place for this, and w1's own note conceded the route: *"only a client poll loop over `payments.getStarGiftAuctionState#5c9ff4d6` could implement it."* That is a route, not a dead end.

### Pattern 3 — Honest `LocalOnly` labelling
**Problem shape:** "this concept has no TL representation."
**Route:** none needed — the feature already works. The defect is that it is advertised indistinguishably from Telegram-backed data. Mark it `LocalOnly` in the tool table and name it so no caller mistakes it.
**Why it matters:** these are legitimate fork features. Deleting them would lose real function; leaving them unlabelled is the actual bug.

### Pattern 4 — Write the counter we already own
**Problem shape:** "this metric is structurally always zero."
**Route:** increment it at the site that already runs. Where the fork reimplemented a feature locally, it *is* the thing performing the action — so it can count. The bug is a missing write, not a missing API.

---

## Itemized analysis

### A. Truly impossible — the capability does not exist

**1. `create_gift_auction`** (`mcp_stars_tools.cpp:160`)
*Why:* star-gift auctions at layer 228 are **Telegram-run**. `starGiftAuctionState#771a4e66` (`api.tl:2139`) carries server-set rounds and `min_bid_amount`; the scheme exposes only get-state, get-active, get-acquired, and bid. There is no create method for anyone. The tool also carries a false "API not available in this version" comment at `:192`.
*Route to possible:* **none.** No client can create an auction.
*Do instead:* delete. Expose `get_auction_state` (`payments.getStarGiftAuctionState#5c9ff4d6`) and `place_auction_bid` (`inputInvoiceStarGiftAuctionBid#1ecafa10` → `payments.getPaymentForm` → `payments.sendStarsForm#7998c914`), both of which are genuinely implementable.

**2. `cancel_auction`** (`:349`)
*Why:* no cancel or withdraw-bid method exists at 228. The nearest thing — a *resale* delist via `updateStarGiftPrice` with 0 — operates on a different object. A placed bid cannot be rescinded through the API.
*Route to possible:* **none.**
*Do instead:* delete. If bid regret matters, surface it as a pre-bid confirmation in `place_auction_bid`, which is the only point where the user still has a choice.

**3. `set_reaction_price`** (`:743`)
*Why:* paid reactions have no per-chat price. `reactionPaid#523da4eb` (`api.tl:1627`) carries no amount — the **sender** chooses `count` in `messages.sendPaidReaction#58bbcb50` (`:2674`). There is nothing for a recipient to set. Today the tool returns its arguments verbatim with no DB write and no API call.
*Route to possible:* **none** — the concept doesn't exist in the protocol.
*Do instead:* delete, and expose `send_paid_reaction(peer, message_id, count)` against the real method.

**4. `set_content_price`** (`:811`)
*Why:* `stars_amount` is fixed at send time inside `messageMediaPaidMedia#a8852491` (`api.tl:143`), and `messages.editMessage` exposes no stars field. An already-posted paid post's price is immutable.
*Route to possible:* **none for existing content.**
*Do instead:* move the price to creation — `send_paid_media(peer, files[], stars_amount)`. The capability is real; only the "set afterwards" framing is impossible.

**5. `get_ad_filter_stats`** (`mcp_premium_tools.cpp:472`)
*Why:* `ads_blocked` (`:484`) is never incremented by any code path in `mcp/*.cpp` — structurally always 0. Pattern 4 would normally rescue this, but there is nothing to count once ad filtering is replaced by the real server-side control (item 17): Telegram doesn't "block" ads client-side, Premium suppresses them server-side.
*Route to possible:* **none that means anything.**
*Do instead:* delete.

### B. Misclassified — actually PARTIAL

**6. `refund_content`** (`mcp_stars_tools.cpp:912`)
*Claimed why:* `payments.refundStarsCharge#25ae8f4a` (`api.tl:2882`) is bot-side, taking `user_id` + `charge_id`; no user-facing refund exists.
*Correction:* that makes it **conditionally possible, not impossible.** The method exists and works — it requires the account to be operating as a bot and to know the `charge_id`. The fork has `bots.exportBotToken` territory and a bot framework; a refund tool gated on bot context with a required `charge_id` argument is implementable and honest.
*Do instead:* reclassify **PARTIAL**. Implement as `refund_stars_charge(user_id, charge_id)`, and have it fail cleanly with an explanatory error when the session is not a bot. Today it writes a fake `+price` row with `status:"refunded"` — a fabricated financial record, and the second-worst defect class in this codebase after `send_gift`'s phantom debit.

### C. Recoverable — Pattern 1 (local series over real snapshots)

**7. `backtest_strategy`** (`mcp_settings_tools.cpp:1509`)
*Why claimed impossible:* pure local math over `price_history`, whose only writer is the caller's own asking price (`toolListGiftForSale:494`). No Telegram API involved and no server source populates the table.
*Route to possible:* feed `price_history` from `payments.getResaleStarGifts` on a schedule. The backtest then runs over **real observed market prices**. Backtesting remains local analysis — that's fine and honest — but it stops being a simulation over the user's own guesses.
*Resulting class:* IMPLEMENTABLE as `LocalOnly` analysis over server-sourced data. Must state the observation window in its output.

**8. `get_rating_history`** (`:1769`)
*Why claimed impossible:* layer 228 exposes the current rating plus `stars_my_pending_rating` only; no time series.
*Route to possible:* snapshot the real current rating on a schedule; serve the accumulated series. Label it "history since first observation," not "your rating history," because it cannot reconstruct the past.
*Resulting class:* IMPLEMENTABLE with an explicit `observed_from` field in the response.

**9. `get_price_predictions`** (`mcp_stars_tools.cpp:1066`)
*Why claimed impossible:* moving-average extrapolation over self-entered prices. No market history at 228, so `predicted_price` and `trend` are arithmetic over the caller's own inputs.
*Route to possible:* Pattern 1 makes the *history* real. **Prediction itself should still go.** Forecasting is not a Telegram capability, and a tool that emits `predicted_price` invites a caller to treat a local extrapolation as market intelligence — the same harm class as the fabricated balances.
*Do instead:* replace with `get_resale_price_history(gift_id, since)` returning real observed listings. Drop the prediction framing entirely.

### D. Recoverable — Pattern 2 (client-side watcher)

**10. `create_auction_alert`** (`mcp_settings_tools.cpp:1401`)
*Why claimed impossible:* no server-side alert or subscription API; today it writes local `price_alerts` rows with an `auction:` prefix.
*Route to possible:* register the watch locally, then have a scheduled task poll `payments.getStarGiftAuctionState#5c9ff4d6` and fire when the condition trips. The alert is real because the auction state is real.
*Resulting class:* IMPLEMENTABLE. Needs a real scheduler entry, not just a DB row — which is exactly what it lacks today.

**11. `get_auction_alerts`** (`:1424`)
*Why claimed impossible:* reads back only the local rows written by item 10; nothing server-side to read.
*Route to possible:* becomes meaningful the moment item 10 is a real watcher — it then reports live watches with their last observed state.
*Resulting class:* IMPLEMENTABLE, dependent on item 10.

**12. `get_subscription_alerts`** (`:1983`)
*Why claimed impossible:* no server alert API; reads local `price_alerts` rows matching `sub:%`. The real subscription list is `payments.getStarsSubscriptions#32512c5`, which the tool never calls.
*Route to possible:* poll `getStarsSubscriptions`, diff against the last snapshot, and alert on renewal dates, price changes, and cancellations. All three are derivable from real data.
*Resulting class:* IMPLEMENTABLE. Note this tool has been reading a table adjacent to the answer while never asking the server.

### E. Recoverable — Pattern 3 (honest LocalOnly)

**13. `set_chatbot_prompt`** (`mcp_settings_tools.cpp:700`)
*Why claimed impossible:* an LLM persona has no TL representation.
*Route to possible:* **it already works.** This is a legitimate fork-local feature backed by `local_llm.cpp` (verified initialized at `mcp_server_complete.cpp` startup). The only defect is that it is advertised indistinguishably from Telegram-backed tools.
*Do instead:* mark `Backing::LocalOnly`, and name it so the boundary is visible — e.g. `set_local_chatbot_prompt`.

**14. `delete_chat_rule`** (`:841`)
*Why claimed impossible:* "chat rules" is a fork-local automation concept with no TL counterpart.
*Route to possible:* same as above — already functional, needs labelling. Note the *rules engine* it belongs to should be audited separately for whether the rules ever actually fire; a delete tool for rules that never execute is cosmetic.

### F. Recoverable — Pattern 4 (write the counter we own)

**15. `get_auto_reply_stats`** (`mcp_business_tools.cpp:1546`)
*Why claimed impossible:* sums `times_triggered` (`:1551`), but that column is **never incremented anywhere** in `mcp/*.cpp` — only ever SELECTed. Structurally always zero.
*Route to possible:* increment it at the auto-reply send site. Because the fork reimplemented auto-replies **locally**, our own code is what sends them — so it is fully able to count them. This is a missing write, not a missing API.
*Resulting class:* IMPLEMENTABLE as `LocalOnly`. One `UPDATE` at the send path converts a permanently-zero stat into a true one.

### G. Recoverable — swap for the real control

**16. `configure_ad_filter`** (`mcp_premium_tools.cpp:446`)
*Why claimed impossible:* writes a local `ad_filter_config` (`:453`). Keyword-based ad filtering has no API — **Telegram's actual mechanism is that Premium suppresses sponsored messages server-side.**
*Route to possible:* implement the real capability instead: `account.toggleSponsoredMessages#b9d9a38d`, reachable via `Api::SponsoredToggle` (`api/api_premium.h:238`, `api_premium.cpp:746`).
*Do instead:* replace with `set_sponsored_messages_enabled(bool)`. Keyword filtering stays impossible; turning sponsored messages off is real, one call, and is what the user actually wanted.

### H. Recoverable but heuristic

**17. `get_greeting_stats`** (`mcp_settings_tools.cpp:736`) and **18. `get_away_stats`** (`:2346`)
*Why claimed impossible:* `account.updateBusinessGreetingMessage#66cdafc4` and `updateBusinessAwayMessage#a26a7fa5` set configuration only; layer 228 has no delivery counters.
*Route to possible — with a real caveat:* Business greeting and away replies are dispatched **server-side** (that is their purpose — they fire while you are offline), so Pattern 4 does not apply; our code never sends them and cannot count them. They do, however, land in chat history as outgoing messages. A count can therefore be **derived** by scanning history for messages matching the configured greeting/away text.
*Honest assessment:* this is text-matching heuristics, not telemetry. It will miscount if the greeting text changes, if the user sends the same text manually, or if the server templates it.
*Resulting class:* **PARTIAL**, and the response must carry a `method: "derived_from_history"` marker plus the matching window. If that caveat is unacceptable, deleting is better than a number that looks authoritative.

---

## Cross-cutting defect class discovered while doing this

Three of the 18 (`get_auto_reply_stats`, `get_ad_filter_stats`, plus `ads_blocked`-style columns elsewhere) share one shape: **a metric column that is SELECTed but never UPDATEd.** These report `0` forever while returning `success: true`, and no test catches them because zero is a plausible answer.

This is worth a mechanical sweep of its own during the port: for every counter column in the MCP schema, assert there is at least one writer. It is a grep-level check that would have caught all three, and it belongs in the same debug-assert pass as the tool-table uniqueness check.

## What this changes in the port plan

- **5 deletions** are now justified with evidence rather than assumption.
- **11 tools move out of the "can't be done" bucket**, most of them behind two small pieces of shared infrastructure: a **scheduled snapshot store** (Pattern 1) and a **watcher/differ** (Pattern 2). Build those two once and eight tools become implementable.
- **1 verdict was wrong** (`refund_content` is PARTIAL, not impossible) — worth noting that the sweep's own verdicts need this kind of adversarial re-reading before they drive deletions.
- The `Backing::LocalOnly` field in the proposed tool table is doing more work than expected: it is the honest home for 4–5 of these tools, and without it they can only be misadvertised or deleted.

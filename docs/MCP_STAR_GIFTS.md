# Stars — gifts, auctions, the marketplace and the wallet: what is real

The star-gift corner of the MCP surface was the worst offender for inventing
answers. Around twenty tools were backed by local SQLite tables — `auctions`,
`marketplace_listings`, `gift_collections`, `collection_items` — that no server
ever saw. A caller could "create an auction", read it back, "bid" on it, and
watch the bid rise, with none of it existing outside this client's database.

The rule applied here, and to be applied to whatever is fixed next:

> **A tool reports what happened. It never records what it wishes had
> happened.** If there is no API, the tool goes. If there is an API it cannot
> reach yet, it fails and says why. It does not write a plausible row.

## The two failure shapes

Worth naming, because they need different detection:

**Pure fabrication** — a local table stands in for the server. Easy to spot
once you look at the body.

**Declared-but-not-done** — the tool *claims* an `Mtproto` backing and either
never calls anything (`list_gift_for_sale`, `delist_gift`, both logging that
the API "was not available in this version" — it had been there all along), or
calls the right method and throws the reply away (`list_gift_collections`,
which handed real collections to `qWarning` and returned invented ones). The
four-site check passes cleanly on all of these: the drift is between the
declared backing and the behaviour, which no static check can see.

## Auctions

Telegram runs star-gift auctions itself. An auction is a property of the gift
being auctioned — `InputStarGiftAuction` is `{gift_id}` — so there is **no
auction id**, no client-side create, and no cancel anywhere in the schema.

| Tool | Now | Backed by |
|---|---|---|
| `create_gift_auction` | **removed** | nothing — no create method exists |
| `cancel_auction` | **removed** | nothing — no cancel method exists |
| `list_auctions` | real | `payments.getStarGiftActiveAuctions` |
| `get_auction_status` | real | `payments.getStarGiftAuctionState` |
| `get_auction_history` | real | `payments.getStarGiftAuctionAcquiredGifts` |
| `place_bid` | refuses | spends stars |

The removals are not deferrals — no further work makes them possible.

Verified live: `list_auctions` returns `[]` because none are running, and
`get_auction_history` with a fabricated gift id returns **`STARGIFT_INVALID`
from Telegram**, which a local table could never do.

## The resale market

Telegram resells unique gifts **by gift type**: you browse one gift's listings
and get the individual copies on sale, each with its own asking price and issue
number. There is no global feed, no free-text category, and no listing id — a
copy is identified by its slug, and your own gift by the message id it occupies
in your profile. The old `category` / `sort_by=recent` / `listing_id` shape had
nothing to map onto because it described a local table.

| Tool | Now | Backed by |
|---|---|---|
| `list_marketplace` | real | `payments.getResaleStarGifts` |
| `list_gift_for_sale` | real | `payments.updateStarGiftPrice` |
| `delist_gift` | real | same call, price zero |
| `buy_gift` | refuses | spends stars |

`list_gift_for_sale` and `delist_gift` delegate to `update_listing`, which
already drove `updateStarGiftPrice` correctly, rather than repeating it — that
is also how they inherit its slug-or-msg_id addressing.

## Collections

Collection membership is server state, so there is nothing for a local table to
add.

| Tool | Now | Backed by |
|---|---|---|
| `list_gift_collections` | real | `payments.getStarGiftCollections` |
| `get_collection_details` | real | same |
| `get_collection_completion` | real | that plus `payments.getSavedStarGifts` |
| `create_gift_collection` | real | `payments.createStarGiftCollection` |
| `delete_gift_collection` | **new** | `payments.deleteStarGiftCollection` |
| `add_to_collection` | real | `payments.updateStarGiftCollection` |
| `remove_from_collection` | real | same call, other vector |
| `share_collection` | **removed** | nothing — see below |

`create_gift_collection` dropped its `description` and `public` parameters:
Telegram collections have a title and their gifts, nothing else. It now requires
`msg_ids`, because `createStarGiftCollection` takes a non-optional
`Vector<InputSavedStarGift>`. Leaving it writing local rows would have been
worse than before this pass, since `list_gift_collections` returns server
collections and would never have shown them.

One thing testing turned up, contrary to what the TL signature suggests: the
server **silently drops gift references you do not own and creates the
collection empty** rather than refusing. A `success: true` is therefore not
proof the gifts went in — check `gifts_count` in the reply.

`delete_gift_collection` is new. The surface could create, list and add to
collections but never delete one, so anything created was permanent as far as
an MCP caller was concerned. `payments.deleteStarGiftCollection` had been there
the whole time.

`share_collection` set `is_public` on a local row and reported a collection
shared with a user who was never told anything. Gift collections have no share,
publish or link method in the schema, and no visibility flag at all.

`get_collection_completion` used to divide one orphaned table by another and,
with nothing writing to either, would have reported 0% forever. It is now a
ratio over two counts Telegram returns.

## Deferred, and on what

Deferred means implementable, not impossible.

| Tool | Blocked on |
|---|---|
| `place_bid` | stars — `inputInvoiceStarGiftAuctionBid` |
| `buy_gift` | stars — `inputInvoiceStarGiftResale` |
| `send_gift` | stars — the gift payment form |

The account holds **0 stars and 0 gifts**, so none can be exercised end-to-end
even once written. The same emptiness stops the *written* tools from being
exercised too: `add_to_collection` and `list_gift_for_sale` need a gift to point
at, so they can only be shown reaching Telegram and being refused, not
succeeding.

The star-spending ones are marked `Unimplemented` and fail through the
`callTool()` gate — a gate that had never fired before, since the August audit
recorded zero `Unimplemented` entries and noted it "protects nothing today".

## Impossible as named

`send_stars` cannot be finished. Telegram has **no user-to-user star
transfer**: `inputInvoiceStars` tops up your own balance, and
`inputStorePaymentStarsGift` buys stars for someone with real money, not with
stars you hold. The nearest route is a gift the recipient converts with
`convertStarGift` — a different operation, at their discretion, usually worth
less than the gift cost. It must not be described as an equivalent transfer.

It used to write a **negative row into `wallet_spending`** and answer success
with status `"recorded"`, so a transfer that never happened appeared in the
spending history while the recipient got nothing. `buy_gift` did the same and
also inserted a `gift_transfers` row claiming a gift had arrived. Both
corrupted the very ledger `get_balance_history` and `get_stars_history` read
back; both now write nothing.

## The wallet and earnings

Fixing the tools above orphaned `wallet_spending` — and that exposed five more
writers of it, all fabricating in the same way.

| Tool | Now | Why |
|---|---|---|
| `send_gift` | refuses | spends stars through the payment form |
| `subscribe_to_channel` | refuses | a recurring purchase behind an invite link |
| `withdraw_earnings` | refuses | needs an SRP proof of the 2FA password |
| `refund_content` | real | `payments.refundStarsCharge` |
| `request_stars` | **removed** | Telegram has no such feature |

`send_gift` was the one I had previously called "deferred" — wrongly. It wrote a
negative `wallet_spending` row and a `gift_transfers` row marked `sent`, then
returned a `transaction_id` that was a SQLite rowid dressed as Telegram's. It
also claimed `checkCanSendGift` was "not available in this version"; that method
exists.

`withdraw_earnings` is the clearest case of a request that could only fail being
used as cover for a fabricated row. It passed `inputCheckPasswordEmpty` — not a
way to "trigger a 2FA prompt", just a password the server rejects — attached a
`.fail()` that logged, **had no `.done()` at all**, then wrote a negative row and
answered success with status `"password_required"`. Money recorded as leaving an
account that had paid out nothing.

`subscribe_to_channel` recorded "subscription intent". An intent is not a
subscription, and storing one in the spending log only made the two hard to tell
apart.

`request_stars` composed a message asking for stars, decided sending it was
"intrusive", stored a row and reported success — so the person being asked was
never contacted. Its own note said Telegram has no native request feature. Use
`send_message`.

`refund_content` now calls the real method, and testing turned up its ceiling:
it answers **`USER_BOT_REQUIRED`**. `refundStarsCharge` is bot-only, so this
tool is correct but unusable from a user account — worth knowing before
debugging it as broken. Its arguments changed from a local `content_id` to the
`user_id` and `charge_id` that identify an actual charge.

With those five fixed, `get_balance_history` returns an empty history rather
than a fictional one. That is the honest answer for an account with no
recorded activity, and it stays `LocalOnly` because it reads a local ledger by
design — `get_stars_history` is the tool that asks Telegram.

## Aliases

Several names are pass-throughs, kept so existing callers keep working, with
their schemas corrected to match what they delegate to:

| Alias | Delegates to |
|---|---|
| `list_active_auctions` | `list_auctions` |
| `get_auction_details` | `get_auction_status` |
| `place_auction_bid` | `place_bid` |
| `browse_gift_marketplace`, `get_fragment_listings` | `list_marketplace` |
| `cancel_listing` | `delist_gift` |

`get_fragment_listings` was labelled "this client's local marketplace table
(not Telegram Fragment)" — accurate when written, wrong once the tool it
delegates to started querying Telegram.

These six names are **kept, deliberately.** They cost nothing now that their
schemas match what they delegate to, and removing a name that a caller may
already reference breaks that caller for no gain. This is a settled decision,
not an open question — do not remove them on the grounds that they duplicate
another tool.

## The checker

`tools/check_mcp_tools.py` gained one rule here: it follows a delegation
through a **copy** of the args object, not only the literal parameter. Without
it, `delist_gift` — which forwards its gift with a price of zero — looked like
a tool ignoring its entire schema.

It still compares declarations, not truth. It cannot tell whether a tool's
answer is real, which is why this document exists.

## Count

352 tools, 610 described parameters, four declaration sites agreeing — down
from 355 by four removals (`create_gift_auction`, `cancel_auction`,
`share_collection`, `request_stars`) against one addition
(`delete_gift_collection`).

## Still local, deliberately

`create_auction_alert` and `get_auction_alerts` remain `LocalOnly`, correctly:
they are this client's own reminders and Telegram has no notion of them. Their
descriptions now say so.

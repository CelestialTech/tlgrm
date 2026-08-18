# Star gifts, auctions and the marketplace — what is real

The star-gift corner of the MCP surface was the worst offender for inventing
answers. Fourteen tools were backed by three SQLite tables — `auctions`,
`marketplace_listings`, `gift_collections` — that no server ever saw. A caller
could "create an auction", read it back, "bid" on it, and watch the bid rise,
with none of it existing outside this client's database.

The rule applied here, and to be applied to whatever is fixed next:

> **A tool reports what happened. It never records what it wishes had
> happened.** If there is no API, the tool goes. If there is an API it cannot
> reach yet, it fails and says why. It does not write a plausible row.

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
| `place_bid` | refuses | spends stars; see below |

The two removals are not deferrals. No amount of further work makes them
possible, because the server has no method to call — they were removed from all
four declaration sites rather than left advertised.

`get_auction_status` and `get_auction_history` took `auction_id` (a UUID this
client minted). They now take `gift_id`, an integer, matching how Telegram
identifies an auction.

Verified live against the account: `list_auctions` returns `[]` because no
auction is running, and `get_auction_history` with a fabricated gift id returns
**`STARGIFT_INVALID` from Telegram** — the request leaves the client and the
server rejects it, which is exactly what a local table could never do.

## Collections

`list_gift_collections` was the subtlest failure. It *did* call
`payments.getStarGiftCollections` — then passed the reply to `qWarning` and
returned rows from the local `gift_collections` table. The real collections went
to stderr; the caller got invented ones. Its backing was already declared
`Mtproto`, so the four-site check saw nothing wrong: the drift was between the
declared backing and the behaviour, which no static check catches.

It now returns the account's real collections (`collection_id`, `title`,
`gifts_count`). Verified live: `[]`, because the account has none.

## What is deferred, and on what

Deferred means implementable, not impossible — blocked on something this account
does not currently have.

| Tool | Blocked on |
|---|---|
| `place_bid` | stars — a bid is an invoice (`inputInvoiceStarGiftAuctionBid`) |
| `buy_gift` | stars — `inputInvoiceStarGiftResale` |
| `send_gift` | stars — the gift payment form |
| `create_gift_collection` | owning a gift; `createStarGiftCollection` requires a non-empty `Vector<InputSavedStarGift>` |
| `list_gift_for_sale`, `delist_gift` | owning a gift (`updateStarGiftPrice` is free, but needs one) |

The account holds **0 stars and 0 gifts**, so none of these can be exercised
end-to-end even after being written. `place_bid` is marked `Unimplemented` and
now fails through the `callTool()` gate. That gate had never fired before —
the August audit recorded zero `Unimplemented` entries and noted it "protects
nothing today".

## Still fabricated — next in line

These remain backed by local tables and still invent answers. They have real
counterparts and are the next thing to fix:

| Tool | Should use |
|---|---|
| `list_marketplace` | `payments.getResaleStarGifts` |
| `get_collection_details` | `payments.getStarGiftCollections` |
| `share_collection` | nothing — likely another removal |
| `send_stars` | no direct transfer exists; the gift path plus `convertStarGift` is the only equivalent |

`send_stars` deserves care: there is no user-to-user star transfer in the API.
The nearest equivalent is sending a gift the recipient then converts to stars,
which is not the same operation and must not be described as if it were.

The `auctions` and `marketplace_listings` tables are now orphaned by the auction
work; `marketplace_listings` still backs the marketplace tools above.

## Aliases

`list_active_auctions`, `get_auction_details` and `place_auction_bid` are
pass-throughs to `list_auctions`, `get_auction_status` and `place_bid`. They
were kept so existing callers keep working, and their schemas were corrected to
match. Whether to keep three duplicate names on the surface is an open question,
not a decision this pass made.

## Count

353 tools, 610 described parameters, four declaration sites agreeing —
down from 355 by the two removals. `tools/check_mcp_tools.py` enforces the
agreement at build time; it cannot tell whether a tool's answer is true, which
is why this document exists.

# MCP Wallet Feature Implementation Analysis

This document analyzes all Telegram Wallet/Stars/TON features and determines which can be implemented via MCP.

**Analysis Date:** 2025-11-28

---

## Summary

| Category | Count | Features |
|----------|-------|----------|
| **CAN IMPLEMENT** | 8 | Full read access + analytics via MCP |
| **PARTIAL** | 4 | Read-only or limited functionality |
| **BEYOND WALLET** | 3 | MCP-exclusive crypto/payment integrations |
| **CANNOT IMPLEMENT** | 4 | Server-enforced payment processing |

**Total New Tools:** 32

---

## Telegram Wallet Features (15 total)

### Core Wallet
1. Telegram Stars (balance, top-up, spend)
2. TON Balance (for channel monetization)
3. Transaction History
4. Subscriptions Management

### Stars Usage
5. Star Gifts (send/receive)
6. Paid Media (unlock content)
7. Star Reactions
8. Mini App Purchases
9. Premium Gifts

### Monetization
10. Channel Ad Revenue (50% split)
11. Bot Earnings
12. Paid Posts
13. Star Ratings

### Advanced
14. Giveaways
15. Fragment Integration (withdrawals)

---

## CAN IMPLEMENT via MCP (8 features)

### 1. Wallet Balance & Analytics
**Feature:** View Stars and TON balance with detailed analytics

**MCP Implementation:**
- Real-time balance tracking via `Api::CreditsStatus`
- Balance history over time
- Spending patterns analysis
- Income vs outflow reports

```
Tool: get_wallet_balance
Args: {}
Returns: {
  "stars": {
    "balance": 1250,
    "pending": 50,
    "last_updated": timestamp
  },
  "ton": {
    "balance": 12.5,
    "available_for_withdrawal": 10.0,
    "usd_value": 25.00,
    "usd_rate": 2.00
  }
}

Tool: get_balance_history
Args: { "period": "30d", "currency": "stars" }
Returns: {
  "history": [
    { "date": "2024-11-01", "balance": 1000, "change": +50 },
    { "date": "2024-11-02", "balance": 1050, "change": +200 },
    ...
  ],
  "summary": {
    "total_earned": 500,
    "total_spent": 250,
    "net_change": 250
  }
}

Tool: get_spending_analytics
Args: { "period": "30d" }
Returns: {
  "by_category": {
    "gifts": 100,
    "paid_media": 50,
    "reactions": 25,
    "mini_apps": 75
  },
  "by_recipient": [
    { "peer_id": 123, "name": "Channel A", "amount": 50 },
    ...
  ],
  "trends": {
    "daily_average": 8.3,
    "peak_day": "2024-11-15",
    "peak_amount": 100
  }
}
```

---

### 2. Transaction History
**Feature:** Complete transaction history with filtering

**MCP Implementation:**
- Uses `Api::CreditsHistory` API
- Full transaction details from `CreditsHistoryEntry`
- Filter by type (in/out), date, amount, peer

```
Tool: get_transactions
Args: {
  "filter": "all",  // "incoming", "outgoing", "gifts", "subscriptions"
  "limit": 50,
  "offset": 0,
  "date_from": "2024-11-01",
  "date_to": "2024-11-30",
  "min_amount": 10,
  "peer_id": null  // Filter by specific peer
}
Returns: {
  "transactions": [
    {
      "id": "tx_abc123",
      "type": "gift_received",
      "amount": 50,
      "currency": "stars",
      "date": timestamp,
      "peer": { "id": 123, "name": "John", "type": "user" },
      "description": "Gift",
      "refunded": false,
      "pending": false
    },
    ...
  ],
  "total_count": 150,
  "has_more": true
}

Tool: get_transaction_details
Args: { "transaction_id": "tx_abc123" }
Returns: { detailed transaction info including linked messages, gifts, etc. }

Tool: export_transactions
Args: { "format": "csv", "period": "all" }
Returns: { "file_path": "/tmp/transactions_export.csv" }
```

---

### 3. Gift Management
**Feature:** View, track, and manage received/sent gifts

**MCP Implementation:**
- List all gifts with full metadata
- Gift value tracking
- Conversion status
- Analytics on gift patterns

```
Tool: list_gifts
Args: {
  "type": "all",  // "received", "sent", "converted"
  "include_unique": true
}
Returns: {
  "gifts": [
    {
      "id": "gift_123",
      "sticker_id": 456,
      "from_user": { "id": 789, "name": "Alice" },
      "date": timestamp,
      "stars_value": 100,
      "converted": false,
      "can_upgrade": true,
      "upgrade_cost": 50,
      "unique_gift": null,  // or unique gift details
      "pinned": true,
      "saved_to_profile": true
    },
    ...
  ],
  "total_value": 500,
  "convertible_value": 300
}

Tool: get_gift_details
Args: { "gift_id": "gift_123" }

Tool: get_gift_analytics
Args: {}
Returns: {
  "total_received": 25,
  "total_sent": 10,
  "total_value_received": 500,
  "total_value_sent": 200,
  "top_gifters": [{ "user_id": 123, "count": 5, "total_value": 150 }],
  "top_recipients": [...],
  "by_month": [{ "month": "2024-11", "received": 5, "sent": 2 }]
}
```

---

### 4. Subscription Management
**Feature:** View and manage Star subscriptions

**MCP Implementation:**
- List active subscriptions via `requestSubscriptions`
- Track subscription costs
- Renewal alerts
- Cancellation handling

```
Tool: list_subscriptions
Args: { "status": "all" }  // "active", "cancelled", "expired"
Returns: {
  "subscriptions": [
    {
      "id": "sub_123",
      "title": "Premium Channel Access",
      "peer": { "id": 456, "name": "News Channel" },
      "cost_per_month": 50,
      "currency": "stars",
      "started_at": timestamp,
      "renews_at": timestamp,
      "status": "active",
      "total_paid": 150
    },
    ...
  ],
  "total_monthly_cost": 100,
  "next_renewal": timestamp
}

Tool: get_subscription_alerts
Args: {}
Returns: {
  "renewals_this_week": [{ subscription details }],
  "low_balance_warning": true,  // If balance < monthly costs
  "recommended_topup": 50
}
```

---

### 5. Channel Monetization Stats
**Feature:** Detailed analytics for channel earnings

**MCP Implementation:**
- Uses `Api::CreditsEarnStatistics`
- Revenue graphs
- Ad impression tracking
- Withdrawal readiness

```
Tool: get_channel_earnings
Args: { "channel_id": int }
Returns: {
  "overview": {
    "current_balance": 1000,
    "available_for_withdrawal": 800,
    "overall_revenue": 5000,
    "usd_rate": 0.02,
    "usd_value": 100.00
  },
  "withdrawal": {
    "enabled": true,
    "next_available_at": timestamp,
    "minimum_amount": 100
  },
  "ads_url": "https://ads.telegram.org/..."
}

Tool: get_earnings_chart
Args: { "channel_id": int, "period": "30d" }
Returns: {
  "impressions": [{ "date": "2024-11-01", "count": 15000 }, ...],
  "revenue": [{ "date": "2024-11-01", "stars": 50, "ton": 0.5 }, ...],
  "cpm": 3.5  // Cost per mille
}

Tool: get_all_channels_earnings
Args: {}
Returns: {
  "channels": [
    { "id": 123, "name": "My Channel", "balance": 1000, "monthly_avg": 200 },
    ...
  ],
  "total_balance": 2500,
  "total_monthly_revenue": 500
}
```

---

### 6. Star Reactions Analytics
**Feature:** Track Star reactions sent and received

**MCP Implementation:**
- Monitor reactions on your content
- Track your reaction spending
- Top supporters leaderboard

```
Tool: get_reaction_stats
Args: { "channel_id": int, "period": "30d" }
Returns: {
  "received": {
    "total_stars": 500,
    "total_reactions": 150,
    "unique_senders": 75,
    "top_senders": [{ "user_id": 123, "stars": 50, "count": 10 }]
  },
  "top_posts": [
    { "message_id": 456, "stars": 100, "reactions": 25 }
  ]
}

Tool: get_my_reaction_spending
Args: { "period": "30d" }
Returns: {
  "total_spent": 200,
  "by_channel": [{ "channel_id": 123, "stars": 100 }],
  "by_post": [{ "message_id": 456, "stars": 50 }]
}
```

---

### 7. Paid Content Analytics
**Feature:** Track paid media purchases and earnings

**MCP Implementation:**
- Content you've unlocked
- Content you've monetized
- Revenue per post

```
Tool: get_unlocked_content
Args: { "limit": 50 }
Returns: {
  "content": [
    {
      "message_id": 123,
      "chat_id": 456,
      "type": "photo",
      "cost": 25,
      "unlocked_at": timestamp
    },
    ...
  ],
  "total_spent": 300
}

Tool: get_paid_content_earnings
Args: { "channel_id": int }
Returns: {
  "posts": [
    {
      "message_id": 123,
      "type": "video",
      "price": 50,
      "unlocks": 100,
      "revenue": 5000,
      "commission": 1000,
      "net_revenue": 4000
    },
    ...
  ],
  "total_revenue": 10000,
  "total_unlocks": 500
}
```

---

### 8. Giveaway Management
**Feature:** Create and track giveaways

**MCP Implementation:**
- Uses `Api::CreditsGiveawayOptions`
- View giveaway options and pricing
- Track active giveaways
- Winner history

```
Tool: get_giveaway_options
Args: { "channel_id": int }
Returns: {
  "options": [
    {
      "stars": 1000,
      "currency": "USD",
      "price": 50.00,
      "winners": [
        { "count": 10, "stars_per_user": 100, "is_default": true },
        { "count": 5, "stars_per_user": 200 }
      ],
      "yearly_boosts": 10
    },
    ...
  ]
}

Tool: list_giveaways
Args: { "channel_id": int, "status": "all" }
Returns: {
  "giveaways": [
    {
      "id": "giveaway_123",
      "channel_id": 456,
      "total_stars": 1000,
      "winners_count": 10,
      "start_date": timestamp,
      "end_date": timestamp,
      "status": "completed",
      "winners": [{ "user_id": 789, "stars_won": 100 }]
    },
    ...
  ]
}
```

---

## PARTIALLY IMPLEMENTABLE (4 features)

### 9. Top-up Options (Read-Only)
**Feature:** View available top-up packages

**MCP Can Do:**
- List all top-up options and prices via `Api::CreditsTopupOptions`
- Compare pricing across platforms

**MCP Cannot Do:**
- Actually process payment (requires App Store/Play Store)

```
Tool: get_topup_options
Args: {}
Returns: {
  "options": [
    { "stars": 100, "price": 1.99, "currency": "USD", "platform": "ios" },
    { "stars": 500, "price": 7.99, "currency": "USD", "platform": "ios" },
    { "stars": 1000, "price": 14.99, "currency": "USD", "platform": "ios" },
    ...
  ],
  "best_value": { "stars": 2500, "price_per_star": 0.012 }
}
```

---

### 10. Send Stars (Balance-Dependent)
**Feature:** Send Stars to users/channels

**MCP Can Do:**
- Initiate Star transfer if balance is sufficient
- The API exists, we just can't add balance

```
Tool: send_stars
Args: {
  "recipient_id": int,
  "amount": 50,
  "message": "Thanks for the great content!"
}
Returns: {
  "success": true,
  "transaction_id": "tx_123",
  "new_balance": 1200
}
// OR
Returns: {
  "success": false,
  "error": "insufficient_balance",
  "current_balance": 30,
  "required": 50
}
```

---

### 11. Star Ratings (View Only)
**Feature:** View your star rating and what affects it

**MCP Can Do:**
- Read current rating
- See rating factors

**MCP Cannot Do:**
- Manipulate rating (server-calculated)

```
Tool: get_star_rating
Args: {}
Returns: {
  "current_rating": 85,
  "future_rating": 90,
  "updates_in_days": 3,
  "factors": {
    "gifts_from_telegram": { "impact": "+30", "details": "100% of Stars from Telegram gifts" },
    "gifts_from_users": { "impact": "+15", "details": "20% of resold gifts" },
    "refunds": { "impact": "-5", "details": "10x of refunded Stars" }
  },
  "is_negative": false
}
```

---

### 12. Withdrawal Status (Read-Only)
**Feature:** Check TON withdrawal eligibility

**MCP Can Do:**
- Check if withdrawal is enabled
- See minimum amounts
- Track pending withdrawals

**MCP Cannot Do:**
- Actually withdraw (requires Fragment)

```
Tool: get_withdrawal_status
Args: { "channel_id": int }
Returns: {
  "enabled": true,
  "available_balance": 500,
  "minimum_withdrawal": 100,
  "next_available_at": timestamp,
  "pending_withdrawals": [
    { "amount": 100, "status": "processing", "initiated_at": timestamp }
  ],
  "fragment_url": "https://fragment.com/..."
}
```

---

## CANNOT IMPLEMENT - Server Enforced (4 features)

| # | Feature | Why Impossible |
|---|---------|----------------|
| 1 | **Buy Stars** | Requires Apple/Google payment processing |
| 2 | **Withdraw TON** | Server-side Fragment integration required |
| 3 | **Create Giveaways** | Requires payment for prize pool |
| 4 | **Boost Channels** | Server validates Premium subscription |

---

## BEYOND WALLET - MCP Exclusive Features

These features extend wallet functionality beyond what Telegram offers:

### 13. External Crypto Integration
**Feature:** Accept crypto payments via external wallets

**MCP Implementation:**
- Bridge to external crypto wallets (MetaMask, Phantom, etc.)
- Accept payments in any crypto
- Convert to Stars via exchanges

```
Architecture:
┌─────────────────┐     ┌──────────────┐     ┌─────────────────┐
│ MCP Server      │────▶│ pythonMCP    │────▶│ Crypto Backend  │
│                 │     │ (bridge)     │     │ - Web3.py       │
│                 │◀────│              │◀────│ - TON SDK       │
│                 │     │              │     │ - Exchange APIs │
└─────────────────┘     └──────────────┘     └─────────────────┘

Tool: create_crypto_payment
Args: {
  "amount_usd": 10.00,
  "accepted_currencies": ["TON", "USDT", "ETH"],
  "description": "Premium content access",
  "expiry_minutes": 30
}
Returns: {
  "payment_id": "pay_123",
  "addresses": {
    "TON": "EQ...",
    "USDT": "0x...",
    "ETH": "0x..."
  },
  "amounts": {
    "TON": 5.0,
    "USDT": 10.0,
    "ETH": 0.003
  },
  "qr_codes": { ... },
  "expiry": timestamp
}

Tool: check_payment_status
Args: { "payment_id": "pay_123" }
Returns: {
  "status": "confirmed",  // "pending", "expired", "failed"
  "received_currency": "TON",
  "received_amount": 5.0,
  "transaction_hash": "..."
}
```

---

### 14. Financial Reporting
**Feature:** Generate tax-ready financial reports

**MCP Implementation:**
- Aggregate all transactions
- Convert to fiat values at time of transaction
- Generate reports for tax compliance

```
Tool: generate_financial_report
Args: {
  "year": 2024,
  "format": "pdf",  // "csv", "json"
  "include_fiat_conversion": true,
  "currency": "USD"
}
Returns: {
  "report_path": "/tmp/telegram_financial_2024.pdf",
  "summary": {
    "total_income_usd": 1500.00,
    "total_expenses_usd": 300.00,
    "net_usd": 1200.00,
    "transactions_count": 250,
    "gifts_received_value": 500.00,
    "gifts_sent_value": 100.00,
    "ad_revenue": 800.00,
    "paid_content_revenue": 200.00
  }
}

Tool: get_tax_summary
Args: { "year": 2024, "jurisdiction": "US" }
Returns: {
  "income_categories": [
    { "category": "Gifts Received", "amount": 500, "taxable": true },
    { "category": "Ad Revenue", "amount": 800, "taxable": true },
    { "category": "Paid Content", "amount": 200, "taxable": true }
  ],
  "deductible_expenses": [
    { "category": "Gifts Sent", "amount": 100 }
  ],
  "notes": "Consult a tax professional for your specific situation"
}
```

---

### 15. Budget & Alerts
**Feature:** Set spending limits and alerts

**MCP Implementation:**
- Track spending against budgets
- Alert on large transactions
- Low balance warnings

```
Tool: set_wallet_budget
Args: {
  "daily_limit": 50,
  "weekly_limit": 200,
  "monthly_limit": 500,
  "alert_threshold": 0.8,  // Alert at 80% of limit
  "categories": {
    "gifts": 100,
    "reactions": 50,
    "paid_content": 100
  }
}

Tool: get_budget_status
Args: {}
Returns: {
  "daily": { "spent": 30, "limit": 50, "remaining": 20, "percent": 60 },
  "weekly": { "spent": 150, "limit": 200, "remaining": 50, "percent": 75 },
  "monthly": { "spent": 400, "limit": 500, "remaining": 100, "percent": 80 },
  "alerts": [
    { "type": "approaching_limit", "category": "weekly", "percent": 75 }
  ]
}

Tool: configure_wallet_alerts
Args: {
  "low_balance_threshold": 50,
  "large_transaction_threshold": 100,
  "subscription_renewal_days": 3,
  "notify_via": "telegram"  // or "email", "both"
}
```

---

## Proposed MCP Tools Summary

### New Tools to Add (32 tools)

**Balance & Analytics (4 tools):**
1. `get_wallet_balance` - Current Stars/TON balance
2. `get_balance_history` - Balance over time
3. `get_spending_analytics` - Spending patterns
4. `get_income_analytics` - Income patterns

**Transactions (4 tools):**
5. `get_transactions` - List transactions with filters
6. `get_transaction_details` - Single transaction details
7. `export_transactions` - Export to CSV/JSON
8. `search_transactions` - Search by amount/peer/date

**Gifts (4 tools):**
9. `list_gifts` - All gifts received/sent
10. `get_gift_details` - Single gift details
11. `get_gift_analytics` - Gift statistics
12. `send_stars` - Send Stars (if balance permits)

**Subscriptions (3 tools):**
13. `list_subscriptions` - Active subscriptions
14. `get_subscription_alerts` - Renewal warnings
15. `cancel_subscription` - Cancel subscription

**Monetization (5 tools):**
16. `get_channel_earnings` - Single channel earnings
17. `get_all_channels_earnings` - All channels summary
18. `get_earnings_chart` - Revenue graphs
19. `get_reaction_stats` - Star reaction analytics
20. `get_paid_content_earnings` - Paid media revenue

**Giveaways (3 tools):**
21. `get_giveaway_options` - Available giveaway packages
22. `list_giveaways` - Active/past giveaways
23. `get_giveaway_stats` - Giveaway analytics

**Advanced (4 tools):**
24. `get_topup_options` - Top-up package prices
25. `get_star_rating` - Rating and factors
26. `get_withdrawal_status` - TON withdrawal status
27. `create_crypto_payment` - External crypto payment

**Budget & Reporting (5 tools):**
28. `set_wallet_budget` - Configure spending limits
29. `get_budget_status` - Current budget usage
30. `configure_wallet_alerts` - Set up alerts
31. `generate_financial_report` - Tax-ready reports
32. `get_tax_summary` - Tax categorization

---

## Implementation Priority

| Priority | Feature | Effort | Impact |
|----------|---------|--------|--------|
| **P0** | Balance & Transactions | Low | High - Core functionality |
| **P0** | Spending Analytics | Medium | High - Unique value |
| **P1** | Gift Management | Low | Medium - Popular feature |
| **P1** | Monetization Stats | Medium | High - Creator essential |
| **P2** | Budget & Alerts | Medium | Medium - Nice to have |
| **P2** | Financial Reporting | High | Medium - Tax compliance |
| **P3** | Crypto Integration | High | Low - Niche use |

---

## API Classes Used

From tdesktop source code:

```cpp
// api/api_credits.h
Api::CreditsTopupOptions     // Get top-up packages
Api::CreditsGiveawayOptions  // Get giveaway options
Api::CreditsStatus           // Get balance + history slice
Api::CreditsHistory          // Get transaction history
Api::CreditsEarnStatistics   // Get channel earnings

// data/data_credits.h
Data::CreditsHistoryEntry    // Single transaction
Data::CreditsStatusSlice     // Balance + transactions + subscriptions
Data::CreditTopupOption      // Top-up package details

// data/data_credits_earn.h
Data::CreditsEarnStatistics  // Channel monetization data
```

---

## Database Schema

```sql
-- Budget Configuration
CREATE TABLE wallet_budget (
  id INTEGER PRIMARY KEY,
  daily_limit INTEGER,
  weekly_limit INTEGER,
  monthly_limit INTEGER,
  alert_threshold REAL DEFAULT 0.8
);

-- Budget Category Limits
CREATE TABLE budget_categories (
  category TEXT PRIMARY KEY,
  monthly_limit INTEGER
);

-- Alert Configuration
CREATE TABLE wallet_alerts (
  id INTEGER PRIMARY KEY,
  alert_type TEXT,
  threshold INTEGER,
  enabled BOOLEAN DEFAULT 1
);

-- Spending Tracking (local cache)
CREATE TABLE spending_log (
  id INTEGER PRIMARY KEY,
  date TEXT,
  category TEXT,
  amount INTEGER,
  transaction_id TEXT
);

-- External Crypto Payments
CREATE TABLE crypto_payments (
  payment_id TEXT PRIMARY KEY,
  amount_usd REAL,
  status TEXT,
  created_at TIMESTAMP,
  expires_at TIMESTAMP,
  received_currency TEXT,
  received_amount REAL,
  transaction_hash TEXT
);
```

---

## Feature Comparison

| Capability | Telegram App | MCP Wallet |
|------------|--------------|------------|
| View balance | ✅ | ✅ |
| Transaction history | ✅ Basic | ✅ **Advanced filters** |
| Spending analytics | ❌ | ✅ **Detailed charts** |
| Budget limits | ❌ | ✅ **With alerts** |
| Tax reports | ❌ | ✅ **Auto-generated** |
| Multi-channel earnings | ❌ | ✅ **Aggregated view** |
| Crypto payments | ❌ | ✅ **External wallets** |
| Export data | ❌ | ✅ **CSV/JSON/PDF** |
| Gift analytics | ❌ | ✅ **Full tracking** |
| Subscription alerts | ❌ | ✅ **Proactive warnings** |

---

*Generated by Claude Code MCP Analysis*

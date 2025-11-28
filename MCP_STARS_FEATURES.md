# MCP Stars Feature Implementation Analysis

This document analyzes all Telegram Stars ecosystem features and possible MCP integrations.

**Analysis Date:** 2025-11-28

---

## Summary

| Category | Count | Features |
|----------|-------|----------|
| **CAN IMPLEMENT** | 12 | Full read access + management via MCP |
| **PARTIAL** | 4 | Read-only or limited functionality |
| **BEYOND STARS** | 4 | MCP-exclusive integrations |
| **CANNOT IMPLEMENT** | 3 | Server-enforced payment processing |

**Total New Tools:** 45

---

## Stars Ecosystem Overview

Telegram Stars has evolved into a full digital economy:

```
                    ┌─────────────────────┐
                    │   TELEGRAM STARS    │
                    └─────────────────────┘
                              │
       ┌──────────────────────┼──────────────────────┐
       │                      │                      │
       ▼                      ▼                      ▼
┌─────────────┐      ┌─────────────┐      ┌─────────────────┐
│   SPEND     │      │   GIFTS     │      │   EARN          │
├─────────────┤      ├─────────────┤      ├─────────────────┤
│ • Mini Apps │      │ • Star Gifts│      │ • Ad Revenue    │
│ • Paid Media│      │ • Unique    │      │ • Paid Content  │
│ • Reactions │      │   (NFT-like)│      │ • Paid Messages │
│ • Paid Msgs │      │ • Upgrades  │      │ • Gift Sales    │
│ • Premium   │      │ • Auctions  │      │ • Referrals     │
└─────────────┘      │ • Resale    │      └─────────────────┘
                     │ • Transfer  │
                     └─────────────┘
```

---

## Stars Features (19 total)

### Core Stars
1. Stars Balance
2. Stars Rating
3. Transaction History
4. Top-up Packages

### Star Gifts
5. Regular Star Gifts (send/receive)
6. Unique/Collectible Gifts (NFT-like)
7. Gift Upgrades
8. Gift Auctions
9. Gift Resale (marketplace)
10. Gift Transfer
11. Gift Collections
12. Gift Display (on profile)

### Stars Usage
13. Mini App Purchases
14. Paid Media Unlock
15. Star Reactions
16. Paid Messages
17. Premium Purchase

### Stars Earning
18. Ad Revenue (monetization)
19. Referral Programs

---

## CAN IMPLEMENT via MCP (12 features)

### 1. Star Gifts Management
**Feature:** Complete gift inventory management

**MCP Implementation:**
- Uses `Data::SavedStarGift`, `Data::StarGift`, `Data::UniqueGift`
- Full gift metadata access
- Filter by type, value, rarity

```cpp
// From data_star_gift.h
struct StarGift {
    uint64 id;
    int64 stars;
    int64 starsConverted;      // Conversion value
    int64 starsToUpgrade;      // Upgrade cost
    int64 starsResellMin;      // Minimum resale price
    DocumentData* document;     // Gift sticker
    int limitedLeft;           // Remaining if limited
    int limitedCount;          // Total if limited
    bool upgradable;
    bool soldOut;
};

Tool: list_star_gifts
Args: {
  "type": "all",  // "received", "sent", "unique", "regular", "upgradable"
  "sort": "value",  // "date", "rarity", "value"
  "filter": {
    "min_value": 100,
    "limited_only": false,
    "unique_only": false,
    "can_upgrade": true
  }
}
Returns: {
  "gifts": [
    {
      "id": "gift_123",
      "type": "unique",
      "stars_value": 500,
      "stars_converted": 425,  // 85% conversion
      "upgrade_cost": 50,
      "from_user": { "id": 123, "name": "Alice" },
      "date": timestamp,
      "sticker_id": 456,
      "limited": { "left": 10, "total": 1000 },
      "unique_info": {
        "title": "Golden Dragon #42",
        "slug": "golden_dragon_42",
        "number": 42,
        "model": "Dragon",
        "pattern": "Scales",
        "backdrop": "Gold",
        "rarity": { "model": 5, "pattern": 10, "backdrop": 2 }
      },
      "pinned": true,
      "displayed": true,
      "can_transfer": true,
      "can_resell": true
    },
    ...
  ],
  "total_count": 25,
  "total_value": 5000,
  "unique_count": 5
}

Tool: get_gift_details
Args: { "gift_id": "gift_123" }
Returns: { full gift details including original sender, message, upgrade history }
```

---

### 2. Unique/Collectible Gift Analytics
**Feature:** Track NFT-like collectible gifts

**MCP Implementation:**
- Full `UniqueGift` data structure access
- Rarity tracking
- Value history
- Market data

```cpp
// From data_star_gift.h
struct UniqueGift {
    CollectibleId id;
    QString slug;
    QString title;
    int number;
    UniqueGiftModel model;      // 3D model/sticker
    UniqueGiftPattern pattern;  // Pattern overlay
    UniqueGiftBackdrop backdrop; // Colors
    UniqueGiftValue value;      // Market value
    PeerId ownerId;
    bool canBeTheme;
};

struct UniqueGiftValue {
    int64 valuePrice;           // Current value
    int64 initialSalePrice;     // First sale
    int64 lastSalePrice;        // Recent sale
    int64 averagePrice;         // Market average
    int64 minimumPrice;         // Floor price
    int forSaleOnTelegram;      // Listed count
    int forSaleOnFragment;      // Fragment listings
    QString fragmentUrl;
};

Tool: get_unique_gift_analytics
Args: { "gift_id": "unique_123" }
Returns: {
  "gift": {
    "id": "unique_123",
    "title": "Golden Dragon #42",
    "number": 42,
    "slug": "golden_dragon_42"
  },
  "attributes": {
    "model": { "name": "Dragon", "rarity_permille": 50 },  // 5%
    "pattern": { "name": "Scales", "rarity_permille": 100 },  // 10%
    "backdrop": { "name": "Gold", "rarity_permille": 20 }  // 2%
  },
  "combined_rarity_score": 0.0001,  // Extremely rare
  "value": {
    "current_price_stars": 5000,
    "initial_price_stars": 100,
    "last_sale_stars": 4500,
    "average_stars": 4200,
    "floor_stars": 3000,
    "appreciation": "+4800%"
  },
  "market": {
    "for_sale_telegram": 3,
    "for_sale_fragment": 5,
    "fragment_url": "https://fragment.com/..."
  },
  "history": [
    { "event": "created", "date": timestamp, "price": 100 },
    { "event": "sold", "date": timestamp, "price": 1000, "buyer": "..." },
    { "event": "upgraded", "date": timestamp, "cost": 50 },
    ...
  ]
}

Tool: get_collectibles_portfolio
Args: { "include_market_value": true }
Returns: {
  "unique_gifts": [...],
  "total_count": 10,
  "total_value_stars": 25000,
  "total_value_usd": 500.00,
  "rarest_item": { ... },
  "most_valuable": { ... },
  "appreciation_total": "+340%"
}
```

---

### 3. Gift Auction Monitoring
**Feature:** Track and analyze gift auctions

**MCP Implementation:**
- Real-time auction data
- Bid tracking
- Win probability analysis

```
Tool: list_active_auctions
Args: { "gift_type": "all" }
Returns: {
  "auctions": [
    {
      "id": "auction_123",
      "gift_name": "Legendary Phoenix",
      "slug": "legendary_phoenix",
      "status": "active",
      "round": { "current": 3, "total": 5 },
      "top_bidders": 10,
      "gifts_this_round": 5,
      "minimum_bid": 1000,
      "current_top_bid": 2500,
      "your_bid": 2000,
      "your_position": 7,
      "time_left_seconds": 3600,
      "ends_at": timestamp
    },
    ...
  ]
}

Tool: get_auction_details
Args: { "auction_id": "auction_123" }
Returns: {
  "auction": { full auction details },
  "bid_history": [
    { "amount": 2500, "position": 1, "time": timestamp },
    ...
  ],
  "statistics": {
    "average_winning_bid": 2200,
    "total_participants": 150,
    "total_bids": 500
  },
  "your_status": {
    "current_bid": 2000,
    "position": 7,
    "win_probability": 0.3,
    "suggested_bid": 2600
  }
}

Tool: get_auction_alerts
Args: {}
Returns: {
  "alerts": [
    { "type": "outbid", "auction_id": "123", "your_bid": 2000, "top_bid": 2500 },
    { "type": "ending_soon", "auction_id": "456", "time_left": 600 },
    { "type": "won", "auction_id": "789", "gift": "..." }
  ]
}
```

---

### 4. Gift Resale Marketplace
**Feature:** Browse and analyze gift resale market

**MCP Implementation:**
- Uses `Data::ResaleGiftsDescriptor`
- Filter by attributes, price, rarity
- Market analytics

```cpp
// From data_star_gift.h
struct ResaleGiftsDescriptor {
    uint64 giftId;
    std::vector<StarGift> list;
    std::vector<UniqueGiftModelCount> models;
    std::vector<UniqueGiftBackdropCount> backdrops;
    std::vector<UniqueGiftPatternCount> patterns;
    ResaleGiftsSort sort;  // Date, Price, Number
};

Tool: browse_gift_marketplace
Args: {
  "gift_type": "golden_dragon",  // or "all"
  "sort": "price",  // "date", "price", "number"
  "filter": {
    "min_price": 100,
    "max_price": 10000,
    "models": ["Dragon", "Phoenix"],
    "patterns": ["Scales"],
    "backdrops": ["Gold", "Silver"]
  },
  "limit": 50
}
Returns: {
  "listings": [
    {
      "gift_id": "unique_123",
      "title": "Golden Dragon #42",
      "price_stars": 5000,
      "price_ton": 2.5,
      "seller": { "id": 123, "name": "CryptoCollector" },
      "attributes": { "model": "Dragon", "pattern": "Scales", "backdrop": "Gold" },
      "rarity_score": 0.001,
      "listed_at": timestamp
    },
    ...
  ],
  "market_stats": {
    "total_listings": 150,
    "floor_price": 3000,
    "average_price": 4500,
    "volume_24h": 50000
  },
  "attribute_distribution": {
    "models": [{ "name": "Dragon", "count": 50 }, ...],
    "patterns": [...],
    "backdrops": [...]
  }
}

Tool: get_market_trends
Args: { "gift_type": "golden_dragon", "period": "30d" }
Returns: {
  "price_history": [
    { "date": "2024-11-01", "floor": 2000, "average": 3000, "volume": 100 },
    ...
  ],
  "trend": "bullish",  // "bearish", "stable"
  "price_change_percent": +25,
  "volume_change_percent": +40,
  "prediction": {
    "confidence": 0.7,
    "expected_floor_7d": 3500,
    "reasoning": "Increasing demand, limited supply"
  }
}
```

---

### 5. Gift Collections
**Feature:** Organize and track gift collections

**MCP Implementation:**
- Uses `Data::GiftCollection`
- Collection completion tracking
- Value analytics per collection

```cpp
struct GiftCollection {
    int id;
    int count;
    QString title;
    DocumentData* icon;
};

Tool: list_gift_collections
Args: {}
Returns: {
  "collections": [
    {
      "id": 1,
      "title": "Dragons",
      "icon_id": 123,
      "total_items": 100,
      "owned_items": 15,
      "completion_percent": 15,
      "total_value": 10000,
      "rarest_owned": { "name": "Golden Dragon #1", "rarity": 0.001 }
    },
    ...
  ],
  "overall_stats": {
    "collections_started": 5,
    "collections_complete": 1,
    "total_unique_owned": 50
  }
}

Tool: get_collection_details
Args: { "collection_id": 1 }
Returns: {
  "collection": { ... },
  "items": [
    { "gift": {...}, "owned": true, "rarity": 0.01 },
    { "gift": {...}, "owned": false, "floor_price": 500 }
  ],
  "completion_cost_estimate": 15000,  // To complete collection
  "missing_items_count": 85
}
```

---

### 6. Star Reactions Tracking
**Feature:** Track Star reactions given and received

**MCP Implementation:**
- Reaction analytics
- Top supporters
- Content performance

```
Tool: get_star_reactions_received
Args: { "period": "30d", "chat_id": null }
Returns: {
  "total_stars": 1500,
  "total_reactions": 300,
  "unique_supporters": 150,
  "top_supporters": [
    { "user_id": 123, "name": "Fan1", "stars": 200, "reactions": 40 },
    ...
  ],
  "top_posts": [
    { "message_id": 456, "chat_id": 789, "stars": 300, "reactions": 50 },
    ...
  ],
  "by_day": [
    { "date": "2024-11-01", "stars": 50, "reactions": 10 },
    ...
  ]
}

Tool: get_star_reactions_sent
Args: { "period": "30d" }
Returns: {
  "total_spent": 500,
  "total_reactions": 100,
  "by_channel": [
    { "channel_id": 123, "name": "News", "stars": 200 },
    ...
  ]
}
```

---

### 7. Paid Messages Analytics
**Feature:** Track paid messages sent/received

**MCP Implementation:**
- Messages that cost Stars to receive
- Earnings from paid messages

```
Tool: get_paid_messages_stats
Args: { "period": "30d" }
Returns: {
  "sent": {
    "total_messages": 50,
    "total_spent": 500,
    "by_recipient": [
      { "user_id": 123, "name": "Creator", "messages": 10, "spent": 100 }
    ]
  },
  "received": {
    "total_messages": 200,
    "total_earned": 2000,
    "commission_paid": 400,  // Telegram's cut
    "net_earned": 1600,
    "by_sender": [
      { "user_id": 456, "messages": 20, "earned": 200 }
    ]
  },
  "settings": {
    "charge_per_message": 10,
    "enabled": true
  }
}

Tool: configure_paid_messages
Args: {
  "enabled": true,
  "price_per_message": 10,
  "exempt_contacts": true,
  "exempt_premium": false
}
```

---

### 8. Paid Media Analytics
**Feature:** Track paid media purchases and sales

**MCP Implementation:**
- Content you've unlocked
- Content you've sold
- Revenue per post

```
Tool: get_paid_media_stats
Args: { "role": "both" }  // "buyer", "seller", "both"
Returns: {
  "purchases": {
    "total_content": 25,
    "total_spent": 500,
    "by_type": { "photos": 15, "videos": 10 },
    "by_channel": [...]
  },
  "sales": {
    "total_content": 10,
    "total_unlocks": 500,
    "total_earned": 5000,
    "commission_paid": 1000,
    "net_earned": 4000,
    "by_post": [
      { "message_id": 123, "price": 50, "unlocks": 100, "earned": 5000 }
    ],
    "conversion_rate": 0.15  // 15% of viewers unlock
  }
}
```

---

### 9. Mini App Spending Tracker
**Feature:** Track Stars spent in Mini Apps

**MCP Implementation:**
- Spending per app
- Purchase history
- Budget tracking

```
Tool: get_miniapp_spending
Args: { "period": "30d" }
Returns: {
  "total_spent": 1000,
  "by_app": [
    {
      "app_id": "game_xyz",
      "name": "Crypto Game",
      "spent": 500,
      "purchases": 10,
      "last_purchase": timestamp
    },
    ...
  ],
  "purchase_history": [
    {
      "app_id": "game_xyz",
      "item": "Premium Pack",
      "amount": 100,
      "date": timestamp
    },
    ...
  ]
}

Tool: set_miniapp_budget
Args: {
  "monthly_limit": 500,
  "per_app_limit": 100,
  "alert_threshold": 0.8
}
```

---

### 10. Star Rating Analytics
**Feature:** Understand and track Star rating

**MCP Implementation:**
- Rating factors breakdown
- Rating history
- Improvement suggestions

```
Tool: get_star_rating_details
Args: {}
Returns: {
  "current_rating": 85,
  "future_rating": 90,
  "update_in_days": 3,
  "factors": {
    "positive": [
      {
        "type": "gifts_from_telegram",
        "description": "100% of Stars from Telegram gifts",
        "impact": +30,
        "stars_involved": 500
      },
      {
        "type": "gifts_from_users",
        "description": "20% of resold gifts and paid messages",
        "impact": +15,
        "stars_involved": 1000
      }
    ],
    "negative": [
      {
        "type": "refunds",
        "description": "10x of refunded Stars",
        "impact": -5,
        "stars_involved": 50
      }
    ]
  },
  "history": [
    { "date": "2024-11-01", "rating": 80 },
    { "date": "2024-11-15", "rating": 85 },
    ...
  ],
  "is_negative": false,
  "fix_cost": null  // Stars needed to fix negative rating
}

Tool: simulate_rating_change
Args: { "action": "convert_gift", "stars": 500 }
Returns: {
  "current_rating": 85,
  "projected_rating": 82,
  "impact": -3,
  "recommendation": "Consider keeping the gift displayed instead"
}
```

---

### 11. Gift Display Management
**Feature:** Manage gifts displayed on profile

**MCP Implementation:**
- Profile gift arrangement
- Pin/unpin gifts
- Visibility settings

```
Tool: get_profile_gifts
Args: {}
Returns: {
  "displayed_gifts": [
    { "gift_id": "123", "pinned": true, "position": 1 },
    { "gift_id": "456", "pinned": false, "position": 2 },
    ...
  ],
  "hidden_gifts": [...],
  "total_displayed": 10,
  "max_displayed": 100,
  "notifications_enabled": true
}

Tool: update_gift_display
Args: {
  "gift_id": "123",
  "displayed": true,
  "pinned": true
}

Tool: reorder_profile_gifts
Args: {
  "order": ["gift_123", "gift_456", "gift_789"]
}
```

---

### 12. Gift Transfer/Upgrade Management
**Feature:** Track and manage gift transfers and upgrades

**MCP Implementation:**
- Transfer history
- Upgrade options
- Cost analysis

```
Tool: get_gift_transfer_history
Args: { "gift_id": "unique_123" }
Returns: {
  "gift": { ... },
  "transfers": [
    {
      "from": { "id": 123, "name": "Alice" },
      "to": { "id": 456, "name": "Bob" },
      "date": timestamp,
      "price_stars": 5000
    },
    ...
  ],
  "original_owner": { "id": 789, "name": "Creator" },
  "original_date": timestamp,
  "total_transfers": 5
}

Tool: get_upgrade_options
Args: { "gift_id": "regular_123" }
Returns: {
  "gift": { ... },
  "can_upgrade": true,
  "upgrade_cost": 50,
  "expected_rarity": {
    "model_chances": [{ "name": "Dragon", "chance": 10 }, ...],
    "pattern_chances": [...],
    "backdrop_chances": [...]
  },
  "estimated_value_after_upgrade": {
    "minimum": 200,
    "average": 500,
    "maximum": 5000
  },
  "recommendation": "Good value - upgrade cost is 50, average return is 500"
}
```

---

## PARTIALLY IMPLEMENTABLE (4 features)

### 13. Send Star Gifts (Balance-Dependent)
**Feature:** Send Star gifts to users

**MCP Can Do:**
- Send if balance sufficient
- Select gift type
- Add message

```
Tool: send_star_gift
Args: {
  "recipient_id": 123,
  "gift_type": "golden_dragon",  // or specific gift_id
  "message": "Happy birthday!",
  "anonymous": false
}
Returns: {
  "success": true,
  "transaction_id": "tx_123",
  "gift_sent": { ... },
  "new_balance": 500
}
```

---

### 14. Place Auction Bids (Balance-Dependent)
**Feature:** Bid in gift auctions

**MCP Can Do:**
- Place bid if balance sufficient
- Track bid status

```
Tool: place_auction_bid
Args: {
  "auction_id": "auction_123",
  "amount": 2500
}
Returns: {
  "success": true,
  "bid_id": "bid_123",
  "position": 3,
  "new_balance": 7500
}
```

---

### 15. List Gift for Resale (Owner Only)
**Feature:** List owned gift on marketplace

**MCP Can Do:**
- Set price
- Choose currency (Stars/TON)
- Manage listing

```
Tool: list_gift_for_sale
Args: {
  "gift_id": "unique_123",
  "price_stars": 5000,
  "accept_ton": true
}
Returns: {
  "success": true,
  "listing_id": "listing_123",
  "marketplace_url": "t.me/..."
}
```

---

### 16. Transfer Gift (Owner Only)
**Feature:** Transfer gift to another user

```
Tool: transfer_gift
Args: {
  "gift_id": "unique_123",
  "recipient_id": 456,
  "price_stars": 0  // 0 for free transfer
}
```

---

## CANNOT IMPLEMENT - Server Enforced (3 features)

| # | Feature | Why Impossible |
|---|---------|----------------|
| 1 | **Buy Stars** | Requires Apple/Google payment |
| 2 | **Withdraw to Fragment** | Server-side Fragment integration |
| 3 | **Upgrade Gifts** | Server-side randomization + payment |

---

## BEYOND STARS - MCP Exclusive Features

### 17. AI Gift Advisor
**Feature:** AI-powered gift investment advice

**MCP Implementation:**
- Analyze market trends
- Suggest buys/sells
- Portfolio optimization

```
Tool: get_gift_investment_advice
Args: { "budget_stars": 5000 }
Returns: {
  "recommendations": [
    {
      "action": "buy",
      "gift_type": "golden_dragon",
      "target_price": 3000,
      "reasoning": "Floor rising, limited supply, 30% upside potential",
      "confidence": 0.75
    },
    {
      "action": "sell",
      "gift_id": "unique_456",
      "target_price": 5000,
      "reasoning": "Near all-time high, market showing weakness",
      "confidence": 0.6
    }
  ],
  "portfolio_health": "good",
  "diversification_score": 0.7,
  "suggested_rebalancing": [...]
}

Tool: backtest_strategy
Args: {
  "strategy": "buy_floor_sell_peak",
  "gift_types": ["golden_dragon"],
  "period": "90d"
}
Returns: {
  "simulated_return": "+45%",
  "max_drawdown": "-15%",
  "win_rate": 0.65,
  "trades": 20
}
```

---

### 18. Gift Price Alerts
**Feature:** Automated price notifications

**MCP Implementation:**
- Watch specific gifts
- Alert on price changes
- Auction reminders

```
Tool: create_price_alert
Args: {
  "gift_type": "golden_dragon",
  "condition": "floor_below",
  "threshold": 3000,
  "notify_via": "telegram"
}

Tool: create_auction_alert
Args: {
  "auction_id": "auction_123",
  "alerts": ["outbid", "ending_soon", "won"]
}

Tool: list_active_alerts
Args: {}
Returns: {
  "price_alerts": [...],
  "auction_alerts": [...],
  "triggered_today": 3
}
```

---

### 19. Gift Portfolio Tracker
**Feature:** Track gift portfolio performance over time

**MCP Implementation:**
- Historical value tracking
- ROI calculations
- Comparison to market

```
Tool: get_portfolio_performance
Args: { "period": "30d" }
Returns: {
  "current_value": 25000,
  "value_30d_ago": 20000,
  "change_absolute": 5000,
  "change_percent": 25,
  "vs_market": "+10%",  // Outperformed market by 10%
  "best_performer": { "gift_id": "123", "change": "+50%" },
  "worst_performer": { "gift_id": "456", "change": "-10%" },
  "daily_values": [
    { "date": "2024-11-01", "value": 20000 },
    ...
  ]
}

Tool: export_portfolio_report
Args: { "format": "pdf", "period": "year" }
Returns: {
  "file_path": "/tmp/gift_portfolio_2024.pdf",
  "summary": { ... }
}
```

---

### 20. Cross-Platform Gift Tracker
**Feature:** Track Fragment listings and sales

**MCP Implementation:**
- Monitor Fragment marketplace
- Compare prices
- Arbitrage detection

```
Tool: get_fragment_listings
Args: { "gift_type": "golden_dragon" }
Returns: {
  "telegram_floor": 3000,
  "fragment_floor": 2800,
  "arbitrage_opportunity": {
    "exists": true,
    "profit_potential": 200,
    "direction": "buy_fragment_sell_telegram"
  },
  "listings": [...]
}
```

---

## Proposed MCP Tools Summary

### New Tools to Add (45 tools)

**Gift Management (8 tools):**
1. `list_star_gifts` - List all gifts with filters
2. `get_gift_details` - Single gift details
3. `get_unique_gift_analytics` - Collectible analytics
4. `get_collectibles_portfolio` - Portfolio overview
5. `send_star_gift` - Send gift (if balance)
6. `get_gift_transfer_history` - Transfer history
7. `get_upgrade_options` - Upgrade cost/value analysis
8. `transfer_gift` - Transfer to user

**Gift Collections (3 tools):**
9. `list_gift_collections` - All collections
10. `get_collection_details` - Collection items
11. `get_collection_completion` - Completion status

**Auctions (5 tools):**
12. `list_active_auctions` - Active auctions
13. `get_auction_details` - Auction status
14. `get_auction_alerts` - Bid/win alerts
15. `place_auction_bid` - Place bid (if balance)
16. `get_auction_history` - Past auctions

**Marketplace (5 tools):**
17. `browse_gift_marketplace` - Browse listings
18. `get_market_trends` - Price trends
19. `list_gift_for_sale` - Create listing
20. `update_listing` - Modify price
21. `cancel_listing` - Remove listing

**Star Reactions (3 tools):**
22. `get_star_reactions_received` - Reactions received
23. `get_star_reactions_sent` - Reactions sent
24. `get_top_supporters` - Top supporters list

**Paid Content (4 tools):**
25. `get_paid_messages_stats` - Paid message analytics
26. `configure_paid_messages` - Set message price
27. `get_paid_media_stats` - Paid media analytics
28. `get_unlocked_content` - Content you've unlocked

**Mini Apps (3 tools):**
29. `get_miniapp_spending` - Spending by app
30. `get_miniapp_history` - Purchase history
31. `set_miniapp_budget` - Spending limits

**Star Rating (3 tools):**
32. `get_star_rating_details` - Rating breakdown
33. `get_rating_history` - Rating over time
34. `simulate_rating_change` - Predict rating impact

**Profile Display (4 tools):**
35. `get_profile_gifts` - Displayed gifts
36. `update_gift_display` - Show/hide gift
37. `reorder_profile_gifts` - Change order
38. `toggle_gift_notifications` - New gift alerts

**AI & Analytics (7 tools):**
39. `get_gift_investment_advice` - AI recommendations
40. `backtest_strategy` - Strategy simulation
41. `get_portfolio_performance` - Portfolio tracking
42. `create_price_alert` - Price notifications
43. `create_auction_alert` - Auction notifications
44. `get_fragment_listings` - Cross-platform data
45. `export_portfolio_report` - Generate reports

---

## API Classes Used

```cpp
// data/data_star_gift.h
Data::StarGift              // Regular gift
Data::UniqueGift            // Collectible/NFT
Data::UniqueGiftValue       // Market value
Data::SavedStarGift         // Saved gift with metadata
Data::GiftCollection        // Collection info
Data::ResaleGiftsDescriptor // Marketplace listings
Data::ResaleGiftsFilter     // Filter options

// Helper functions
Data::MyUniqueGiftsSlice()  // Get owned unique gifts
Data::ResaleGiftsSlice()    // Get marketplace listings
```

---

## Feature Comparison

| Capability | Telegram App | MCP Stars |
|------------|--------------|-----------|
| View gifts | ✅ Basic | ✅ **Advanced filters** |
| Gift analytics | ❌ | ✅ **Full stats** |
| Rarity tracking | ❌ | ✅ **Rarity scores** |
| Market trends | ❌ | ✅ **Price history** |
| Auction monitoring | ✅ Basic | ✅ **Win probability** |
| Portfolio tracking | ❌ | ✅ **ROI analysis** |
| Price alerts | ❌ | ✅ **Automated** |
| AI recommendations | ❌ | ✅ **Investment advice** |
| Cross-platform | ❌ | ✅ **Fragment integration** |
| Export reports | ❌ | ✅ **PDF/CSV** |

---

*Generated by Claude Code MCP Analysis*

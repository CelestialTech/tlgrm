# Tlgrm - Telegram Desktop with MCP Integration

A custom build of [Telegram Desktop](https://github.com/telegramdesktop/tdesktop) with integrated **Model Context Protocol (MCP)** server, enabling AI assistants like Claude to interact with your Telegram chats, messages, and contacts directly through the local database.

## What is This?

Tlgrm adds an MCP server to Telegram Desktop that:

- **Reads messages instantly** from the local database (no API rate limits)
- **Lists all your chats** with real-time access
- **Sends messages** through the Telegram client
- **Searches messages** locally
- **Exposes chat data** to AI assistants via Model Context Protocol

**Key Advantage:** Direct database access means instant responses - no waiting for API calls or dealing with rate limits.

## Features

### MCP Tools Available (187 Total)

Tlgrm provides **187 MCP tools** organized into categories:

#### Core Messaging (11 tools)
| Tool | Description |
|------|-------------|
| `list_chats` | Get all Telegram chats from local database |
| `get_chat_info` | Get detailed info about a specific chat |
| `read_messages` | Read messages from local database (instant!) |
| `send_message` | Send a message to a chat |
| `search_messages` | Search messages in local database |
| `get_user_info` | Get information about a Telegram user |
| `delete_message` | Delete a message from a chat |
| `edit_message` | Edit a message in a chat |
| `forward_message` | Forward a message to another chat |
| `pin_message` | Pin a message in a chat |
| `add_reaction` | Add a reaction to a message |

#### Profile & Privacy (13 tools)
| Tool | Description |
|------|-------------|
| `get_profile_settings` | Get firstName, lastName, username, phone, bio |
| `update_profile_bio` | Update bio via API |
| `get_privacy_settings` | Get all privacy settings |
| `update_last_seen_privacy` | Set last seen visibility |
| `update_profile_photo_privacy` | Set profile photo visibility |
| `update_phone_number_privacy` | Set phone visibility |
| `update_forwards_privacy` | Set forwards link visibility |
| `update_birthday_privacy` | Set birthday visibility |
| `update_about_privacy` | Set bio/about visibility |
| `get_blocked_users` | Get blocked users list |
| `block_user` | Block a user |
| `unblock_user` | Unblock a user |
| `update_auto_delete_period` | Set default auto-delete timer |

#### Security (3 tools)
| Tool | Description |
|------|-------------|
| `get_security_settings` | Get auto-delete period |
| `get_active_sessions` | List all active sessions |
| `terminate_session` | Terminate a session by hash |

#### Premium Equivalent Features (17 tools)
| Tool | Description |
|------|-------------|
| `transcribe_voice_message` | Queue voice message for transcription |
| `get_voice_transcription` | Get transcription result |
| `translate_message` | Translate message text |
| `get_translation_languages` | List supported languages |
| `add_message_tag` | Add custom tag to message |
| `remove_message_tag` | Remove tag from message |
| `get_message_tags` | Get tags for a message |
| `search_by_tag` | Search messages by tag |
| `set_ad_filter` | Configure ad filtering rules |
| `get_ad_filter` | Get current ad filter settings |
| `set_chat_rules` | Set custom chat rules |
| `get_chat_rules` | Get chat rules |
| `create_task` | Create a task from message |
| `get_tasks` | Get all tasks |
| `update_task` | Update task status |
| `delete_task` | Delete a task |
| `get_task_reminders` | Get upcoming task reminders |

#### Business Features (36 tools)
| Tool | Description |
|------|-------------|
| `create_quick_reply` | Create quick reply template |
| `get_quick_replies` | List all quick replies |
| `update_quick_reply` | Update quick reply |
| `delete_quick_reply` | Delete quick reply |
| `send_quick_reply` | Send a quick reply to chat |
| `create_greeting` | Create greeting message |
| `get_greetings` | List all greetings |
| `update_greeting` | Update greeting |
| `delete_greeting` | Delete greeting |
| `set_away_message` | Set away message |
| `get_away_message` | Get away message settings |
| `update_away_message` | Update away message |
| `disable_away_message` | Disable away message |
| `set_business_hours` | Set business hours |
| `get_business_hours` | Get business hours |
| `update_business_hours` | Update business hours |
| `set_ai_chatbot_config` | Configure AI chatbot |
| `get_ai_chatbot_config` | Get AI chatbot config |
| `update_ai_chatbot_config` | Update AI chatbot |
| `disable_ai_chatbot` | Disable AI chatbot |
| `get_ai_chatbot_stats` | Get chatbot statistics |
| `create_tts_message` | Create text-to-speech message |
| `get_tts_voices` | List available TTS voices |
| `get_tts_history` | Get TTS history |
| `create_video_message` | Create text-to-video message |
| `get_video_templates` | Get video templates |
| `get_video_history` | Get video generation history |
| `create_auto_reply_rule` | Create auto-reply rule |
| `get_auto_reply_rules` | List auto-reply rules |
| `update_auto_reply_rule` | Update auto-reply rule |
| `delete_auto_reply_rule` | Delete auto-reply rule |
| `test_auto_reply_rule` | Test rule against message |
| `get_auto_reply_stats` | Get auto-reply statistics |
| `get_business_analytics` | Get business analytics |
| `export_business_data` | Export business data |
| `get_customer_insights` | Get customer insights |

#### Wallet Features (32 tools)
| Tool | Description |
|------|-------------|
| `get_wallet_balance` | Get wallet balance |
| `get_wallet_analytics` | Get wallet analytics |
| `get_transaction_history` | Get transaction history |
| `get_transaction_details` | Get transaction details |
| `export_transactions` | Export transactions |
| `categorize_transaction` | Categorize a transaction |
| `send_gift` | Send a gift |
| `get_sent_gifts` | List sent gifts |
| `get_received_gifts` | List received gifts |
| `get_gift_catalog` | Browse gift catalog |
| `create_subscription` | Create subscription |
| `get_subscriptions` | List subscriptions |
| `cancel_subscription` | Cancel subscription |
| `get_subscription_analytics` | Get subscription stats |
| `get_monetization_stats` | Get monetization stats |
| `set_monetization_settings` | Configure monetization |
| `get_revenue_report` | Get revenue report |
| `withdraw_earnings` | Withdraw earnings |
| `set_budget` | Set spending budget |
| `get_budgets` | Get all budgets |
| `update_budget` | Update budget |
| `delete_budget` | Delete budget |
| `get_budget_alerts` | Get budget alerts |
| `transfer_stars` | Transfer stars to user |
| `get_stars_balance` | Get stars balance |
| `get_stars_history` | Get stars history |
| `convert_stars` | Convert stars to currency |
| `get_stars_rate` | Get current stars rate |
| `set_spending_limit` | Set spending limit |
| `get_spending_limits` | Get spending limits |
| `get_spending_analytics` | Get spending analytics |
| `get_financial_summary` | Get financial summary |

#### Stars Features (45 tools)
| Tool | Description |
|------|-------------|
| `create_gift_collection` | Create gift collection |
| `get_gift_collections` | List gift collections |
| `add_to_collection` | Add gift to collection |
| `remove_from_collection` | Remove from collection |
| `share_collection` | Share gift collection |
| `create_auction` | Create gift auction |
| `get_auctions` | List active auctions |
| `place_bid` | Place auction bid |
| `get_auction_history` | Get auction history |
| `cancel_auction` | Cancel auction |
| `list_marketplace_item` | List item on marketplace |
| `get_marketplace_listings` | Browse marketplace |
| `buy_marketplace_item` | Buy marketplace item |
| `update_listing` | Update marketplace listing |
| `remove_listing` | Remove listing |
| `add_star_reaction` | Add star reaction |
| `get_star_reactions` | Get star reactions |
| `get_reaction_stats` | Get reaction statistics |
| `create_reaction_pack` | Create reaction pack |
| `get_reaction_packs` | List reaction packs |
| `create_paid_content` | Create paid content |
| `get_paid_content` | List paid content |
| `update_paid_content` | Update paid content |
| `get_paid_content_stats` | Get content stats |
| `unlock_paid_content` | Unlock paid content |
| `get_stars_portfolio` | Get stars portfolio |
| `get_portfolio_history` | Get portfolio history |
| `get_portfolio_analytics` | Get portfolio analytics |
| `set_portfolio_goals` | Set portfolio goals |
| `get_investment_suggestions` | Get suggestions |
| `get_achievements` | Get achievements |
| `get_achievement_progress` | Get achievement progress |
| `claim_achievement_reward` | Claim reward |
| `get_achievement_leaderboard` | Get leaderboard |
| `share_achievement` | Share achievement |
| `get_creator_tools` | Get creator tools |
| `create_exclusive_content` | Create exclusive content |
| `set_supporter_tiers` | Set supporter tiers |
| `get_supporter_list` | Get supporter list |
| `send_supporter_message` | Message supporters |
| `get_creator_analytics` | Get creator analytics |
| `set_payout_settings` | Set payout settings |
| `request_payout` | Request payout |
| `get_payout_history` | Get payout history |
| `get_creator_dashboard` | Get creator dashboard |

#### Analytics & Automation (30+ tools)
Additional tools for chat analytics, message scheduling, contact management, notification preferences, folder management, sticker operations, bot management, and audit logging.

### Platform Support

- **macOS** Ventura (13.0+) or Sonoma (14.0+)
- **Apple Silicon** (M1/M2/M3) or Intel Mac
- **Universal Binary** supporting both architectures

## Quick Start

### Download & Install

1. **Download the DMG** from releases
2. **Mount the DMG** (double-click)
3. **Drag Tlgrm.app** to Applications folder
4. **Run install package** (initiate.pkg) to set up session data
5. **Launch Tlgrm** from Applications

### Building from Source

#### Prerequisites

- macOS Ventura 13.0+
- Xcode 14.0+
- Homebrew with build tools
- 50GB+ free disk space
- Telegram API credentials from https://my.telegram.org

#### Build Steps

```bash
# 1. Install dependencies
brew install git cmake python3 automake autoconf libtool pkg-config ninja wget meson nasm

# 2. Clone repository
git clone --recursive https://github.com/your-repo/tlgrm-tdesktop.git
cd tlgrm-tdesktop

# 3. Build dependencies (40-70 min)
cd Telegram
./build/prepare/mac.sh silent

# 4. Configure
./configure.sh \
  -D TDESKTOP_API_ID=your_api_id \
  -D TDESKTOP_API_HASH=your_api_hash

# 5. Build in Xcode
open out/Telegram.xcodeproj
# Select "Telegram" scheme, Release configuration
# Product → Build (Cmd+B)

# Or build from command line
cd out && xcodebuild -project Telegram.xcodeproj -scheme Telegram -configuration Release build
```

**Build Time:**
- First build (clean): 60-90 minutes
- Incremental rebuilds: 2-5 minutes

## Usage

### Running with MCP

```bash
# Start Telegram with MCP server
./out/Release/Tlgrm.app/Contents/MacOS/Tlgrm --mcp
```

The MCP server will listen on stdin/stdout for JSON-RPC requests.

### Integrating with Claude Desktop

1. **Configure Claude Desktop:**

Edit `~/Library/Application Support/Claude/claude_desktop_config.json`:

```json
{
  "mcpServers": {
    "tlgrm": {
      "command": "/Applications/Tlgrm.app/Contents/MacOS/Tlgrm",
      "args": ["--mcp"]
    }
  }
}
```

2. **Restart Claude:**

```bash
killall Claude
open -a Claude
```

3. **Use in conversation:**

```
Claude, can you list my Telegram chats?
Claude, read the last 10 messages from my "Family" chat
Claude, send a message to Alice saying "Hello!"
```

Claude will use the MCP tools to interact with your Telegram!

## Distribution

### Creating a DMG

```bash
# Run the DMG creation script
./create_beautiful_dmg.sh
```

The script will create: `out/DMG/Tlgrm_<version>.dmg`

**DMG includes:**
- Tlgrm.app (the application)
- Applications symlink (for easy installation)
- README.txt (English instructions)
- ПРОЧТИ.txt (Russian instructions)
- initiate.pkg (session data installer)
- Beige gradient background

### DMG Features

- **Professional Layout**: Custom window with positioned icons
- **Drag-and-Drop**: Easy installation to Applications
- **Bilingual Support**: English and Russian README files
- **Compressed**: ~18MB using zlib compression
- **Session Setup**: Includes installer for tdata files

## Architecture

### How It Works

```
┌─────────────────┐
│  Claude Desktop │
└────────┬────────┘
         │ JSON-RPC over stdio
         ▼
┌─────────────────┐
│   MCP Server    │
│  (in Telegram)  │
└────────┬────────┘
         │ Direct access
         ▼
┌─────────────────┐
│  Telegram Core  │
│  Local Database │
│  (SQLite)       │
└─────────────────┘
```

1. **Claude Desktop** launches Tlgrm with `--mcp` flag
2. **MCP Server** initializes within Telegram process
3. **Direct access** to Telegram's session and local database
4. **No API calls** needed - everything is local and instant

## Troubleshooting

### Build Errors

**Error: "You must have aclocal/autoconf/automake installed"**

```bash
brew install automake autoconf libtool
./Telegram/build/prepare/mac.sh silent
```

**Error: "wget: command not found"**

```bash
brew install wget
./Telegram/build/prepare/mac.sh silent
```

**Error: "meson: command not found"**

```bash
brew install meson
./Telegram/build/prepare/mac.sh silent
```

### Runtime Issues

**Tlgrm won't start with --mcp flag**

Check logs:
```bash
log show --predicate 'process == "Tlgrm"' --last 5m | grep MCP
```

**Claude can't connect to MCP server**

1. Check Claude config file exists and is valid JSON
2. Restart Claude completely: `killall Claude && open -a Claude`
3. Check Claude's developer console for errors

**DMG won't mount**

```bash
rm -f out/DMG/Tlgrm_*.dmg
./create_beautiful_dmg.sh
```

### Performance Issues

**Build taking >2 hours**

- Check Activity Monitor for memory pressure
- Disable Time Machine during build
- Ensure building on SSD
- Close background apps

## Updating to New Telegram Versions

When Telegram Desktop releases a new version:

```bash
# 1. Fetch latest official changes
git remote add upstream https://github.com/telegramdesktop/tdesktop.git
git fetch upstream

# 2. Checkout new version
git checkout upstream/dev  # or specific tag like v6.4

# 3. Try applying patches
git apply --check patches/0001-add-mcp-cmake-configuration.patch
git apply --check patches/0002-add-mcp-application-integration.patch

# 4a. If patches apply cleanly:
./apply-patches.sh

# 4b. If patches have conflicts:
git apply --reject patches/0001-add-mcp-cmake-configuration.patch
# Fix conflicts in *.rej files
# Update patches

# 5. Rebuild
./Telegram/build/prepare/mac.sh silent
./configure.sh -D TDESKTOP_API_ID=... -D TDESKTOP_API_HASH=...
cd out && xcodebuild -project Telegram.xcodeproj -scheme Telegram -configuration Release build
```

## Contributing

Contributions welcome! Areas of interest:

1. ~~**Real data integration**~~ ✅ Complete - 187 tools with database integration
2. **Semantic search** - Add FAISS embedding search for messages
3. **Voice transcription backend** - Connect Whisper.cpp to transcription tools
4. **TTS/TTV backends** - Connect real TTS/video generation services
5. **Stars API integration** - Connect to Telegram Stars API
6. **HTTP transport** - Enable SSE notifications
7. **Tests** - Unit tests, integration tests
8. **External API integrations** - Translation services, AI chatbot backends

## License

This project extends [Telegram Desktop](https://github.com/telegramdesktop/tdesktop), which is licensed under GPLv3.

All MCP integration code is also licensed under GPLv3.

See [LEGAL](https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL) for details.

## Resources

- **MCP Specification:** https://modelcontextprotocol.io/
- **Telegram Desktop:** https://github.com/telegramdesktop/tdesktop
- **Build Optimization Guide:** https://difhel.dev/en/blog/how-to-build-tdesktop-on-macos
- **Apple Silicon Support:** https://github.com/telegramdesktop/tdesktop/issues/9952

## Support

For issues related to this MCP integration:
- Open an issue in this repository

---

**Version:** 6.3.4
**Last Updated:** 2025-11-28
**Base Commit:** aadc81279a
**Platform:** macOS (Apple Silicon + Intel Universal Binary)
**Target:** macOS Ventura 13.0+
**MCP Tools:** 187 tools across 8 categories

For developer/contributor documentation, see [CLAUDE.md](CLAUDE.md).

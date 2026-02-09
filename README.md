# Tlgrm - Telegram Desktop with MCP Integration

![Tlgrm](docs/assets/tlgrm-main.png)

A custom fork of [Telegram Desktop](https://github.com/telegramdesktop/tdesktop) with an embedded Model Context Protocol (MCP) server, enabling AI assistants like Claude to interact directly with your Telegram data.

## Table of Contents

- [Features](#features)
- [Quick Start](#quick-start)
- [Preconfigured Profiles](#preconfigured-profiles)
- [MCP Mode](#mcp-mode)
- [Tool Reference](#tool-reference)
- [Use Cases & Examples](#use-cases--examples)
- [Unrestricted Export](#unrestricted-export)
- [Claude Desktop Integration](#claude-desktop-integration)
- [Troubleshooting](#troubleshooting)
- [Architecture](#architecture)
- [Development](#development)
- [Security](#security)

---

## Features

| Feature | Description |
|---------|-------------|
| **200+ MCP Tools** | Comprehensive API for AI assistants to interact with Telegram |
| **Direct Database Access** | Instant message reads without API rate limits |
| **Preconfigured Profiles** | Auto-detect and use existing session data from `~/tdata` |
| **Unrestricted Export** | Gradual export mode bypasses rate limits for complete backups |
| **Markdown Export** | Export chats as clean Markdown files for documentation |
| **Privacy Controls** | Manage who can see your data through MCP commands |
| **Message Management** | Send, edit, delete, forward, and pin messages |
| **Analytics** | Get statistics about your chats and usage patterns |

---

## Quick Start

### Option 1: Download Pre-built DMG

Download the latest release from the [Releases](../../releases) page.

```bash
# Mount the DMG
hdiutil attach Tlgrm_*.dmg

# Copy to Applications
cp -r /Volumes/Tlgrm/Tlgrm.app /Applications/

# Eject
hdiutil detach /Volumes/Tlgrm
```

### Option 2: Build from Source

**System Requirements:**
- macOS Ventura (13.0+) or Sonoma (14.0+)
- Xcode 14.0+ with Command Line Tools
- 50GB+ free disk space

**Step 1: Install Dependencies**
```bash
# Install Homebrew if not present
/bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"

# Install required packages
brew install git cmake python3 automake autoconf libtool pkg-config ninja wget meson nasm
```

**Step 2: Clone and Build**
```bash
# Clone the repository with submodules
git clone --recursive https://github.com/CelestialTech/tlgrm.git
cd tlgrm

# Build dependencies (40-70 minutes on first run)
cd Telegram
./build/prepare/mac.sh silent

# Configure (uses built-in API credentials)
./configure.sh

# Build the application (10-20 minutes)
cd ../out
xcodebuild -project Telegram.xcodeproj -scheme Telegram -configuration Release build
```

> **Note:** Tlgrm uses the official Telegram Desktop API credentials. No need to register your own app at my.telegram.org.

**Step 4: Run**
```bash
# Launch Tlgrm
./Release/Tlgrm.app/Contents/MacOS/Tlgrm
```

---

## Preconfigured Profiles

Tlgrm supports using preconfigured Telegram session data, allowing you to deploy the app with pre-authenticated accounts.

### What is tdata?

The `tdata` directory contains all Telegram Desktop session data:

| Component | Description |
|-----------|-------------|
| **Session Keys** | Encrypted authentication credentials |
| **Local Cache** | Cached messages, media thumbnails, chat data |
| **Settings** | App preferences, notifications, themes |
| **Encryption Keys** | Local database encryption keys |

**Default Location:**
```
~/Library/Application Support/Telegram Desktop/tdata
```

### How Auto-Detection Works

On startup, Tlgrm checks for `~/tdata`:

```
1. App launches
2. Checks if ~/tdata exists
3. If found → Uses ~/tdata for session data
4. If not found → Uses default location
```

### Setup Guide

#### Example 1: Use Existing Telegram Session

Copy your current Telegram Desktop session to Tlgrm:

```bash
# Close official Telegram Desktop first
pkill Telegram

# Copy session data to home directory
cp -r ~/Library/Application\ Support/Telegram\ Desktop/tdata ~/tdata

# Launch Tlgrm - it will auto-detect ~/tdata
/Applications/Tlgrm.app/Contents/MacOS/Tlgrm
```

**Verification:** Check Console.app or terminal output for:
```
TData: Auto-detected tdata directory at: /Users/yourname/tdata
```

#### Example 2: Multiple Account Management

Manage multiple Telegram accounts by switching tdata directories:

```bash
# Create directories for each account
mkdir -p ~/telegram-accounts/personal
mkdir -p ~/telegram-accounts/work
mkdir -p ~/telegram-accounts/bot

# Copy existing sessions
cp -r ~/Library/Application\ Support/Telegram\ Desktop/tdata ~/telegram-accounts/personal/tdata

# Switch between accounts
rm -rf ~/tdata
ln -s ~/telegram-accounts/personal/tdata ~/tdata
/Applications/Tlgrm.app/Contents/MacOS/Tlgrm

# To switch to work account:
rm ~/tdata
ln -s ~/telegram-accounts/work/tdata ~/tdata
/Applications/Tlgrm.app/Contents/MacOS/Tlgrm
```

#### Example 3: Automated Deployment Script

```bash
#!/bin/bash
# deploy-tlgrm.sh - Deploy Tlgrm with preconfigured account

ACCOUNT_NAME="$1"
TDATA_SOURCE="$2"

if [ -z "$ACCOUNT_NAME" ] || [ -z "$TDATA_SOURCE" ]; then
    echo "Usage: deploy-tlgrm.sh <account-name> <tdata-source>"
    exit 1
fi

# Backup existing tdata
if [ -d ~/tdata ]; then
    mv ~/tdata ~/tdata.backup.$(date +%Y%m%d_%H%M%S)
fi

# Copy new tdata
cp -r "$TDATA_SOURCE" ~/tdata

# Set permissions
chmod -R 700 ~/tdata

# Launch Tlgrm
/Applications/Tlgrm.app/Contents/MacOS/Tlgrm &

echo "Tlgrm launched with account: $ACCOUNT_NAME"
```

#### Example 4: Custom Working Directory

Use `-workdir` flag for complete control:

```bash
# Run with specific working directory
/Applications/Tlgrm.app/Contents/MacOS/Tlgrm -workdir /path/to/custom/directory

# The app will look for tdata inside this directory:
# /path/to/custom/directory/tdata
```

### Security Considerations

- **Never share tdata directories** - they contain your session keys
- **Use unique tdata per machine** - sharing can cause session conflicts
- **Backup regularly** - tdata corruption means re-authentication
- **Set restrictive permissions**: `chmod -R 700 ~/tdata`

---

## MCP Mode

MCP (Model Context Protocol) mode enables AI assistants to interact with your Telegram data programmatically.

### Starting MCP Mode

```bash
# Start Tlgrm in MCP mode
/Applications/Tlgrm.app/Contents/MacOS/Tlgrm --mcp
```

The MCP server listens on a Unix socket at `/tmp/tdesktop_mcp.sock`.

### Testing the Connection

#### Basic Ping Test
```bash
echo '{"jsonrpc":"2.0","id":1,"method":"ping","params":{}}' | nc -U /tmp/tdesktop_mcp.sock
```

Expected response:
```json
{"jsonrpc":"2.0","id":1,"result":{"status":"ok","timestamp":1234567890}}
```

#### Initialize Protocol
```bash
echo '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2024-11-05","clientInfo":{"name":"test","version":"1.0"}}}' | nc -U /tmp/tdesktop_mcp.sock
```

#### List Available Tools
```bash
echo '{"jsonrpc":"2.0","id":2,"method":"tools/list","params":{}}' | nc -U /tmp/tdesktop_mcp.sock
```

### JSON-RPC Protocol

All MCP communication uses JSON-RPC 2.0:

```json
{
  "jsonrpc": "2.0",
  "id": 1,
  "method": "tools/call",
  "params": {
    "name": "tool_name",
    "arguments": {
      "arg1": "value1"
    }
  }
}
```

Response format:
```json
{
  "jsonrpc": "2.0",
  "id": 1,
  "result": {
    "content": [
      {
        "type": "text",
        "text": "Tool output here"
      }
    ]
  }
}
```

---

## Tool Reference

### Core Messaging Tools

#### `list_chats` - Get All Chats
```bash
echo '{"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"list_chats","arguments":{}}}' | nc -U /tmp/tdesktop_mcp.sock
```

Response:
```json
{
  "chats": [
    {
      "id": 123456789,
      "name": "John Doe",
      "type": "private",
      "unread_count": 5,
      "last_message": "Hey, how are you?"
    },
    {
      "id": -100123456789,
      "name": "Work Group",
      "type": "supergroup",
      "unread_count": 42,
      "member_count": 150
    }
  ]
}
```

#### `get_chat_info` - Get Chat Details
```bash
echo '{"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"get_chat_info","arguments":{"chat_id":123456789}}}' | nc -U /tmp/tdesktop_mcp.sock
```

Response:
```json
{
  "id": 123456789,
  "name": "John Doe",
  "type": "private",
  "username": "johndoe",
  "phone": "+1234567890",
  "bio": "Software developer",
  "photo_url": "...",
  "common_groups_count": 3,
  "is_blocked": false,
  "is_contact": true
}
```

#### `read_messages` - Read Messages from Chat
```bash
# Read last 50 messages
echo '{"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"read_messages","arguments":{"chat_id":123456789,"limit":50}}}' | nc -U /tmp/tdesktop_mcp.sock

# Read messages with offset (pagination)
echo '{"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"read_messages","arguments":{"chat_id":123456789,"limit":50,"offset_id":12345}}}' | nc -U /tmp/tdesktop_mcp.sock
```

Response:
```json
{
  "messages": [
    {
      "id": 12345,
      "date": "2024-01-15T10:30:00Z",
      "from_id": 123456789,
      "from_name": "John Doe",
      "text": "Hello!",
      "reply_to_id": null,
      "forwarded_from": null
    },
    {
      "id": 12346,
      "date": "2024-01-15T10:31:00Z",
      "from_id": 987654321,
      "from_name": "You",
      "text": "Hi there!",
      "reply_to_id": 12345
    }
  ],
  "has_more": true
}
```

#### `send_message` - Send a Message
```bash
# Send text message
echo '{"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"send_message","arguments":{"chat_id":123456789,"text":"Hello from MCP!"}}}' | nc -U /tmp/tdesktop_mcp.sock

# Send reply
echo '{"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"send_message","arguments":{"chat_id":123456789,"text":"This is a reply","reply_to_id":12345}}}' | nc -U /tmp/tdesktop_mcp.sock
```

#### `search_messages` - Search Messages
```bash
# Search in specific chat
echo '{"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"search_messages","arguments":{"chat_id":123456789,"query":"meeting tomorrow"}}}' | nc -U /tmp/tdesktop_mcp.sock

# Global search
echo '{"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"search_messages","arguments":{"query":"project deadline"}}}' | nc -U /tmp/tdesktop_mcp.sock
```

### Message Operations

#### `edit_message` - Edit a Message
```bash
echo '{"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"edit_message","arguments":{"chat_id":123456789,"message_id":12345,"text":"Updated message text"}}}' | nc -U /tmp/tdesktop_mcp.sock
```

#### `delete_message` - Delete a Message
```bash
echo '{"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"delete_message","arguments":{"chat_id":123456789,"message_id":12345}}}' | nc -U /tmp/tdesktop_mcp.sock
```

#### `forward_message` - Forward a Message
```bash
echo '{"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"forward_message","arguments":{"from_chat_id":123456789,"to_chat_id":987654321,"message_id":12345}}}' | nc -U /tmp/tdesktop_mcp.sock
```

#### `pin_message` - Pin a Message
```bash
echo '{"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"pin_message","arguments":{"chat_id":123456789,"message_id":12345}}}' | nc -U /tmp/tdesktop_mcp.sock
```

#### `add_reaction` - Add Reaction
```bash
echo '{"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"add_reaction","arguments":{"chat_id":123456789,"message_id":12345,"emoji":"👍"}}}' | nc -U /tmp/tdesktop_mcp.sock
```

### Privacy Settings

#### `get_privacy_settings` - Get Current Settings
```bash
echo '{"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"get_privacy_settings","arguments":{}}}' | nc -U /tmp/tdesktop_mcp.sock
```

Response:
```json
{
  "last_seen": "contacts",
  "profile_photo": "everybody",
  "phone_number": "nobody",
  "forwards": "contacts",
  "bio": "everybody",
  "birthday": "contacts"
}
```

#### Update Privacy Settings

**Privacy Values:** `everybody`, `contacts`, `close_friends`, `nobody`

```bash
# Set last seen to contacts only
echo '{"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"update_last_seen_privacy","arguments":{"option":"contacts"}}}' | nc -U /tmp/tdesktop_mcp.sock

# Hide phone number from everyone
echo '{"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"update_phone_number_privacy","arguments":{"option":"nobody"}}}' | nc -U /tmp/tdesktop_mcp.sock

# Show profile photo to everyone
echo '{"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"update_profile_photo_privacy","arguments":{"option":"everybody"}}}' | nc -U /tmp/tdesktop_mcp.sock
```

### Security Tools

#### `get_active_sessions` - List Active Sessions
```bash
echo '{"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"get_active_sessions","arguments":{}}}' | nc -U /tmp/tdesktop_mcp.sock
```

Response:
```json
{
  "sessions": [
    {
      "hash": "abc123...",
      "device": "MacBook Pro",
      "platform": "macOS",
      "app_name": "Tlgrm",
      "app_version": "6.3",
      "ip": "192.168.1.1",
      "country": "United States",
      "date_active": "2024-01-15T10:30:00Z",
      "is_current": true
    },
    {
      "hash": "def456...",
      "device": "iPhone 15",
      "platform": "iOS",
      "app_name": "Telegram",
      "app_version": "10.5",
      "ip": "203.0.113.1",
      "country": "United States",
      "date_active": "2024-01-14T15:00:00Z",
      "is_current": false
    }
  ]
}
```

#### `terminate_session` - Log Out a Session
```bash
echo '{"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"terminate_session","arguments":{"hash":"def456..."}}}' | nc -U /tmp/tdesktop_mcp.sock
```

#### `block_user` / `unblock_user`
```bash
# Block a user
echo '{"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"block_user","arguments":{"user_id":123456789}}}' | nc -U /tmp/tdesktop_mcp.sock

# Unblock a user
echo '{"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"unblock_user","arguments":{"user_id":123456789}}}' | nc -U /tmp/tdesktop_mcp.sock
```

### Profile Settings

#### `get_profile_settings` - Get Your Profile
```bash
echo '{"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"get_profile_settings","arguments":{}}}' | nc -U /tmp/tdesktop_mcp.sock
```

Response:
```json
{
  "first_name": "John",
  "last_name": "Doe",
  "username": "johndoe",
  "phone": "+1234567890",
  "bio": "Software developer",
  "birthday": "1990-05-15"
}
```

#### `update_profile_bio` - Update Bio
```bash
echo '{"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"update_profile_bio","arguments":{"bio":"Building cool stuff with AI"}}}' | nc -U /tmp/tdesktop_mcp.sock
```

### Archive & Export Tools

#### `export_chat` - Export Chat History
```bash
# Export as JSON
echo '{"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"export_chat","arguments":{"chat_id":123456789,"format":"json","output_path":"/tmp/chat_export.json"}}}' | nc -U /tmp/tdesktop_mcp.sock

# Export as HTML
echo '{"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"export_chat","arguments":{"chat_id":123456789,"format":"html","output_path":"/tmp/chat_export.html"}}}' | nc -U /tmp/tdesktop_mcp.sock

# Export as plain text
echo '{"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"export_chat","arguments":{"chat_id":123456789,"format":"txt","output_path":"/tmp/chat_export.txt"}}}' | nc -U /tmp/tdesktop_mcp.sock
```

#### `get_message_stats` - Get Statistics
```bash
echo '{"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"get_message_stats","arguments":{"chat_id":123456789}}}' | nc -U /tmp/tdesktop_mcp.sock
```

Response:
```json
{
  "total_messages": 15234,
  "messages_sent": 7890,
  "messages_received": 7344,
  "media_count": 523,
  "links_count": 189,
  "first_message_date": "2022-03-15T10:00:00Z",
  "most_active_hour": 14,
  "most_active_day": "Wednesday"
}
```

---

## Use Cases & Examples

### Use Case 1: Daily Message Summary

Create a script that summarizes your unread messages:

```bash
#!/bin/bash
# daily-summary.sh

SOCKET="/tmp/tdesktop_mcp.sock"

# Get all chats with unread messages
CHATS=$(echo '{"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"list_chats","arguments":{}}}' | nc -U $SOCKET)

# Parse and display summary (using jq)
echo "$CHATS" | jq -r '.result.content[0].text' | jq -r '.chats[] | select(.unread_count > 0) | "[\(.unread_count)] \(.name): \(.last_message)"'
```

### Use Case 2: Auto-Archive Old Chats

```bash
#!/bin/bash
# archive-old-chats.sh

SOCKET="/tmp/tdesktop_mcp.sock"
DAYS_OLD=30

# Get chat list
CHATS=$(echo '{"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"list_chats","arguments":{}}}' | nc -U $SOCKET | jq -r '.result.content[0].text')

# Archive chats older than threshold
echo "$CHATS" | jq -r '.chats[] | select(.last_message_date < (now - ('$DAYS_OLD' * 86400))) | .id' | while read chat_id; do
    echo "Archiving chat: $chat_id"
    echo '{"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"archive_chat","arguments":{"chat_id":'$chat_id'}}}' | nc -U $SOCKET
done
```

### Use Case 3: Privacy Audit Script

```bash
#!/bin/bash
# privacy-audit.sh

SOCKET="/tmp/tdesktop_mcp.sock"

echo "=== Privacy Audit ==="

# Get current privacy settings
PRIVACY=$(echo '{"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"get_privacy_settings","arguments":{}}}' | nc -U $SOCKET | jq -r '.result.content[0].text')

echo "Current Privacy Settings:"
echo "$PRIVACY" | jq '.'

# Get active sessions
SESSIONS=$(echo '{"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"get_active_sessions","arguments":{}}}' | nc -U $SOCKET | jq -r '.result.content[0].text')

echo ""
echo "Active Sessions:"
echo "$SESSIONS" | jq '.sessions[] | "\(.device) - \(.platform) - \(.country) - Last active: \(.date_active)"'

# Get blocked users
BLOCKED=$(echo '{"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"get_blocked_users","arguments":{}}}' | nc -U $SOCKET | jq -r '.result.content[0].text')

echo ""
echo "Blocked Users:"
echo "$BLOCKED" | jq '.users[] | .name'
```

### Use Case 4: Backup Important Chats

```bash
#!/bin/bash
# backup-chats.sh

SOCKET="/tmp/tdesktop_mcp.sock"
BACKUP_DIR="$HOME/telegram-backups/$(date +%Y-%m-%d)"

mkdir -p "$BACKUP_DIR"

# List of important chat IDs
IMPORTANT_CHATS=(123456789 -100123456789 987654321)

for chat_id in "${IMPORTANT_CHATS[@]}"; do
    echo "Backing up chat: $chat_id"

    # Export as JSON
    echo '{"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"export_chat","arguments":{"chat_id":'$chat_id',"format":"json","output_path":"'$BACKUP_DIR'/chat_'$chat_id'.json"}}}' | nc -U $SOCKET

    sleep 1  # Rate limiting
done

echo "Backup complete: $BACKUP_DIR"
```

### Use Case 5: Security Lockdown

Quickly secure your account by terminating all other sessions and tightening privacy:

```bash
#!/bin/bash
# security-lockdown.sh

SOCKET="/tmp/tdesktop_mcp.sock"

echo "=== Security Lockdown ==="

# 1. Get all sessions except current
SESSIONS=$(echo '{"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"get_active_sessions","arguments":{}}}' | nc -U $SOCKET | jq -r '.result.content[0].text')

# 2. Terminate all non-current sessions
echo "$SESSIONS" | jq -r '.sessions[] | select(.is_current == false) | .hash' | while read hash; do
    echo "Terminating session: $hash"
    echo '{"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"terminate_session","arguments":{"hash":"'$hash'"}}}' | nc -U $SOCKET
done

# 3. Set privacy to maximum
echo "Setting last_seen to nobody..."
echo '{"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"update_last_seen_privacy","arguments":{"option":"nobody"}}}' | nc -U $SOCKET

echo "Hiding phone number..."
echo '{"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"update_phone_number_privacy","arguments":{"option":"nobody"}}}' | nc -U $SOCKET

echo "Setting profile photo to contacts only..."
echo '{"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"update_profile_photo_privacy","arguments":{"option":"contacts"}}}' | nc -U $SOCKET

echo "Security lockdown complete!"
```

---

## Unrestricted Export

Tlgrm includes advanced export capabilities that bypass Telegram's standard export restrictions, allowing complete backup of your chat history.

### Why Unrestricted Export?

Telegram's official export has limitations:
- **Rate limiting** - Server throttles bulk data requests
- **Daily limits** - Restricted number of messages per day
- **Format restrictions** - Limited to HTML/JSON in official app
- **No gradual mode** - Must complete in one session

Tlgrm solves these with:
- **Gradual export mode** - Natural timing patterns avoid rate limits
- **Markdown format** - Export chats as Markdown files
- **Queue system** - Schedule multiple chats for background export
- **Resume capability** - Continue interrupted exports

### Export Formats

| Format | Description | Best For |
|--------|-------------|----------|
| **HTML** | Rich formatted output with styling | Viewing in browser |
| **JSON** | Structured data format | Data analysis, scripts |
| **Markdown** | Clean text with formatting | Documentation, archiving |

### Gradual Export Mode

The gradual export feature mimics natural user behavior to avoid triggering Telegram's rate limits.

#### How It Works

```
┌─────────────────────────────────────────────────────────────┐
│                  Gradual Export Process                      │
├─────────────────────────────────────────────────────────────┤
│                                                             │
│  1. Fetch batch (10-50 messages)                            │
│           │                                                  │
│           ▼                                                  │
│  2. Random delay (3-15 seconds)  ─── simulates reading      │
│           │                                                  │
│           ▼                                                  │
│  3. Repeat until batch limit                                │
│           │                                                  │
│           ▼                                                  │
│  4. Burst pause (1 minute)  ─── every 5 batches             │
│           │                                                  │
│           ▼                                                  │
│  5. Long pause (5 minutes)  ─── every 20 batches            │
│           │                                                  │
│           ▼                                                  │
│  6. Continue until complete                                 │
│                                                             │
└─────────────────────────────────────────────────────────────┘
```

#### Configuration Parameters

```json
{
  "timing": {
    "minDelayMs": 3000,         // Minimum delay between batches (3 sec)
    "maxDelayMs": 15000,        // Maximum delay between batches (15 sec)
    "burstPauseMs": 60000,      // Pause after burst (1 min)
    "longPauseMs": 300000       // Occasional long pause (5 min)
  },
  "batches": {
    "minBatchSize": 10,         // Minimum messages per batch
    "maxBatchSize": 50,         // Maximum messages per batch
    "batchesBeforePause": 5,    // Batches before burst pause
    "batchesBeforeLongPause": 20 // Batches before long pause
  },
  "behavior": {
    "randomizeOrder": true,     // Don't always go oldest-to-newest
    "simulateReading": true,    // Add "reading time" based on length
    "respectActiveHours": true, // Only run during typical hours
    "activeHourStart": 8,       // Start hour (8 AM)
    "activeHourEnd": 23         // End hour (11 PM)
  },
  "limits": {
    "maxMessagesPerDay": 5000,  // Daily limit
    "maxMessagesPerHour": 500,  // Hourly limit
    "stopOnFloodWait": true,    // Stop if rate limited
    "maxRetries": 3             // Max retries on error
  }
}
```

#### Using Gradual Export via MCP

**Start gradual export:**
```bash
echo '{"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"start_gradual_export","arguments":{"chat_id":123456789,"config":{"exportFormat":"markdown","maxMessagesPerDay":3000}}}}' | nc -U /tmp/tdesktop_mcp.sock
```

**Check export status:**
```bash
echo '{"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"get_gradual_export_status","arguments":{}}}' | nc -U /tmp/tdesktop_mcp.sock
```

Response:
```json
{
  "state": "running",
  "chatId": 123456789,
  "chatTitle": "Work Group",
  "totalMessages": 15000,
  "archivedMessages": 3420,
  "batchesCompleted": 78,
  "messagesArchivedToday": 3420,
  "messagesArchivedThisHour": 420,
  "totalBytesProcessed": 2457600,
  "startTime": "2024-01-15T10:00:00Z",
  "estimatedCompletion": "2024-01-18T14:00:00Z",
  "nextActionTime": "2024-01-15T10:05:32Z"
}
```

**Pause/Resume export:**
```bash
# Pause
echo '{"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"pause_gradual_export","arguments":{}}}' | nc -U /tmp/tdesktop_mcp.sock

# Resume
echo '{"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"resume_gradual_export","arguments":{}}}' | nc -U /tmp/tdesktop_mcp.sock
```

**Queue multiple chats:**
```bash
echo '{"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"queue_gradual_export","arguments":{"chat_ids":[123456789,-100987654321,555555555]}}}' | nc -U /tmp/tdesktop_mcp.sock
```

### Markdown Export

Export chats to clean Markdown format, perfect for documentation or long-term archiving.

#### Features

- **Clean formatting** - Headers, bold, italic, code blocks preserved
- **Media handling** - Links to media files or base64 embedding
- **Metadata** - Timestamps, sender info, reply threads
- **Date filtering** - Export specific date ranges

#### Export Options

```json
{
  "includeMedia": true,           // Include media references
  "embedImagesAsBase64": false,   // Embed images inline (large files!)
  "createMediaFolder": true,      // Create separate media directory
  "includeReplies": true,         // Show reply context
  "includeForwards": true,        // Include forwarded from info
  "startDate": "2024-01-01",      // Filter start date
  "endDate": "2024-12-31"         // Filter end date
}
```

#### Markdown Output Example

```markdown
# Chat Export: Work Group

**Exported:** 2024-01-15 10:30:00
**Messages:** 1,234
**Date Range:** 2024-01-01 to 2024-01-15

---

## 2024-01-15

### 10:30 - John Doe (@johndoe)

Hey everyone, here's the project update!

📎 [project_update.pdf](media/project_update.pdf)

---

### 10:32 - Jane Smith (@janesmith)
> Replying to John Doe

Thanks! I'll review it today.

---

### 10:35 - John Doe (@johndoe)

Here's a code snippet:

\`\`\`python
def process_data(items):
    return [item.value for item in items]
\`\`\`

---
```

#### Export via MCP

```bash
# Export as Markdown
echo '{"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"export_chat_markdown","arguments":{"chat_id":123456789,"output_path":"/tmp/export","options":{"includeMedia":true,"createMediaFolder":true}}}}' | nc -U /tmp/tdesktop_mcp.sock
```

### Complete Export Workflow

Here's a complete workflow for backing up important chats:

#### Step 1: List Chats to Export
```bash
#!/bin/bash
SOCKET="/tmp/tdesktop_mcp.sock"

# Get all chats
echo '{"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"list_chats","arguments":{}}}' | nc -U $SOCKET | jq '.result.content[0].text' | jq '.chats[] | {id, name, type, message_count}'
```

#### Step 2: Start Gradual Export for Large Chats
```bash
#!/bin/bash
# export-large-chat.sh

SOCKET="/tmp/tdesktop_mcp.sock"
CHAT_ID="$1"
OUTPUT_DIR="$HOME/telegram-exports/$(date +%Y-%m-%d)"

mkdir -p "$OUTPUT_DIR"

# Start gradual export with conservative settings
echo '{"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"start_gradual_export","arguments":{
  "chat_id":'$CHAT_ID',
  "config":{
    "exportFormat":"markdown",
    "exportPath":"'$OUTPUT_DIR'",
    "maxMessagesPerDay":3000,
    "maxMessagesPerHour":300,
    "respectActiveHours":true,
    "activeHourStart":9,
    "activeHourEnd":22
  }
}}}' | nc -U $SOCKET

echo "Export started. Check status with:"
echo 'echo '"'"'{"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"get_gradual_export_status","arguments":{}}}'"'"' | nc -U '$SOCKET
```

#### Step 3: Monitor Progress
```bash
#!/bin/bash
# monitor-export.sh

SOCKET="/tmp/tdesktop_mcp.sock"

while true; do
    STATUS=$(echo '{"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"get_gradual_export_status","arguments":{}}}' | nc -U $SOCKET | jq -r '.result.content[0].text')

    STATE=$(echo "$STATUS" | jq -r '.state')
    ARCHIVED=$(echo "$STATUS" | jq -r '.archivedMessages')
    TOTAL=$(echo "$STATUS" | jq -r '.totalMessages')
    PERCENT=$((ARCHIVED * 100 / TOTAL))

    clear
    echo "=== Export Progress ==="
    echo "State: $STATE"
    echo "Progress: $ARCHIVED / $TOTAL ($PERCENT%)"
    echo "Today: $(echo "$STATUS" | jq -r '.messagesArchivedToday')"
    echo "This hour: $(echo "$STATUS" | jq -r '.messagesArchivedThisHour')"
    echo "Next action: $(echo "$STATUS" | jq -r '.nextActionTime')"
    echo "ETA: $(echo "$STATUS" | jq -r '.estimatedCompletion')"

    if [ "$STATE" = "completed" ] || [ "$STATE" = "failed" ]; then
        echo ""
        echo "Export finished with state: $STATE"
        break
    fi

    sleep 30
done
```

#### Step 4: Export Completion
```bash
#!/bin/bash
# on-export-complete.sh

OUTPUT_DIR="$1"

# Create index file
echo "# Telegram Export Index" > "$OUTPUT_DIR/README.md"
echo "" >> "$OUTPUT_DIR/README.md"
echo "Exported: $(date)" >> "$OUTPUT_DIR/README.md"
echo "" >> "$OUTPUT_DIR/README.md"
echo "## Chats" >> "$OUTPUT_DIR/README.md"

for f in "$OUTPUT_DIR"/*.md; do
    if [ "$f" != "$OUTPUT_DIR/README.md" ]; then
        name=$(basename "$f" .md)
        echo "- [$name]($name.md)" >> "$OUTPUT_DIR/README.md"
    fi
done

# Compress for backup
tar -czf "$OUTPUT_DIR.tar.gz" -C "$(dirname $OUTPUT_DIR)" "$(basename $OUTPUT_DIR)"

echo "Export complete: $OUTPUT_DIR.tar.gz"
```

### Export Tool Reference

| Tool | Description |
|------|-------------|
| `export_chat` | Standard export (HTML/JSON/TXT) |
| `export_chat_markdown` | Export as Markdown |
| `start_gradual_export` | Start gradual/covert export |
| `get_gradual_export_status` | Check export progress |
| `pause_gradual_export` | Pause running export |
| `resume_gradual_export` | Resume paused export |
| `cancel_gradual_export` | Cancel export |
| `queue_gradual_export` | Add chats to export queue |
| `get_export_queue` | View queued chats |
| `clear_export_queue` | Clear pending queue |

### Best Practices for Large Exports

1. **Use conservative rate limits** - Set `maxMessagesPerDay` to 3000 or less
2. **Enable active hours** - Only export during typical usage times
3. **Monitor for rate limits** - Watch for `RateLimited` state
4. **Use queue for multiple chats** - Don't start parallel exports
5. **Verify exports** - Check exported file counts match expectations
6. **Backup regularly** - Don't wait for massive exports

### Export State Persistence

Gradual exports save state to disk, allowing resumption after:
- App restart
- System reboot
- Unexpected crashes

State file location: `~/Library/Application Support/Tlgrm/export_state.json`

---

## Claude Desktop Integration

### Configuration

Add Tlgrm to Claude Desktop config file:

**Location:** `~/Library/Application Support/Claude/claude_desktop_config.json`

```json
{
  "mcpServers": {
    "telegram": {
      "command": "/Applications/Tlgrm.app/Contents/MacOS/Tlgrm",
      "args": ["--mcp"]
    }
  }
}
```

### With Custom tdata Location

```json
{
  "mcpServers": {
    "telegram": {
      "command": "/Applications/Tlgrm.app/Contents/MacOS/Tlgrm",
      "args": ["--mcp", "-workdir", "/Users/yourname/telegram-work"]
    }
  }
}
```

### Using Claude with Telegram

Once configured, you can ask Claude things like:

- "Show me my unread messages"
- "Search my Telegram for 'meeting tomorrow'"
- "Send a message to John saying I'll be late"
- "Export my chat with the Work Group"
- "What are my current privacy settings?"
- "Terminate all sessions except this one"
- "Who are my most active contacts?"

### Example Claude Interactions

**You:** "Summarize my unread messages"

**Claude:** *Uses `list_chats` to get chats with unread_count > 0, then `read_messages` for each, and summarizes the content.*

**You:** "Find all messages about the project deadline"

**Claude:** *Uses `search_messages` with query "project deadline" and presents results.*

**You:** "Make my account more private"

**Claude:** *Uses privacy tools to set last_seen, phone_number to "nobody" and explains the changes made.*

---

## Troubleshooting

### MCP Server Not Starting

**Symptom:** No response from socket

**Solution:**
```bash
# Check if Tlgrm is running
pgrep -l Tlgrm

# Check if socket exists
ls -la /tmp/tdesktop_mcp.sock

# Restart with verbose logging
/Applications/Tlgrm.app/Contents/MacOS/Tlgrm --mcp 2>&1 | tee /tmp/tlgrm.log
```

### "No Active Session" Error

**Symptom:** Tools return `{"error": "No active session"}`

**Solution:** You must be logged into Telegram for tools to work.
```bash
# Launch Tlgrm normally first to log in
/Applications/Tlgrm.app/Contents/MacOS/Tlgrm

# After logging in, restart in MCP mode
pkill Tlgrm
/Applications/Tlgrm.app/Contents/MacOS/Tlgrm --mcp
```

### tdata Not Detected

**Symptom:** App uses default location instead of ~/tdata

**Solution:**
```bash
# Verify tdata exists and has correct permissions
ls -la ~/tdata

# Should show directories like:
# D877F783D5D3EF8C/
# D877F783D5D3EF8Cs

# Check permissions (should be 700)
chmod -R 700 ~/tdata

# Check logs for detection message
/Applications/Tlgrm.app/Contents/MacOS/Tlgrm 2>&1 | grep TData
```

### Socket Permission Denied

**Symptom:** `nc: /tmp/tdesktop_mcp.sock: Permission denied`

**Solution:**
```bash
# Check socket permissions
ls -la /tmp/tdesktop_mcp.sock

# Remove stale socket
rm /tmp/tdesktop_mcp.sock

# Restart Tlgrm
pkill Tlgrm
/Applications/Tlgrm.app/Contents/MacOS/Tlgrm --mcp
```

### Build Errors

**Missing dependencies:**
```bash
brew install automake autoconf libtool wget meson nasm cmake python3
```

**Submodule issues:**
```bash
git submodule update --init --recursive --depth 1
```

**Clean rebuild:**
```bash
rm -rf out/
cd Telegram
./configure.sh
cd ../out
xcodebuild -project Telegram.xcodeproj -scheme Telegram -configuration Release clean build
```

---

## Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                    Tlgrm Application                         │
│  ┌─────────────────────────────────────────────────────────┐ │
│  │                    MCP Server                           │ │
│  │  ┌─────────────┐  ┌─────────────┐  ┌─────────────────┐  │ │
│  │  │ JSON-RPC    │  │ Tool        │  │ Transport       │  │ │
│  │  │ Parser      │  │ Dispatcher  │  │ (Unix Socket)   │  │ │
│  │  └─────────────┘  └─────────────┘  └─────────────────┘  │ │
│  │                         │                               │ │
│  │  ┌─────────────────────┴─────────────────────────────┐  │ │
│  │  │              200+ Tool Implementations             │  │ │
│  │  │  • Messaging    • Privacy    • Security           │  │ │
│  │  │  • Analytics    • Export     • Profile            │  │ │
│  │  └───────────────────────────────────────────────────┘  │ │
│  └─────────────────────────────────────────────────────────┘ │
│                              │                               │
│  ┌─────────────────────────────────────────────────────────┐ │
│  │              Telegram Desktop Core                      │ │
│  │  ┌─────────────┐  ┌─────────────┐  ┌─────────────────┐  │ │
│  │  │ Local       │  │ MTProto     │  │ Data            │  │ │
│  │  │ Database    │  │ API Client  │  │ Structures      │  │ │
│  │  └─────────────┘  └─────────────┘  └─────────────────┘  │ │
│  └─────────────────────────────────────────────────────────┘ │
└─────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────┐
│              AI Assistant (Claude Desktop)                   │
│  • Connects via Unix socket                                  │
│  • Sends JSON-RPC requests                                   │
│  • Receives structured responses                             │
│  • Performs actions on user's behalf                         │
└─────────────────────────────────────────────────────────────┘
```

### Key Components

| Component | File | Description |
|-----------|------|-------------|
| MCP Server | `mcp/mcp_server.h/cpp` | Main protocol implementation |
| Tool Handlers | `mcp/mcp_server_complete.cpp` | 200+ tool implementations |
| IPC Bridge | `mcp/mcp_bridge.h/cpp` | Unix socket communication |
| Cache Manager | `mcp/cache_manager.h/cpp` | LRU caching for performance |
| Integration | `core/application.cpp` | MCP initialization on `--mcp` flag |

---

## Development

### Project Structure

```
tdesktop/
├── Telegram/
│   ├── SourceFiles/
│   │   ├── mcp/                          # MCP implementation
│   │   │   ├── mcp_server.h              # Server header
│   │   │   ├── mcp_server_complete.cpp   # Tool implementations
│   │   │   ├── mcp_bridge.h/cpp          # IPC bridge
│   │   │   ├── mcp_helpers.h/cpp         # Utilities
│   │   │   └── cache_manager.h/cpp       # Caching
│   │   └── core/
│   │       ├── application.h/cpp         # MCP integration
│   │       └── launcher.cpp              # tdata auto-detection
│   ├── CMakeLists.txt                    # Build configuration
│   └── build/
│       └── prepare/mac.sh                # Dependency builder
├── CLAUDE.md                             # Technical docs for AI
├── README.md                             # This file
└── patches/                              # Patches for upstream updates
```

### Adding New Tools

**Step 1:** Declare in `mcp_server.h`
```cpp
QJsonObject toolYourNewTool(const QJsonObject &args);
```

**Step 2:** Implement in `mcp_server_complete.cpp`
```cpp
QJsonObject Server::toolYourNewTool(const QJsonObject &args) {
    // Validate session
    if (!_session) {
        return toolError("No active session");
    }

    // Get arguments
    QString param = args["param"].toString();

    // Implement logic
    QJsonObject result;
    result["success"] = true;
    result["data"] = "...";

    return result;
}
```

**Step 3:** Register handler in `initializeToolHandlers()`
```cpp
_toolHandlers["your_new_tool"] = [this](const QJsonObject &args) {
    return toolYourNewTool(args);
};
```

**Step 4:** Register metadata in `registerTools()`
```cpp
Tool yourTool;
yourTool.name = "your_new_tool";
yourTool.description = "What your tool does";
yourTool.inputSchema = createInputSchema({
    {"param", "string", true, "Parameter description"}
});
_tools.append(yourTool);
```

---

## Security

### Security Model

| Aspect | Implementation |
|--------|----------------|
| **Transport** | Unix socket only (no network exposure) |
| **Authentication** | Inherits Telegram session auth |
| **Scope** | Can only access what user can access |
| **Isolation** | Runs in same process as Telegram |

### Best Practices

1. **Keep tdata secure** - Use `chmod 700 ~/tdata`
2. **Regular session audits** - Use `get_active_sessions` periodically
3. **Terminate unused sessions** - Remove devices you don't recognize
4. **Privacy settings** - Set appropriate visibility levels

### What MCP Can Access

- Messages you can read
- Chats you're a member of
- Your profile information
- Your privacy settings
- Your active sessions

### What MCP Cannot Access

- Other users' private data
- Channels you're not subscribed to
- Messages deleted by others
- Server-side-only data

---

## License

This project is licensed under the same terms as Telegram Desktop. See [LICENSE](LICENSE) for details.

## Credits

- Based on [Telegram Desktop](https://github.com/telegramdesktop/tdesktop) by Telegram Messenger LLP

## Support

- **Issues:** [GitHub Issues](../../issues)
- **Telegram Desktop docs:** https://desktop.telegram.org
- **MCP Protocol:** https://modelcontextprotocol.io

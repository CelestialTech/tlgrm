# MCP Server Integration Notes

## Files Modified

### 1. CMakeLists.txt
Added MCP source files at line ~1210:
```cmake
mcp/mcp_bridge.cpp
mcp/mcp_bridge.h
mcp/mcp_server.cpp
mcp/mcp_server.h
```

### 2. core/application.h
Added:
- Forward declaration: `namespace MCP { class Server; }`
- Public getter: `mcpServer()` method
- Private member: `std::unique_ptr<MCP::Server> _mcpServer;`

### 3. core/application.cpp
Added initialization in `Application::run()` method (after domain/session setup):
```cpp
#include "mcp/mcp_server.h"

// Start MCP server if --mcp flag is present
const auto args = QCoreApplication::arguments();
if (args.contains(u"--mcp"_q)) {
    _mcpServer = std::make_unique<MCP::Server>();
    if (_mcpServer->start(MCP::TransportType::Stdio)) {
        LOG(("MCP: Server started successfully"));
    } else {
        LOG(("MCP Error: Failed to start server"));
        _mcpServer = nullptr;
    }
}
```

## Available MCP Tools (30 total)

### Core Messaging Tools (11 tools)
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

### Profile Settings Tools (5 tools)
| Tool | Description | Status |
|------|-------------|--------|
| `get_profile_settings` | Get firstName, lastName, username, phone, bio, birthday | Working |
| `update_profile_name` | Update first/last name | Not supported (requires GUI) |
| `update_profile_bio` | Update bio via `api().saveSelfBio()` | **Working** |
| `update_profile_username` | Update username | Not supported (requires verification) |
| `update_profile_phone` | Update phone number | Not supported (requires SMS) |

### Privacy Settings Tools (8 tools)
| Tool | Description | API Used |
|------|-------------|----------|
| `get_privacy_settings` | Reload all privacy settings | Reloads all keys |
| `update_last_seen_privacy` | Set last seen visibility | `Api::UserPrivacy::Key::LastSeen` |
| `update_profile_photo_privacy` | Set profile photo visibility | `Api::UserPrivacy::Key::ProfilePhoto` |
| `update_phone_number_privacy` | Set phone visibility | `Api::UserPrivacy::Key::PhoneNumber` |
| `update_forwards_privacy` | Set forwards link visibility | `Api::UserPrivacy::Key::Forwards` |
| `update_birthday_privacy` | Set birthday visibility | `Api::UserPrivacy::Key::Birthday` |
| `update_about_privacy` | Set bio/about visibility | `Api::UserPrivacy::Key::About` |
| `get_blocked_users` | Get blocked users list | `blockedPeers().reload()` |

### Security Settings Tools (6 tools)
| Tool | Description | API Used |
|------|-------------|----------|
| `get_security_settings` | Get auto-delete period | `selfDestruct()` |
| `get_active_sessions` | List all active sessions | `authorizations().list()` |
| `terminate_session` | Terminate a session by hash | `authorizations().requestTerminate()` |
| `block_user` | Block a user | `blockedPeers().block()` |
| `unblock_user` | Unblock a user | `blockedPeers().unblock()` |
| `update_auto_delete_period` | Set default auto-delete (0/86400/604800/2592000) | `selfDestruct().updateDefaultHistoryTTL()` |

## Command-Line Usage

Run Telegram Desktop with MCP enabled:
```bash
# On macOS
/Applications/Tlgrm.app/Contents/MacOS/Tlgrm --mcp

# Or from build directory
./out/Release/Tlgrm.app/Contents/MacOS/Tlgrm --mcp
```

## Claude Desktop Integration

Add to `~/Library/Application Support/Claude/claude_desktop_config.json`:
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

## Privacy Rules

When updating privacy settings, use one of:
- `"everybody"` - Everyone can see
- `"contacts"` - Only contacts can see
- `"close_friends"` - Only close friends can see
- `"nobody"` - Nobody can see

## Auto-Delete Periods

Valid values for `update_auto_delete_period`:
- `0` - Disabled
- `86400` - 1 day (24 hours)
- `604800` - 1 week (7 days)
- `2592000` - 1 month (30 days)

## Build Command

```bash
cd ~/xCode/tlgrm/tdesktop
./configure.sh -D TDESKTOP_API_ID=YOUR_ID -D TDESKTOP_API_HASH=YOUR_HASH

# Build with Xcode
cd out
xcodebuild -project Telegram.xcodeproj -scheme Telegram -configuration Release build
```

## Version Info

- Base: Telegram Desktop 6.3
- MCP Protocol: 2024-11-05
- Last Updated: 2024-11-28

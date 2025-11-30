# Tlgrm - Telegram Desktop with MCP Integration

A custom fork of [Telegram Desktop](https://github.com/telegramdesktop/tdesktop) with an embedded Model Context Protocol (MCP) server, enabling AI assistants like Claude to interact directly with your Telegram data.

## Features

- **200+ MCP Tools** for interacting with Telegram data
- **Direct local database access** - instant message reads without API rate limits
- **Privacy controls** - manage who can see your data
- **Message management** - send, edit, delete, forward, pin messages
- **Analytics** - get statistics about your chats and usage
- **Archive & Export** - backup and export chat histories

## Quick Start

### Download Pre-built DMG

Download the latest release from the [Releases](../../releases) page.

### Build from Source

**Requirements:**
- macOS Ventura (13.0+) or Sonoma (14.0+)
- Xcode 14.0+ with Command Line Tools
- Homebrew packages: `brew install git cmake python3 automake autoconf libtool pkg-config ninja wget meson nasm`
- 50GB+ free disk space
- Telegram API credentials from https://my.telegram.org

**Build Steps:**

```bash
# Clone the repository
git clone --recursive https://github.com/pashaprig/tdesktop.git
cd tdesktop

# Build dependencies (takes 40-70 minutes)
cd Telegram
./build/prepare/mac.sh silent

# Configure with your API credentials
./configure.sh -D TDESKTOP_API_ID=YOUR_ID -D TDESKTOP_API_HASH=YOUR_HASH

# Build (takes 10-20 minutes)
cd ../out
xcodebuild -project Telegram.xcodeproj -scheme Telegram -configuration Release build
```

The built app will be at `out/Release/Tlgrm.app`

## Using MCP Mode

### Start Telegram in MCP Mode

```bash
./Tlgrm.app/Contents/MacOS/Tlgrm --mcp
```

This starts Telegram with the MCP server listening on a Unix socket at `/tmp/tdesktop_mcp.sock`.

### Test the Connection

```bash
# Test ping
echo '{"jsonrpc":"2.0","id":1,"method":"ping","params":{}}' | nc -U /tmp/tdesktop_mcp.sock

# List your chats
echo '{"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"list_chats","arguments":{}}}' | nc -U /tmp/tdesktop_mcp.sock
```

### Configure Claude Desktop

Add to your Claude Desktop config (`~/Library/Application Support/Claude/claude_desktop_config.json`):

```json
{
  "mcpServers": {
    "telegram": {
      "command": "/path/to/Tlgrm.app/Contents/MacOS/Tlgrm",
      "args": ["--mcp"]
    }
  }
}
```

## Available Tools

### Core Messaging (6 tools)
| Tool | Description |
|------|-------------|
| `list_chats` | Get all your Telegram chats |
| `get_chat_info` | Get detailed info about a chat |
| `read_messages` | Read messages from a chat |
| `send_message` | Send a message |
| `search_messages` | Search messages |
| `get_user_info` | Get info about a user |

### Message Operations (6 tools)
| Tool | Description |
|------|-------------|
| `edit_message` | Edit a message |
| `delete_message` | Delete a message |
| `forward_message` | Forward a message |
| `pin_message` | Pin a message |
| `unpin_message` | Unpin a message |
| `add_reaction` | Add a reaction |

### Privacy & Security (14 tools)
| Tool | Description |
|------|-------------|
| `get_privacy_settings` | Get current privacy settings |
| `update_last_seen_privacy` | Set who can see your last seen |
| `update_profile_photo_privacy` | Set who can see your photo |
| `get_active_sessions` | List active sessions |
| `terminate_session` | Log out a session |
| `block_user` / `unblock_user` | Block/unblock users |

### Profile Settings (5 tools)
| Tool | Description |
|------|-------------|
| `get_profile_settings` | Get your profile info |
| `update_profile_bio` | Update your bio |

### Archive & Analytics (17 tools)
Tools for archiving chats, exporting data, and getting statistics about your messaging patterns.

### And 150+ More
Including business features, wallet integration, star gifts, and more. See [CLAUDE.md](CLAUDE.md) for the complete list.

## Architecture

The MCP server is embedded directly in the Telegram Desktop process:

```
┌─────────────────────────────────────────┐
│         Telegram Desktop                │
│  ┌───────────────────────────────────┐  │
│  │         MCP Server                │  │
│  │  - JSON-RPC 2.0 protocol          │  │
│  │  - Unix socket transport          │  │
│  │  - Direct database access         │  │
│  └───────────────────────────────────┘  │
│                  │                      │
│  ┌───────────────┴───────────────────┐  │
│  │       Telegram APIs               │  │
│  │  - Local SQLite database          │  │
│  │  - MTProto API                    │  │
│  │  - Data structures                │  │
│  └───────────────────────────────────┘  │
└─────────────────────────────────────────┘
            │
            ▼
┌─────────────────────────────────────────┐
│       AI Assistant (Claude)             │
│  - Sends JSON-RPC requests              │
│  - Receives chat/message data           │
│  - Can send messages on your behalf     │
└─────────────────────────────────────────┘
```

## Security

- **Local only**: MCP server only accepts local Unix socket connections
- **No network exposure**: Your data never leaves your machine
- **Session required**: Tools only work when logged into Telegram
- **Read your data**: AI can only access data you could access normally

## Development

### Project Structure

```
tdesktop/
├── Telegram/
│   ├── SourceFiles/
│   │   ├── mcp/                    # MCP server implementation
│   │   │   ├── mcp_server.h        # Server header (200+ tool declarations)
│   │   │   ├── mcp_server_complete.cpp  # Tool implementations
│   │   │   ├── mcp_bridge.h/cpp    # IPC bridge (optional)
│   │   │   ├── mcp_helpers.h/cpp   # Helper functions
│   │   │   └── cache_manager.h/cpp # LRU cache
│   │   └── core/
│   │       ├── application.h/cpp   # MCP integration point
│   │       └── ...
│   └── CMakeLists.txt              # Build configuration
├── CLAUDE.md                       # AI assistant documentation
└── README.md                       # This file
```

### Adding New Tools

1. Declare in `mcp_server.h`:
   ```cpp
   QJsonObject toolYourTool(const QJsonObject &args);
   ```

2. Implement in `mcp_server_complete.cpp`:
   ```cpp
   QJsonObject Server::toolYourTool(const QJsonObject &args) {
       if (!_session) return toolError("No session");
       // Implementation
       QJsonObject result;
       result["success"] = true;
       return result;
   }
   ```

3. Register in `initializeToolHandlers()`

4. Register metadata in `registerTools()`

See [CLAUDE.md](CLAUDE.md) for detailed documentation.

## Contributing

Contributions are welcome! Please:

1. Fork the repository
2. Create a feature branch
3. Make your changes
4. Test the build
5. Submit a pull request

## License

This project is licensed under the same terms as Telegram Desktop. See [LICENSE](LICENSE) for details.

## Credits

- Based on [Telegram Desktop](https://github.com/telegramdesktop/tdesktop) by Telegram Messenger LLP
- MCP integration developed with assistance from Claude (Anthropic)

## Support

- Issues: [GitHub Issues](../../issues)
- Telegram Desktop docs: https://desktop.telegram.org
- MCP Protocol: https://modelcontextprotocol.io

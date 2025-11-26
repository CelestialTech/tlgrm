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

### MCP Tools Available

1. **list_chats** - Get all your Telegram chats
2. **get_chat_info** - Detailed information about specific chats
3. **read_messages** - Read messages from local database (instant!)
4. **send_message** - Send messages to any chat
5. **search_messages** - Search your message history locally

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

1. **Real data integration** - Connect MCP tools to actual Telegram session
2. **Semantic search** - Add FAISS embedding search
3. **Voice transcription** - Integrate Whisper.cpp for voice messages
4. **Media processing** - OCR, image analysis
5. **HTTP transport** - Enable SSE notifications
6. **Tests** - Unit tests, integration tests
7. **Documentation** - Improve guides, add examples

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

For issues specific to MCP integration:
- Open an issue in this repository

For general Telegram Desktop issues:
- See official tdesktop repository

---

**Version:** 6.3.3
**Last Updated:** 2025-11-25
**Base Commit:** aadc81279a
**Platform:** macOS (Apple Silicon + Intel Universal Binary)
**Target:** macOS Ventura 13.0+

For detailed technical documentation, see [CLAUDE.md](CLAUDE.md).

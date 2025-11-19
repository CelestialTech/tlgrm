# Telegram Desktop MCP Integration Guide

## 🎯 Overview

This project integrates **Model Context Protocol (MCP)** with Telegram Desktop through a hybrid C++/Python architecture that combines native desktop integration with advanced AI/ML capabilities.

## 🏗️ Architecture

```
┌────────────────────────────────────────────────────────┐
│               AI Model (Claude/GPT)                    │
└───────────────────┬────────────────────────────────────┘
                    │ MCP Protocol (JSON-RPC)
┌───────────────────▼────────────────────────────────────┐
│          Python MCP Server (Enhanced)                  │
│   • Semantic search & embeddings                       │
│   • Intent classification                              │
│   • Topic extraction                                   │
│   • Conversation summarization                         │
└───────────────────┬────────────────────────────────────┘
                    │ HTTP/IPC Bridge
┌───────────────────▼────────────────────────────────────┐
│          C++ MCP Server (tdesktop)                     │
│   • Native Telegram Desktop integration                │
│   • ChatArchiver (47 tools)                            │
│   • Analytics & Statistics                             │
│   • Qt SQL Database                                    │
└────────────────────────────────────────────────────────┘
```

## 📦 Components

### 1. C++ Bot Framework (tdesktop)

**Location**: `/Users/pasha/xCode/tlgrm/tdesktop/`

**Key Files**:
- `Telegram/SourceFiles/mcp/mcp_server_complete.cpp` - Main MCP server
- `Telegram/SourceFiles/mcp/chat_archiver.cpp/h` - Message archiving (Qt SQL)
- `Telegram/SourceFiles/mcp/analytics.cpp/h` - Chat analytics
- `Telegram/SourceFiles/mcp/bot_manager.cpp/h` - Bot lifecycle management
- `Telegram/SourceFiles/settings/settings_bots.cpp/h` - UI settings panel

**Features**:
- ✅ 47 MCP tools for chat operations
- ✅ Qt SQL database for message storage
- ✅ Real-time message archiving
- ✅ Export to JSON/JSONL/CSV formats
- ✅ Ephemeral message capture
- ⚠️ UI widgets partially implemented (checkboxes/sliders TODO)

**Build Status**: ✅ Compiles successfully on macOS (Apple Silicon)

### 2. Python MCP Server (Enhanced)

**Location**: `/Users/pasha/PycharmProjects/telegramMCP/`

**Key Files**:
- `src/mcp_server_enhanced.py` - Main Python MCP server
- `src/tdesktop_bridge.py` - Bridge client to C++ server

**Features**:
- ✅ AI/ML using sentence-transformers
- ✅ Vector database (ChromaDB)
- ✅ Semantic search across messages
- ✅ Intent classification (BART)
- ✅ Bridge integration with C++ server
- ✅ 4 hybrid tools (C++ + Python AI)

**Dependencies**:
```python
mcp>=1.0.0
sentence-transformers
chromadb
torch  # Apple Silicon MPS support
transformers
langchain
python-telegram-bot>=20.0
loguru
aiohttp
```

### 3. IPC Bridge

**Protocol**: HTTP (async with aiohttp)
**Port**: 8765 (default)
**Status**: ⚠️ Implemented in Python, needs C++ HTTP endpoints

## 🚀 Quick Start

### Prerequisites

```bash
# macOS (Apple Silicon)
- Xcode 14+
- CMake 3.20+
- Qt 6.x
- Python 3.12+
- Telegram Desktop build environment
```

### 1. Build C++ Server

```bash
cd /Users/pasha/xCode/tlgrm/tdesktop
./configure.sh
cd out
cmake .. -DCMAKE_BUILD_TYPE=Release
xcodebuild -project Telegram.xcodeproj -scheme Telegram -configuration Release build
```

**Output**: `/Users/pasha/xCode/tlgrm/out/Release/Telegram.app`

### 2. Setup Python Server

```bash
cd /Users/pasha/PycharmProjects/telegramMCP

# Create virtual environment
python3.12 -m venv venv
source venv/bin/activate

# Install dependencies
pip install -r requirements.txt

# Configure environment
cp .env.example .env
# Edit .env with your TELEGRAM_BOT_TOKEN
```

### 3. Run the System

```bash
# Terminal 1: Start Telegram Desktop with MCP server
/Users/pasha/xCode/tlgrm/out/Release/Telegram.app/Contents/MacOS/Telegram --enable-mcp --mcp-port=8765

# Terminal 2: Start Python MCP server
cd /Users/pasha/PycharmProjects/telegramMCP
python src/mcp_server_enhanced.py
```

## 🔧 Configuration

### C++ Server Configuration

Edit `tdesktop/Telegram/SourceFiles/mcp/config.json` (if exists):

```json
{
  "mcp_enabled": true,
  "http_port": 8765,
  "database_path": "./telegram_mcp.db",
  "max_context_messages": 50
}
```

### Python Server Configuration

Edit `/Users/pasha/PycharmProjects/telegramMCP/.env`:

```env
TELEGRAM_BOT_TOKEN=your_bot_token_here
OPENAI_API_KEY=optional_for_gpt_features
MCP_SERVER_PORT=3000
TDESKTOP_IPC_PORT=8765
```

## 📚 MCP Tools Reference

### Hybrid Tools (Python + C++)

#### 1. `get_chat_messages_with_ai`
Fetches messages from C++ and indexes them for semantic search.

```json
{
  "name": "get_chat_messages_with_ai",
  "arguments": {
    "chat_id": -1001234567890,
    "limit": 50
  }
}
```

#### 2. `semantic_search_with_context`
Semantic search using Python AI, enriched with full message data from C++.

```json
{
  "name": "semantic_search_with_context",
  "arguments": {
    "query": "discussions about project deadlines",
    "chat_id": -1001234567890,
    "limit": 10
  }
}
```

#### 3. `analyze_chat_with_stats`
Comprehensive chat analysis combining C++ statistics and Python AI insights.

```json
{
  "name": "analyze_chat_with_stats",
  "arguments": {
    "chat_id": -1001234567890
  }
}
```

#### 4. `export_chat_with_embeddings`
Exports chat with optional AI-generated embeddings for ML training.

```json
{
  "name": "export_chat_with_embeddings",
  "arguments": {
    "chat_id": -1001234567890,
    "format": "jsonl",
    "include_embeddings": true
  }
}
```

### C++ Native Tools (47 tools)

Access via Python bridge or directly through tdesktop:

- `archive_message` - Archive a message to database
- `get_messages` - Fetch messages from database
- `search_messages` - Text search in messages
- `get_chat_stats` - Chat statistics
- `get_user_activity` - User activity analysis
- `export_chat` - Export chat to JSON/JSONL/CSV
- `list_archived_chats` - List all archived chats
- And 40 more...

Full list in: `tdesktop/Telegram/SourceFiles/mcp/mcp_server_complete.cpp`

## 🧪 Testing

### Test Bridge Connection

```python
import asyncio
from src.tdesktop_bridge import get_bridge

async def test():
    bridge = await get_bridge()
    health = await bridge.health_check()
    print(f"Bridge healthy: {health}")

asyncio.run(test())
```

### Test MCP Tools

Using `mcp` CLI or Claude Desktop:

```bash
# List available tools
mcp list-tools http://localhost:3000

# Call a tool
mcp call-tool semantic_search_with_context --query "meeting schedule" http://localhost:3000
```

## 🛠️ Development

### Adding New C++ Tools

1. Edit `mcp_server_complete.cpp`
2. Register tool in `registerTools()`
3. Implement handler function
4. Rebuild tdesktop

### Adding New Python Tools

1. Edit `mcp_server_enhanced.py`
2. Add `@mcp.tool()` decorator
3. Implement async function
4. Restart Python server

### Adding Bridge Methods

1. Edit `tdesktop_bridge.py`
2. Add async method
3. Map to C++ HTTP endpoint
4. Use in Python tools

## 📋 Next Steps

### Immediate (This Week)
- [ ] Add HTTP server to C++ MCP implementation
- [ ] Test end-to-end integration
- [ ] Complete UI widget implementation (checkboxes, sliders)
- [ ] Add `.gitignore` to Python project

### Short-term (Next 2 Weeks)
- [ ] Unit tests for bridge communication
- [ ] Error handling improvements
- [ ] Logging infrastructure
- [ ] Performance profiling

### Long-term (Next Month)
- [ ] CI/CD pipeline
- [ ] Security hardening
- [ ] Multi-platform support (Windows, Linux)
- [ ] Advanced AI features (summarization, translation)

## 🐛 Troubleshooting

### C++ Server won't start
- Check Telegram Desktop is not already running
- Verify `--enable-mcp` flag
- Check port 8765 is not in use

### Python Bridge fails
- Ensure C++ server is running first
- Check `TDESKTOP_IPC_PORT` in .env
- Verify network connectivity (localhost:8765)

### AI/ML features slow
- Reduce batch sizes
- Use MPS acceleration (Apple Silicon)
- Check ChromaDB indices

## 📖 References

- [MCP Specification](https://modelcontextprotocol.io/)
- [tdesktop Build Guide](https://github.com/telegramdesktop/tdesktop#build-instructions)
- [python-telegram-bot Docs](https://docs.python-telegram-bot.org/)
- [Sentence Transformers](https://www.sbert.net/)

## 📄 License

GPL-3.0 with OpenSSL exception (following tdesktop license)

---

**Last Updated**: 2025-11-19  
**Platform**: macOS (Apple Silicon M1/M2/M3+)  
**Status**: Development (C++ working, Python integration in progress)

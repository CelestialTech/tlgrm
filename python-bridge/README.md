# Python MCP Bridge

This directory contains a **fallback Python MCP server** implementation for Telegram. While the primary MCP implementation is the C++ server embedded in Telegram Desktop (see `tdesktop/Telegram/SourceFiles/mcp/`), this Python bridge can be used for:

1. **Development and Testing** - Rapid prototyping of new MCP features
2. **IPC Communication** - Connecting to the C++ MCP bridge via Unix socket
3. **Standalone Mode** - Running MCP server independently (uses Telegram Bot API, not direct database access)

## Architecture

```
┌─────────────────┐
│   AI Model      │
│   (Claude)      │
└────────┬────────┘
         │ MCP Protocol
         │
┌────────▼────────┐
│  Python Bridge  │  ← This directory
│  (FastMCP)      │
└────────┬────────┘
         │
         ├─────────────────────┐
         │                     │
    [Option 1]            [Option 2]
    Direct Bot API        IPC to C++ Server
    (rate limited)        (fast, local DB)
         │                     │
         ▼                     ▼
    Telegram API        C++ tdesktop MCP
```

## Files

- **`mcp_server.py`** - FastMCP server implementation (standalone mode)
- **`tdesktop_bridge.py`** - IPC client for connecting to C++ MCP bridge
- **`telegram_client.py`** - Telegram Bot API wrapper
- **`config.py`** - Configuration loader (TOML + environment variables)
- **`cache.py`** - Message caching (in-memory)
- **`main.py`** - Entry point for standalone mode

## Usage

### Standalone Mode (Telegram Bot API)

```bash
# Install dependencies
pip install -r requirements.txt

# Configure
cp config.toml config.local.toml
# Edit config.local.toml with your bot token

# Run
python main.py
```

### IPC Mode (Connect to C++ Server)

```python
from tdesktop_bridge import TDesktopBridge

bridge = TDesktopBridge("/tmp/tdesktop_mcp.sock")
messages = await bridge.get_messages_local(chat_id=-1001234567890, limit=50)
```

## Configuration

See `config.toml` for all available options:

- `telegram.token_env_var` - Environment variable for bot token
- `telegram.allowed_chats` - Whitelist of chat IDs
- `mcp.transport` - "stdio" or "http"
- `cache.max_messages` - Max messages to cache

## Why Both C++ and Python?

| Feature | C++ (Primary) | Python (Fallback) |
|---------|---------------|-------------------|
| **Speed** | **10-100x faster** | Slower (API calls) |
| **Database Access** | **Direct SQLite** | None (API only) |
| **Rate Limits** | **None** | 30 msg/sec |
| **Deployment** | Single binary | Requires Python |
| **Development** | Slower (rebuild) | **Rapid prototyping** |

**Recommendation**: Use C++ server for production, Python for development.

## Development

To add a new MCP tool:

1. Prototype in Python (`mcp_server.py`)
2. Test with Claude Desktop
3. Port to C++ once stable (`tdesktop/Telegram/SourceFiles/mcp/mcp_server.cpp`)

## See Also

- Main documentation: `../docs/`
- C++ MCP server: `../tdesktop/Telegram/SourceFiles/mcp/`
- Architecture decision: `../docs/ARCHITECTURE_DECISION.md`

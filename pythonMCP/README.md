# pythonMCP - Unified Telegram MCP Server

Complementary Python MCP server for Telegram with optional AI/ML capabilities.

## Overview

This is the **Python component** of the Telegram MCP system, working alongside the C++ server to provide:

**Core Features** (always available):
- Message retrieval and search
- Chat listing and statistics
- IPC bridge to C++ server for fast local database access

**AI/ML Features** (optional):
- 🔍 Semantic search using embeddings
- 🧠 Intent classification
- 📊 Topic extraction
- 💬 Conversation summarization
- 🍎 Apple Silicon GPU acceleration (MPS)

## Architecture

```
┌──────────────┐
│   Claude AI  │
└──────┬───────┘
       │ MCP Protocol
       │
┌──────▼────────┐      IPC Socket      ┌─────────────┐
│  pythonMCP    │◄────────────────────►│  C++ Server │
│  (This)       │  /tmp/telegram_mcp.  │  (tdesktop) │
└──────┬────────┘       sock            └──────┬──────┘
       │                                        │
┌──────▼────────┐                      ┌───────▼──────┐
│  Vector DB    │                      │  SQLite DB   │
│  (ChromaDB)   │                      │  (Fast)      │
│  AI Models    │                      │  Local Cache │
└───────────────┘                      └──────────────┘
```

## Quick Start

### Option 1: Minimal Install (No AI/ML)

Fast, lightweight - perfect for basic MCP operations.

```bash
cd pythonMCP

# Install minimal dependencies using uv
uv pip install -r requirements-minimal.txt

# Run in bridge mode (connect to C++ server)
python src/mcp_server.py --mode bridge --no-aiml
```

### Option 2: Full Install (With AI/ML)

Complete feature set including semantic search and intent classification.

```bash
cd pythonMCP

# Install full dependencies (may take 5-10 min with uv, faster than pip!)
uv pip install -r requirements.txt

# Download spaCy model (optional)
python -m spacy download en_core_web_sm

# Run in hybrid mode (C++ + AI/ML)
python src/mcp_server.py --mode hybrid
```

## Server Modes

### 1. Standalone Mode
```bash
python src/mcp_server.py --mode standalone
```
- Uses Telegram Bot API directly
- No C++ dependency
- **Limitation**: Rate limited (30 messages/sec)
- **Use case**: Development, testing without tdesktop

### 2. Bridge Mode
```bash
python src/mcp_server.py --mode bridge
```
- Connects to C++ server via Unix socket
- Fast local database access
- No rate limits
- **Requires**: C++ MCP server running
- **Use case**: Fast queries, no AI needed

### 3. Hybrid Mode (Default)
```bash
python src/mcp_server.py --mode hybrid
```
- Best of both: C++ speed + Python AI/ML
- Reads data from C++, enhances with AI
- **Requires**: C++ server + AI/ML dependencies
- **Use case**: Production with full features

## Configuration

### Environment Variables

Create `.env` file:

```bash
# Telegram credentials
TELEGRAM_API_ID=12345678
TELEGRAM_API_HASH=your_api_hash
TELEGRAM_BOT_TOKEN=your_bot_token

# IPC configuration
IPC_SOCKET_PATH=/tmp/telegram_mcp.sock

# AI/ML settings (optional)
EMBEDDING_MODEL=sentence-transformers/all-MiniLM-L6-v2
VECTOR_DB_PATH=./data/chromadb
DEVICE=mps  # mps (Apple Silicon), cuda, or cpu
```

### Configuration File

Create `config.toml` (see `config.toml.example`):

```toml
[telegram]
api_id_env = "TELEGRAM_API_ID"
api_hash_env = "TELEGRAM_API_HASH"

[mcp]
server_name = "Telegram MCP"
mode = "hybrid"

[ipc]
cpp_socket_path = "/tmp/telegram_mcp.sock"

[aiml]
enabled = true
embedding_model = "sentence-transformers/all-MiniLM-L6-v2"
device = "mps"
```

## MCP Tools Reference

### Core Tools (Always Available)

#### `get_messages(chat_id, limit=50)`
Retrieve recent messages from a chat.

```json
{
  "tool": "get_messages",
  "arguments": {
    "chat_id": -1001234567890,
    "limit": 50
  }
}
```

#### `search_messages(chat_id, query, limit=50)`
Keyword-based message search.

```json
{
  "tool": "search_messages",
  "arguments": {
    "chat_id": -1001234567890,
    "query": "project deadline",
    "limit": 20
  }
}
```

#### `list_chats()`
List all available chats.

### AI/ML Tools (Require Full Install)

#### `semantic_search(query, chat_id=None, limit=10)`
Find messages by meaning, not keywords.

```json
{
  "tool": "semantic_search",
  "arguments": {
    "query": "discussions about postponing the meeting",
    "limit": 5
  }
}
```

#### `classify_intent(text)`
Understand what a message is asking for.

```json
{
  "tool": "classify_intent",
  "arguments": {
    "text": "Can you help me find the meeting notes?"
  }
}
```

Returns: `{"intent": "request", "confidence": 0.92}`

## Development

### Project Structure

```
pythonMCP/
├── src/
│   ├── mcp_server.py          # Main server (unified)
│   ├── ipc_bridge.py          # IPC to C++ server
│   ├── core/                  # Core features
│   │   ├── telegram_client.py # Telegram API client
│   │   ├── cache.py           # Message caching
│   │   └── config.py          # Configuration
│   └── aiml/                  # AI/ML features (optional)
│       ├── service.py         # AI/ML service layer
│       └── ...
├── requirements.txt           # Full dependencies
├── requirements-minimal.txt   # Core dependencies
├── pyproject.toml            # Poetry config
└── tests/                    # Unit tests
```

### Running Tests

```bash
# Test minimal install
pip install -r requirements-minimal.txt
pytest tests/test_core.py

# Test full install
pip install -r requirements.txt
pytest tests/

# With coverage
pytest --cov=src tests/
```

### PyCharm Setup

1. **Open Project**: File → Open → `/Users/pasha/xCode/tlgrm/pythonMCP`
2. **Configure Interpreter**:
   - Create virtual environment in `pythonMCP/.venv`
   - Select interpreter: `.venv/bin/python`
3. **Mark Sources**: Right-click `src/` → Mark Directory as → Sources Root
4. **Run Configuration**:
   - Script: `src/mcp_server.py`
   - Working directory: `pythonMCP/`
   - Environment: Load from `.env`

## Performance

### Benchmarks (Apple M1, Hybrid Mode)

| Operation | Time | Notes |
|-----------|------|-------|
| Get 50 messages (via C++) | 5ms | Direct SQLite |
| Keyword search (1K msgs) | 15ms | SQLite FTS |
| Generate embedding | 20ms | MPS accelerated |
| Semantic search (10K msgs) | 30ms | ChromaDB query |
| Intent classification | 100ms | BART model |

### Memory Usage

- **Minimal**: ~100 MB
- **Full AI/ML**: ~2 GB (models loaded)

## Troubleshooting

### "AI/ML features not available"

**Problem**: AI/ML tools return error.

**Solution**:
```bash
pip install -r requirements.txt
```

### "IPC connection failed"

**Problem**: Cannot connect to C++ server.

**Solution**:
1. Ensure C++ MCP server is running
2. Check socket path: `ls -la /tmp/telegram_mcp.sock`
3. Try standalone mode: `--mode standalone`

### "MPS acceleration not working"

**Problem**: Not using Apple Silicon GPU.

**Solution**:
```bash
python -c "import torch; print(torch.backends.mps.is_available())"
# Should return True on Apple Silicon
```

## Integration with C++ Server

This Python server **complements** the C++ server:

- **C++ handles**: Fast database queries, local message cache
- **Python handles**: AI/ML processing, semantic understanding
- **Communication**: Unix socket IPC (JSON-RPC)

See `../docs/ipc-protocol.md` for details.

## License

GPL-3.0-or-later (matches tdesktop)

## See Also

- [Root README](../README.md) - Complete system overview
- [Architecture](../ARCHITECTURE.md) - How components work together
- [C++ Server](../tdesktop/) - Native Telegram Desktop integration
- [MCP Tools API](../docs/mcp-tools.md) - Complete API reference

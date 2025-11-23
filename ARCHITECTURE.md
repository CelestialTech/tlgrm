# Architecture: Complementary C++ + Python MCP System

## Overview

This project uses a **complementary dual-language architecture** where C++ and Python each provide unique, essential capabilities:

```
┌─────────────────────────────────────────────────────────────┐
│                      AI Model (Claude)                      │
└────────────────────────┬────────────────────────────────────┘
                         │ MCP Protocol
            ┌────────────┴────────────┐
            │                         │
    ┌───────▼────────┐       ┌───────▼────────┐
    │ C++ MCP Server │       │ Python AI/ML   │
    │   (tdesktop)   │◄─────►│  MCP Server    │
    └───────┬────────┘  IPC  └───────┬────────┘
            │                         │
    ┌───────▼────────┐       ┌───────▼────────┐
    │ Local SQLite   │       │ Vector DB      │
    │ Message Cache  │       │ (ChromaDB)     │
    │ Fast Queries   │       │ AI Models      │
    └────────────────┘       └────────────────┘
```

## Component Responsibilities

### C++ Component (`tdesktop/`)
**Role**: Native Telegram integration, high-performance data access

**Strengths**:
- Direct SQLite database access (10-100x faster than API)
- No rate limits (local-only operations)
- Native UI integration with Telegram Desktop
- MTProto protocol implementation
- Real-time message streaming
- Media file access

**MCP Tools**:
- `get_messages_local()` - Instant message retrieval
- `search_messages_db()` - Fast full-text search
- `get_chat_history()` - Local cache queries
- `export_chat()` - Bulk data export
- `get_media_files()` - Media access

### Python Component (`python-ml/`)
**Role**: AI/ML processing, semantic understanding

**Strengths**:
- Sentence transformers for embeddings
- Vector database for semantic search
- Intent classification (BART, T5)
- Topic extraction and clustering
- Conversation summarization
- Entity recognition (NER)
- Apple Silicon GPU acceleration (MPS)

**MCP Tools**:
- `semantic_search()` - Meaning-based search
- `classify_intent()` - Understand user goals
- `extract_topics()` - Conversation themes
- `summarize_conversation()` - AI summaries
- `find_similar_chats()` - Cluster analysis
- `generate_embeddings()` - Vector representations

## Integration Patterns

### Pattern 1: Hybrid Queries
**Use Case**: Semantic search with full message context

```
1. User asks: "Find discussions about project deadlines"
2. Python AI/ML: Generate query embedding, search vector DB
3. Python → C++: Request full message data for top results (IPC)
4. C++ Server: Fetch complete messages from SQLite
5. Return: Semantically relevant messages with full metadata
```

### Pattern 2: AI-Enhanced Streaming
**Use Case**: Real-time message analysis

```
1. C++ Server: Stream new messages from Telegram
2. C++ → Python: Send message text (IPC)
3. Python AI/ML: Classify intent, extract entities, update embeddings
4. Python → Vector DB: Index for future semantic search
5. Return: Enriched message metadata to MCP client
```

### Pattern 3: Bulk Processing
**Use Case**: Analyze entire chat history

```
1. User asks: "What are the main topics in this group?"
2. C++ Server: Export last 1000 messages (fast SQLite query)
3. C++ → Python: Batch transfer (IPC)
4. Python AI/ML: Topic modeling (LDA/clustering)
5. Return: Topic distribution, keywords, timeline
```

## Communication Layer

### IPC Mechanism: Unix Domain Socket (JSON-RPC)

**Socket Path**: `/tmp/telegram_mcp.sock`

**Message Format**:
```json
{
  "jsonrpc": "2.0",
  "method": "get_messages",
  "params": {
    "chat_id": -1001234567890,
    "limit": 100
  },
  "id": 1
}
```

**Why Unix Socket?**
- Faster than HTTP (no TCP overhead)
- Secure (filesystem permissions)
- Bidirectional (both components can initiate)
- Low latency (<1ms)

### Alternative: Shared SQLite Database

For read-only operations, Python can directly read from C++'s SQLite:

```python
# Python direct read (no IPC needed)
db = sqlite3.connect("/Users/pasha/Library/Application Support/Telegram Desktop/tdata/user.db")
messages = db.execute("SELECT * FROM messages WHERE chat_id = ?", (chat_id,))
```

**Benefits**:
- Zero IPC overhead
- Simpler architecture
- Scales to millions of messages

**Limitations**:
- Read-only (to avoid corruption)
- No access to active sessions (must use IPC)

## Directory Structure (Monorepo)

```
tlgrm/                              # Root (git repository)
│
├── README.md                       # Overview of entire system
├── ARCHITECTURE.md                 # This file
├── .gitignore                      # Unified ignore rules
├── .env.example                    # Shared environment template
├── docker-compose.yml              # Optional: containerized development
│
├── tdesktop/                       # C++ Telegram Desktop (Xcode)
│   ├── Telegram.xcodeproj          # Xcode project
│   ├── Telegram/
│   │   └── SourceFiles/
│   │       └── mcp/                # MCP server implementation
│   │           ├── mcp_server.cpp
│   │           ├── mcp_server.h
│   │           ├── bot_framework.cpp
│   │           └── json_rpc.cpp
│   ├── cmake/                      # CMake build files
│   └── README.md                   # C++ specific docs
│
├── python-bridge/                  # IPC bridge (fallback/development)
│   ├── requirements.txt            # Pyrogram, FastAPI
│   ├── mcp_server.py               # Basic MCP implementation
│   ├── tdesktop_bridge.py          # IPC client
│   └── README.md                   # Bridge documentation
│
├── python-ml/                      # NEW: AI/ML server (primary Python)
│   ├── requirements.txt            # Transformers, LangChain, ChromaDB
│   ├── pyproject.toml              # Poetry/modern Python config
│   ├── src/
│   │   ├── __init__.py
│   │   ├── mcp_server_aiml.py      # AI/ML MCP server
│   │   ├── semantic_search.py      # Vector search engine
│   │   ├── intent_classifier.py    # Intent classification
│   │   ├── topic_extractor.py      # Topic modeling
│   │   ├── embeddings.py           # Sentence embeddings
│   │   └── ipc_client.py           # Connect to C++ server
│   ├── models/                     # Downloaded AI models
│   ├── data/
│   │   └── chromadb/               # Vector database
│   ├── tests/
│   └── README.md                   # AI/ML documentation
│
├── docs/                           # Unified documentation
│   ├── architecture.md             # System design
│   ├── quickstart.md               # Getting started guide
│   ├── mcp-tools.md                # Complete MCP API reference
│   ├── ipc-protocol.md             # C++ ↔ Python communication
│   ├── deployment.md               # Production setup
│   └── development.md              # Contributing guide
│
├── scripts/                        # Build & deployment scripts
│   ├── build-all.sh                # Build both C++ and Python
│   ├── setup-dev.sh                # Development environment setup
│   ├── run-tests.sh                # Run all tests
│   └── deploy.sh                   # Production deployment
│
└── Makefile                        # Unified build commands
```

## Build & Development Workflow

### Unified Commands (via Makefile)

```bash
# One-time setup
make setup                  # Install all dependencies (C++ + Python)

# Development
make build-cpp              # Build C++ (Xcode/CMake)
make build-python           # Install Python dependencies
make build                  # Build everything

# Running
make run-cpp                # Launch Telegram Desktop with MCP
make run-python-ml          # Start Python AI/ML server
make run-all                # Start both (tmux/screen session)

# Testing
make test-cpp               # C++ unit tests
make test-python            # Python tests
make test                   # All tests

# Cleanup
make clean                  # Remove build artifacts
```

### IDE Workflows

**Xcode (C++ Development)**:
```bash
cd /Users/pasha/xCode/tlgrm/tdesktop
open Telegram.xcodeproj
# Develop in Xcode as usual
```

**PyCharm (Python Development)**:
```bash
cd /Users/pasha/xCode/tlgrm/python-ml
# Open in PyCharm
# PyCharm will detect pyproject.toml and setup venv automatically
```

**Both IDEs can be open simultaneously** - they work on different subdirectories.

## Configuration Management

### Shared `.env` (root level)
```bash
# Telegram credentials (shared)
TELEGRAM_API_ID=12345678
TELEGRAM_API_HASH=abcdef...
TELEGRAM_BOT_TOKEN=1234567890:ABC...

# IPC configuration (shared)
IPC_SOCKET_PATH=/tmp/telegram_mcp.sock
SQLITE_DB_PATH=/Users/pasha/Library/Application Support/Telegram Desktop/tdata/user.db

# C++ specific
MCP_CPP_ENABLED=true
MCP_CPP_PORT=3000

# Python AI/ML specific
EMBEDDING_MODEL=sentence-transformers/all-MiniLM-L6-v2
VECTOR_DB_PATH=./python-ml/data/chromadb
DEVICE=mps  # Apple Silicon GPU
```

### Language-Specific Configs
- `tdesktop/config.h` - C++ compile-time config
- `python-ml/config.toml` - Python runtime config

## Deployment Scenarios

### Development (Local)
```bash
# Terminal 1: C++ server
make run-cpp

# Terminal 2: Python AI/ML server
make run-python-ml

# Terminal 3: Test MCP client
mcp dev --server tcp://localhost:3000
```

### Production (Single Machine)
```bash
# systemd service for C++
systemctl start telegram-mcp-cpp

# systemd service for Python
systemctl start telegram-mcp-aiml

# Both communicate via Unix socket
```

### Production (Distributed)
```bash
# Machine 1: C++ server + database
# Exposes IPC over TCP (secured with mTLS)

# Machine 2: Python AI/ML cluster
# Connects to Machine 1 for data
# Handles all AI inference
```

## Performance Optimization

### C++ Optimizations
- Connection pooling for SQLite
- Memory-mapped database files
- Zero-copy message passing
- Batch queries for bulk operations

### Python Optimizations
- Apple Silicon MPS acceleration
- Model quantization (int8/fp16)
- Batch embedding generation
- Redis caching for hot queries

### IPC Optimizations
- Message batching (reduce syscalls)
- Protobuf/MessagePack (smaller payloads)
- Connection pooling
- Async I/O (both sides)

## Versioning & Compatibility

### API Versioning
```json
{
  "mcp_version": "1.0",
  "cpp_component_version": "6.3.0",
  "python_component_version": "2.0.0"
}
```

### Compatibility Matrix
| C++ Version | Python ML Version | MCP Version |
|-------------|-------------------|-------------|
| 6.3.x       | 2.0.x             | 1.0         |
| 6.4.x       | 2.1.x             | 1.0         |

## Security Considerations

### IPC Security
- Unix socket permissions: 0600 (owner only)
- Optional: mTLS for TCP mode
- No authentication needed (local-only)

### Data Privacy
- All processing local (no cloud)
- Encrypted database (Telegram's encryption)
- Vector DB stored encrypted at rest

## Future Enhancements

1. **Rust Component** - For low-latency streaming
2. **Web Dashboard** - FastAPI + React for monitoring
3. **Voice Processing** - Whisper integration in Python
4. **Multi-Modal** - Image understanding (CLIP, BLIP)
5. **Agent Framework** - LangChain agents for automation

---

**Key Principle**: Each language does what it's best at.
- C++: Speed, native access, real-time operations
- Python: AI/ML, rapid prototyping, rich ecosystem

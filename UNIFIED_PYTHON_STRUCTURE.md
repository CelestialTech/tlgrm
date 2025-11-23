# Unified Python Structure: Single `pythonMCP` Directory

## Rationale

Instead of maintaining two separate Python codebases (`python-bridge/` and `pythonMCP/`), we merge them into **one unified Python component** with optional AI/ML features.

## Proposed Structure

```
pythonMCP/
├── README.md
├── pyproject.toml              # Modern Python packaging
├── requirements.txt            # Full install (with AI/ML)
├── requirements-minimal.txt    # Minimal install (no AI/ML)
├── config.toml                 # Configuration
├── .env.example
│
├── src/
│   ├── __init__.py
│   │
│   ├── mcp_server.py           # Main MCP server (unified)
│   ├── telegram_client.py      # Telegram API client (Pyrogram)
│   ├── ipc_bridge.py           # IPC to C++ server (Unix socket)
│   ├── config.py               # Config loader
│   ├── cache.py                # Message caching
│   │
│   ├── core/                   # Core MCP tools (no AI deps)
│   │   ├── __init__.py
│   │   ├── messages.py         # get_messages, search_messages
│   │   ├── chats.py            # list_chats, get_chat_info
│   │   ├── export.py           # export_chat
│   │   └── stats.py            # get_chat_stats, user_activity
│   │
│   └── aiml/                   # AI/ML features (optional)
│       ├── __init__.py
│       ├── embeddings.py       # Sentence embeddings
│       ├── semantic_search.py  # Vector search
│       ├── intent.py           # Intent classification
│       ├── topics.py           # Topic extraction
│       └── summarization.py    # Conversation summaries
│
├── models/                     # Downloaded AI models (git-ignored)
│   └── .gitkeep
│
├── data/
│   ├── chromadb/               # Vector database (git-ignored)
│   └── .gitkeep
│
└── tests/
    ├── test_core.py
    └── test_aiml.py
```

## Key Design Decisions

### 1. **Optional AI/ML Dependencies**

**Minimal Install** (Fast, lightweight):
```bash
pip install -r requirements-minimal.txt
# Installs: mcp, pyrogram, structlog, fastapi
# Use case: Bridge to C++, basic MCP tools
```

**Full Install** (AI/ML enabled):
```bash
pip install -r requirements.txt
# Installs: Above + transformers, torch, chromadb, langchain
# Use case: Semantic search, intent classification, etc.
```

**Implementation**:
```python
# src/mcp_server.py
try:
    from .aiml import semantic_search, classify_intent
    AIML_ENABLED = True
except ImportError:
    AIML_ENABLED = False
    logger.info("AI/ML features disabled (dependencies not installed)")

@mcp.tool()
async def semantic_search_messages(query: str):
    if not AIML_ENABLED:
        return {"error": "AI/ML features not installed. Run: pip install -r requirements.txt"}
    return await semantic_search.search(query)
```

### 2. **Unified MCP Server**

One server, multiple modes:

**Mode 1: Standalone (Bot API)**
```bash
python src/mcp_server.py --mode standalone
# Uses Telegram Bot API
# No C++ dependency
# Rate limited (30 msg/sec)
```

**Mode 2: Bridge (IPC to C++)**
```bash
python src/mcp_server.py --mode bridge
# Connects to C++ server via /tmp/telegram_mcp.sock
# Fast database access via IPC
# Unlimited queries
```

**Mode 3: Hybrid (Best of both)**
```bash
python src/mcp_server.py --mode hybrid
# Reads from C++ for speed
# Adds AI/ML enhancement
# Default mode
```

### 3. **Unified Configuration**

**`config.toml`**:
```toml
[telegram]
api_id_env = "TELEGRAM_API_ID"
api_hash_env = "TELEGRAM_API_HASH"
bot_token_env = "TELEGRAM_BOT_TOKEN"

[mcp]
server_name = "Telegram MCP"
mode = "hybrid"  # standalone | bridge | hybrid
transport = "stdio"  # stdio | http

[ipc]
cpp_socket_path = "/tmp/telegram_mcp.sock"
enabled = true
timeout_ms = 5000

[aiml]
enabled = true  # Auto-detected based on dependencies
embedding_model = "sentence-transformers/all-MiniLM-L6-v2"
device = "mps"  # mps | cuda | cpu
vector_db_path = "./data/chromadb"
model_cache_dir = "./models"

[cache]
enabled = true
max_messages = 10000
ttl_seconds = 3600
```

## Migration from Two Directories

### What to Keep from Each

**From `python-bridge/`** (existing):
- ✅ `telegram_client.py` (Pyrogram - better than python-telegram-bot)
- ✅ `config.py` (TOML config loader)
- ✅ `cache.py` (Message caching)
- ✅ `tdesktop_bridge.py` → Rename to `ipc_bridge.py`
- ✅ `mcp_server.py` → Base structure

**From `telegramMCP/`** (PyCharm project):
- ✅ `mcp_server_enhanced.py` → Extract AI/ML tools
- ✅ AI/ML service layer → `aiml/` module
- ✅ Requirements (transformers, chromadb, etc.)
- ✅ README documentation about AI features

**Delete/Archive**:
- ❌ Duplicate implementations
- ❌ `python-telegram-bot` (keep Pyrogram)
- ❌ Two separate MCP servers

## Dependencies Management

**`requirements-minimal.txt`**:
```txt
# Core MCP
mcp>=1.0.0

# Telegram
pyrogram>=2.0.0
tgcrypto>=1.2.5

# Configuration
python-dotenv>=1.0.0
toml>=0.10.2

# Logging
structlog>=23.0.0
rich>=13.0.0

# Async
aiofiles>=23.0.0

# Optional: Debug dashboard
fastapi>=0.104.0
uvicorn[standard]>=0.24.0
```

**`requirements.txt`**:
```txt
# Include minimal
-r requirements-minimal.txt

# AI/ML
transformers>=4.35.0
sentence-transformers>=2.2.2
torch>=2.1.0
langchain>=0.1.0
chromadb>=0.4.18

# NLP
spacy>=3.7.0
nltk>=3.8.1

# Data processing
numpy>=1.24.0
pandas>=2.1.0
scikit-learn>=1.3.0
```

**`pyproject.toml`** (for Poetry users):
```toml
[tool.poetry]
name = "telegram-mcp"
version = "2.0.0"
description = "Unified Python MCP server for Telegram with optional AI/ML"

[tool.poetry.dependencies]
python = "^3.12"
mcp = "^1.0.0"
pyrogram = "^2.0.0"
# ... (minimal deps)

[tool.poetry.extras]
aiml = [
    "transformers",
    "sentence-transformers",
    "torch",
    "langchain",
    "chromadb"
]

# Install with: poetry install -E aiml
```

## Unified MCP Tools API

### Core Tools (Always Available)

```python
# No AI dependencies
@mcp.tool()
async def get_messages(chat_id: int, limit: int = 50)

@mcp.tool()
async def search_messages(chat_id: int, query: str)

@mcp.tool()
async def list_chats()

@mcp.tool()
async def get_chat_stats(chat_id: int)

@mcp.tool()
async def export_chat(chat_id: int, format: str = "json")
```

### AI/ML Tools (Require Full Install)

```python
# Require AI/ML dependencies
@mcp.tool()
async def semantic_search(query: str, chat_id: int = None)

@mcp.tool()
async def classify_intent(text: str)

@mcp.tool()
async def extract_topics(chat_id: int)

@mcp.tool()
async def summarize_conversation(chat_id: int, limit: int = 50)
```

## Development Workflow

### PyCharm Setup

```bash
# Open unified Python directory
# File → Open → /Users/pasha/xCode/tlgrm/pythonMCP

# Create virtual environment
cd pythonMCP
python -m venv .venv
source .venv/bin/activate

# Choose install level
pip install -r requirements.txt          # Full (AI/ML)
# OR
pip install -r requirements-minimal.txt  # Minimal
```

### Run Configurations

**PyCharm Run Config 1: Minimal Mode**
- Name: "MCP Server (Minimal)"
- Script: `src/mcp_server.py`
- Args: `--mode standalone --no-aiml`
- Env: Load from `.env`

**PyCharm Run Config 2: Full AI/ML**
- Name: "MCP Server (AI/ML)"
- Script: `src/mcp_server.py`
- Args: `--mode hybrid`
- Env: Load from `.env`

**PyCharm Run Config 3: Debug Dashboard**
- Name: "Debug Dashboard"
- Module: `uvicorn`
- Args: `src.mcp_server:app --reload --port 8000`

## Testing Strategy

```bash
# Test minimal install
pip install -r requirements-minimal.txt
pytest tests/test_core.py

# Test full install
pip install -r requirements.txt
pytest tests/test_aiml.py

# Test IPC bridge
pytest tests/test_ipc.py
```

## Updated Root Structure

```
tlgrm/                          # Git root
├── README.md
├── ARCHITECTURE.md
├── .gitignore
├── .env.example
├── Makefile
│
├── tdesktop/                   # C++ component
│   └── ...
│
├── pythonMCP/                  # Unified Python (replaces both)
│   ├── src/
│   │   ├── core/              # Basic features
│   │   └── aiml/              # AI/ML features (optional)
│   ├── requirements.txt
│   └── requirements-minimal.txt
│
├── docs/
│   ├── quickstart.md
│   ├── python-setup.md
│   └── aiml-features.md
│
└── scripts/
    ├── setup-python-minimal.sh
    └── setup-python-full.sh
```

## Migration Steps

1. **Backup existing**:
   ```bash
   cp -r python-bridge python-bridge.backup
   ```

2. **Create unified structure**:
   ```bash
   mkdir -p pythonMCP/{src/{core,aiml},models,data,tests}
   ```

3. **Merge code**:
   - Copy `python-bridge/` core → `pythonMCP/src/core/`
   - Copy `telegramMCP/src/` AI → `pythonMCP/src/aiml/`
   - Unify `mcp_server.py`

4. **Update dependencies**:
   - Create `requirements-minimal.txt` from `python-bridge/`
   - Create `requirements.txt` from `telegramMCP/`

5. **Test**:
   ```bash
   # Minimal
   pip install -r requirements-minimal.txt
   python src/mcp_server.py --no-aiml

   # Full
   pip install -r requirements.txt
   python src/mcp_server.py
   ```

6. **Delete old directories**:
   ```bash
   rm -rf python-bridge
   rm -rf /Users/pasha/PycharmProjects/telegramMCP
   ```

## Benefits

✅ **Single codebase** - One place for all Python MCP code
✅ **Optional AI/ML** - Install only what you need
✅ **Flexible deployment** - Minimal, full, or hybrid mode
✅ **Easier maintenance** - No duplicate code
✅ **Clear organization** - Core vs AI/ML separation
✅ **Better testing** - Test with/without AI dependencies
✅ **Simpler docs** - One README, one setup guide

---

**This is the right approach!** One unified Python component with optional features.

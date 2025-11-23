# Migration Plan: Integrate Python AI/ML into Monorepo

## Goal
Move `/Users/pasha/PycharmProjects/telegramMCP` into `/Users/pasha/xCode/tlgrm/python-ml/` as a **complementary component** (not fallback).

## Current State

### Source Location
```
/Users/pasha/PycharmProjects/telegramMCP/
├── .claude/
│   └── settings.local.json
├── src/
│   ├── __init__.py
│   ├── mcp_server_enhanced.py     # 678 lines - AI/ML MCP server
│   └── tdesktop_bridge.py         # 394 lines - Subprocess IPC bridge
├── requirements.txt                # AI/ML dependencies
├── .env.example
└── README.md                       # AI/ML focused documentation
```

### Target Location
```
/Users/pasha/xCode/tlgrm/
├── tdesktop/                       # C++ component (existing)
├── python-bridge/                  # IPC bridge (existing, keep as fallback)
└── python-ml/                      # NEW: AI/ML component (to create)
    ├── src/
    │   ├── __init__.py
    │   ├── mcp_server_aiml.py      # Renamed from mcp_server_enhanced.py
    │   ├── semantic_search.py      # Extracted from enhanced
    │   ├── intent_classifier.py    # Extracted from enhanced
    │   ├── embeddings.py           # Extracted from enhanced
    │   └── ipc_client.py           # Refactored tdesktop_bridge.py
    ├── requirements.txt            # AI/ML specific deps
    ├── pyproject.toml              # Modern Python config
    ├── .env.example
    ├── models/                     # AI model cache
    ├── data/
    │   └── chromadb/               # Vector database
    └── README.md
```

## Migration Steps

### Phase 1: Prepare Target Directory (5 min)

```bash
cd /Users/pasha/xCode/tlgrm

# Create new python-ml directory
mkdir -p python-ml/{src,tests,data,models,docs}
cd python-ml

# Initialize Python project
touch src/__init__.py
touch README.md
```

### Phase 2: Copy Core Files (2 min)

```bash
# Copy Python source files
cp /Users/pasha/PycharmProjects/telegramMCP/src/*.py src/

# Copy configuration
cp /Users/pasha/PycharmProjects/telegramMCP/requirements.txt .
cp /Users/pasha/PycharmProjects/telegramMCP/.env.example .
```

### Phase 3: Refactor Code Structure (15 min)

**Goal**: Split monolithic `mcp_server_enhanced.py` into focused modules.

#### 3.1: Extract AI/ML Service Layer
```bash
# Create new module files
touch src/semantic_search.py
touch src/intent_classifier.py
touch src/embeddings.py
touch src/topic_extractor.py
touch src/conversation_summarizer.py
```

**Move code**:
- `AIMLService` class → Split into multiple modules
- Embedding logic → `src/embeddings.py`
- Vector search → `src/semantic_search.py`
- Intent classification → `src/intent_classifier.py`

#### 3.2: Refactor IPC Bridge
```bash
# Rename and refactor bridge
cp src/tdesktop_bridge.py src/ipc_client.py
```

**Changes**:
- Remove subprocess approach (keep Unix socket only)
- Align with `/tmp/telegram_mcp.sock` path from C++ server
- Add async/await throughout

#### 3.3: Create Main MCP Server
```bash
mv src/mcp_server_enhanced.py src/mcp_server_aiml.py
```

**Simplify**:
- Import from new modules (semantic_search, embeddings, etc.)
- Remove embedded AIMLService (now imported)
- Focus on MCP tool definitions only

### Phase 4: Update Dependencies (5 min)

**Create `pyproject.toml`** (modern Python standard):
```toml
[tool.poetry]
name = "telegram-mcp-aiml"
version = "2.0.0"
description = "AI/ML enhanced MCP server for Telegram"
authors = ["Your Name <you@example.com>"]

[tool.poetry.dependencies]
python = "^3.12"
mcp = "^1.0.0"
sentence-transformers = "^2.2.2"
transformers = "^4.35.0"
torch = "^2.1.0"
langchain = "^0.1.0"
chromadb = "^0.4.18"
# ... (full list from requirements.txt)

[tool.poetry.dev-dependencies]
pytest = "^7.4.0"
pytest-asyncio = "^0.21.0"
ruff = "^0.1.0"

[build-system]
requires = ["poetry-core"]
build-backend = "poetry.core.masonry.api"
```

**Update requirements.txt**:
- Keep AI/ML specific deps only
- Remove general utils (move to shared)

### Phase 5: Update Configuration (5 min)

**Root-level `.env` (shared)**:
```bash
cd /Users/pasha/xCode/tlgrm

# Merge .env files
cat python-ml/.env.example >> .env.example
```

**Python-specific config** (`python-ml/config.toml`):
```toml
[ai_ml]
embedding_model = "sentence-transformers/all-MiniLM-L6-v2"
device = "mps"  # Apple Silicon
vector_db_path = "./data/chromadb"
model_cache_dir = "./models"

[mcp]
server_name = "Telegram AI/ML"
transport = "stdio"

[ipc]
cpp_socket_path = "/tmp/telegram_mcp.sock"
timeout_ms = 5000
```

### Phase 6: Update Documentation (10 min)

**Create `python-ml/README.md`**:
```markdown
# Telegram MCP: AI/ML Component

Python-based AI/ML server providing semantic understanding for Telegram messages.

## Features
- Semantic search using sentence embeddings
- Intent classification (BART)
- Topic extraction (LDA)
- Conversation summarization
- Apple Silicon GPU acceleration

## Quick Start
\`\`\`bash
cd python-ml
pip install -r requirements.txt
python src/mcp_server_aiml.py
\`\`\`

## Integration with C++
Communicates with C++ MCP server via Unix socket (`/tmp/telegram_mcp.sock`)
for fast message retrieval.

## MCP Tools
- `semantic_search(query, chat_id?, limit?)`
- `classify_intent(text)`
- `extract_topics(chat_id)`
- `summarize_conversation(chat_id, limit?)`
- `find_similar_chats(query, threshold?)`

See [../docs/mcp-tools.md](../docs/mcp-tools.md) for full API reference.
```

**Update root README.md**:
Add section explaining the three components:
1. C++ MCP server (tdesktop)
2. Python IPC bridge (python-bridge) - development fallback
3. Python AI/ML (python-ml) - semantic features

### Phase 7: Git Integration (3 min)

```bash
cd /Users/pasha/xCode/tlgrm

# Check what's new
git status

# Add python-ml to git
git add python-ml/

# Update .gitignore
echo "
# Python AI/ML
python-ml/data/chromadb/
python-ml/models/*.bin
python-ml/models/*.pt
python-ml/.venv/
python-ml/__pycache__/
" >> .gitignore

# Commit
git add .gitignore
git commit -m "Add Python AI/ML component as complementary service

- Moved from separate PyCharm project
- Split monolithic code into focused modules
- Updated documentation to reflect complementary architecture
- Added pyproject.toml for modern Python packaging

This component provides:
- Semantic search (vector embeddings)
- Intent classification
- Topic extraction
- Conversation summarization

Complements C++ server's fast database access with AI understanding.
"
```

### Phase 8: PyCharm Configuration (2 min)

**Open in PyCharm**:
```bash
# Option 1: Open subdirectory in PyCharm
# File → Open → /Users/pasha/xCode/tlgrm/python-ml

# Option 2: Open entire monorepo with multiple modules
# File → Open → /Users/pasha/xCode/tlgrm
# PyCharm will detect both python-bridge and python-ml
```

**PyCharm Setup**:
1. Mark `python-ml/src` as "Sources Root"
2. Create virtual environment: `python-ml/.venv`
3. Install dependencies: `pip install -r requirements.txt`
4. Configure interpreter to use `.venv`

**Run Configuration**:
- Name: "MCP AI/ML Server"
- Script: `src/mcp_server_aiml.py`
- Working directory: `python-ml/`
- Environment: Load from `.env`

### Phase 9: Create Unified Build Scripts (10 min)

**Create `Makefile`**:
```makefile
.PHONY: help setup build run test clean

help:
	@echo "Telegram MCP Monorepo"
	@echo "  setup        - Install all dependencies"
	@echo "  build        - Build C++ and Python components"
	@echo "  run-cpp      - Run C++ MCP server (Telegram Desktop)"
	@echo "  run-aiml     - Run Python AI/ML server"
	@echo "  test         - Run all tests"
	@echo "  clean        - Remove build artifacts"

setup:
	@echo "Setting up C++ dependencies..."
	cd tdesktop && ./configure.sh
	@echo "Setting up Python AI/ML..."
	cd python-ml && pip install -r requirements.txt
	@echo "Setting up Python Bridge..."
	cd python-bridge && pip install -r requirements.txt

build:
	@echo "Building C++ component..."
	cd tdesktop && xcodebuild -project Telegram.xcodeproj -scheme Telegram -configuration Release
	@echo "Python components ready (no build needed)"

run-cpp:
	./out/Release/Telegram.app/Contents/MacOS/Telegram

run-aiml:
	cd python-ml && python src/mcp_server_aiml.py

run-bridge:
	cd python-bridge && python main.py

test:
	@echo "Running C++ tests..."
	cd tdesktop && xcodebuild test
	@echo "Running Python tests..."
	cd python-ml && pytest
	cd python-bridge && pytest

clean:
	@echo "Cleaning C++ build..."
	cd tdesktop && xcodebuild clean
	@echo "Cleaning Python cache..."
	find . -type d -name "__pycache__" -exec rm -rf {} +
	find . -type f -name "*.pyc" -delete
```

### Phase 10: Validation (5 min)

**Checklist**:
- [ ] C++ server builds successfully
- [ ] Python AI/ML imports work (`python -c "from src.mcp_server_aiml import *"`)
- [ ] IPC connection works (test Unix socket)
- [ ] PyCharm recognizes project structure
- [ ] Xcode project still opens correctly
- [ ] Git status clean (all files committed or ignored)
- [ ] Documentation updated

**Test Integration**:
```bash
# Terminal 1: Start C++ server
make run-cpp

# Terminal 2: Start Python AI/ML server
make run-aiml

# Terminal 3: Test IPC
cd python-ml
python -c "
from src.ipc_client import get_bridge
import asyncio

async def test():
    bridge = await get_bridge()
    health = await bridge.health_check()
    print(f'IPC Health: {health}')

asyncio.run(test())
"
```

## Post-Migration

### Cleanup Original Directory
```bash
# Only after validating migration worked!
# Optionally keep a backup
mv /Users/pasha/PycharmProjects/telegramMCP /Users/pasha/PycharmProjects/telegramMCP.backup

# Or delete if confident
# rm -rf /Users/pasha/PycharmProjects/telegramMCP
```

### Update .claude Settings
The `.claude/settings.local.json` permissions are already configured for `/Users/pasha/xCode/tlgrm`, so no changes needed.

## Timeline

| Phase | Time | Critical? |
|-------|------|-----------|
| 1. Prepare directories | 5 min | ✅ Yes |
| 2. Copy files | 2 min | ✅ Yes |
| 3. Refactor code | 15 min | ⚠️ Important |
| 4. Dependencies | 5 min | ✅ Yes |
| 5. Configuration | 5 min | ✅ Yes |
| 6. Documentation | 10 min | ⚠️ Important |
| 7. Git integration | 3 min | ✅ Yes |
| 8. PyCharm setup | 2 min | Optional |
| 9. Build scripts | 10 min | ⚠️ Important |
| 10. Validation | 5 min | ✅ Yes |
| **Total** | **~60 min** | |

## Risk Mitigation

**Backup Strategy**:
```bash
# Before starting
tar -czf ~/telegramMCP-backup-$(date +%Y%m%d).tar.gz /Users/pasha/PycharmProjects/telegramMCP
tar -czf ~/tlgrm-backup-$(date +%Y%m%d).tar.gz /Users/pasha/xCode/tlgrm
```

**Rollback Plan**:
```bash
# If migration fails
rm -rf /Users/pasha/xCode/tlgrm/python-ml
git checkout .  # Revert changes
# Restore from backup if needed
```

## Success Criteria

✅ **Structure**:
- [ ] `python-ml/` directory exists in monorepo
- [ ] All Python files migrated
- [ ] Git tracks new files

✅ **Functionality**:
- [ ] C++ server runs without issues
- [ ] Python AI/ML server starts successfully
- [ ] IPC communication works between components
- [ ] MCP tools accessible from both servers

✅ **Developer Experience**:
- [ ] PyCharm can open and run Python code
- [ ] Xcode can build C++ project
- [ ] Unified `make` commands work
- [ ] Documentation is clear and complete

✅ **Production Ready**:
- [ ] All tests pass
- [ ] No hardcoded paths
- [ ] Configuration via `.env`
- [ ] Proper error handling

---

**Ready to proceed?** Let me know if you'd like me to execute this migration plan!

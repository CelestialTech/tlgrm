# pythonMCP - Production-Ready Telegram MCP Server

Complementary Python MCP server for Telegram with optional AI/ML capabilities and production monitoring.

## Overview

This is the **Python component** of the Telegram MCP system, working alongside the C++ server to provide:

**Core Features** (always available):
- Message retrieval and search
- Chat listing and statistics
- IPC bridge to C++ server for fast local database access
- Configuration validation with fail-fast startup
- Production-ready monitoring and health checks

**Bridge Features** (via C++ server):
- Batch operations (mark_read, archive, mute, delete)
- Message scheduler (schedule, list, cancel)
- Message tags (tag, list, search)
- Translation (translate, get languages)
- Voice transcription (transcribe, history)

**AI/ML Features** (optional):
- 🔍 Semantic search using embeddings
- 🧠 Intent classification
- 📊 Topic extraction
- 💬 Conversation summarization
- 🍎 Apple Silicon GPU acceleration (MPS)

**Production Features** (Phase 2 & 3 - Complete):
- ✅ Prometheus metrics (19 metrics tracking performance, errors, usage)
- ✅ Health check endpoints (liveness, readiness, component status)
- ✅ Configuration validation (environment variables, paths, ports)
- ✅ Excellent test coverage (195 tests, 80%+ coverage)
- ✅ CI/CD pipeline (GitHub Actions, macOS, multi-Python)

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
│ Prometheus    │                      │  No Limits   │
└───────────────┘                      └──────────────┘
     │
     │ HTTP :9090 (metrics)
     │ HTTP :8080 (health)
     ▼
┌───────────────┐
│  Monitoring   │
│  (Grafana,    │
│   Prometheus) │
└───────────────┘
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
IPC_SOCKET_PATH=/tmp/tlgrm_mcp.sock

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
cpp_socket_path = "/tmp/tlgrm_mcp.sock"

[aiml]
enabled = true
embedding_model = "sentence-transformers/all-MiniLM-L6-v2"
device = "mps"

[monitoring]
# Prometheus metrics
enable_prometheus = true
metrics_port = 9090

# Health checks
enable_health_check = true
health_check_port = 8080
```

**Important**: The server validates configuration on startup and fails fast if there are issues. See [VALIDATION_USAGE.md](VALIDATION_USAGE.md) for details.

## Production Monitoring

pythonMCP includes comprehensive monitoring for production deployments:

### Health Check Endpoints

```bash
# Liveness probe (is the server running?)
curl http://localhost:8080/health/live

# Readiness probe (can it serve traffic?)
curl http://localhost:8080/health/ready

# Detailed component status
curl http://localhost:8080/health/components
```

### Prometheus Metrics

```bash
# View all metrics
curl http://localhost:9090/metrics

# Example metrics available:
# - pythonmcp_tool_calls_total
# - pythonmcp_tool_call_duration_seconds
# - pythonmcp_messages_retrieved_total
# - pythonmcp_bridge_requests_total
# - pythonmcp_aiml_request_duration_seconds
# ... and 14 more metrics
```

**See [docs/monitoring.md](docs/monitoring.md) for complete monitoring guide including:**
- All 19 available metrics
- Prometheus integration
- Grafana dashboard examples
- Kubernetes deployment with probes

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

### Bridge Tools (Require C++ Server Connection)

#### Batch Operations

##### `batch_operation(operation, targets, options=None)`
Execute batch operations on multiple chats (mark_read, archive, mute, delete).

```json
{
  "tool": "batch_operation",
  "arguments": {
    "operation": "mark_read",
    "targets": [123456789, 987654321]
  }
}
```

##### `batch_forward_messages(source_chat_id, message_ids, target_chat_ids)`
Forward multiple messages to multiple chats.

##### `batch_delete_messages(chat_id, message_ids, revoke=True)`
Delete multiple messages at once.

#### Message Scheduler

##### `schedule_message(chat_id, text, send_at)`
Schedule a message for later delivery.

```json
{
  "tool": "schedule_message",
  "arguments": {
    "chat_id": -1001234567890,
    "text": "Hello from the future!",
    "send_at": 1735689600
  }
}
```

##### `list_scheduled_messages(chat_id)`
List all scheduled messages for a chat.

##### `cancel_scheduled_message(chat_id, message_id)`
Cancel a scheduled message.

#### Message Tags

##### `tag_message(chat_id, message_id, tag)`
Add a tag to a message.

```json
{
  "tool": "tag_message",
  "arguments": {
    "chat_id": -1001234567890,
    "message_id": 12345,
    "tag": "important"
  }
}
```

##### `get_message_tags(chat_id, message_id)`
Get all tags for a message.

##### `list_tags()`
List all available tags.

##### `get_tagged_messages(tag, limit=50)`
Get all messages with a specific tag.

##### `remove_message_tag(chat_id, message_id, tag)`
Remove a tag from a message.

```json
{
  "tool": "remove_message_tag",
  "arguments": {
    "chat_id": -1001234567890,
    "message_id": 12345,
    "tag": "important"
  }
}
```

#### Translation

##### `translate_message(chat_id, message_id, target_language)`
Translate a message to another language.

```json
{
  "tool": "translate_message",
  "arguments": {
    "chat_id": -1001234567890,
    "message_id": 12345,
    "target_language": "es"
  }
}
```

##### `get_translation_languages()`
Get list of supported translation languages.

##### `configure_auto_translate(chat_id, enabled, target_language=None)`
Configure automatic translation for a chat.

```json
{
  "tool": "configure_auto_translate",
  "arguments": {
    "chat_id": -1001234567890,
    "enabled": true,
    "target_language": "en"
  }
}
```

#### Voice Transcription

##### `transcribe_voice_message(chat_id, message_id)`
Transcribe a voice message using Whisper.

```json
{
  "tool": "transcribe_voice_message",
  "arguments": {
    "chat_id": -1001234567890,
    "message_id": 12345
  }
}
```

##### `get_transcription_history(chat_id=None, limit=50)`
Get history of transcribed messages.

##### `get_transcription_status(task_id)`
Check the status of an ongoing transcription task.

```json
{
  "tool": "get_transcription_status",
  "arguments": {
    "task_id": "task_12345"
  }
}
```

Returns status: `pending`, `completed`, or `failed`, along with progress or transcribed text.

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
│   │   ├── config.py          # Configuration
│   │   └── validation.py      # Config validation ✨ NEW
│   ├── monitoring/            # Production monitoring ✨ NEW
│   │   ├── health.py          # Health check server
│   │   └── metrics.py         # Prometheus metrics
│   └── aiml/                  # AI/ML features (optional)
│       ├── service.py         # AI/ML service layer
│       └── ...
├── scripts/                   # Development scripts ✨ NEW
│   └── benchmark.py          # Performance benchmarking suite
├── tests/                       # Unit tests (195 tests, 80%+ coverage)
│   ├── test_mcp_tools.py       # Core tool tests (18 tests)
│   ├── test_health_check.py    # Health check tests (25 tests)
│   ├── test_cache.py           # Cache module tests (20 tests)
│   ├── test_config_validation.py # Config/validation tests (37 tests)
│   ├── test_ipc_bridge.py      # IPC bridge tests (39 tests)
│   ├── test_bridge_tools.py    # Bridge tools tests (17 tests) ✨ NEW
│   ├── conftest.py             # Test fixtures
│   └── ...
├── .github/workflows/         # CI/CD configuration ✨ Phase 3
│   └── tests.yml             # GitHub Actions workflow
├── docs/                      # Documentation
│   └── monitoring.md         # Monitoring guide
├── requirements.txt           # Full dependencies
├── requirements-minimal.txt   # Core dependencies
├── pyproject.toml            # Poetry config
├── config.toml.example       # Example configuration
├── .coveragerc               # Coverage configuration ✨ Phase 3
├── GAP_ANALYSIS.md           # Development roadmap
└── README.md                 # This file
```

### Running Tests

```bash
# Install test dependencies
pip install -r requirements.txt
pip install pytest pytest-cov pytest-asyncio

# Run all tests (excluding AI/ML which needs full deps)
pytest tests/ -v -k "not aiml"

# Run all tests including AI/ML
pytest tests/

# Run with coverage
pytest tests/ --cov=src --cov-report=html --cov-report=term -v -k "not aiml"

# Run specific test files
pytest tests/test_health_check.py -v      # Health check tests (25)
pytest tests/test_mcp_tools.py -v         # MCP tools tests (18)
pytest tests/test_cache.py -v             # Cache tests (20)
pytest tests/test_config_validation.py -v # Config tests (37)
pytest tests/test_ipc_bridge.py -v        # IPC bridge tests (39)
pytest tests/test_bridge_tools.py -v      # Bridge tools tests (17)

# Run specific test categories
pytest tests/ -m unit          # Unit tests only
pytest tests/ -m integration   # Integration tests
pytest tests/ -m aiml          # AI/ML tests (requires full install)
```

**CI/CD**: Tests automatically run on GitHub Actions for every push/PR across:
- Operating System: macOS only
- Python Versions: 3.10, 3.11, 3.12, 3.13
- Test Suites: Minimal (no AI/ML) and Full (with AI/ML)

**Current Test Coverage**: 80%+ overall (195 tests), ~95% for core modules, health checks, monitoring, config, IPC bridge, and bridge tools.

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

**Core Operations** (via C++):
| Operation | Time | Notes |
|-----------|------|-------|
| Get 50 messages (via C++) | 5ms | Direct SQLite |
| Keyword search (1K msgs) | 15ms | SQLite FTS |
| Generate embedding | 20ms | MPS accelerated |
| Semantic search (10K msgs) | 30ms | ChromaDB query |
| Intent classification | 100ms | BART model |

**Monitoring Infrastructure** (100 iterations, actual results):
| Operation | Median | P95 | P99 | Notes |
|-----------|--------|-----|-----|-------|
| Health liveness check | 0.39ms | 0.60ms | 9.27ms | HTTP /health/live |
| Health readiness check | 0.33ms | 0.36ms | 0.51ms | HTTP /health/ready |
| Health components check | 0.31ms | 0.34ms | 0.55ms | HTTP /health/components |
| Prometheus metrics scrape | 0.38ms | 0.43ms | 0.56ms | HTTP /metrics |
| Component health check | 0.001ms | 0.001ms | 0.006ms | Internal function call |
| Metrics recording | 0.014ms | 0.018ms | 0.063ms | Record to Prometheus |

Run benchmarks yourself:
```bash
python scripts/benchmark.py --iterations 100
python scripts/benchmark.py --iterations 1000 --output results.json
```

### Memory Usage

- **Minimal**: ~100 MB (no AI/ML, no monitoring)
- **With Monitoring**: ~150 MB (Prometheus + health checks)
- **Full AI/ML**: ~2 GB (models loaded)

### Production Overhead

- **Metrics collection**: <1% CPU, <10MB memory
- **Health checks**: <0.1% CPU (background thread)
- **Config validation**: One-time startup cost (<100ms)
- **Monitoring infrastructure**: Sub-millisecond latency, minimal overhead

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
2. Check socket path: `ls -la /tmp/tlgrm_mcp.sock`
3. Try standalone mode: `--mode standalone`

### "Configuration validation failed"

**Problem**: Server fails to start with validation errors.

**Solution**:
1. Check environment variables are set correctly
2. Review error messages for specific issues
3. See [VALIDATION_USAGE.md](VALIDATION_USAGE.md) for configuration guide
4. Verify paths exist and ports are available

### "Metrics not available"

**Problem**: Prometheus metrics endpoint not responding.

**Solution**:
1. Check `enable_prometheus = true` in config.toml
2. Verify port 9090 is not in use: `lsof -i :9090`
3. Check logs for metrics initialization errors

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

## Documentation

- [Cache Guide](docs/cache.md) - Message caching with in-memory and SQLite backends
- [Monitoring Guide](docs/monitoring.md) - Comprehensive monitoring and observability guide
- [Configuration Validation](VALIDATION_USAGE.md) - Configuration validation reference
- [Metrics Reference](METRICS_REFERENCE.md) - Complete Prometheus metrics documentation
- [Gap Analysis](GAP_ANALYSIS.md) - Development roadmap and feature gaps
- [Implementation Summary](IMPLEMENTATION_SUMMARY.md) - Phase 2 implementation details

## License

GPL-3.0-or-later (matches tdesktop)

## See Also

- [Root README](../README.md) - Complete system overview
- [Architecture](../ARCHITECTURE.md) - How components work together
- [C++ Server](../tdesktop/) - Native Telegram Desktop integration
- [MCP Tools API](../docs/mcp-tools.md) - Complete API reference

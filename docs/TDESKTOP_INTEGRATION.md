# Telegram Desktop (tdesktop) Integration Plan

## Overview

This document outlines how to modify the Telegram Desktop source code at `~/xCode/tlgrm` to add advanced MCP features that complement the Python MCP server.

## Architecture

```
┌─────────────────────────────────────────────────────────┐
│                     Claude / AI Model                    │
└───────────────────────┬─────────────────────────────────┘
                        │ MCP Protocol (JSON-RPC)
┌───────────────────────▼─────────────────────────────────┐
│              Python MCP Server (FastMCP)                 │
│  - MCP protocol handling                                 │
│  - AI integration layer                                  │
│  - Simple API exposure                                   │
└───────────────────────┬─────────────────────────────────┘
                        │ IPC (Unix Domain Socket / Named Pipe)
┌───────────────────────▼─────────────────────────────────┐
│           Modified Telegram Desktop (C++/Qt)             │
│  ┌────────────────────────────────────────────────────┐ │
│  │ MCP Bridge Service                                 │ │
│  │  - IPC server (Unix socket)                        │ │
│  │  - Command dispatcher                              │ │
│  │  - Response serializer (JSON)                      │ │
│  └────────────────────────────────────────────────────┘ │
│  ┌────────────────────────────────────────────────────┐ │
│  │ Local Database Access                              │ │
│  │  - Direct SQLite access (no API calls)             │ │
│  │  - Message history                                 │ │
│  │  - Media metadata                                  │ │
│  │  - User/chat information                           │ │
│  └────────────────────────────────────────────────────┘ │
│  ┌────────────────────────────────────────────────────┐ │
│  │ Voice AI Module                                    │ │
│  │  - Whisper integration (OpenAI)                    │ │
│  │  - Real-time transcription                         │ │
│  │  - Transcription cache                             │ │
│  └────────────────────────────────────────────────────┘ │
│  ┌────────────────────────────────────────────────────┐ │
│  │ Semantic Search Engine                             │ │
│  │  - Sentence embeddings                             │ │
│  │  - FAISS vector index                              │ │
│  │  - Similarity search                               │ │
│  └────────────────────────────────────────────────────┘ │
│  ┌────────────────────────────────────────────────────┐ │
│  │ Media Processing Pipeline                          │ │
│  │  - OCR (Tesseract)                                 │ │
│  │  - Video frame analysis                            │ │
│  │  - Document text extraction                        │ │
│  └────────────────────────────────────────────────────┘ │
└─────────────────────────────────────────────────────────┘
```

## Features to Implement

### 1. MCP Bridge Service

**Location:** `~/xCode/tlgrm/tdesktop/Telegram/SourceFiles/mcp/`

**Files to create:**
- `mcp_bridge.h` - Bridge service header
- `mcp_bridge.cpp` - IPC server implementation
- `mcp_commands.h` - Command definitions
- `mcp_handlers.cpp` - Command handlers

**Functionality:**
- Unix domain socket server on `/tmp/tdesktop_mcp.sock`
- JSON-RPC style command/response protocol
- Commands:
  - `get_messages(chat_id, limit, offset)` - Direct DB access
  - `search_local(query, chat_id)` - Local search
  - `transcribe_voice(file_path)` - Voice transcription
  - `extract_media_text(file_path)` - OCR/document extraction
  - `semantic_search(query, limit)` - Vector similarity search

**Protocol Example:**
```json
Request:
{
  "id": 1,
  "method": "get_messages",
  "params": {
    "chat_id": -1001234567890,
    "limit": 50,
    "offset": 0
  }
}

Response:
{
  "id": 1,
  "result": {
    "messages": [...],
    "total": 1250
  }
}
```

### 2. Local Database Access Module

**Location:** `~/xCode/tlgrm/tdesktop/Telegram/SourceFiles/mcp/database_bridge.cpp`

**Integration points:**
- `~/xCode/tlgrm/tdesktop/Telegram/SourceFiles/storage/storage_account.h`
- `~/xCode/tlgrm/tdesktop/Telegram/SourceFiles/history/history.h`

**Capabilities:**
- Direct SQLite queries (no MTProto API calls)
- Instant message retrieval (offline)
- Full-text search on local cache
- Media file location mapping

**Benefits:**
- **Speed:** 1000x faster than API calls
- **No rate limits:** Read as much as needed
- **Offline:** Works without internet
- **Complete history:** Access to all synced messages

### 3. Voice AI Module

**Location:** `~/xCode/tlgrm/tdesktop/Telegram/SourceFiles/mcp/voice_ai.cpp`

**Dependencies to add:**
- OpenAI C++ SDK or direct HTTP calls to Whisper API
- Or local Whisper.cpp integration

**Integration:**
- Hook into voice message download pipeline
- Auto-transcribe on download
- Store transcriptions in local DB
- Expose via MCP bridge

**Implementation:**
```cpp
// voice_ai.h
class VoiceAI {
public:
    VoiceAI(const QString &apiKey);

    // Transcribe voice message
    TranscriptionResult transcribe(const QString &audioPath);

    // Get cached transcription
    std::optional<QString> getCachedTranscription(int64 messageId);

private:
    QString _apiKey;
    QSqlDatabase _transcriptionCache;
};
```

### 4. Semantic Search Engine

**Location:** `~/xCode/tlgrm/tdesktop/Telegram/SourceFiles/mcp/semantic_search.cpp`

**Dependencies:**
- FAISS (Facebook AI Similarity Search)
- SentenceTransformers model (via Python subprocess or ONNX Runtime)

**Functionality:**
- Index messages with vector embeddings
- Incremental indexing (new messages)
- Fast similarity search
- Cluster detection (conversation topics)

**Implementation approach:**
```cpp
// semantic_search.h
class SemanticSearchEngine {
public:
    SemanticSearchEngine(const QString &modelPath);

    // Index a message
    void indexMessage(int64 messageId, const QString &text);

    // Search for similar messages
    std::vector<SearchResult> search(const QString &query, int limit = 10);

private:
    std::unique_ptr<faiss::Index> _index;
    QProcess _embeddingService; // Python subprocess
};
```

### 5. Media Processing Pipeline

**Location:** `~/xCode/tlgrm/tdesktop/Telegram/SourceFiles/mcp/media_processor.cpp`

**Features:**
- **OCR:** Extract text from images (Tesseract)
- **Document parsing:** PDF, DOCX text extraction
- **Video analysis:** Extract frames, detect scenes
- **Image classification:** Detect objects, faces

**Integration:**
```cpp
// media_processor.h
class MediaProcessor {
public:
    // Extract text from image
    QString extractTextFromImage(const QString &imagePath);

    // Parse document
    DocumentContent parseDocument(const QString &docPath);

    // Analyze video
    VideoAnalysis analyzeVideo(const QString &videoPath);
};
```

## Implementation Steps

### Phase 1: IPC Bridge (Foundation)
1. Create `~/xCode/tlgrm/tdesktop/Telegram/SourceFiles/mcp/` directory
2. Implement `mcp_bridge.cpp` with Unix socket server
3. Add command dispatcher and JSON serialization
4. Test with simple "ping" command from Python

### Phase 2: Database Bridge
1. Create `database_bridge.cpp`
2. Expose message history via IPC
3. Add local search functionality
4. Test message retrieval from Python MCP

### Phase 3: Voice AI
1. Integrate OpenAI Whisper API
2. Add transcription cache to SQLite
3. Hook into voice message pipeline
4. Expose via MCP bridge

### Phase 4: Semantic Search
1. Integrate FAISS library
2. Create embedding service (Python subprocess)
3. Build incremental indexing
4. Expose search via MCP

### Phase 5: Media Processing
1. Integrate Tesseract (OCR)
2. Add document parsers
3. Implement video analysis
4. Expose via MCP bridge

## Build Integration

### CMakeLists.txt modifications

**Add to:** `~/xCode/tlgrm/tdesktop/Telegram/CMakeLists.txt`

```cmake
# MCP Bridge dependencies
find_package(OpenSSL REQUIRED)

# Add MCP source files
target_sources(Telegram PRIVATE
    SourceFiles/mcp/mcp_bridge.cpp
    SourceFiles/mcp/mcp_bridge.h
    SourceFiles/mcp/database_bridge.cpp
    SourceFiles/mcp/voice_ai.cpp
    SourceFiles/mcp/semantic_search.cpp
    SourceFiles/mcp/media_processor.cpp
)

# Link libraries
target_link_libraries(Telegram PRIVATE
    faiss
    tesseract
)
```

## Python MCP Integration

### Bridge client in Python

**Create:** `~/PycharmProjects/telegramMCP/src/tdesktop_bridge.py`

```python
import socket
import json
from typing import Dict, Any, List

class TDesktopBridge:
    """Bridge to modified Telegram Desktop."""

    def __init__(self, socket_path="/tmp/tdesktop_mcp.sock"):
        self.socket_path = socket_path
        self._request_id = 0

    async def call_method(self, method: str, params: Dict[str, Any]) -> Any:
        """Call a method on tdesktop MCP bridge."""
        self._request_id += 1

        request = {
            "id": self._request_id,
            "method": method,
            "params": params
        }

        # Connect to Unix socket
        sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        sock.connect(self.socket_path)

        # Send request
        sock.sendall(json.dumps(request).encode() + b'\n')

        # Receive response
        response = b""
        while True:
            chunk = sock.recv(4096)
            if not chunk:
                break
            response += chunk
            if b'\n' in chunk:
                break

        sock.close()

        return json.loads(response.decode())

    async def get_messages_local(self, chat_id: int, limit: int = 50) -> List[Dict]:
        """Get messages from local tdesktop database (instant, no API calls)."""
        result = await self.call_method("get_messages", {
            "chat_id": chat_id,
            "limit": limit
        })
        return result.get("messages", [])

    async def transcribe_voice(self, file_path: str) -> str:
        """Transcribe voice message using tdesktop's Whisper integration."""
        result = await self.call_method("transcribe_voice", {
            "file_path": file_path
        })
        return result.get("text", "")

    async def semantic_search(self, query: str, limit: int = 10) -> List[Dict]:
        """Semantic search using tdesktop's FAISS index."""
        result = await self.call_method("semantic_search", {
            "query": query,
            "limit": limit
        })
        return result.get("results", [])
```

## Benefits of This Approach

### Performance
- **Local DB access:** 1000x faster than API calls
- **No rate limits:** Unlimited queries
- **Offline capability:** Works without internet
- **Native processing:** C++ performance for heavy tasks

### Features
- **Complete history:** Access to all synced messages
- **Advanced AI:** Voice transcription, semantic search
- **Media intelligence:** OCR, document parsing, video analysis
- **Real-time:** Instant responses

### Maintainability
- **Clean separation:** C++ for heavy lifting, Python for MCP
- **Modular design:** Each feature is independent
- **Easy updates:** Python MCP can update without rebuilding tdesktop

## Next Steps

Would you like me to:
1. **Start with Phase 1** - Create the IPC bridge foundation?
2. **Implement database bridge first** - Get instant local message access?
3. **Build voice AI module** - Add Whisper transcription?
4. **Create a proof-of-concept** - Simple command to test the architecture?

Let me know which feature interests you most, and we can start modifying the tdesktop source code!

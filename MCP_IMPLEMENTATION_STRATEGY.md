# MCP Feature Implementation Strategy

## Decision Framework: C++ vs Python Implementation

### **Implement in C++ when:**
- ✅ Requires direct access to tdesktop's local database
- ✅ Needs real-time access to active Telegram session
- ✅ Performance-critical operations (>1000 calls/sec)
- ✅ Direct MTProto API access needed
- ✅ Memory-sensitive operations
- ✅ Core Telegram functionality

### **Implement in Python when:**
- ✅ Requires AI/ML processing (embeddings, NLP)
- ✅ Needs external libraries (transformers, spaCy, etc.)
- ✅ Complex data transformations
- ✅ Integration with external services
- ✅ Rapid prototyping needed
- ✅ Computationally expensive (can run async)

### **Passthrough Pattern:**
Python delegates to C++ for fast operations, adds AI/ML on top.

---

## Core Features (Priority 1) - ALL IN C++

### 1. ✅ **list_chats** - IMPLEMENTED
- **Status:** Complete with live data
- **Implementation:** `toolListChats()` in mcp_server_complete.cpp:1483
- **Data Source:** `Main::Session::data().chatsListFor()`
- **Performance:** <5ms for 100 chats

### 2. ❌ **read_messages** - TO IMPLEMENT
- **Decision:** **C++** (direct database + session access)
- **Implementation Plan:**
  ```cpp
  // Access via Main::Session
  auto history = session->data().history(peerId);
  auto messages = history->messages();

  // Format each message with:
  // - message_id, date, text, from_user
  // - reply_to, forward_from, edit_date
  // - media info (if present)
  ```
- **Location:** `toolReadMessages()` in mcp_server_complete.cpp:1537
- **Performance Target:** <10ms for 50 messages

### 3. ❌ **send_message** - TO IMPLEMENT
- **Decision:** **C++** (requires tdesktop API session)
- **Implementation Plan:**
  ```cpp
  // Use Main::Session API
  auto &api = session->api();
  api.request(MTPmessages_SendMessage(
      MTP_flags(0),
      peer->input,
      MTP_int(0), // reply_to
      MTP_string(text),
      MTP_long(randomId),
      MTPReplyMarkup(),
      MTPVector<MTPMessageEntity>(),
      MTP_int(0), // schedule_date
      MTPInputPeer() // send_as
  )).done([](const MTPUpdates &result) {
      // Handle response
  }).fail([](const MTP::Error &error) {
      // Handle error
  }).send();
  ```
- **Location:** `toolSendMessage()` in mcp_server_complete.cpp:1551
- **Performance Target:** Network-bound (100-500ms)

### 4. ❌ **get_user_info** - TO IMPLEMENT
- **Decision:** **C++** (session data)
- **Implementation Plan:**
  ```cpp
  auto user = session->data().user(userId);
  return {
      {"id", user->id.value},
      {"username", user->username()},
      {"first_name", user->firstName},
      {"last_name", user->lastName},
      {"phone", user->phone()},
      {"is_bot", user->isBot()},
      {"is_premium", user->isPremium()}
  };
  ```
- **Location:** `toolGetUserInfo()` in mcp_server_complete.cpp:1583
- **Performance Target:** <1ms

### 5. ❌ **get_chat_info** - ENHANCE EXISTING
- **Status:** Currently uses ChatArchiver (archived data)
- **Decision:** **Enhance C++** with live data
- **Implementation Plan:** Add live session data access
- **Location:** `toolGetChatInfo()` in mcp_server_complete.cpp:1576
- **Performance Target:** <5ms

### 6. ❌ **search_messages** - ENHANCE EXISTING
- **Status:** Currently uses ChatArchiver (SQLite FTS)
- **Decision:** **Keep C++ for keyword**, **Python for semantic**
- **C++ Implementation:** Use tdesktop's search API
- **Python Implementation:** Semantic search with embeddings
- **Location:** `toolSearchMessages()` in mcp_server_complete.cpp:1568
- **Performance Target:** C++ <20ms, Python <50ms

---

## Message Operations (Priority 2) - ALL IN C++

### 7. ❌ **edit_message**
- **Decision:** **C++** (requires API access)
- **Implementation:** `session->api().request(MTPmessages_EditMessage(...))`
- **Location:** Create new method in mcp_server_complete.cpp

### 8. ❌ **delete_message**
- **Decision:** **C++** (requires API access)
- **Implementation:** `session->api().request(MTPmessages_DeleteMessages(...))`

### 9. ❌ **forward_message**
- **Decision:** **C++** (requires API access)
- **Implementation:** `session->api().request(MTPmessages_ForwardMessages(...))`

### 10. ❌ **pin_message**
- **Decision:** **C++** (requires API access)
- **Implementation:** `session->api().request(MTPmessages_UpdatePinnedMessage(...))`

### 11. ❌ **add_reaction**
- **Decision:** **C++** (requires API access)
- **Implementation:** `session->api().request(MTPmessages_SendReaction(...))`

---

## Media Operations (Priority 3) - HYBRID

### 12. ❌ **send_media** (photo/video/file)
- **Decision:** **C++ primary**, **Python preprocessing**
- **C++ Implementation:** Upload and send via `MTPmessages_SendMedia`
- **Python Role:** Image processing, compression, format conversion
- **Workflow:**
  1. Python preprocesses file (resize, compress)
  2. Python calls C++ via IPC
  3. C++ uploads to Telegram
  4. C++ sends message with media

### 13. ❌ **download_media**
- **Decision:** **C++** (tdesktop has built-in downloader)
- **Implementation:** Access `Data::DocumentMedia` or `Data::PhotoMedia`
- **Python Role:** Post-processing (OCR, face detection)

---

## Advanced Features (Priority 4) - PYTHON WITH C++ PASSTHROUGH

### 14. ❌ **semantic_search**
- **Decision:** **Python** (requires embeddings)
- **Implementation:**
  ```python
  # Python side
  async def semantic_search(query, limit=10):
      # 1. Get messages from C++ (fast)
      messages = await bridge.call_method('get_messages', {'limit': 1000})

      # 2. Generate query embedding
      query_emb = model.encode(query)

      # 3. Search vector database
      results = chromadb.query(query_emb, n_results=limit)

      return results
  ```
- **C++ Role:** Provide raw message data
- **Python Role:** Embedding generation, vector search

### 15. ❌ **classify_intent**
- **Decision:** **Python** (requires ML model)
- **Implementation:** Use BART or similar transformer model
- **C++ Role:** None
- **Python Role:** Model inference

### 16. ❌ **extract_entities**
- **Decision:** **Python** (requires NLP)
- **Implementation:** Use spaCy or custom NER model
- **C++ Role:** None
- **Python Role:** NLP processing

### 17. ❌ **summarize_conversation**
- **Decision:** **Python** (requires LLM)
- **Implementation:**
  ```python
  async def summarize_conversation(chat_id, num_messages=100):
      # 1. Get messages from C++ (fast)
      messages = await bridge.call_method('get_messages', {
          'chat_id': chat_id,
          'limit': num_messages
      })

      # 2. Format for LLM
      context = format_messages(messages)

      # 3. Generate summary
      summary = llm.generate(f"Summarize: {context}")

      return summary
  ```
- **C++ Role:** Provide message data
- **Python Role:** LLM inference

---

## Archive & Analytics (Priority 5) - C++

### 18. ❌ **archive_chat**
- **Decision:** **C++** (database writes)
- **Implementation:** Enhanced ChatArchiver with live data sync
- **Status:** Partially implemented in ChatArchiver

### 19. ❌ **get_message_stats**
- **Decision:** **C++** (SQL aggregations)
- **Implementation:** Use ChatArchiver's analytics module
- **Status:** Partially implemented

### 20. ❌ **get_user_activity**
- **Decision:** **C++** (SQL queries)
- **Implementation:** Query archived database
- **Status:** Partially implemented

---

## Implementation Order

### **Phase 1: Core Data Access (This Week)**
1. ✅ list_chats (DONE)
2. read_messages
3. get_user_info
4. get_chat_info (enhance)
5. search_messages (enhance)

### **Phase 2: Message Operations (Next Week)**
6. send_message
7. edit_message
8. delete_message
9. forward_message

### **Phase 3: Python Integration (Week 3)**
10. semantic_search (Python)
11. Verify IPC bridge works
12. Test hybrid mode

### **Phase 4: Advanced Features (Week 4+)**
13. Media operations
14. AI/ML features
15. Analytics enhancements

---

## Code Structure

```
Telegram/SourceFiles/mcp/
├── mcp_server.h                    # Tool declarations
├── mcp_server_complete.cpp         # Tool implementations (C++)
├── mcp_bridge.h/cpp                # IPC server (C++)
├── chat_archiver.h/cpp             # SQLite archiving
├── analytics.h/cpp                 # Stats and analytics
├── semantic_search.h/cpp           # Vector search (stub)
├── voice_transcription.h/cpp       # Whisper integration (stub)
└── bot_manager.h/cpp               # Bot framework (stub)

pythonMCP/src/
├── mcp_server.py                   # Main Python MCP server
├── ipc_bridge.py                   # IPC client to C++
├── core/
│   ├── telegram_client.py          # Pyrogram fallback
│   └── cache.py                    # Message caching
└── aiml/
    ├── service.py                  # AI/ML service layer
    ├── embeddings.py               # Semantic search
    ├── intent.py                   # Intent classification
    └── summarization.py            # Conversation summary
```

---

## Testing Strategy

### **Unit Tests (C++)**
- Test each tool with mock Main::Session
- Verify JSON serialization
- Test error handling

### **Integration Tests (Python)**
- Test IPC bridge connection
- Test C++ tool invocation via IPC
- Test hybrid workflows (C++ data → Python AI)

### **End-to-End Tests**
- Test Claude Desktop integration
- Verify all 55+ tools work
- Performance benchmarks

---

## Performance Targets

| Operation | Target | Implementation |
|-----------|--------|----------------|
| list_chats (100 chats) | <5ms | C++ |
| read_messages (50 msgs) | <10ms | C++ |
| send_message | 100-500ms | C++ (network) |
| search_messages (keyword) | <20ms | C++ (SQLite FTS) |
| semantic_search (10K msgs) | <50ms | Python (ChromaDB) |
| get_user_info | <1ms | C++ |
| IPC round-trip | <2ms | Bridge |

---

## Next Actions

1. ✅ **Completed:** list_chats with live data
2. ⏳ **Next:** Implement read_messages with live data
3. ⏳ **Then:** Implement send_message with API
4. ⏳ **After:** Test IPC bridge end-to-end

**Goal:** Complete Phase 1 (core data access) by end of today! 🚀

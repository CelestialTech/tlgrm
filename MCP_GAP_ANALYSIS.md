# MCP Implementation - Gap Analysis & Development Plan

**Date:** 2025-11-24
**Status:** Phase 1 Partially Complete
**Next Milestone:** Complete Phase 1, Begin Phase 2

---

## 📊 Current Implementation Status

### ✅ **COMPLETED - Phase 1: Core Data Access (5/6 tools)**

#### 1. list_chats ✅ DONE
- **Location:** `mcp_server_complete.cpp:1483-1529`
- **Status:** Fully implemented with live data
- **Features:**
  - Lists all chats from active session
  - Returns: chat_id, name, username, source
  - Fallback to archived data
- **Performance:** <5ms target (expected achieved)

#### 2. read_messages ✅ DONE
- **Location:** `mcp_server_complete.cpp:1539-1642`
- **Status:** Fully implemented with live data
- **Features:**
  - Iterates through History->blocks->messages
  - Returns: message_id, date, text, from_user, is_outgoing, is_pinned, reply_to
  - Pagination support (limit parameter)
  - Time filtering (before_timestamp)
  - Fallback to archived data
- **Performance:** <10ms target (expected achieved)

#### 3. get_user_info ✅ DONE
- **Location:** `mcp_server_complete.cpp:1675-1752`
- **Status:** Fully implemented with live data
- **Features:**
  - Comprehensive user data extraction
  - Returns: id, name, username, first_name, last_name, phone, is_bot, is_self, is_contact, is_mutual_contact, is_premium, is_verified, is_scam, is_fake, online_till, about
  - Type validation (ensures peer is user)
- **Performance:** <1ms target (expected achieved)

#### 4. get_chat_info ✅ DONE
- **Location:** `mcp_server_complete.cpp:1531-1628`
- **Status:** Fully implemented with live data
- **Features:**
  - Chat type detection (user/group/supergroup/channel)
  - Returns: id, name, type, username, member_count, is_verified, is_scam, is_fake, about, loaded_message_count
  - Channel-specific fields: is_broadcast, is_megagroup, is_creator
  - Fallback to archived data
- **Performance:** <5ms target (expected achieved)

#### 5. send_message ✅ DONE
- **Location:** `mcp_server_complete.cpp:1737-1796`
- **Status:** Fully implemented with tdesktop API
- **Features:**
  - Uses Api::SendAction and Api::MessageToSend
  - Calls session->api().sendMessage()
  - Returns: success status, chat_id, text, status
- **Performance:** 100-500ms (network-bound)

#### 6. search_messages ❌ **TODO** (Priority 1)
- **Location:** `mcp_server_complete.cpp:1798-1810`
- **Current Status:** Using ChatArchiver (archived data only)
- **Needs:** Live data search implementation
- **Plan:**
  - Use tdesktop's search API for live messages
  - Keep ChatArchiver for full-text search on archived data
  - Hybrid approach: search live + archived
- **Estimated Time:** 2-4 hours

---

## ❌ **GAP: Phase 2 - Message Operations (0/5 tools)**

### Priority: HIGH (Core Telegram functionality)

#### 7. edit_message ❌ TODO
- **Decision:** C++ (requires API access)
- **Implementation:**
  ```cpp
  _session->api().request(MTPmessages_EditMessage(
      MTP_flags(...),
      peer->input,
      MTP_int(messageId),
      MTP_string(newText),
      ...
  )).done([](const MTPUpdates &result) {
      // Success
  }).fail([](const MTP::Error &error) {
      // Error
  }).send();
  ```
- **Location:** New method in mcp_server_complete.cpp
- **Estimated Time:** 3-5 hours
- **Dependencies:** None (can implement immediately)

#### 8. delete_message ❌ TODO
- **Decision:** C++ (requires API access)
- **Implementation:**
  ```cpp
  _session->api().request(MTPmessages_DeleteMessages(
      MTP_flags(MTPmessages_DeleteMessages::Flag::f_revoke),
      MTP_vector<MTPint>(1, MTP_int(messageId))
  )).done([](const MTPmessages_AffectedMessages &result) {
      // Success
  }).send();
  ```
- **Estimated Time:** 2-3 hours

#### 9. forward_message ❌ TODO
- **Decision:** C++ (requires API access)
- **Implementation:**
  ```cpp
  _session->api().request(MTPmessages_ForwardMessages(
      MTP_flags(...),
      fromPeer->input,
      MTP_vector<MTPint>(1, MTP_int(messageId)),
      MTP_vector<MTPlong>(...),
      toPeer->input,
      ...
  )).send();
  ```
- **Estimated Time:** 3-4 hours

#### 10. pin_message ❌ TODO
- **Decision:** C++ (requires API access)
- **Implementation:**
  ```cpp
  _session->api().request(MTPmessages_UpdatePinnedMessage(
      MTP_flags(...),
      peer->input,
      MTP_int(messageId)
  )).send();
  ```
- **Estimated Time:** 2-3 hours

#### 11. add_reaction ❌ TODO
- **Decision:** C++ (requires API access)
- **Implementation:**
  ```cpp
  _session->api().request(MTPmessages_SendReaction(
      MTP_flags(...),
      peer->input,
      MTP_int(messageId),
      reaction
  )).send();
  ```
- **Estimated Time:** 2-3 hours

**Phase 2 Total Estimate:** 12-18 hours

---

## ❌ **GAP: Phase 3 - Media Operations (0/2 tools)**

### Priority: MEDIUM (Useful but not critical)

#### 12. send_media ❌ TODO (HYBRID C++/Python)
- **Decision:** C++ for upload, Python for preprocessing
- **C++ Implementation:**
  ```cpp
  // Use session->api().sendFiles() or similar
  // Upload via MTPmessages_SendMedia
  ```
- **Python Role:**
  - Image resizing/compression
  - Format conversion
  - Metadata extraction
- **Workflow:**
  1. Python preprocesses file
  2. Python calls C++ via IPC bridge
  3. C++ uploads to Telegram
  4. C++ sends message with media
- **Estimated Time:** 8-12 hours (complex)

#### 13. download_media ❌ TODO
- **Decision:** C++ (tdesktop has built-in downloader)
- **Implementation:**
  ```cpp
  // Access Data::DocumentMedia or Data::PhotoMedia
  // Use existing download manager
  auto media = document->createMediaView();
  media->automaticLoad(...);
  ```
- **Python Role:** Post-processing (OCR, face detection)
- **Estimated Time:** 4-6 hours

**Phase 3 Total Estimate:** 12-18 hours

---

## ✅ **EXISTING BUT NOT EXPOSED: Ephemeral Message System**

### Status: IMPLEMENTED in ChatArchiver, needs MCP exposure

#### Already Built (chat_archiver.cpp:856-910):
- ✅ `EphemeralArchiver` class
- ✅ `setCaptureTypes(bool selfDestruct, bool viewOnce, bool vanishing)`
- ✅ `detectEphemeralType()` - detects via `item->ttlDestroyAt()`
- ✅ `captureMessage()` - archives before destruction
- ✅ Statistics tracking (selfDestructCount, viewOnceCount)

### ❌ **GAP: MCP Tools to Expose Ephemeral System (0/3 tools)**

#### 14. configure_ephemeral_capture ❌ TODO
- **Purpose:** Enable/disable self-destruct protection
- **Implementation:**
  ```cpp
  QJsonObject Server::toolConfigureEphemeralCapture(const QJsonObject &args) {
      bool selfDestruct = args["capture_self_destruct"].toBool(true);
      bool viewOnce = args["capture_view_once"].toBool(true);
      bool vanishing = args["capture_vanishing"].toBool(true);

      _archiver->ephemeralArchiver()->setCaptureTypes(
          selfDestruct, viewOnce, vanishing);

      return {
          {"success", true},
          {"capture_self_destruct", selfDestruct},
          {"capture_view_once", viewOnce},
          {"capture_vanishing", vanishing}
      };
  }
  ```
- **Estimated Time:** 1-2 hours

#### 15. get_ephemeral_messages ❌ TODO
- **Purpose:** Query captured self-destruct messages
- **Implementation:**
  ```cpp
  QJsonObject Server::toolGetEphemeralMessages(const QJsonObject &args) {
      QString type = args["type"].toString(); // "self_destruct", "view_once", "vanishing"
      int limit = args["limit"].toInt(50);

      // Query from database
      QJsonArray messages = _archiver->queryEphemeralMessages(type, limit);

      return {
          {"messages", messages},
          {"count", messages.size()},
          {"type", type}
      };
  }
  ```
- **Estimated Time:** 2-3 hours

#### 16. get_ephemeral_stats ❌ TODO
- **Purpose:** Get capture statistics
- **Implementation:**
  ```cpp
  QJsonObject Server::toolGetEphemeralStats(const QJsonObject &args) {
      auto stats = _archiver->ephemeralArchiver()->getStats();

      return {
          {"self_destruct_count", stats.selfDestructCount},
          {"view_once_count", stats.viewOnceCount},
          {"vanishing_count", stats.vanishingCount},
          {"total_saved", stats.totalSaved}
      };
  }
  ```
- **Estimated Time:** 1 hour

**Ephemeral MCP Tools Total Estimate:** 4-6 hours

---

## ❌ **GAP: Phase 4 - Python MCP Integration (Not Started)**

### Status: Architecture defined, IPC bridge implemented, not connected

#### Existing Infrastructure:
- ✅ `mcp_bridge.h/cpp` - IPC bridge (QLocalServer on /tmp/tdesktop_mcp.sock)
- ✅ `pythonMCP/` directory structure
- ✅ Basic Python MCP server skeleton

#### What's Missing:

#### 17. Python MCP Server Implementation ❌ TODO
- **Location:** `pythonMCP/src/mcp_server.py`
- **Status:** Skeleton exists, needs full implementation
- **Needs:**
  - Complete MCP protocol handlers
  - IPC client to connect to C++ bridge
  - Tool registration and dispatch
- **Estimated Time:** 8-12 hours

#### 18. IPC Bridge Testing & Integration ❌ TODO
- **Status:** C++ side implemented, not tested end-to-end
- **Needs:**
  - Test C++ → Python communication
  - Test Python → C++ delegation
  - Error handling and reconnection logic
- **Estimated Time:** 4-6 hours

**Python Integration Total Estimate:** 12-18 hours

---

## ❌ **GAP: Phase 5 - AI/ML Features (Python) (Not Started)**

### Priority: LOW (Advanced features, not core functionality)

#### 19. semantic_search ❌ TODO (Python + C++ data)
- **Decision:** Python (requires embeddings)
- **Implementation Plan:**
  ```python
  async def semantic_search(query, limit=10):
      # 1. Get messages from C++ via IPC
      messages = await bridge.call_method('get_messages', {'limit': 1000})

      # 2. Generate query embedding
      query_emb = model.encode(query)

      # 3. Search vector database (ChromaDB)
      results = vector_db.query(query_emb, n_results=limit)

      return results
  ```
- **Dependencies:**
  - sentence-transformers or similar
  - ChromaDB or FAISS
  - Working IPC bridge
- **Estimated Time:** 12-16 hours

#### 20. classify_intent ❌ TODO (Python)
- **Decision:** Python (requires ML model)
- **Implementation:** Use BART or similar transformer
- **Estimated Time:** 6-8 hours

#### 21. extract_entities ❌ TODO (Python)
- **Decision:** Python (requires NLP)
- **Implementation:** Use spaCy or custom NER model
- **Estimated Time:** 6-8 hours

#### 22. summarize_conversation ❌ TODO (Python)
- **Decision:** Python (requires LLM)
- **Implementation:**
  ```python
  async def summarize_conversation(chat_id, num_messages=100):
      # 1. Get messages from C++
      messages = await bridge.call_method('get_messages', {
          'chat_id': chat_id,
          'limit': num_messages
      })

      # 2. Format for LLM
      context = format_messages(messages)

      # 3. Generate summary (using Anthropic API or local model)
      summary = llm.generate(f"Summarize: {context}")

      return summary
  ```
- **Estimated Time:** 4-6 hours

**AI/ML Features Total Estimate:** 28-38 hours

---

## 📋 **Comprehensive Gap Summary**

| Phase | Tools | Completed | Missing | Estimate |
|-------|-------|-----------|---------|----------|
| Phase 1: Core Data | 6 | 5 (83%) | 1 | 2-4h |
| Phase 2: Message Ops | 5 | 0 (0%) | 5 | 12-18h |
| Phase 3: Media Ops | 2 | 0 (0%) | 2 | 12-18h |
| Phase 4: Ephemeral | 3 | 0 (0%) | 3 | 4-6h |
| Phase 5: Python Setup | 2 | 0 (0%) | 2 | 12-18h |
| Phase 6: AI/ML | 4 | 0 (0%) | 4 | 28-38h |
| **TOTAL** | **22** | **5 (23%)** | **17** | **70-102h** |

---

## 🚀 **Recommended Implementation Plan**

### **Sprint 1: Complete Phase 1 (2-4 hours)**
**Goal:** Finish core data access tools

1. **search_messages** - Implement live data search
   - Use tdesktop's search API
   - Hybrid live + archived search
   - Test with various queries

**Deliverable:** All 6 Phase 1 tools fully functional

---

### **Sprint 2: Message Operations (12-18 hours)**
**Goal:** Implement all message manipulation tools

**Day 1-2:**
1. **edit_message** - Basic implementation
2. **delete_message** - With revoke support
3. Test edit/delete operations

**Day 3:**
4. **forward_message** - Single and batch
5. **pin_message** - Pin/unpin support
6. **add_reaction** - Emoji reactions

**Deliverable:** Complete message operation suite

---

### **Sprint 3: Ephemeral Exposure (4-6 hours)**
**Goal:** Make self-destruct protection accessible via MCP

**Tasks:**
1. Implement `configure_ephemeral_capture`
2. Implement `get_ephemeral_messages`
3. Implement `get_ephemeral_stats`
4. Test capture of view-once messages
5. Document ephemeral features

**Deliverable:** Full control over self-destruct protection via MCP

---

### **Sprint 4: Build & Test (4-6 hours)**
**Goal:** Ensure all C++ tools work correctly

**Tasks:**
1. Fix style file errors blocking build
2. Build with all new tools
3. Test each tool via MCP protocol
4. Document all tools
5. Create usage examples

**Deliverable:** Working binary with Phases 1-3 complete

---

### **Sprint 5: Python Integration (12-18 hours)**
**Goal:** Connect Python MCP server to C++ backend

**Tasks:**
1. Complete Python MCP server implementation
2. Implement IPC client (Python side)
3. Test C++ ↔ Python communication
4. Implement delegation pattern
5. Test hybrid operations

**Deliverable:** Working Python-C++ integration

---

### **Sprint 6: Media Operations (12-18 hours)**
**Goal:** Enable media sending/downloading

**Tasks:**
1. Implement `send_media` (C++ upload)
2. Add Python preprocessing for images
3. Implement `download_media`
4. Test with various media types
5. Document media features

**Deliverable:** Full media support

---

### **Sprint 7: AI/ML Features (28-38 hours)**
**Goal:** Add advanced Python-based features

**Week 1:**
1. Setup vector database (ChromaDB)
2. Implement semantic search
3. Test semantic queries

**Week 2:**
4. Implement intent classification
5. Implement entity extraction
6. Implement conversation summarization
7. Integration testing

**Deliverable:** Complete AI/ML feature suite

---

## 🎯 **Priority Roadmap**

### **NOW (This Week):**
- ✅ Complete Phase 1: search_messages
- ✅ Build and test existing implementations
- ✅ Document Phase 1 tools

### **SHORT-TERM (Next 2 Weeks):**
- Phase 2: All message operations
- Phase 4: Ephemeral MCP tools
- Fix build issues

### **MEDIUM-TERM (Next Month):**
- Phase 5: Python integration
- Phase 3: Media operations
- Testing and documentation

### **LONG-TERM (Next Quarter):**
- Phase 6: AI/ML features
- Performance optimization
- Advanced features

---

## 🔧 **Technical Debt & Blockers**

### **Current Blockers:**

#### 1. Style File Errors
- **Issue:** `credits.style:237` and `calls.style:1730` have syntax errors
- **Impact:** Prevents building
- **Solution:**
  - Fixed `credits.style` (removed invalid margins parameter)
  - Need to fix `calls.style` (missing parent definition)
- **Time:** 1-2 hours to investigate and fix

#### 2. Missing Session Connection
- **Issue:** MCP server needs session passed from Application
- **Status:** Architecture in place, needs connection
- **Solution:** Ensure `application.cpp` calls `_mcpServer->setSession()` when session is ready
- **Time:** 30 minutes to verify

### **Technical Debt:**

1. **Error Handling:** Many try-catch blocks could be more specific
2. **Logging:** Need consistent logging strategy (qInfo vs qWarning vs qDebug)
3. **Performance Testing:** No benchmarks yet for Phase 1 tools
4. **Memory Management:** Review lifetime of History/HistoryItem pointers
5. **Documentation:** Need inline documentation for all MCP tools

---

## 📈 **Success Metrics**

### **Phase 1 Complete:**
- [x] 5/6 tools implemented
- [ ] 1/6 remaining (search_messages)
- [ ] All tools tested
- [ ] Performance targets met
- [ ] Documentation complete

### **Overall Progress:**
- **Tools Completed:** 5/22 (23%)
- **Code Complete:** ~30% (Phase 1 mostly done)
- **Integration Complete:** ~15% (IPC bridge ready but not tested)
- **Documentation Complete:** ~20% (strategy docs done)

---

## 🎓 **Key Learnings**

### **What Worked Well:**
1. **Strategy Document:** Having MCP_IMPLEMENTATION_STRATEGY.md upfront was critical
2. **Fallback Pattern:** Live data with archived fallback is excellent design
3. **Incremental Implementation:** Building Phase 1 tools one-by-one worked well
4. **Existing Infrastructure:** ChatArchiver/EphemeralArchiver saved massive time

### **What Needs Improvement:**
1. **Build System:** Style file errors block progress, need better handling
2. **Testing Strategy:** Should have tests running alongside development
3. **Python Integration:** Planned but not prioritized early enough
4. **Documentation:** Should document as we build, not after

---

## 📝 **Next Actions (Immediate)**

1. **Fix build blockers** (calls.style error)
2. **Implement search_messages** with live data
3. **Test Phase 1 tools** with actual Telegram session
4. **Begin Phase 2** (edit_message as first target)
5. **Document all Phase 1 tools** for users

---

**Last Updated:** 2025-11-24
**Next Review:** After Sprint 1 completion

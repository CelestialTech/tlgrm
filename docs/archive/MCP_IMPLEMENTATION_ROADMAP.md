# MCP Implementation Roadmap
## Telegram Desktop MCP Integration - Full Feature Implementation Plan

**Current Version:** v6.3.3-10 (Tlgrm)
**Base Commit:** 8d27ac4c90
**Status:** Core tools stub, ready for implementation
**Last Updated:** 2024-11-24

---

## Table of Contents
1. [Implementation Status](#implementation-status)
2. [Phase 1: Core Tools (Week 1)](#phase-1-core-tools-week-1)
3. [Phase 2: Message Operations (Week 2)](#phase-2-message-operations-week-2)
4. [Phase 3: Media & Files (Week 3)](#phase-3-media--files-week-3)
5. [Phase 4: Advanced Features (Week 4+)](#phase-4-advanced-features-week-4)
6. [Technical Architecture](#technical-architecture)
7. [Dependencies & Prerequisites](#dependencies--prerequisites)

---

## Implementation Status

### ✅ Completed (Current)
- [x] Basic MCP protocol implementation (stdio transport)
- [x] Tool registration framework
- [x] JSON-RPC request handling
- [x] 6 core tools (stubs only, return dummy data)
- [x] 50+ tool definitions (headers only)
- [x] TData logging for debugging
- [x] Custom Tlgrm branding and icon
- [x] Updated to Telegram Desktop v6.3.3

### 🔄 In Progress
- [ ] **Phase 1:** Core 6 tools with real data (TODAY)

### 📋 Planned
- [ ] **Phase 2:** Message operations (edit, delete, forward, pin)
- [ ] **Phase 3:** Media handling (send, download, thumbnails)
- [ ] **Phase 4:** Advanced features (analytics, semantic search, bots)

---

## Phase 1: Core Tools (Week 1)

**Goal:** Make MCP actually useful for basic Telegram operations
**Estimated Time:** 20-40 hours
**Priority:** 🔴 CRITICAL

### Tool 1: `list_chats` - Real Chat List
**Status:** 🔄 In Progress
**Complexity:** Medium
**Files to modify:**
- `mcp_server.cpp` - `toolListChats()` implementation
- `core/application.cpp` - Pass `Main::Session` to MCP server

**Implementation:**
```cpp
QJsonObject Server::toolListChats(const QJsonObject &args) {
    if (!_session) return errorResponse("No active session");

    QJsonArray chats;
    auto &owner = _session->data();
    auto &list = owner.chatsListFor(Data::Folder::kAll);

    // Get filters from args
    int limit = args["limit"].toInt(50);
    QString filter = args["filter"].toString(); // "all", "unread", "groups", "channels"

    for (const auto &dialog : list->all()) {
        if (chats.size() >= limit) break;

        auto peer = dialog->peer();
        auto history = peer->owner().history(peer);

        QJsonObject chat;
        chat["id"] = QString::number(peer->id.value);
        chat["title"] = peer->name();
        chat["type"] = peerTypeString(peer); // "user", "group", "channel"
        chat["unread_count"] = history->unreadCount();
        chat["is_pinned"] = dialog->isPinnedDialog(Data::Folder::kAll);
        chat["is_muted"] = peer->isMuted();

        if (auto user = peer->asUser()) {
            chat["is_bot"] = user->isBot();
            chat["phone"] = user->phone();
            chat["username"] = user->username();
        }

        chats.append(chat);
    }

    return successResponse(QJsonObject{{"chats", chats}, {"count", chats.size()}});
}
```

**Testing:**
```bash
echo '{"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"list_chats","arguments":{"limit":10}}}' | \
  ./out/Release/Tlgrm.app/Contents/MacOS/Tlgrm --mcp
```

**Completion Criteria:**
- [ ] Returns real chat list from active session
- [ ] Supports pagination (limit, offset)
- [ ] Filters work (all, unread, groups, channels)
- [ ] Includes unread counts and mute status
- [ ] Handles no session gracefully

---

### Tool 2: `get_chat_info` - Chat Details
**Status:** ⏳ Pending
**Complexity:** Medium
**Dependencies:** list_chats

**Implementation:**
```cpp
QJsonObject Server::toolGetChatInfo(const QJsonObject &args) {
    QString chatId = args["chat_id"].toString();
    if (chatId.isEmpty()) return errorResponse("Missing chat_id");

    PeerId peerId = peerFromString(chatId);
    auto peer = _session->data().peer(peerId);
    if (!peer) return errorResponse("Chat not found");

    QJsonObject info;
    info["id"] = chatId;
    info["title"] = peer->name();
    info["type"] = peerTypeString(peer);
    info["photo_url"] = getPhotoUrl(peer);
    info["description"] = peer->about();

    if (auto chat = peer->asChat()) {
        info["members_count"] = chat->count;
        info["is_creator"] = chat->amCreator();
    }

    if (auto channel = peer->asChannel()) {
        info["members_count"] = channel->membersCount();
        info["is_public"] = channel->isPublic();
        info["username"] = channel->username();
    }

    return successResponse(info);
}
```

**Completion Criteria:**
- [ ] Returns complete chat information
- [ ] Includes member counts for groups/channels
- [ ] Provides permissions info
- [ ] Returns photo URL or base64 thumbnail

---

### Tool 3: `read_messages` - Message History
**Status:** ⏳ Pending
**Complexity:** High
**Dependencies:** get_chat_info

**Implementation:**
```cpp
QJsonObject Server::toolReadMessages(const QJsonObject &args) {
    QString chatId = args["chat_id"].toString();
    int limit = args["limit"].toInt(50);
    int offsetId = args["offset_id"].toInt(0);

    PeerId peerId = peerFromString(chatId);
    auto history = _session->data().history(peerId);

    QJsonArray messages;
    history->forEachMessage([&](not_null<HistoryItem*> item) {
        if (messages.size() >= limit) return false;
        if (offsetId && item->id <= offsetId) return true;

        messages.append(serializeMessage(item));
        return true;
    });

    return successResponse(QJsonObject{
        {"messages", messages},
        {"has_more", messages.size() == limit}
    });
}

QJsonObject Server::serializeMessage(not_null<HistoryItem*> item) {
    QJsonObject msg;
    msg["id"] = item->id;
    msg["date"] = item->date().toTime_t();
    msg["text"] = item->originalText().text;
    msg["from_id"] = QString::number(item->from()->id.value);
    msg["from_name"] = item->from()->name();

    // Handle different message types
    if (auto media = item->media()) {
        msg["media_type"] = mediaTypeString(media);
        if (auto photo = media->photo()) {
            msg["photo"] = serializePhoto(photo);
        } else if (auto document = media->document()) {
            msg["document"] = serializeDocument(document);
        }
    }

    // Reactions
    if (auto reactions = item->reactions()) {
        msg["reactions"] = serializeReactions(reactions);
    }

    return msg;
}
```

**Completion Criteria:**
- [ ] Returns message history with pagination
- [ ] Handles text messages correctly
- [ ] Includes sender information
- [ ] Serializes media metadata (photos, documents, etc.)
- [ ] Supports offset-based pagination
- [ ] Returns reactions and forwarded info

---

### Tool 4: `send_message` - Send Text Messages
**Status:** ⏳ Pending
**Complexity:** High
**Dependencies:** read_messages

**Implementation:**
```cpp
QJsonObject Server::toolSendMessage(const QJsonObject &args) {
    QString chatId = args["chat_id"].toString();
    QString text = args["text"].toString();
    int replyToId = args["reply_to_id"].toInt(0);

    if (text.isEmpty()) return errorResponse("Empty message text");

    PeerId peerId = peerFromString(chatId);
    auto history = _session->data().history(peerId);

    Api::SendOptions options;
    options.replyTo = replyToId;

    auto &api = _session->api();
    auto textWithTags = TextWithTags{text, TextWithTags::Tags()};

    history->session().api().sendMessage(
        textWithTags,
        options,
        history
    );

    return successResponse(QJsonObject{
        {"status", "sent"},
        {"chat_id", chatId}
    });
}
```

**Completion Criteria:**
- [ ] Sends text messages successfully
- [ ] Supports reply-to message ID
- [ ] Handles markdown/formatting (bold, italic, code)
- [ ] Returns message ID after sending
- [ ] Error handling for invalid chats
- [ ] Silent mode option

---

### Tool 5: `search_messages` - Message Search
**Status:** ⏳ Pending
**Complexity:** High
**Dependencies:** read_messages

**Implementation:**
```cpp
QJsonObject Server::toolSearchMessages(const QJsonObject &args) {
    QString query = args["query"].toString();
    QString chatId = args["chat_id"].toString(); // Optional
    int limit = args["limit"].toInt(50);

    QJsonArray results;

    if (chatId.isEmpty()) {
        // Global search across all chats
        auto &data = _session->data();
        // Use Telegram's search API
        // TODO: Implement global search
    } else {
        // Search within specific chat
        PeerId peerId = peerFromString(chatId);
        auto history = _session->data().history(peerId);

        // Search through local messages first
        history->forEachMessage([&](not_null<HistoryItem*> item) {
            if (results.size() >= limit) return false;

            if (item->originalText().text.contains(query, Qt::CaseInsensitive)) {
                results.append(serializeMessage(item));
            }
            return true;
        });
    }

    return successResponse(QJsonObject{
        {"messages", results},
        {"count", results.size()}
    });
}
```

**Completion Criteria:**
- [ ] Searches within specific chat
- [ ] Global search across all chats
- [ ] Supports filters (from user, date range, media type)
- [ ] Case-insensitive search
- [ ] Returns ranked results

---

### Tool 6: `get_user_info` - User Details
**Status:** ⏳ Pending
**Complexity:** Medium
**Dependencies:** None

**Implementation:**
```cpp
QJsonObject Server::toolGetUserInfo(const QJsonObject &args) {
    QString userId = args["user_id"].toString();

    PeerId peerId = peerFromString(userId);
    auto user = _session->data().peer(peerId)->asUser();
    if (!user) return errorResponse("User not found");

    QJsonObject info;
    info["id"] = userId;
    info["first_name"] = user->firstName;
    info["last_name"] = user->lastName;
    info["username"] = user->username();
    info["phone"] = user->phone();
    info["is_bot"] = user->isBot();
    info["is_verified"] = user->isVerified();
    info["is_premium"] = user->isPremium();
    info["about"] = user->about();
    info["photo_url"] = getPhotoUrl(user);

    return successResponse(info);
}
```

**Completion Criteria:**
- [ ] Returns complete user information
- [ ] Includes profile photo
- [ ] Shows premium/verified status
- [ ] Provides user bio/about
- [ ] Returns common chats count

---

## Phase 2: Message Operations (Week 2)

**Goal:** Enable full message management
**Estimated Time:** 15-25 hours
**Priority:** 🟠 HIGH

### 2.1 Edit Messages
- `edit_message` - Modify sent messages
- Supports text editing only (media changes require resend)
- Time limit check (48 hours for non-channel messages)

### 2.2 Delete Messages
- `delete_message` - Delete for self or everyone
- Batch delete via `batch_delete`
- Time limit enforcement

### 2.3 Forward Messages
- `forward_message` - Forward to other chats
- Batch forward via `batch_forward`
- Option to forward without author

### 2.4 Pin/Unpin
- `pin_message` - Pin message in chat
- `unpin_message` - Unpin message
- Admin permission check

### 2.5 Reactions
- `add_reaction` - Add emoji reaction
- List available reactions per chat
- Premium reactions support

---

## Phase 3: Media & Files (Week 3)

**Goal:** Full media handling capabilities
**Estimated Time:** 25-40 hours
**Priority:** 🟠 HIGH

### 3.1 Send Media
**Tools:**
- `send_photo` - Upload and send images
- `send_video` - Upload and send videos
- `send_document` - Upload files
- `send_audio` - Upload audio files
- `send_voice` - Upload voice messages

**Implementation Notes:**
- Use `Api::SendAction` for upload progress
- Support for albums (multiple media)
- Caption support with formatting
- Thumbnail generation
- File size limits (2GB standard, 4GB premium)

### 3.2 Download Media
**Tools:**
- `download_media` - Download to disk
- `get_media_url` - Get file path/URL
- `get_thumbnail` - Get thumbnail data

**Implementation Notes:**
- Use Telegram's file cache
- Progress reporting via MCP notifications
- Automatic format conversion options

### 3.3 Stickers & GIFs
- `send_sticker` - Send sticker by ID
- `list_sticker_packs` - Installed sticker sets
- `search_gifs` - Search GIFs via bot
- `send_gif` - Send animated GIF

---

## Phase 4: Advanced Features (Week 4+)

### 4.1 Chat Management (8-12 hours)
- Create groups/channels
- Add/remove members
- Manage permissions
- Admin rights management
- Chat folders

### 4.2 Analytics & Statistics (15-20 hours)
- Message frequency analysis
- User activity tracking
- Top words/hashtags
- Time series data
- Export to CSV/JSON

### 4.3 Semantic Search (20-30 hours)
- Integrate FAISS for vector search
- Generate embeddings for messages
- Topic detection
- Intent classification
- Similar message finder

**Dependencies:**
- FAISS library
- Sentence transformers model
- ChromaDB (optional)

### 4.4 Voice Transcription (10-15 hours)
- Integrate Whisper.cpp
- Transcribe voice messages
- Cache transcriptions
- Multiple language support

**Dependencies:**
- Whisper.cpp
- Model files (~150MB)

### 4.5 Bot Framework (15-25 hours)
- Bot command handling
- Inline bot queries
- Bot keyboards
- Web app bots
- Payment processing

### 4.6 Archive & Export (10-15 hours)
- Full chat export (HTML, JSON)
- Selective export by date/type
- Media inclusion options
- Incremental backups
- Scheduled exports

### 4.7 Batch Operations (8-12 hours)
- Batch send (broadcasting)
- Batch delete with filters
- Batch forward
- Bulk user management
- Rate limiting

### 4.8 Scheduler (5-10 hours)
- Schedule messages
- Recurring messages
- Edit/cancel scheduled
- Timezone handling

---

## Technical Architecture

### Session Management
```cpp
// In core/application.cpp
void Application::initializeMcpServer() {
    _mcpServer = std::make_unique<MCP::Server>(this);

    // Wait for session to be ready
    if (auto session = maybeActiveSession()) {
        _mcpServer->setSession(session);
    } else {
        // Connect signal for when session becomes available
        connect(this, &Application::sessionReady, [this](Main::Session *session) {
            _mcpServer->setSession(session);
        });
    }
}
```

### Data Access Patterns
```cpp
// Accessing chats
auto &data = _session->data();
auto &chats = data.chatsListFor(Data::Folder::kAll);

// Accessing history
auto history = data.history(peerId);
history->forEachMessage([](not_null<HistoryItem*> item) {
    // Process message
    return true; // Continue iteration
});

// Sending messages
auto &api = _session->api();
api.sendMessage(textWithTags, options, history);

// Uploading files
auto &uploader = _session->uploader();
uploader.upload(fileId, FileLoadTo::None);
```

### Error Handling Strategy
```cpp
try {
    // MCP operation
} catch (const std::exception &e) {
    return errorResponse(id, -32603, QString("Internal error: %1").arg(e.what()));
}

// Return structured errors
enum ErrorCode {
    NoSession = -32001,
    InvalidChatId = -32002,
    PermissionDenied = -32003,
    MessageTooLong = -32004,
    MediaUploadFailed = -32005,
    RateLimitExceeded = -32006
};
```

---

## Dependencies & Prerequisites

### Required
- ✅ Qt 6.2.12+ (already included)
- ✅ Telegram Desktop base (v6.3.3)
- ✅ MCP protocol framework (implemented)
- ✅ JSON-RPC handler (implemented)

### Optional (Phase 4)
- [ ] FAISS (semantic search)
- [ ] Whisper.cpp (voice transcription)
- [ ] ChromaDB (vector database)
- [ ] Sentence transformers model

### Build System
- CMake configuration already includes MCP sources
- No additional dependencies for Phase 1-3
- Phase 4 requires adding FAISS/Whisper to `Telegram/CMakeLists.txt`

---

## Testing Strategy

### Unit Tests
```cpp
// tests/mcp/mcp_tools_test.cpp
TEST(MCPTools, ListChatsReturnsValidData) {
    auto server = createTestServer();
    auto response = server->toolListChats(QJsonObject());
    ASSERT_TRUE(response.contains("chats"));
}
```

### Integration Tests
```bash
# test_mcp_integration.sh
echo '{"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"list_chats"}}' | \
  ./Tlgrm.app/Contents/MacOS/Tlgrm --mcp | \
  jq '.result.chats | length'
```

### Manual Testing with Claude Desktop
```json
// ~/.config/claude/mcp.json
{
  "mcpServers": {
    "telegram": {
      "command": "/path/to/Tlgrm.app/Contents/MacOS/Tlgrm",
      "args": ["--mcp"]
    }
  }
}
```

---

## Performance Targets

### Response Times (Phase 1)
- `list_chats`: < 100ms for 50 chats
- `read_messages`: < 200ms for 50 messages
- `send_message`: < 500ms (network dependent)
- `search_messages`: < 500ms for local search

### Throughput
- Handle 10 concurrent requests
- 1000 messages/minute send rate (respecting Telegram limits)
- Batch operations: 100 items/batch

### Memory
- Base overhead: < 50MB
- Per-chat cache: < 1MB
- Media cache: Configurable (default 500MB)

---

## Security Considerations

### Phase 1 (Current)
- ✅ No authentication (local-only via stdio)
- ✅ No network exposure
- ✅ Inherits Telegram Desktop permissions

### Phase 2+ (If HTTP transport added)
- [ ] API key authentication
- [ ] Rate limiting per key
- [ ] Scope-based permissions
- [ ] Audit logging
- [ ] HTTPS only

---

## Migration Path

### From Stub to Real Implementation
1. Add `Main::Session *_session` member to `Server` class
2. Modify `Application::initializeMcpServer()` to pass session
3. Replace stub implementations one by one
4. Test each tool before moving to next
5. Maintain backward compatibility (stubs return errors if no session)

### Version Strategy
- v1.0.0: Phase 1 complete (core 6 tools)
- v1.1.0: Phase 2 complete (message operations)
- v1.2.0: Phase 3 complete (media handling)
- v2.0.0: Phase 4 complete (all advanced features)

---

## Success Metrics

### Phase 1 Success
- [ ] All 6 core tools working with real data
- [ ] Successfully connect to Claude Desktop
- [ ] Can list chats, read messages, send messages
- [ ] Zero crashes during normal operation
- [ ] Response times meet targets

### Full Success (All Phases)
- [ ] 50+ tools implemented
- [ ] Full Telegram functionality via MCP
- [ ] Production-ready with error handling
- [ ] Documentation complete
- [ ] Test coverage > 80%

---

## Timeline Estimates

| Phase | Features | Time | Priority |
|-------|----------|------|----------|
| Phase 1 | Core 6 tools | 20-40h | 🔴 Critical |
| Phase 2 | Message ops | 15-25h | 🟠 High |
| Phase 3 | Media/Files | 25-40h | 🟠 High |
| Phase 4.1 | Chat mgmt | 8-12h | 🟡 Medium |
| Phase 4.2 | Analytics | 15-20h | 🟢 Low |
| Phase 4.3 | Semantic | 20-30h | 🟢 Low |
| Phase 4.4 | Voice | 10-15h | 🟢 Low |
| Phase 4.5 | Bots | 15-25h | 🟡 Medium |
| Phase 4.6 | Archive | 10-15h | 🟢 Low |
| Phase 4.7 | Batch ops | 8-12h | 🟡 Medium |
| Phase 4.8 | Scheduler | 5-10h | 🟢 Low |
| **TOTAL** | **All features** | **~200h** | - |

---

## Next Steps (TODAY)

1. ✅ Create this roadmap document
2. 🔄 Implement `list_chats` with real data
3. ⏳ Implement `send_message`
4. ⏳ Implement `read_messages`
5. ⏳ Test with Claude Desktop
6. ⏳ Commit Phase 1 implementation

---

**Last Updated:** 2024-11-24
**Maintainer:** Claude Code
**Status:** 🔄 Phase 1 In Progress

# Discord-Inspired MCP Features for Tlgrm

**Date**: 2025-11-24
**Status**: Enhancement Plan
**Priority**: High

## Overview

Based on research of [Discord MCP implementations](https://github.com/v-3/discordmcp) and [MCP Server tutorials](https://www.speakeasy.com/blog/build-a-mcp-server-tutorial), this document outlines Discord-inspired features to enhance the Telegram MCP server.

## Current Implementation Status

### ✅ Implemented (5 Tools)

1. **list_chats** - List all available chats
2. **get_chat_info** - Get detailed chat information
3. **read_messages** - Read message history
4. **send_message** - Send text messages
5. **search_messages** - Search messages by query

## Discord MCP Feature Analysis

Discord MCP servers typically offer ~87 tools covering:
- Message management (send/read/delete/edit/react)
- Channel management (create/delete/organize)
- Server/user information
- Permissions and roles
- Webhooks and bots
- Media handling

## Priority Features to Implement

###  Priority 1: Message Management (Critical)

#### 1. delete_message
**Discord Equivalent**: Delete channel message
**Use Case**: Remove inappropriate messages, clean up after errors
**Implementation**:
```cpp
QJsonObject Server::toolDeleteMessage(const QJsonObject &args) {
    qint64 chatId = args["chat_id"].toVariant().toLongLong();
    qint64 messageId = args["message_id"].toVariant().toLongLong();

    if (!_session) {
        return errorResult("Session not available");
    }

    try {
        PeerId peerId(chatId);
        auto history = _session->data().history(peerId);
        if (!history) {
            return errorResult("Chat not found");
        }

        // Find and delete the message
        MsgId msgId(messageId);
        if (auto item = history->owner().message(peerId, msgId)) {
            item->destroy();  // Or use API for server-side deletion

            QJsonObject result;
            result["success"] = true;
            result["message_id"] = QString::number(messageId);
            result["chat_id"] = QString::number(chatId);
            return result;
        }

        return errorResult("Message not found");

    } catch (...) {
        return errorResult("Failed to delete message");
    }
}
```

**Priority**: ⭐⭐⭐⭐⭐ (Critical)
**Effort**: Low (1-2 hours)

#### 2. edit_message
**Discord Equivalent**: Edit sent message
**Use Case**: Fix typos, update information
**Implementation**:
```cpp
QJsonObject Server::toolEditMessage(const QJsonObject &args) {
    qint64 chatId = args["chat_id"].toVariant().toLongLong();
    qint64 messageId = args["message_id"].toVariant().toLongLong();
    QString newText = args["text"].toString();

    if (!_session) {
        return errorResult("Session not available");
    }

    try {
        PeerId peerId(chatId);
        MsgId msgId(messageId);

        // Use API to edit message
        Api::EditTextOptions options;
        options.text = TextWithTags{ newText };

        _session->api().editMessage(peerId, msgId, options);

        QJsonObject result;
        result["success"] = true;
        result["message_id"] = QString::number(messageId);
        result["new_text"] = newText;
        return result;

    } catch (...) {
        return errorResult("Failed to edit message");
    }
}
```

**Priority**: ⭐⭐⭐⭐⭐ (Critical)
**Effort**: Low (1-2 hours)

#### 3. forward_message
**Discord Equivalent**: Forward/copy message to another channel
**Use Case**: Share messages across chats
**Implementation**:
```cpp
QJsonObject Server::toolForwardMessage(const QJsonObject &args) {
    qint64 fromChatId = args["from_chat_id"].toVariant().toLongLong();
    qint64 toChatId = args["to_chat_id"].toVariant().toLongLong();
    qint64 messageId = args["message_id"].toVariant().toLongLong();

    if (!_session) {
        return errorResult("Session not available");
    }

    try {
        PeerId fromPeerId(fromChatId);
        PeerId toPeerId(toChatId);
        MsgId msgId(messageId);

        // Forward message via API
        auto fromHistory = _session->data().history(fromPeerId);
        auto toHistory = _session->data().history(toPeerId);

        if (!fromHistory || !toHistory) {
            return errorResult("Chat not found");
        }

        if (auto item = fromHistory->owner().message(fromPeerId, msgId)) {
            _session->api().forwardMessages({ item }, toHistory);

            QJsonObject result;
            result["success"] = true;
            result["from_chat_id"] = QString::number(fromChatId);
            result["to_chat_id"] = QString::number(toChatId);
            return result;
        }

        return errorResult("Message not found");

    } catch (...) {
        return errorResult("Failed to forward message");
    }
}
```

**Priority**: ⭐⭐⭐⭐ (High)
**Effort**: Medium (2-3 hours)

### Priority 2: Reactions & Interactions

#### 4. add_reaction
**Discord Equivalent**: Add reaction to message
**Use Case**: React to messages with emoji
**Implementation**:
```cpp
QJsonObject Server::toolAddReaction(const QJsonObject &args) {
    qint64 chatId = args["chat_id"].toVariant().toLongLong();
    qint64 messageId = args["message_id"].toVariant().toLongLong();
    QString emoji = args["emoji"].toString();

    if (!_session) {
        return errorResult("Session not available");
    }

    try {
        PeerId peerId(chatId);
        MsgId msgId(messageId);

        auto history = _session->data().history(peerId);
        if (auto item = history->owner().message(peerId, msgId)) {
            // Send reaction via API
            _session->api().sendReaction(item, emoji);

            QJsonObject result;
            result["success"] = true;
            result["message_id"] = QString::number(messageId);
            result["emoji"] = emoji;
            return result;
        }

        return errorResult("Message not found");

    } catch (...) {
        return errorResult("Failed to add reaction");
    }
}
```

**Priority**: ⭐⭐⭐⭐ (High)
**Effort**: Low (1-2 hours)

#### 5. remove_reaction
**Discord Equivalent**: Remove reaction from message
**Implementation**: Similar to add_reaction but removes
**Priority**: ⭐⭐⭐ (Medium)
**Effort**: Low (1 hour)

#### 6. pin_message
**Discord Equivalent**: Pin message in channel
**Use Case**: Highlight important messages
**Implementation**:
```cpp
QJsonObject Server::toolPinMessage(const QJsonObject &args) {
    qint64 chatId = args["chat_id"].toVariant().toLongLong();
    qint64 messageId = args["message_id"].toVariant().toLongLong();
    bool notify = args.value("notify").toBool(false);

    if (!_session) {
        return errorResult("Session not available");
    }

    try {
        PeerId peerId(chatId);
        MsgId msgId(messageId);

        auto history = _session->data().history(peerId);
        if (auto item = history->owner().message(peerId, msgId)) {
            _session->api().pinMessage(item, notify);

            QJsonObject result;
            result["success"] = true;
            result["message_id"] = QString::number(messageId);
            result["pinned"] = true;
            return result;
        }

        return errorResult("Message not found");

    } catch (...) {
        return errorResult("Failed to pin message");
    }
}
```

**Priority**: ⭐⭐⭐⭐ (High)
**Effort**: Low (1-2 hours)

#### 7. unpin_message
**Discord Equivalent**: Unpin message
**Implementation**: Similar to pin_message but unpins
**Priority**: ⭐⭐⭐ (Medium)
**Effort**: Low (1 hour)

### Priority 3: User & Member Management

#### 8. get_user_info
**Discord Equivalent**: Get user information
**Use Case**: Retrieve user profile details
**Implementation**:
```cpp
QJsonObject Server::toolGetUserInfo(const QJsonObject &args) {
    qint64 userId = args["user_id"].toVariant().toLongLong();

    if (!_session) {
        return errorResult("Session not available");
    }

    try {
        PeerId peerId(userId);
        auto user = _session->data().peer(peerId)->asUser();

        if (!user) {
            return errorResult("User not found");
        }

        QJsonObject userInfo;
        userInfo["id"] = QString::number(user->id.value);
        userInfo["first_name"] = user->firstName;
        userInfo["last_name"] = user->lastName;
        userInfo["username"] = user->username();
        userInfo["phone"] = user->phone();
        userInfo["is_bot"] = user->isBot();
        userInfo["is_verified"] = user->isVerified();
        userInfo["is_premium"] = user->isPremium();
        userInfo["is_scam"] = user->isScam();
        userInfo["is_fake"] = user->isFake();
        userInfo["about"] = user->about();

        return userInfo;

    } catch (...) {
        return errorResult("Failed to get user info");
    }
}
```

**Priority**: ⭐⭐⭐⭐ (High)
**Effort**: Low (1-2 hours)

#### 9. list_chat_members
**Discord Equivalent**: List members in server/channel
**Use Case**: Get all members of a group/channel
**Implementation**:
```cpp
QJsonObject Server::toolListChatMembers(const QJsonObject &args) {
    qint64 chatId = args["chat_id"].toVariant().toLongLong();
    int limit = args.value("limit").toInt(100);

    if (!_session) {
        return errorResult("Session not available");
    }

    try {
        PeerId peerId(chatId);
        auto peer = _session->data().peer(peerId);

        QJsonArray members;

        if (auto channel = peer->asChannel()) {
            // Request members list from API
            _session->api().requestParticipants(channel, limit);

            // Get loaded members
            for (const auto &member : channel->mgInfo->lastParticipants) {
                QJsonObject memberInfo;
                memberInfo["user_id"] = QString::number(member->userId().value);
                memberInfo["name"] = member->user()->name();
                memberInfo["username"] = member->user()->username();
                memberInfo["is_admin"] = member->isAdmin();
                members.append(memberInfo);
            }
        } else if (auto chat = peer->asChat()) {
            // Get chat members
            for (const auto &[userId, participant] : chat->participants) {
                QJsonObject memberInfo;
                memberInfo["user_id"] = QString::number(userId.value);
                memberInfo["is_admin"] = participant.isAdmin();
                members.append(memberInfo);
            }
        }

        QJsonObject result;
        result["members"] = members;
        result["count"] = members.size();
        result["chat_id"] = QString::number(chatId);
        return result;

    } catch (...) {
        return errorResult("Failed to list members");
    }
}
```

**Priority**: ⭐⭐⭐⭐ (High)
**Effort**: Medium (2-3 hours)

### Priority 4: Media & Rich Content

#### 10. send_photo
**Discord Equivalent**: Send image to channel
**Use Case**: Share photos in chats
**Implementation**:
```cpp
QJsonObject Server::toolSendPhoto(const QJsonObject &args) {
    qint64 chatId = args["chat_id"].toVariant().toLongLong();
    QString photoPath = args["photo_path"].toString();
    QString caption = args.value("caption").toString();

    if (!_session) {
        return errorResult("Session not available");
    }

    try {
        PeerId peerId(chatId);
        auto history = _session->data().history(peerId);

        // Create upload task
        Api::SendAction action(history);
        Api::SendOptions options;

        // Upload and send photo
        _session->api().sendFiles(
            { photoPath },
            QByteArray(),  // MIME type (auto-detect)
            caption,
            Api::SendType::Normal,
            options
        );

        QJsonObject result;
        result["success"] = true;
        result["chat_id"] = QString::number(chatId);
        result["type"] = "photo";
        return result;

    } catch (...) {
        return errorResult("Failed to send photo");
    }
}
```

**Priority**: ⭐⭐⭐⭐ (High)
**Effort**: Medium (3-4 hours)

#### 11. send_file
**Discord Equivalent**: Send file attachment
**Priority**: ⭐⭐⭐ (Medium)
**Effort**: Medium (2-3 hours)

#### 12. send_voice
**Discord Equivalent**: Send voice message
**Priority**: ⭐⭐⭐ (Medium)
**Effort**: Medium (3-4 hours)

### Priority 5: Group Management

#### 13. create_group
**Discord Equivalent**: Create new server/channel
**Use Case**: Programmatically create groups
**Implementation**:
```cpp
QJsonObject Server::toolCreateGroup(const QJsonObject &args) {
    QString title = args["title"].toString();
    QJsonArray userIds = args["users"].toArray();

    if (!_session) {
        return errorResult("Session not available");
    }

    try {
        // Convert user IDs to peer list
        std::vector<not_null<UserData*>> users;
        for (const auto &idValue : userIds) {
            qint64 userId = idValue.toVariant().toLongLong();
            PeerId peerId(userId);
            if (auto user = _session->data().peer(peerId)->asUser()) {
                users.push_back(user);
            }
        }

        // Create group via API
        _session->api().createGroup(title, users);

        QJsonObject result;
        result["success"] = true;
        result["title"] = title;
        result["member_count"] = users.size();
        return result;

    } catch (...) {
        return errorResult("Failed to create group");
    }
}
```

**Priority**: ⭐⭐⭐ (Medium)
**Effort**: Medium (3-4 hours)

#### 14. add_chat_member
**Discord Equivalent**: Add member to server
**Priority**: ⭐⭐⭐ (Medium)
**Effort**: Medium (2-3 hours)

#### 15. remove_chat_member
**Discord Equivalent**: Kick/remove member
**Priority**: ⭐⭐ (Low)
**Effort**: Medium (2-3 hours)

#### 16. set_chat_admin
**Discord Equivalent**: Manage roles/permissions
**Priority**: ⭐⭐ (Low)
**Effort**: High (4-6 hours)

### Priority 6: Advanced Features

#### 17. create_poll
**Discord Equivalent**: Create poll/vote
**Priority**: ⭐⭐⭐ (Medium)
**Effort**: Medium (3-4 hours)

#### 18. get_chat_statistics
**Discord Equivalent**: Server/channel statistics
**Priority**: ⭐⭐ (Low)
**Effort**: Medium (3-4 hours)

#### 19. export_chat_history
**Discord Equivalent**: Export channel history
**Priority**: ⭐⭐ (Low)
**Effort**: High (5-6 hours)

## Implementation Roadmap

### Phase 1: Critical Message Management (Week 1)
- [x] Existing: list_chats, get_chat_info, read_messages, send_message, search_messages
- [ ] delete_message
- [ ] edit_message
- [ ] forward_message

**Total Effort**: 5-7 hours
**Impact**: High - Core Discord-like functionality

### Phase 2: Reactions & Interactions (Week 1-2)
- [ ] add_reaction
- [ ] remove_reaction
- [ ] pin_message
- [ ] unpin_message

**Total Effort**: 4-6 hours
**Impact**: High - Enhanced engagement

### Phase 3: User Management (Week 2)
- [ ] get_user_info
- [ ] list_chat_members

**Total Effort**: 3-5 hours
**Impact**: High - Essential for group management

### Phase 4: Media Support (Week 2-3)
- [ ] send_photo
- [ ] send_file
- [ ] send_voice

**Total Effort**: 8-11 hours
**Impact**: High - Rich content support

### Phase 5: Group Management (Week 3-4)
- [ ] create_group
- [ ] add_chat_member
- [ ] remove_chat_member
- [ ] set_chat_admin

**Total Effort**: 11-16 hours
**Impact**: Medium - Advanced features

### Phase 6: Advanced Features (Week 4+)
- [ ] create_poll
- [ ] get_chat_statistics
- [ ] export_chat_history

**Total Effort**: 11-14 hours
**Impact**: Low - Nice-to-have

## Total Estimate

**New Tools**: 18 additional tools
**Total Effort**: 42-59 hours (5-7 working days)
**Priority Distribution**:
- Critical (⭐⭐⭐⭐⭐): 2 tools
- High (⭐⭐⭐⭐): 8 tools
- Medium (⭐⭐⭐): 6 tools
- Low (⭐⭐): 2 tools

## Implementation Guidelines

### Code Patterns

All tools should follow this pattern:

```cpp
QJsonObject Server::toolXxx(const QJsonObject &args) {
    // 1. Extract and validate parameters
    // 2. Check session availability
    // 3. Try-catch for error handling
    // 4. Access Telegram data via _session
    // 5. Return structured JSON result
    // 6. Log all operations
}
```

### Error Handling

```cpp
QJsonObject errorResult(const QString &message) {
    QJsonObject result;
    result["success"] = false;
    result["error"] = message;
    return result;
}
```

### Testing Strategy

For each new tool:
1. Unit test with mock session
2. Integration test with real Telegram data
3. Claude Desktop end-to-end test
4. Performance profiling

### Documentation

Each tool needs:
- JSON-RPC method signature
- Parameter descriptions
- Return value format
- Usage examples
- Error cases

## Sources

- [Building a Model Context Protocol (MCP) server for Discord](https://www.speakeasy.com/blog/build-a-mcp-server-tutorial)
- [Discord MCP Server by v-3](https://github.com/v-3/discordmcp)
- [MCP Discord Integration by Composio](https://mcp.composio.dev/discord)
- [Discord MCP - Claude MCP Servers](https://www.claudemcp.com/servers/discord-mcp)

## Conclusion

Implementing these Discord-inspired features will transform the Telegram MCP server into a comprehensive messaging automation platform comparable to Discord MCP servers, enabling:

- Full message lifecycle management
- Rich interactions (reactions, pins)
- Complete user/member management
- Multi-media support
- Advanced group administration

The phased approach ensures critical features are delivered first while maintaining code quality and test coverage.

---

**Next Steps**: Begin Phase 1 implementation with delete_message, edit_message, and forward_message tools.

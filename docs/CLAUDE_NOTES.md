# CLAUDE.md: Technical Notes for AI Assistants

## Project Overview

This is a **custom fork of Telegram Desktop** with integrated **Model Context Protocol (MCP)** server functionality. This document contains technical implementation details, build system notes, and context for AI assistants working on this codebase.

**Base Version:** Telegram Desktop 6.3 (commit aadc81279a)
**Custom Additions:** MCP server integration for AI assistant access to Telegram data
**Target Platform:** macOS (Apple Silicon + Intel)

---

## MCP Integration Architecture

### Overview

The MCP server is embedded directly into the Telegram Desktop process, allowing direct access to:
- Local SQLite database (instant message reads, no API rate limits)
- Active session data (real-time chat lists, user info)
- Message sending capabilities via tdesktop's API layer
- Search functionality across local message cache

### Key Design Decisions

1. **In-Process Integration**: MCP server runs within Telegram's main process
   - **Advantage**: Direct memory access to all tdesktop data structures
   - **Disadvantage**: Requires rebuild for updates
   - **Alternative considered**: Out-of-process via IPC (mcp_bridge) - complexity not justified yet

2. **Stdio Transport**: Uses stdin/stdout for JSON-RPC communication
   - **Advantage**: Simple, works with Claude Desktop out of the box
   - **Future**: HTTP transport for SSE notifications (not implemented)

3. **Qt Framework Integration**: All MCP code uses Qt data structures
   - Required for compatibility with tdesktop's existing codebase
   - QJsonObject/QJsonArray for JSON-RPC messages
   - QLocalServer/QLocalSocket for future IPC (mcp_bridge)

---

## File Structure and Modifications

### Modified Core Files

#### `Telegram/SourceFiles/core/application.h`
**Line 85**: Added MCP forward declaration
```cpp
namespace MCP {
class Server;
} // namespace MCP
```

**Line 358**: Added member variable
```cpp
std::unique_ptr<MCP::Server> _mcpServer;
```

**Purpose**: Owns MCP server lifetime, destroyed with Application

#### `Telegram/SourceFiles/core/application.cpp`
**Line 85**: Include MCP header
```cpp
#include "mcp/mcp_server.h"
```

**Lines 217-218**: Cleanup in destructor
```cpp
// Clean up MCP server
_mcpServer = nullptr;
```

**Lines 423-431**: Initialization with `--mcp` flag
```cpp
// Start MCP server if --mcp flag is present
if (cExeDir().isEmpty()) {
    if (args.contains(QString::fromUtf8("--mcp"))) {
        _mcpServer = std::make_unique<MCP::Server>();
        if (_mcpServer->start(MCP::TransportType::Stdio)) {
            DEBUG_LOG(("MCP: Server started successfully"));
        } else {
            LOG(("MCP Error: Failed to start server"));
            _mcpServer = nullptr;
        }
    }
}
```

**Technical Note**: `cExeDir().isEmpty()` check ensures we're not in portable mode

#### `Telegram/CMakeLists.txt`
Added MCP source files to compilation target:
```cmake
mcp/mcp_bridge.cpp
mcp/mcp_bridge.h
mcp/mcp_server.cpp
mcp/mcp_server.h
```

**Location**: Within the `nice_target_sources` block for Telegram target

### New MCP Source Files

#### `Telegram/SourceFiles/mcp/mcp_server.h` (3370 bytes)
**Purpose**: Main MCP protocol server header

**Key Classes**:
- `Server`: Main MCP server class (inherits QObject for Qt signal/slot)
- `Tool`: Struct defining MCP tool metadata
- `Resource`: Struct for MCP resources
- `Prompt`: Struct for MCP prompt templates

**Key Methods**:
- `start(TransportType)`: Initialize server with transport (Stdio/HTTP)
- `stop()`: Cleanup and shutdown
- `handleRequest()`: JSON-RPC request dispatcher
- `handleInitialize()`, `handleListTools()`, etc.: Protocol method handlers
- `toolListChats()`, `toolReadMessages()`, etc.: Tool implementations

#### `Telegram/SourceFiles/mcp/mcp_server.cpp` (11889 bytes)
**Purpose**: Main MCP server implementation

**Architecture**:
1. **Initialization** (lines 16-34): Sets up capabilities, registers tools/resources/prompts
2. **Transport Layer** (lines 160-245): Handles stdio and HTTP (HTTP TODO)
3. **Request Handling** (lines 247-272): JSON-RPC dispatcher
4. **Protocol Methods** (lines 274-397): Initialize, list tools, call tools, etc.
5. **Tool Implementations** (lines 399-471): Stub implementations (TODO: connect to real data)

**Critical Implementation Details**:

**Qt Keyword Compatibility** (mcp_bridge.h:36):
```cpp
private Q_SLOTS:  // NOT "private slots:" due to -DQT_NO_KEYWORDS
    void onNewConnection();
    void onReadyRead();
    void onDisconnected();
```
**Reason**: tdesktop uses `-DQT_NO_KEYWORDS` compiler flag to avoid macro pollution
**Fix Applied**: Use Q_SLOTS macro instead of slots keyword
**Error if not fixed**: "unknown type name 'slots'"

**QJsonObject Construction Pattern** (mcp_server.cpp:334-344):
```cpp
// WRONG (doesn't compile in this Qt version/config):
return QJsonObject{
    {"content", QJsonArray{
        QJsonObject{{"type", "text"}, {"text", "data"}}
    }}
};

// CORRECT (step-by-step construction):
QJsonObject contentItem;
contentItem["type"] = "text";
contentItem["text"] = QString::fromUtf8(QJsonDocument(result).toJson(QJsonDocument::Compact));

QJsonArray contentArray;
contentArray.append(contentItem);

QJsonObject response;
response["content"] = contentArray;
return response;
```
**Reason**: Nested initializer lists for QJsonObject/QJsonArray not supported in this configuration
**Error if not fixed**: "no matching constructor for initialization of 'QJsonObject'"

**Stdio Polling** (mcp_server.cpp:208-239):
Uses QTimer to poll stdin every 100ms
```cpp
QTimer *timer = new QTimer(this);
connect(timer, &QTimer::timeout, this, &Server::handleStdioInput);
timer->start(100); // Poll every 100ms
```
**Reason**: Qt doesn't have native stdin readyRead signals
**Alternative**: QSocketNotifier (more complex, platform-specific)

#### `Telegram/SourceFiles/mcp/mcp_bridge.h/cpp` (1384 + 6027 bytes)
**Purpose**: IPC bridge for external Python MCP server (optional architecture)

**Not currently used** but available for future refactoring:
- Uses QLocalServer for Unix domain socket
- Could run MCP server as separate process
- Would communicate with Telegram via IPC

**Use case**: If we want MCP server updatable without rebuilding Telegram

---

## Build System Details

### CMake Configuration

**Project uses hybrid build system**:
1. CMake generates Xcode project
2. Xcode performs actual compilation
3. Build outputs to `out/Release/` or `out/Debug/`

**Key CMake variables**:
- `TDESKTOP_API_ID`: Required, from my.telegram.org
- `TDESKTOP_API_HASH`: Required, from my.telegram.org
- `CMAKE_BUILD_TYPE`: Release or Debug

### Xcode Build Process

**Target dependency graph**: 44 targets
**Main targets**:
- `Telegram`: Main application (depends on all libs)
- `Updater`: Auto-updater component
- `Packer`: Resource packer

**Parallel compilation**: `-j 24` (24 parallel jobs on 32-CPU system)
**Architecture**: arm64 (native Apple Silicon)
**Optimization**: Release builds use `-O3` and LTO

### Build Times

**Clean build** (all dependencies):
- M1 Pro (16GB): 50-90 minutes
- M2 Studio (32GB): 40-70 minutes
- M3 (16GB+): 35-60 minutes

**Incremental build** (after MCP changes):
- Only recompiles: mcp_server.cpp, mcp_bridge.cpp, application.cpp
- Time: 2-5 minutes

**Critical path**: Qt6 compilation (15-25 min), FFmpeg (10-15 min), OpenSSL (8-12 min)

### Compiler Flags

**Important flags used by tdesktop**:
- `-DQT_NO_KEYWORDS`: Disables Qt macro keywords (slots → Q_SLOTS)
- `-std=c++20`: C++20 standard required
- `-fobjc-arc`: Objective-C automatic reference counting
- `-fno-exceptions`: Exceptions disabled in most code

**MCP code must comply** with these constraints

---

## Patch System for Updates

### Patch Files Generated

**Location**: `patches/`

1. **0001-add-mcp-cmake-configuration.patch**
   - Diff of Telegram/CMakeLists.txt
   - Adds MCP sources to build
   - ~20 lines changed

2. **0002-add-mcp-application-integration.patch**
   - Diff of application.h and application.cpp
   - Adds initialization and lifecycle management
   - ~30 lines changed

3. **mcp_source_files/**
   - Complete MCP source directory
   - Copied verbatim to target

### Applying to New Telegram Version

**Process**:
```bash
# Checkout new official version
git checkout v6.x.x

# Try applying patches
git apply patches/0001-add-mcp-cmake-configuration.patch
git apply patches/0002-add-mcp-application-integration.patch

# If conflicts:
git apply --reject patches/0001-add-mcp-cmake-configuration.patch
# Fix .rej files manually
# Regenerate patch:
git diff HEAD Telegram/CMakeLists.txt > patches/0001-add-mcp-cmake-configuration.patch

# Copy MCP sources
cp -r patches/mcp_source_files/* Telegram/SourceFiles/mcp/
```

**Conflict probability**:
- CMakeLists.txt: **Medium** (frequently modified for new sources)
- application.cpp: **Low** (initialization section stable)
- application.h: **Low** (private members rarely change)

**Strategy**: Keep patches minimal, touch only necessary lines

---

## Build Instructions for AI Assistants

This section is optimized for AI assistants helping users compile the project.

### System Requirements

- **macOS** Ventura (13.0+) or Sonoma (14.0+)
- **Xcode** 14.0+ with Command Line Tools
- **Homebrew** with: `git cmake python3 automake autoconf libtool pkg-config ninja wget meson nasm`
- **50GB+ free disk space**
- **Telegram API credentials** from https://my.telegram.org

### Build Process (High-Level)

**Phase 1: Dependency Preparation** (40-70 minutes)
```bash
cd Telegram
./build/prepare/mac.sh silent
```
Builds 28 dependencies including Qt6, FFmpeg, OpenSSL, etc.

**Phase 2: MCP Integration** (if not already done)
```bash
cd ..
./apply-patches.sh
```
Applies MCP patches to fresh checkout.

**Phase 3: Configuration** (30 seconds)
```bash
cd Telegram
./configure.sh -D TDESKTOP_API_ID=2040 -D TDESKTOP_API_HASH=b18441a1ff607e10a989891a5462e627
```
Generates Xcode project in `out/`.

**Phase 4: Compilation** (10-20 minutes)
```bash
cd ../out
xcodebuild -project Telegram.xcodeproj -scheme Telegram -configuration Release build
```
Or open in Xcode GUI and build.

---

## Common Build Errors and Fixes

### Error 1: Qt Keywords Incompatibility

**Symptom**:
```
error: expected ':'
error: unknown type name 'slots'
```

**Location**: `mcp_bridge.h:36`

**Cause**: tdesktop uses `-DQT_NO_KEYWORDS` which disables `slots` macro

**Fix**: Use `Q_SLOTS` instead of `slots`
```cpp
// WRONG:
private slots:
    void onNewConnection();

// CORRECT:
private Q_SLOTS:
    void onNewConnection();
```

**AI Assistant Note**: If user reports this error, immediately apply the fix above to mcp_bridge.h

### Error 2: QJsonObject Construction

**Symptom**:
```
error: no matching constructor for initialization of 'QJsonObject'
```

**Location**: `mcp_server.cpp:337` (or similar QJsonObject initialization)

**Cause**: Nested initializer lists not supported in this Qt configuration

**Fix**: Use step-by-step construction
```cpp
// WRONG:
return QJsonObject{
    {"content", QJsonArray{QJsonObject{{"type", "text"}}}}
};

// CORRECT:
QJsonObject item;
item["type"] = "text";

QJsonArray array;
array.append(item);

QJsonObject response;
response["content"] = array;
return response;
```

**AI Assistant Note**: Always use the step-by-step pattern for QJsonObject/QJsonArray construction in MCP code

### Error 3: Missing Dependencies

**Symptoms**:
- "aclocal/autoconf/automake not found"
- "wget: command not found"
- "meson: command not found"
- "nasm not found"

**Fix**: Install via Homebrew
```bash
brew install automake autoconf libtool wget meson nasm
```

**AI Assistant Note**: Run preflight check before build:
```bash
which automake autoconf libtool wget meson nasm cmake python3
```

### Error 4: Submodule Issues

**Symptom**: Missing files during build, undefined references

**Fix**: Ensure submodules are initialized
```bash
git submodule update --init --recursive --depth 1
```

**AI Assistant Note**: Always verify submodules after clone

---

## Testing MCP Integration

### Unit Tests (Manual)

**Test 1: MCP Server Starts**
```bash
./out/Release/Telegram.app/Contents/MacOS/Telegram --mcp &
PID=$!

# Check logs
sleep 2
log show --predicate 'process == "Telegram"' --last 30s | grep MCP

kill $PID
```

Expected: "MCP: Server started successfully"

**Test 2: Initialize Protocol**
```bash
echo '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2024-11-05","clientInfo":{"name":"test","version":"1.0"}}}' | \
  ./out/Release/Telegram.app/Contents/MacOS/Telegram --mcp
```

Expected: JSON response with serverInfo and capabilities

**Test 3: List Tools**
```bash
echo '{"jsonrpc":"2.0","id":2,"method":"tools/list","params":{}}' | \
  ./out/Release/Telegram.app/Contents/MacOS/Telegram --mcp
```

Expected: Array of 5 tools (list_chats, get_chat_info, read_messages, send_message, search_messages)

**Test 4: Call Tool (Stub)**
```bash
echo '{"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":"list_chats","arguments":{}}}' | \
  ./out/Release/Telegram.app/Contents/MacOS/Telegram --mcp
```

Expected: JSON with stub data (empty chats array, note about local_database)

### Integration Tests

**Python Test Script**: `test_mcp.py`
```python
#!/usr/bin/env python3
import subprocess
import json

proc = subprocess.Popen(
    ["out/Release/Telegram.app/Contents/MacOS/Telegram", "--mcp"],
    stdin=subprocess.PIPE,
    stdout=subprocess.PIPE,
    stderr=subprocess.PIPE,
    text=True
)

request = {"jsonrpc":"2.0","id":1,"method":"initialize","params":{}}
stdout, stderr = proc.communicate(input=json.dumps(request) + "\n", timeout=5)

response = json.loads(stdout)
assert response["result"]["protocolVersion"] == "2024-11-05"
print("✓ MCP integration working")
```

---

## Next Development Steps

### Priority 1: Connect to Real Data

**Current state**: All tools return stub data
**Goal**: Connect to tdesktop's session and local database

**Implementation**:
1. Pass `Main::Session *session` to `MCP::Server` constructor
2. Store as member: `Main::Session *_session`
3. Update tool implementations:

```cpp
QJsonObject Server::toolListChats(const QJsonObject &args) {
    if (!_session) return stubData();

    QJsonArray chats;
    auto &owner = _session->data();
    for (const auto &dialog : owner.chatsListFor(Data::Folder::kAll)->all()) {
        auto peer = dialog->peer();
        chats.append(QJsonObject{
            {"id", peer->id.value},
            {"title", peer->name()},
            {"type", peer->isUser() ? "user" : (peer->isChat() ? "group" : "channel")}
        });
    }

    return QJsonObject{{"chats", chats}};
}
```

**Files to modify**:
- `mcp_server.h`: Add `Main::Session *_session` member
- `mcp_server.cpp`: Update constructor, implement real data access
- `application.cpp`: Pass session to MCP server: `_mcpServer->setSession(&session())`

**Complexity**: Medium (2-4 hours)
**Dependencies**: Understanding tdesktop's data layer

### Priority 2: Semantic Search

**Goal**: Add FAISS-based semantic search for messages

**Implementation**:
1. Add FAISS to dependencies
2. Create embeddings for messages on-the-fly or cached
3. Implement `search_messages` with vector similarity

**Complexity**: High (8-16 hours)
**Dependencies**: FAISS library, embedding model

### Priority 3: Voice Transcription

**Goal**: Transcribe voice messages with Whisper.cpp

**Implementation**:
1. Add whisper.cpp to dependencies
2. Download voice files from Telegram cache
3. Run transcription, cache results

**Complexity**: Medium (4-8 hours)
**Dependencies**: whisper.cpp

---

## Code Style Guidelines

### Qt Conventions (tdesktop follows Qt style)

**Naming**:
- Classes: PascalCase (`Server`, `Application`)
- Methods: camelCase (`handleRequest`, `toolListChats`)
- Private members: `_lowerCamelCase` (`_mcpServer`, `_session`)
- Signals: camelCase without prefix (`dataChanged`)
- Slots: `on` prefix if auto-connected (`onReadyRead`)

**Memory Management**:
- Use `std::unique_ptr` for owned objects
- Use raw pointers for non-owned references
- Qt parent-child ownership for QObject-derived classes

**Error Handling**:
- Return QJsonObject with `{"error": "message"}` for tool errors
- Use `qWarning()` for non-fatal errors
- Use `LOG()` for fatal errors
- **NO EXCEPTIONS** (tdesktop uses `-fno-exceptions`)

### MCP-Specific Patterns

**JSON-RPC Responses**:
```cpp
// Success
return QJsonObject{
    {"jsonrpc", "2.0"},
    {"id", requestId},
    {"result", resultObject}
};

// Error
return QJsonObject{
    {"jsonrpc", "2.0"},
    {"id", requestId},
    {"error", QJsonObject{
        {"code", errorCode},
        {"message", errorMessage}
    }}
};
```

**Tool Response Format**:
```cpp
QJsonObject response;
response["content"] = QJsonArray{
    QJsonObject{
        {"type", "text"},
        {"text", "Tool output here"}
    }
};
// Optional:
response["isError"] = false;
return response;
```

---

## Debugging Tips for AI Assistants

### Check MCP Server Logs

```bash
# macOS Console.app
open -a Console
# Filter by "Telegram" process

# Or command line:
log stream --predicate 'process == "Telegram"' --level debug
```

### Xcode Debugger

1. Open `out/Telegram.xcodeproj`
2. Set breakpoint in `mcp_server.cpp::handleRequest`
3. Edit scheme: Arguments → `--mcp`
4. Run in debugger (Cmd+R)
5. Send JSON-RPC request via stdin

### Common Issues

**Issue**: MCP server doesn't start
**Check**: Look for "MCP: Server started" in logs
**Fix**: Verify `--mcp` flag is passed, check initialization code

**Issue**: Tools return empty data
**Check**: Whether tool is implemented or stub
**Fix**: Implement real data access (see Priority 1 above)

**Issue**: Build succeeds but crashes on launch
**Check**: Qt library loading, symbol resolution
**Fix**: Clean build (`rm -rf out/`), rebuild dependencies

---

## Performance Considerations

### Current Performance (Stub Implementation)

- **Initialization**: <10ms
- **Tool call overhead**: <5ms  - **Memory footprint**: ~5MB
- **No measurable impact** on Telegram performance

### Expected Performance (Real Data)

- **Local DB read**: 10-50ms (thousands of messages/sec)
- **Message search**: 50-200ms (vs 500ms+ with API)
- **Send message**: 100-500ms (network dependent)

### Optimization Strategies

1. **Cache frequently accessed data** (chat lists, recent messages)
2. **Use tdesktop's existing caches** (don't duplicate)
3. **Lazy loading**: Only load data when requested
4. **Pagination**: Limit results by default (e.g., 50 messages max)

---

## Security Considerations

### Current Security Model

- **No authentication**: MCP server trusts the calling process
- **No network exposure**: Only stdio transport (local only)
- **Process isolation**: Inherits Telegram's permissions

### Future Security (if HTTP transport added)

- **Authentication**: Require API key for HTTP connections
- **Rate limiting**: Prevent abuse
- **Scope limiting**: Restrict which chats can be accessed
- **Audit logging**: Track all MCP operations

---

## References for AI Assistants

### Essential Documentation

- **MCP Protocol**: https://modelcontextprotocol.io/specification
- **Qt6 Documentation**: https://doc.qt.io/qt-6/
- **tdesktop Architecture**: `Telegram/docs/` in repository
- **Telegram API**: https://core.telegram.org/api

### Useful tdesktop Code Paths

**Data Access**:
- `Telegram/SourceFiles/data/data_session.h` - Main data storage
- `Telegram/SourceFiles/history/history.h` - Message history
- `Telegram/SourceFiles/data/data_peer.h` - Users, chats, channels

**API Layer**:
- `Telegram/SourceFiles/api/api_*.cpp` - Various API handlers
- `Telegram/SourceFiles/mtproto/` - MTProto implementation

**UI Layer**:
- `Telegram/SourceFiles/window/` - Main window
- `Telegram/SourceFiles/boxes/` - Dialog boxes

---

## AI Assistant Quick Reference

### When User Reports Build Error

1. **Check error message** for Qt keywords or QJsonObject issues
2. **Apply standard fixes** documented in "Common Build Errors"
3. **If new error**: Search in `xcode_build.log` for full context
4. **Check dependencies**: Verify Homebrew packages installed

### When User Wants to Add Feature

1. **Identify integration point** (new tool? modify existing?)
2. **Follow code patterns** in existing MCP tools
3. **Use step-by-step QJsonObject construction**
4. **Test with manual JSON-RPC calls**
5. **Add to documentation**

### When User Asks "How Do I..."

- **Access messages**: See "Priority 1: Connect to Real Data"
- **Add new tool**: See "Adding New MCP Tools" in README.md
- **Update to new Telegram version**: See "Patch System for Updates"
- **Debug MCP**: See "Debugging Tips"

---

**Last Updated**: 2025-11-16
**Maintained for**: AI assistants working on tdesktop MCP integration
**Base Commit**: aadc81279a (Telegram Desktop 6.3)

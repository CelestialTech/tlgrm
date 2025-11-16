# MCP Server Integration Notes

## Files Modified

### 1. CMakeLists.txt
Added MCP source files at line ~1210:
```cmake
mcp/mcp_bridge.cpp
mcp/mcp_bridge.h
mcp/mcp_server.cpp
mcp/mcp_server.h
```

### 2. core/application.h
Added:
- Forward declaration: `namespace MCP { class Server; }`
- Public getter: `mcpServer()` method
- Private member: `std::unique_ptr<MCP::Server> _mcpServer;`

### 3. core/application.cpp  (TO DO)

Add to includes:
```cpp
#include "mcp/mcp_server.h"
```

Add initialization in `Application::run()` method (after domain/session setup):
```cpp
// Start MCP server if --mcp flag is present
const auto args = QCoreApplication::arguments();
if (args.contains(u"--mcp"_q)) {
    _mcpServer = std::make_unique<MCP::Server>();
    if (_mcpServer->start(MCP::TransportType::Stdio)) {
        LOG(("MCP: Server started successfully"));
    } else {
        LOG(("MCP Error: Failed to start server"));
        _mcpServer = nullptr;
    }
}
```

Add cleanup in destructor:
```cpp
// In Application::~Application()
_mcpServer = nullptr;  // Clean shutdown
```

## Command-Line Usage

Run Telegram Desktop with MCP enabled:
```bash
# On macOS
/Applications/Telegram.app/Contents/MacOS/Telegram --mcp

# Or from build directory
./out/Release/Telegram.app/Contents/MacOS/Telegram --mcp
```

## Claude Desktop Integration

Add to `~/Library/Application Support/Claude/claude_desktop_config.json`:
```json
{
  "mcpServers": {
    "telegram": {
      "command": "/Applications/Telegram.app/Contents/MacOS/Telegram",
      "args": ["--mcp"]
    }
  }
}
```

## Next Steps

1. ✅ Add to CMakeLists.txt
2. ✅ Modify application.h
3. ⏳ Modify application.cpp (add initialization code above)
4. ⏳ Connect to Main::Session data
5. ⏳ Implement actual message retrieval
6. ⏳ Build and test

## Connecting to Real Data

The MCP server needs access to:

### Session Data
```cpp
// In mcp_server.cpp, add session pointer:
void Server::setSession(not_null<Main::Session*> session) {
    _session = session;
}
```

### Message History
```cpp
// In toolReadMessages():
if (!_session) return errorResult("No session");

const auto peer = _session->data().peer(PeerId(chatId));
const auto history = _session->data().history(peer);

// Get messages
QJsonArray messages;
for (const auto &item : history->messages()) {
    messages.append(formatMessage(item));
}
```

### Formatting Messages
```cpp
QJsonObject formatMessage(not_null<HistoryItem*> item) {
    return QJsonObject{
        {"message_id", qint64(item->id)},
        {"date", item->date().toString(Qt::ISODate)},
        {"text", item->originalText().text},
        {"from_user", QJsonObject{
            {"id", qint64(item->from()->id.value)},
            {"name", item->from()->name()}
        }}
    };
}
```

## Build Command

```bash
cd ~/xCode/tlgrm/tdesktop
./configure.sh -D TDESKTOP_API_ID=2040 -D TDESKTOP_API_HASH=b18441a1ff607e10a989891a5462e627

# Then build in Xcode or:
cd out
cmake --build . --config Release
```

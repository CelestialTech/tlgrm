# Building Telegram Desktop with MCP Server

## ✅ Integration Complete!

All code changes have been made. Ready to build!

### Files Modified:
1. ✅ `CMakeLists.txt` - Added MCP source files
2. ✅ `SourceFiles/core/application.h` - Added MCP server member
3. ✅ `SourceFiles/core/application.cpp` - Added initialization and cleanup

### Files Created:
1. ✅ `SourceFiles/mcp/mcp_server.h` - MCP protocol implementation
2. ✅ `SourceFiles/mcp/mcp_server.cpp` - Server logic
3. ✅ `SourceFiles/mcp/mcp_bridge.h` - IPC bridge (optional)
4. ✅ `SourceFiles/mcp/mcp_bridge.cpp` - IPC implementation

## 🚀 Build Instructions

### Step 1: Configure the Build

```bash
cd ~/xCode/tlgrm/tdesktop

# Run configure script
./configure.sh \
  -D TDESKTOP_API_ID=2040 \
  -D TDESKTOP_API_HASH=b18441a1ff607e10a989891a5462e627
```

**Expected output:**
```
Configuring...
Generating Xcode project...
Done!
```

### Step 2: Open in Xcode

```bash
# Open the generated project
open out/Telegram.xcodeproj
```

### Step 3: Build in Xcode

1. Select scheme: **Telegram** (not Updater or Packer)
2. Select build config: **Release** or **Debug**
   - Menu: Product → Scheme → Edit Scheme
   - Build Configuration: Release
3. Build: **Cmd+B** or Product → Build

**Build time**: 10-30 minutes (first time), 2-5 minutes (incremental)

### Alternative: Command-Line Build

```bash
cd ~/xCode/tlgrm/tdesktop/out

# Build Release
cmake --build . --config Release

# Or Debug
cmake --build . --config Debug
```

## 🧪 Testing the Build

### Test 1: Run with MCP Enabled

```bash
cd ~/xCode/tlgrm/tdesktop

# From build output directory
./out/Release/Telegram.app/Contents/MacOS/Telegram --mcp
```

**Expected output in logs:**
```
MCP: Server started successfully
```

**Logs location**:
- macOS: Console.app → Filter by "Telegram"
- Or check: `~/Library/Logs/Telegram Desktop/`

### Test 2: Basic MCP Communication

```bash
# Test stdio transport
echo '{"id":1,"method":"initialize","params":{}}' | \
  ./out/Release/Telegram.app/Contents/MacOS/Telegram --mcp
```

**Expected response:**
```json
{"jsonrpc":"2.0","id":1,"result":{"protocolVersion":"2024-11-05","serverInfo":{"name":"Telegram Desktop MCP","version":"1.0.0"}}}
```

### Test 3: List Tools

```bash
echo '{"id":2,"method":"tools/list","params":{}}' | \
  ./out/Release/Telegram.app/Contents/MacOS/Telegram --mcp
```

**Should show**: list_chats, read_messages, send_message, etc.

## 🔧 Troubleshooting Build Errors

### Error: "mcp_server.h: No such file or directory"

**Cause**: CMakeLists.txt not updated correctly

**Fix**:
```bash
# Verify MCP files are in CMakeLists.txt
grep "mcp/" ~/xCode/tlgrm/tdesktop/Telegram/CMakeLists.txt
```

Should show:
```cmake
mcp/mcp_bridge.cpp
mcp/mcp_bridge.h
mcp/mcp_server.cpp
mcp/mcp_server.h
```

### Error: "'MCP::Server' has not been declared"

**Cause**: Missing forward declaration

**Fix**: Check `core/application.h` has:
```cpp
namespace MCP {
class Server;
} // namespace MCP
```

### Error: Build fails with "undefined reference to MCP::Server"

**Cause**: MCP source files not compiled

**Fix**:
1. Clean build: Product → Clean Build Folder (Cmd+Shift+K)
2. Re-run configure: `./configure.sh ...`
3. Rebuild

### Error: "Cannot find u"--mcp"_q"

**Cause**: Wrong Qt version or string literal issue

**Fix**: Change in `application.cpp`:
```cpp
// From:
if (args.contains(u"--mcp"_q)) {

// To:
if (args.contains(QString::fromUtf8("--mcp"))) {
```

## 📦 Installation

### Option 1: Copy to Applications

```bash
# After successful build
cp -r out/Release/Telegram.app /Applications/

# Run
/Applications/Telegram.app/Contents/MacOS/Telegram --mcp
```

### Option 2: Create Symlink

```bash
# Link build output to Applications
ln -s ~/xCode/tlgrm/tdesktop/out/Release/Telegram.app /Applications/Telegram-MCP.app
```

## 🔗 Claude Desktop Integration

### Step 1: Configure Claude

Edit: `~/Library/Application Support/Claude/claude_desktop_config.json`

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

### Step 2: Restart Claude Desktop

```bash
# Kill existing Claude processes
killall "Claude"

# Reopen Claude Desktop
open -a "Claude"
```

### Step 3: Test in Claude

In Claude chat, try:
```
Can you list my Telegram chats?
```

Claude should use the `list_chats` tool!

## 📊 Build Configuration Options

### Debug Build (for development)

```bash
./configure.sh \
  -D TDESKTOP_API_ID=2040 \
  -D TDESKTOP_API_HASH=b18441a1ff607e10a989891a5462e627 \
  -D CMAKE_BUILD_TYPE=Debug
```

**Benefits**:
- Debug symbols
- Easier debugging
- More verbose logging

**Drawbacks**:
- Slower performance
- Larger binary size

### Release Build (for production)

```bash
./configure.sh \
  -D TDESKTOP_API_ID=2040 \
  -D TDESKTOP_API_HASH=b18441a1ff607e10a989891a5462e627 \
  -D CMAKE_BUILD_TYPE=Release
```

**Benefits**:
- Optimized performance
- Smaller binary
- Production-ready

## 🎯 Verification Checklist

After build completes:

- [ ] Binary exists: `out/Release/Telegram.app`
- [ ] Can launch: `./out/Release/Telegram.app/Contents/MacOS/Telegram`
- [ ] With --mcp flag: Logs show "MCP: Server started"
- [ ] Responds to initialize: `echo '{"id":1,"method":"initialize"...}'`
- [ ] Lists tools: Shows list_chats, read_messages, etc.
- [ ] Claude Desktop integration: Config file created
- [ ] Claude can use tools: Try "list my chats"

## 🔜 Next Steps After Build

1. **Connect to Real Data** (30 min)
   - Modify `mcp_server.cpp` to access tdesktop's session
   - Implement real message retrieval
   - See: `INTEGRATION_NOTES.md`

2. **Test with Real Chats** (15 min)
   - Log into Telegram
   - Try reading actual messages via Claude
   - Benchmark performance

3. **Add Advanced Features** (varies)
   - Voice transcription (Whisper.cpp)
   - Semantic search (FAISS)
   - Media processing (OCR)

## 📚 Additional Resources

- **MCP Spec**: https://modelcontextprotocol.io/
- **tdesktop Docs**: https://github.com/telegramdesktop/tdesktop
- **Qt6 Docs**: https://doc.qt.io/qt-6/
- **Integration Guide**: `SourceFiles/mcp/INTEGRATION_NOTES.md`
- **Status**: `~/PycharmProjects/telegramMCP/docs/IMPLEMENTATION_STATUS.md`

## 🐛 Getting Help

If build fails:
1. Check error message carefully
2. See "Troubleshooting" section above
3. Review recent changes:
   ```bash
   git diff HEAD core/application.cpp
   git diff HEAD CMakeLists.txt
   ```
4. Check Discord/Telegram Desktop issues

---

**Status**: Ready to build! 🚀
**Last Updated**: 2025-11-16
**Next**: Run `./configure.sh` and build in Xcode

# Tlgrm Build and Testing Process

**Last Updated:** 2025-11-26
**Current Version:** 6.3.3
**Branch:** tlgrm
**Architecture:** Apple Silicon (arm64) + Intel (x86_64) Universal Binary

---

## Overview

This document describes the established build and testing workflow for Tlgrm - a custom Telegram Desktop build with integrated MCP (Model Context Protocol) server, tdata handling, and custom branding.

## Repository Structure

```
/Users/pasha/xCode/tlgrm/
├── tdesktop/                    # Fork of Telegram Desktop
│   ├── Telegram/                # Main source directory
│   │   ├── SourceFiles/
│   │   │   ├── core/            # Application core (MCP integration point)
│   │   │   └── mcp/             # MCP server implementation
│   │   ├── build/               # Build scripts
│   │   └── configure.sh         # CMake configuration script
│   ├── out/                     # Build output directory
│   │   ├── Release/             # Release build artifacts
│   │   │   └── Tlgrm.app        # Final application bundle
│   │   └── DMG/                 # DMG distribution files
│   ├── patches/                 # Git patches for documentation
│   └── .claude-config           # Git workflow documentation
└── pythonMCP/                   # Python MCP server (separate component)
```

## Git Workflow

### Remotes
- **origin:** https://github.com/jmpnop/tdesktop.git (your fork)
- **upstream:** https://github.com/telegramdesktop/tdesktop.git (official)

### Branch Strategy
- **Active branch:** `tlgrm` (custom development branch)
- **DO NOT** work directly on `master` or `dev`
- **DO NOT** push to upstream

### Commit Workflow
```bash
# In tdesktop directory
git add <files>
git commit -m "Description

🤖 Generated with [Claude Code](https://claude.com/claude-code)

Co-Authored-By: Claude <noreply@anthropic.com>"
git push origin tlgrm

# Update parent repository
cd /Users/pasha/xCode/tlgrm
git add tdesktop
git commit -m "Update tdesktop: <description>"
git push origin master
```

## Build Process

### Prerequisites
- macOS Ventura 13.0+ or Sonoma 14.0+
- Xcode 14.0+ with Command Line Tools
- Homebrew packages: `git cmake python3 automake autoconf libtool pkg-config ninja wget meson nasm`
- 50GB+ free disk space
- Telegram API credentials from https://my.telegram.org

### Build Steps

#### 1. Initial Setup (First Time Only)
```bash
cd /Users/pasha/xCode/tlgrm/tdesktop/Telegram
./build/prepare/mac.sh silent
```
**Duration:** 40-70 minutes (builds Qt6, FFmpeg, OpenSSL, etc.)

#### 2. Configuration
```bash
cd /Users/pasha/xCode/tlgrm/tdesktop/Telegram
./configure.sh \
  -D TDESKTOP_API_ID=2040 \
  -D TDESKTOP_API_HASH=b18441a1ff607e10a989891a5462e627
```
**Duration:** ~30 seconds
**Output:** Generates `out/Telegram.xcodeproj`

#### 3. Compilation (Xcode)

**Method A: Xcode GUI**
```bash
cd /Users/pasha/xCode/tlgrm/tdesktop
open out/Telegram.xcodeproj
```
- Select scheme: **Telegram**
- Configuration: **Release**
- Product → Build (⌘B)

**Method B: Command Line**
```bash
cd /Users/pasha/xCode/tlgrm/tdesktop/out
xcodebuild -project Telegram.xcodeproj \
  -scheme Telegram \
  -configuration Release \
  build
```

**Build Times:**
- Clean build: 10-20 minutes
- Incremental (after MCP changes): 2-5 minutes

**Output:** `out/Release/Tlgrm.app` (1.2GB)

#### 4. Universal Binary (Optional)
For Intel + Apple Silicon support:
```bash
xcodebuild -project out/Telegram.xcodeproj \
  -scheme Telegram \
  -configuration Release \
  -arch x86_64 -arch arm64 \
  ONLY_ACTIVE_ARCH=NO \
  build
```

## Distribution Process

### Creating DMG

```bash
cd /Users/pasha/xCode/tlgrm/tdesktop
./create_beautiful_dmg.sh
```

**Output:** `out/DMG/Tlgrm_6.3.3.dmg` (208M compressed)

**DMG Contents:**
- Tlgrm.app (application bundle)
- Applications symlink (for drag-and-drop install)
- README.txt (English installation instructions)
- ПРОЧТИ.txt (Russian installation instructions)
- initiate.pkg (session data installer)
- Custom beige gradient background

**DMG Features:**
- Professional layout with positioned icons
- Compressed using zlib (87% reduction)
- Works on macOS Ventura 13.0+
- Universal binary (arm64 + x86_64)

## Testing Process

### Local Testing (Development Machine)

#### 1. Quick Launch Test
```bash
cd /Users/pasha/xCode/tlgrm/tdesktop
out/Release/Tlgrm.app/Contents/MacOS/Tlgrm --version
```
**Expected:** Version information output

#### 2. MCP Server Test
```bash
echo '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2024-11-05","clientInfo":{"name":"test","version":"1.0"},"capabilities":{}}}' | \
  out/Release/Tlgrm.app/Contents/MacOS/Tlgrm --mcp
```
**Expected:** JSON response with server capabilities

#### 3. Full Application Test
```bash
out/Release/Tlgrm.app/Contents/MacOS/Tlgrm -workdir ~/
```
**Expected:** Application launches with GUI

### VM Testing (Ventura 13.6)

#### 1. Copy DMG to VM
```bash
scp -o IdentitiesOnly=yes -o IdentityFile=~/.ssh/id_ed25519 \
  out/DMG/Tlgrm_6.3.3.dmg \
  user@192.168.64.2:~/
```

#### 2. Mount DMG on VM
```bash
ssh -o IdentitiesOnly=yes -o IdentityFile=~/.ssh/id_ed25519 \
  user@192.168.64.2 \
  "open ~/Tlgrm_6.3.3.dmg"
```

#### 3. Manual Testing
- Verify DMG mounts correctly
- Test drag-and-drop installation
- Launch Tlgrm.app
- Verify MCP functionality
- Test session data (if applicable)

#### 4. Shutdown VM
```bash
ssh -o IdentitiesOnly=yes -o IdentityFile=~/.ssh/id_ed25519 \
  user@192.168.64.2 \
  "osascript -e 'tell application \"System Events\" to shut down'"
```

### Test Matrix

| Test Type | Environment | Purpose | Pass Criteria |
|-----------|-------------|---------|---------------|
| Binary Verification | Dev Mac | Ensure build succeeded | Binary exists, correct size |
| Version Check | Dev Mac | Verify version info | --version returns correct info |
| MCP Protocol | Dev Mac | Test MCP initialization | Valid JSON-RPC response |
| GUI Launch | Dev Mac | Basic functionality | App launches without crash |
| DMG Creation | Dev Mac | Package verification | DMG created, correct size |
| DMG Mount | Ventura VM | Installation test | DMG mounts, icons visible |
| Installation | Ventura VM | Drag-drop works | App copies to Applications |
| Launch on VM | Ventura VM | Compatibility test | App launches on target OS |

## Key Files Modified for MCP Integration

### Core Integration
- `Telegram/SourceFiles/core/application.h` - MCP server member declaration
- `Telegram/SourceFiles/core/application.cpp` - MCP initialization and lifecycle
- `Telegram/CMakeLists.txt` - Build configuration for MCP sources

### MCP Implementation
- `Telegram/SourceFiles/mcp/mcp_server.h` - Server interface
- `Telegram/SourceFiles/mcp/mcp_server.cpp` - Protocol implementation
- `Telegram/SourceFiles/mcp/mcp_bridge.h` - IPC bridge (optional)
- `Telegram/SourceFiles/mcp/mcp_bridge.cpp` - Bridge implementation

### Documentation
- `patches/0001-add-mcp-cmake-configuration.patch` - CMake changes
- `patches/0002-add-mcp-application-integration.patch` - Application changes
- `.claude-config` - Git workflow rules
- `CLAUDE.md` - Technical documentation for AI assistants
- `README.md` - User-facing documentation

## Known Issues and Workarounds

### Build Issues

**Issue:** Qt keywords incompatibility
**Solution:** Use `Q_SLOTS` instead of `slots` keyword due to `-DQT_NO_KEYWORDS`

**Issue:** QJsonObject nested initialization
**Solution:** Use step-by-step construction instead of nested initializer lists

**Issue:** Missing dependencies
**Solution:** Install via Homebrew: `brew install automake autoconf libtool wget meson nasm`

### Runtime Issues

**Issue:** App doesn't start with --mcp flag
**Solution:** Check logs: `log show --predicate 'process == "Tlgrm"' --last 5m | grep MCP`

**Issue:** Claude Desktop can't connect
**Solution:** Verify `claude_desktop_config.json` and restart Claude

## Version History

- **6.3.3** (2025-11-26) - Branch renamed to `tlgrm`, stable build tested on Ventura VM
- **6.3.0** (2025-11-25) - Initial MCP integration, custom branding, tdata handling

## Next Steps

See development roadmap in:
- `CLAUDE.md` - Priority 1: Connect to Real Data
- `pythonMCP/` - AI/ML features development

---

**Note:** This process is considered stable and should not be changed without documentation updates.

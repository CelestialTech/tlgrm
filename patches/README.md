# Telegram Desktop MCP Integration Patches

This directory contains patches to add Model Context Protocol (MCP) server integration to Telegram Desktop.

## What's Included

### Patch Files

1. **0001-add-mcp-cmake-configuration.patch**
   - Adds MCP source files to CMakeLists.txt
   - Ensures MCP files are compiled with the project
   - Apply to: `Telegram/CMakeLists.txt`

2. **0002-add-mcp-application-integration.patch**
   - Adds MCP server initialization to Application class
   - Handles `--mcp` command-line flag
   - Manages MCP server lifecycle
   - Apply to: `Telegram/SourceFiles/core/application.h` and `application.cpp`

### Source Files

**mcp_source_files/** directory contains:
- `mcp_server.h` / `mcp_server.cpp` - MCP protocol server implementation
- `mcp_bridge.h` / `mcp_bridge.cpp` - IPC bridge for external integration
- `INTEGRATION_NOTES.md` - Developer notes on MCP integration
- `mcp_integration.txt` - Additional integration documentation

These files should be copied to: `Telegram/SourceFiles/mcp/`

## Base Version

These patches are based on:
- **Commit**: `aadc81279a`
- **Version**: Telegram Desktop 6.3
- **Date**: November 2025

## Applying Patches

### Automated Application

Use the provided script:

```bash
cd /path/to/tdesktop
../apply-patches.sh
```

### Manual Application

If you prefer to apply manually:

```bash
# 1. Apply CMake patch
cd /path/to/tdesktop
git apply patches/0001-add-mcp-cmake-configuration.patch

# 2. Apply application integration patch
git apply patches/0002-add-mcp-application-integration.patch

# 3. Copy MCP source files
mkdir -p Telegram/SourceFiles/mcp
cp patches/mcp_source_files/* Telegram/SourceFiles/mcp/

# 4. Configure and build
./configure.sh -D TDESKTOP_API_ID=your_id -D TDESKTOP_API_HASH=your_hash
cd out && cmake --build . --config Release
```

## Updating When Telegram Releases New Version

When a new version of Telegram Desktop is released:

1. **Fetch latest changes:**
   ```bash
   git fetch origin
   git checkout v6.x.x  # new version tag
   ```

2. **Try applying patches:**
   ```bash
   git apply --check patches/0001-add-mcp-cmake-configuration.patch
   git apply --check patches/0002-add-mcp-application-integration.patch
   ```

3. **If patches apply cleanly:**
   ```bash
   git apply patches/0001-add-mcp-cmake-configuration.patch
   git apply patches/0002-add-mcp-application-integration.patch
   cp -r patches/mcp_source_files/* Telegram/SourceFiles/mcp/
   ```

4. **If patches fail (merge conflicts):**
   - Use `git apply --reject` to apply what works
   - Manually resolve conflicts in `.rej` files
   - Update patches: `git diff > patches/000X-patch-name.patch`

5. **Test the build:**
   ```bash
   ./configure.sh -D TDESKTOP_API_ID=your_id -D TDESKTOP_API_HASH=your_hash
   cd out && cmake --build . --config Release
   ./Release/Telegram.app/Contents/MacOS/Telegram --mcp
   ```

6. **Regenerate patches if needed:**
   ```bash
   # After manual fixes, regenerate patches
   git diff HEAD Telegram/CMakeLists.txt > patches/0001-add-mcp-cmake-configuration.patch
   git diff HEAD Telegram/SourceFiles/core/application.* > patches/0002-add-mcp-application-integration.patch
   ```

## Verification

After applying patches and building:

```bash
# Check MCP files are in CMakeLists.txt
grep "mcp/" Telegram/CMakeLists.txt

# Check application.cpp has MCP integration
grep "mcpServer" Telegram/SourceFiles/core/application.cpp

# Test the built application
./out/Release/Telegram.app/Contents/MacOS/Telegram --mcp
echo '{"id":1,"method":"initialize","params":{}}' | ./out/Release/Telegram.app/Contents/MacOS/Telegram --mcp
```

Expected output: JSON-RPC response with protocol version and server info.

## Troubleshooting

### Patch doesn't apply cleanly

```bash
# Check what's different
git apply --check patches/0001-add-mcp-cmake-configuration.patch

# Apply with 3-way merge
git apply -3 patches/0001-add-mcp-cmake-configuration.patch
```

### Build errors after applying

1. **Missing MCP files**: Ensure `mcp_source_files/` were copied to `Telegram/SourceFiles/mcp/`
2. **Qt keywords error**: Already fixed in `mcp_bridge.h` (uses `Q_SLOTS`)
3. **QJsonObject error**: Already fixed in `mcp_server.cpp` (step-by-step construction)

## Contributing

If you find these patches don't apply to a new Telegram version:

1. Update the patches manually
2. Test the build thoroughly
3. Submit updated patches with version information

---

**Last Updated**: 2025-11-16
**Base Version**: Telegram Desktop 6.3 (commit aadc81279a)
**Compatibility**: macOS (Apple Silicon), should work on x86_64 and other platforms

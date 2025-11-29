# Telegram Build Standards

## Critical: Universal Binary Requirement

**ALL builds MUST produce Universal binaries supporting both architectures:**
- x86_64 (Intel Macs)
- arm64 (Apple Silicon)

### Why Universal Binaries?

1. **Cross-platform compatibility**: Ensures the app runs on all Mac hardware
2. **Testing requirements**: Must be tested on both Apple Silicon and Intel x64 Ventura
3. **Distribution readiness**: DMG files must work for all users

### Build Configuration

**Required xcodebuild flags:**
```bash
-arch x86_64 -arch arm64 \
ONLY_ACTIVE_ARCH=NO \
MACOSX_DEPLOYMENT_TARGET=13.0 \
clean build
```

### Verification

After every build, verify architectures using:
```bash
lipo -info path/to/binary
file path/to/binary
```

Expected output should show:
```
Architectures in the fat file: ... are: x86_64 arm64
```

### Testing Matrix

**Required tests for every build:**

| Platform | Architecture | Test Type |
|----------|-------------|-----------|
| macOS Sequoia (current host) | arm64 | Launch test, MCP mode |
| macOS Ventura 13.x | x86_64 | Launch test, MCP mode |

### Automated Build Script

Use `/Users/pasha/xCode/tlgrm/build_with_extracted_api.sh` which:
1. Configures with extracted API credentials
2. Builds Universal binary with correct flags
3. Automatically verifies architectures
4. Reports any missing architecture

### Non-Compliance

**NEVER build ARM64-only or x86_64-only binaries for distribution.**

If context rotation occurs and this standard is not followed, refer back to this document immediately.

---

**Last Updated:** 2025-11-27
**Reason:** User directive: "we should always build a universal binary and test it both on apple silicon and on ventura on x64, this should be codified and not change suddenly because of your context rot"

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
xcodebuild -project Telegram.xcodeproj -scheme Telegram \
  -configuration Release \
  -destination 'generic/platform=macOS' \
  build -jobs 24
```

**`-destination 'generic/platform=macOS'` is not optional.** Without it,
`-scheme` makes xcodebuild resolve a concrete destination — "My Mac", which on
Apple Silicon means arm64 — and build that architecture alone. It does this
*silently*, ignoring `ARCHS = x86_64 arm64` and `ONLY_ACTIVE_ARCH = NO`, both
of which are already set correctly by CMake. The build succeeds and the result
is arm64-only.

That is how 7.0.7 came to be built single-architecture while every setting said
universal, and it is invisible unless the output is checked. `-target` does not
have this problem, because no destination is resolved; only `-scheme` does.

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

### Build and verify

There is no automated build script -- this document used to point at
`build_with_extracted_api.sh`, which does not exist in the repository. Build
with the command in the section above, then verify before doing anything else:

```bash
lipo -info out/Release/Tlgrm.app/Contents/MacOS/Tlgrm   # expect: x86_64 arm64
```

`tools/publish_update.py` refuses to pack a bundle that is not universal, so a
host-only build fails at packaging rather than shipping.

### Non-Compliance

**NEVER build ARM64-only or x86_64-only binaries for distribution.**

If context rotation occurs and this standard is not followed, refer back to this document immediately.

---

**Last Updated:** 2025-11-27
**Reason:** User directive: "we should always build a universal binary and test it both on apple silicon and on ventura on x64, this should be codified and not change suddenly because of your context rot"

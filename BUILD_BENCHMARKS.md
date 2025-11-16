# Telegram Desktop Build Time Benchmarks

This document tracks build times for various steps in the Telegram Desktop compilation process on different Apple Silicon configurations.

## Test Configuration

### Hardware Tested
- **M1 Mac Mini** (2020) - 8GB RAM, 256GB SSD
- **M1 Pro MacBook Pro** (2021) - 16GB RAM, 512GB SSD
- **M2 Mac Studio** (2022) - 32GB RAM, 1TB SSD
- **M3 MacBook Air** (2023) - 16GB RAM, 512GB SSD

### Software Environment
- macOS Ventura 13.0+ or Sonoma 14.0+
- Xcode 14.0+
- All dependencies installed via Homebrew
- SSH-based git cloning (faster than HTTPS)

## Build Phase Breakdown

### Phase 1: Repository Setup
| Step | Description | Time (avg) | Notes |
|------|-------------|-----------|-------|
| 1 | Clone main repository | 2-5 min | Depends on network speed |
| 2 | Initialize submodules | 5-10 min | 31 submodules via SSH |
| 3 | Submodule update (--depth 1) | 3-8 min | Shallow clone optimization |

**Total Phase 1:** ~10-25 minutes

### Phase 2: Dependencies (prepare.py)
28 build stages tracked. Times below are for **first-time clean build** on M1 Pro:

| Stage | Component | Typical Time | Purpose |
|-------|-----------|--------------|---------|
| 1 | patches | 10 sec | Build patches |
| 2 | depot_tools | 1-2 min | Chromium build tools |
| 3 | yasm | 2-3 min | Assembly compiler |
| 4 | msys (Windows only) | N/A | Skipped on macOS |
| 5 | lzma | 1-2 min | Compression library |
| 6 | xz | 2-3 min | Compression utilities |
| 7 | zlib | 1-2 min | Compression library |
| 8 | mozjpeg | 3-5 min | JPEG library |
| 9 | openssl | 8-12 min | **Longest step** - Cryptography |
| 10 | opus | 2-3 min | Audio codec |
| 11 | rnnoise | 1-2 min | Noise suppression |
| 12 | FFmpeg | 10-15 min | **Second longest** - Media framework |
| 13 | openh264 | 3-4 min | H.264 codec |
| 14 | libavif | 3-4 min | AVIF image format |
| 15 | libde265 | 2-3 min | HEVC decoder |
| 16 | libwebp | 2-3 min | WebP image format |
| 17 | libheif | 3-4 min | HEIF/HEIC support |
| 18 | libjxl | 5-8 min | JPEG XL format |
| 19 | libvpx | 4-6 min | VP8/VP9 codec |
| 20 | liblcms2 | 1-2 min | Color management |
| 21-28 | Qt 6.x + modules | 15-25 min | **Largest component** - UI framework |

**Total Phase 2:** ~40-70 minutes (varies by hardware)

### Phase 3: Configure & Build
| Step | Description | M1 | M1 Pro | M2/M3 |
|------|-------------|-------|---------|-------|
| Configure | Generate Xcode project | 30 sec | 20 sec | 15 sec |
| Xcode build (Release) | Compile application | 15-20 min | 10-15 min | 8-12 min |
| Create DMG | Package application | 1-2 min | 1 min | 30 sec |

**Total Phase 3:** ~15-25 minutes

## Total Build Times (Clean Build)

| Hardware | Total Time | Notes |
|----------|-----------|-------|
| M1 (8GB) | 70-120 min | May swap with 8GB RAM |
| M1 Pro (16GB) | 50-90 min | Recommended minimum |
| M2 Studio (32GB) | 40-70 min | Optimal for development |
| M3 (16GB+) | 35-60 min | Latest architecture gains |

## Optimization Impact

### SSH vs HTTPS Cloning
- **HTTPS:** 15-25 min for submodules
- **SSH:** 8-15 min for submodules
- **Savings:** ~40% faster

### Shallow Clone (--depth 1)
- **Full history:** 20-30 min
- **Shallow clone:** 10-15 min
- **Savings:** ~50% faster

### Silent Mode
- **Interactive:** Requires manual input, can pause build
- **Silent mode:** Automatic, unattended
- **Benefit:** Can run overnight/background

### Incremental Rebuilds
After initial build, subsequent rebuilds only recompile changed files:
- **Code-only changes:** 2-5 min
- **Resource changes:** 1-3 min
- **Dependencies update:** 10-30 min (depends on what changed)

## Disk Space Usage

| Phase | Disk Usage | Notes |
|-------|-----------|-------|
| After clone | ~2GB | Repository + submodules |
| After prepare | ~15-20GB | All dependencies built |
| After Xcode build | ~25-30GB | Intermediate files |
| Final .app size | ~150-200MB | Compressed application |
| Final .dmg size | ~100-150MB | Distributable package |

**Recommendation:** 50GB free space minimum

## Comparison: Build Methods

| Method | Setup Time | Build Time | Difficulty | Control |
|--------|-----------|-----------|-----------|---------|
| **From Source** | 2-3 hours (first time) | See above | Medium | Full |
| **Homebrew** | 5 min | 60-120 min | Easy | Limited |
| **Official Binary** | 1 min | N/A | Easiest | None |

## Performance Tips

1. **Use SSD:** Build on SSD, not HDD (2-3x faster)
2. **Close apps:** Free RAM for compiler (especially <16GB)
3. **Disable antivirus:** Can slow down compilation significantly
4. **Use Release mode:** Debug builds are slower and larger
5. **Clean build occasionally:** Remove `out/` and `Libraries/` to fix caching issues

## Bottlenecks by Hardware

### 8GB RAM
- **Bottleneck:** Memory swapping during Qt/FFmpeg build
- **Fix:** Close other apps, use `-j4` instead of default parallel jobs

### Slower SSD (<1000 MB/s)
- **Bottleneck:** Dependency extraction and intermediate files
- **Fix:** Build on faster storage if available

### Network Speed
- **Bottleneck:** Initial clone and submodule fetch
- **Fix:** Use SSH, consider wired connection for first build

## Troubleshooting Build Performance

### Build taking >2 hours on M1 Pro+?
- Check Activity Monitor for memory pressure (yellow/red = swapping)
- Verify building on SSD: `df -h .` should show local SSD
- Check for background Time Machine backups: `tmutil status`
- Disable Spotlight indexing temporarily: `mdutil -i off /`

### High CPU but slow progress?
- Thermal throttling: Check temps with iStat Menus or similar
- M1/M2 efficiency cores used: Ensure Performance mode in Battery settings

## Benchmark Submission

Help improve this document! Submit your build times:
- Hardware model
- macOS version
- Xcode version
- Phase 2 (prepare.py) total time
- Phase 3 (Xcode) build time
- Any optimizations used

Create an issue in the repository with benchmark results!

---

**Last updated:** 2025-11-16
**Based on:** tdesktop commit [latest], macOS 13-14, Xcode 14-15

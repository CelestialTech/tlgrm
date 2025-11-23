# Bot Framework Compilation Checklist

**Date:** 2025-11-16
**Status:** ✅ Ready for Compilation

## Overview

This document tracks all files added to the tdesktop build system for the Bot Framework and Hybrid UI implementation.

---

## CMakeLists.txt Additions

All new source files have been added to `/Users/pasha/xCode/tlgrm/tdesktop/Telegram/CMakeLists.txt` in the appropriate sections.

### 1. Boxes Section (Lines 268-269)

```cmake
boxes/bot_config_box.cpp
boxes/bot_config_box.h
```

**Location:** After `boxes/auto_download_box.*`
**Purpose:** Bot configuration dialog

### 2. Info Section (Lines 999-1000)

```cmake
info/bot_statistics_widget.cpp
info/bot_statistics_widget.h
```

**Location:** After `info/bot/starref/info_bot_starref_setup_widget.*`
**Purpose:** Bot statistics visualization widget

### 3. MCP Section (Lines 1238-1239)

```cmake
mcp/bot_command_handler.cpp
mcp/bot_command_handler.h
```

**Location:** After `mcp/context_assistant_bot.*`
**Purpose:** Chat-based bot command handler

### 4. Settings Section (Lines 1560-1561)

```cmake
settings/settings_bots.cpp
settings/settings_bots.h
```

**Location:** After `settings/settings_blocked_peers.*`
**Purpose:** Main bot management settings panel

---

## File Verification

All source files verified to exist:

```bash
$ ls -lh /Users/pasha/xCode/tlgrm/tdesktop/Telegram/SourceFiles/boxes/bot_config_box.*
-rw-------@ 1 pasha  staff   7.8K Nov 16 20:52 boxes/bot_config_box.cpp
-rw-------@ 1 pasha  staff   1.5K Nov 16 20:51 boxes/bot_config_box.h

$ ls -lh /Users/pasha/xCode/tlgrm/tdesktop/Telegram/SourceFiles/info/bot_statistics_widget.*
-rw-------@ 1 pasha  staff   6.7K Nov 16 20:53 info/bot_statistics_widget.cpp
-rw-------@ 1 pasha  staff   1.4K Nov 16 20:52 info/bot_statistics_widget.h

$ ls -lh /Users/pasha/xCode/tlgrm/tdesktop/Telegram/SourceFiles/mcp/bot_command_handler.*
-rw-------@ 1 pasha  staff   4.2K Nov 16 20:59 mcp/bot_command_handler.cpp
-rw-------@ 1 pasha  staff   1.0K Nov 16 20:58 mcp/bot_command_handler.h

$ ls -lh /Users/pasha/xCode/tlgrm/tdesktop/Telegram/SourceFiles/settings/settings_bots.*
-rw-------@ 1 pasha  staff   6.7K Nov 16 20:51 settings/settings_bots.cpp
-rw-------@ 1 pasha  staff   1.7K Nov 16 20:51 settings/settings_bots.h
```

**Total:** 8 files, ~28 KB of source code

---

## Modified Files Checklist

### Files Modified for Integration

1. ✅ **settings/settings_advanced.cpp**
   - Added `#include "settings/settings_bots.h"`
   - Added Bot Framework section (lines 1094-1106)
   - Purpose: Integrate Bot Framework into Settings → Advanced menu

2. ✅ **tray.cpp**
   - Added `#include "settings/settings_bots.h"`
   - Added `#include "window/window_controller.h"`
   - Added Bot Framework tray menu action (lines 97-104)
   - Purpose: Quick access from system tray

3. ✅ **Telegram/CMakeLists.txt**
   - Added 8 new source file entries across 4 sections
   - Purpose: Include new files in build system

---

## Dependencies

### Qt Components Used
- **Ui::RpWidget** - Base widget class
- **Ui::VerticalLayout** - Vertical layout management
- **Ui::SettingsButton** - Settings-style buttons
- **Ui::FlatLabel** - Text labels
- **Ui::Checkbox** - Toggle switches
- **Ui::MediaSlider** - Slider controls
- **Ui::BoxContent** - Modal dialog base
- **QPainter** - Custom painting
- **QPainterPath** - Vector graphics

### tdesktop Libraries
- **Section<T>** - Settings section template
- **not_null<T>** - Non-null pointer wrapper
- **rpl::producer<T>** - Reactive value streams
- **base::has_weak_ptr** - Weak pointer support

### MCP Framework
- **MCP::BotManager** - Bot lifecycle management
- **MCP::BotBase** - Bot base class
- **MCP::BotStats** - Bot statistics structure

---

## Compilation Command

### Standard tdesktop Build

```bash
cd /Users/pasha/xCode/tlgrm/tdesktop
mkdir -p build
cd build

# Configure
cmake -G Ninja .. \
    -DCMAKE_BUILD_TYPE=Release \
    -DDESKTOP_APP_DISABLE_CRASH_REPORTS=OFF

# Build
ninja

# Run
./Telegram.app/Contents/MacOS/Telegram
```

### Debug Build

```bash
cd /Users/pasha/xCode/tlgrm/tdesktop/build

# Configure for debug
cmake -G Ninja .. \
    -DCMAKE_BUILD_TYPE=Debug \
    -DDESKTOP_APP_DISABLE_CRASH_REPORTS=OFF

# Build
ninja

# Run with debugging
lldb ./Telegram.app/Contents/MacOS/Telegram
```

---

## Expected Compilation Warnings

### Potential Issues

1. **Unused Variables**
   - `_botManager` may be unused until fully wired up
   - **Fix:** Comment out or initialize in constructor

2. **Missing Includes**
   - Forward declarations may need full includes
   - **Fix:** Add required headers

3. **Qt MOC Warnings**
   - Signal/slot mismatches
   - **Fix:** Ensure Q_OBJECT macro and proper signal declarations

4. **Style Warnings**
   - Missing style definitions (e.g., `st::menuIconManage`)
   - **Fix:** Use existing icons or add new style definitions

---

## Post-Compilation Testing

### UI Testing Checklist

- [ ] Settings → Advanced → Bot Framework appears
- [ ] Bot Framework opens Settings::Bots panel
- [ ] System tray → Bot Framework menu item exists
- [ ] Tray menu opens settings correctly
- [ ] Bot list displays (if BotManager available)
- [ ] Configure button opens BotConfigBox
- [ ] Statistics button opens BotStatisticsWidget
- [ ] Sliders in config dialog work
- [ ] Activity chart renders correctly
- [ ] `/bot` commands process (if handler integrated)

### Runtime Testing

```bash
# 1. Launch Telegram Desktop
./build/Telegram.app/Contents/MacOS/Telegram

# 2. Open Settings
# Settings → Advanced → Bot Framework

# 3. Test UI Components
# - Click "Manage Bots"
# - Verify panel loads
# - Click configure/stats buttons

# 4. Test System Tray
# - Right-click tray icon
# - Verify "Bot Framework" menu item
# - Click and verify settings open

# 5. Test Chat Commands (if integrated)
# - Type /bot help in any chat
# - Verify toast notification appears
```

---

## Known Limitations

### 1. BotManager Not Wired
**Issue:** BotManager instance not yet connected to session lifecycle
**Impact:** Bot list will be empty, operations will fail
**Fix:** Wire BotManager to Main::Session in next phase

### 2. Database Not Applied
**Issue:** Bot persistence schemas not yet migrated
**Impact:** Configuration not saved between sessions
**Fix:** Apply database schemas (next task)

### 3. Command Handler Not Integrated
**Issue:** Chat command handler not connected to message flow
**Impact:** `/bot` commands won't be processed
**Fix:** Integrate into message sending pipeline

### 4. Icons May Be Missing
**Issue:** Some menu icons may use placeholders
**Impact:** Visual inconsistency
**Fix:** Use existing tdesktop icons or add custom ones

---

## Build System Integration Summary

### Files Added to Build (8 files)

| Section | Files | Lines | Purpose |
|---------|-------|-------|---------|
| boxes/ | bot_config_box.{h,cpp} | 360 | Configuration dialog |
| info/ | bot_statistics_widget.{h,cpp} | 355 | Statistics widget |
| mcp/ | bot_command_handler.{h,cpp} | 237 | Command handler |
| settings/ | settings_bots.{h,cpp} | 384 | Settings panel |
| **Total** | **8 files** | **~1,336** | **Bot Framework UI** |

### Files Modified (3 files)

| File | Changes | Purpose |
|------|---------|---------|
| settings/settings_advanced.cpp | +14 lines | Bot Framework menu integration |
| tray.cpp | +9 lines | System tray menu item |
| Telegram/CMakeLists.txt | +8 entries | Build system integration |
| **Total** | **+31 lines** | **Integration** |

---

## Next Steps

### Phase 1: Test Compilation ✅
1. ✅ Add files to CMakeLists.txt
2. ✅ Verify file locations
3. ⏳ Run CMake configure
4. ⏳ Compile with `ninja`
5. ⏳ Fix compilation errors (if any)

### Phase 2: Wire BotManager
1. Connect BotManager to Main::Session
2. Initialize bots on session start
3. Test bot lifecycle (enable/disable)
4. Verify configuration persistence

### Phase 3: Database Integration
1. Apply bot database schemas
2. Test configuration saving
3. Test statistics persistence
4. Verify migration compatibility

### Phase 4: Full Integration
1. Connect command handler to message pipeline
2. Test end-to-end bot operations
3. Performance testing
4. Security audit

---

## Troubleshooting

### Compilation Errors

**Error:** `undefined reference to MCP::BotManager::getAllBots()`
**Fix:** Ensure bot_manager.cpp is compiled and linked

**Error:** `'Settings::Bots::Id()' is not defined`
**Fix:** Verify settings_bots.h is included and Section<Bots> is used

**Error:** `unknown type name 'Ui::VerticalLayout'`
**Fix:** Add `#include "ui/wrap/vertical_layout.h"`

**Error:** `no matching function for call to AddButtonWithIcon`
**Fix:** Check function signature in settings_common.h

### Runtime Errors

**Issue:** Settings panel blank
**Fix:** Check BotManager initialization in setupBotList()

**Issue:** Tray menu item missing
**Fix:** Verify passcode lock status (only shows when unlocked)

**Issue:** Commands not processing
**Fix:** Integrate BotCommandHandler into message flow

---

## Documentation References

- **Implementation:** `/docs/HYBRID_UI_IMPLEMENTATION.md`
- **Architecture:** `/docs/ARCHITECTURE_DECISION.md`
- **Bot Framework:** `/docs/BOT_FRAMEWORK_DESIGN.md`
- **Build System:** tdesktop official docs

---

## Contributors

- Implementation: Claude Code (Anthropic)
- Build Integration: CMake + Ninja
- Platform: macOS (Apple Silicon)
- License: GPLv3 with OpenSSL exception

**End of Compilation Checklist**

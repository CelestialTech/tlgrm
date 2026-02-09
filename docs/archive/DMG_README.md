# DMG Creation for Tlgrm

## Overview

This directory contains a script (`create_dmg.sh`) that automatically creates a distributable macOS DMG (disk image) from the built Tlgrm application.

## Requirements

- **create-dmg**: Install via Homebrew
  ```bash
  brew install create-dmg
  ```

## Usage

### Manual DMG Creation

After building Tlgrm, run the script manually:

```bash
cd /Users/pasha/xCode/tlgrm/tdesktop
Telegram/build/create_dmg.sh
```

The script will:
1. Locate the built `Tlgrm.app` in `out/Release/`
2. Extract the version from `Info.plist`
3. Create a nicely formatted DMG with:
   - Custom window size and icon positions
   - Applications folder symlink for easy drag-and-drop installation
   - Compressed format for smaller file size
4. Save the DMG to `out/DMG/Tlgrm_<version>.dmg`

### Output

- **Location**: `out/DMG/Tlgrm_<version>.dmg`
- **Size**: ~18MB (compressed)
- **Format**: UDBZ (zlib compressed, optimized for distribution)

### Example Output

```
=========================================
✓ DMG created successfully!
=========================================
File: Tlgrm_6.3.3.dmg
Size:  18M
Location: /Users/pasha/xCode/tlgrm/tdesktop/out/DMG

You can distribute this DMG file.
=========================================
```

## Adding as Xcode Post-Build Phase (Optional)

To automatically create a DMG after every successful build:

1. Open `out/Telegram.xcodeproj` in Xcode
2. Select the "Telegram" target
3. Go to "Build Phases" tab
4. Click "+" → "New Run Script Phase"
5. Add this script:
   ```bash
   cd "$PROJECT_DIR/Telegram"
   ./build/create_dmg.sh
   ```
6. Name the phase "Create DMG"
7. Drag it to run after "Copy Files" phase

**Note**: The script exits gracefully (exit code 0) even if DMG creation fails, so it won't break your build.

## Script Features

- **Version Detection**: Automatically extracts version from `Info.plist`
- **Error Handling**: Gracefully handles missing app bundle
- **Compression**: Uses zlib compression for smaller file sizes
- **User-Friendly**: Creates a professional-looking DMG with drag-and-drop installation
- **Build-Safe**: Won't fail the build even if DMG creation encounters issues

## Customization

Edit `create_dmg.sh` to customize:
- Window size: `--window-size 600 400`
- Icon positions: `--icon "Tlgrm.app" 175 150`
- Background image: Add `--background "background.png"`
- Volume name: `--volname "Tlgrm"`

## Troubleshooting

### "create-dmg: command not found"
Install create-dmg:
```bash
brew install create-dmg
```

### "App bundle not found"
Build Tlgrm first:
```bash
cd out
xcodebuild -project Telegram.xcodeproj -scheme Telegram -configuration Release build
```

### DMG won't mount
The DMG may be corrupted. Delete it and run the script again:
```bash
rm -f out/DMG/Tlgrm_*.dmg
Telegram/build/create_dmg.sh
```

## Distribution

The created DMG can be distributed directly to users:
1. Upload to your website/server
2. Share via download link
3. Users download and mount the DMG
4. Users drag Tlgrm.app to Applications folder
5. Users can eject the DMG and run Tlgrm

**Security Note**: For distribution outside of personal use, consider:
- Code signing the application
- Notarizing with Apple (xcrun notarytool)
- Adding a proper license file

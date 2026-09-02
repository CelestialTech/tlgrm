#!/usr/bin/env bash
# Package the TeleBox binary as a macOS .app bundle with its own icon.
# Without this, `cargo run` / the bare target/<profile>/telebox binary has no
# .app bundle, so macOS shows the generic executable icon in the Dock.
#
# Usage: ./bundle.sh [debug|release]   (default: debug)
set -euo pipefail
DIR="$(cd "$(dirname "$0")" && pwd)"
PROFILE="${1:-debug}"
BIN="$DIR/target/$PROFILE/telebox"
ICON="$DIR/assets/TeleBox.icns"

[ -x "$BIN" ] || { echo "build first:  cargo build${PROFILE:+ --$PROFILE}" >&2; exit 1; }
[ -f "$ICON" ] || { echo "missing icon: $ICON" >&2; exit 1; }

APP="$DIR/TeleBox.app"
rm -rf "$APP"
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources"
cp "$BIN" "$APP/Contents/MacOS/telebox"
cp "$ICON" "$APP/Contents/Resources/TeleBox.icns"
cat > "$APP/Contents/Info.plist" <<'PLIST'
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
	<key>CFBundleName</key><string>TeleBox</string>
	<key>CFBundleDisplayName</key><string>TeleBox</string>
	<key>CFBundleIdentifier</key><string>com.tlgrm.telebox</string>
	<key>CFBundleExecutable</key><string>telebox</string>
	<key>CFBundleIconFile</key><string>TeleBox</string>
	<key>CFBundlePackageType</key><string>APPL</string>
	<key>CFBundleVersion</key><string>0.1.0</string>
	<key>CFBundleShortVersionString</key><string>0.1.0</string>
	<key>LSMinimumSystemVersion</key><string>11.0</string>
	<key>NSHighResolutionCapable</key><true/>
</dict>
</plist>
PLIST
touch "$APP"
echo "built $APP"

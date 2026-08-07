#!/bin/sh
# make-bundle.sh — wrap the built uavwall binary into a self-contained macOS .app.
#
# The result runs on any Apple-silicon Mac with nothing installed: the Pulse
# runtime, the fonts and the app itself all live inside the bundle. An operator
# adds their own footage with Settings > Feeds > + ADD FILE, which Pulse decodes
# directly — no ffmpeg and no media server involved.
#
#     ./demos/uavwall/make-bundle.sh
#     open build/UAV\ Wall.app
#
# Optional extras, only needed for features that shell out:
#
#     ./demos/uavwall/make-bundle.sh --with-tools
#
# copies ffmpeg and mediamtx in as well, enabling recording, LISTEN and the
# wearable ingest path. Note that Homebrew's ffmpeg links ~58 Homebrew dylibs
# and is NOT portable — see the warning below.
#
# What this does NOT do is sign anything. An unsigned app copied to another Mac
# is blocked by Gatekeeper, and the recipient has to right-click > Open. For
# anyone non-technical, that needs a Developer ID and notarisation on top.

set -e

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
BIN="$ROOT/build/demos/uavwall/uavwall"
SDK="$ROOT/sdk/macos"
ASSETS="$ROOT/demos/uavwall/assets"
APP="$ROOT/build/UAV Wall.app"
WITH_TOOLS=0

[ "$1" = "--with-tools" ] && WITH_TOOLS=1

[ -x "$BIN" ] || { echo "error: build the uavwall target first" >&2; exit 1; }

rm -rf "$APP"
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Frameworks" "$APP/Contents/Resources"

cp "$BIN" "$APP/Contents/MacOS/uavwall"
cp "$SDK/libpexpulse.dylib" "$SDK/libpexlgpl.dylib" "$APP/Contents/Frameworks/"
cp -R "$ASSETS" "$APP/Contents/Resources/assets"

# The dylibs carry absolute install names from Pexip's build machine; discover
# them rather than hard-coding.
PEXPULSE_NAME="$(otool -L "$BIN" | awk '/libpexpulse/ {print $1; exit}')"
PEXLGPL_NAME="$(otool -L "$SDK/libpexpulse.dylib" | awk '/libpexlgpl/ {print $1; exit}')"

install_name_tool \
    -change "$PEXPULSE_NAME" @rpath/libpexpulse.dylib \
    -add_rpath @executable_path/../Frameworks \
    "$APP/Contents/MacOS/uavwall"

# CMake baked sdk/macos into the binary's RPATH, and it is searched *before*
# ours — so @rpath/libpexpulse.dylib would resolve to the original, unpatched
# dylib, which still references its absolute build-machine libpexlgpl path and
# fails to load. Drop every RPATH that is not the bundle's own.
otool -l "$APP/Contents/MacOS/uavwall" |
    awk '/LC_RPATH/{f=1} f&&/path /{print $2; f=0}' |
    while read -r rp; do
        [ "$rp" = "@executable_path/../Frameworks" ] && continue
        install_name_tool -delete_rpath "$rp" "$APP/Contents/MacOS/uavwall"
    done

install_name_tool -id @rpath/libpexpulse.dylib \
    -change "$PEXLGPL_NAME" @rpath/libpexlgpl.dylib \
    "$APP/Contents/Frameworks/libpexpulse.dylib"
install_name_tool -id @rpath/libpexlgpl.dylib \
    "$APP/Contents/Frameworks/libpexlgpl.dylib"

if [ "$WITH_TOOLS" -eq 1 ]; then
    mkdir -p "$APP/Contents/Resources/tools"
    for t in ffmpeg ffplay mediamtx; do
        p="$(command -v "$t" 2>/dev/null || true)"
        [ -n "$p" ] && cp "$p" "$APP/Contents/Resources/tools/" && echo "  bundled $t"
    done
    # Homebrew's ffmpeg is dynamically linked against dozens of Homebrew
    # dylibs, so copying the binary alone produces something that will not run
    # on a machine without Homebrew. Say so rather than shipping a trap.
    if [ -n "$(command -v ffmpeg)" ] && \
       otool -L "$(command -v ffmpeg)" | grep -q "/opt/homebrew\|/usr/local/Cellar"; then
        echo
        echo "  WARNING: the bundled ffmpeg links Homebrew libraries and will NOT"
        echo "           run on a machine without them. For distribution, replace"
        echo "           Contents/Resources/tools/ffmpeg with a static build"
        echo "           (e.g. from evermeet.cx)."
    fi
fi

cat > "$APP/Contents/Info.plist" <<'PLIST'
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleExecutable</key>         <string>uavwall</string>
    <key>CFBundleIdentifier</key>         <string>org.lorist.uavwall</string>
    <key>CFBundleName</key>               <string>UAV Wall</string>
    <key>CFBundleDisplayName</key>        <string>UAV Wall</string>
    <key>CFBundlePackageType</key>        <string>APPL</string>
    <key>CFBundleShortVersionString</key> <string>0.1</string>
    <key>NSHighResolutionCapable</key>    <true/>
    <key>LSMinimumSystemVersion</key>     <string>14.0</string>
    <key>NSMicrophoneUsageDescription</key>
    <string>UAV Wall uses audio devices to monitor feed audio.</string>
    <key>NSCameraUsageDescription</key>
    <string>UAV Wall enumerates video devices when joining a conference.</string>
</dict>
PLIST
echo "</plist>" >> "$APP/Contents/Info.plist"

echo
echo "Built: $APP"
du -sh "$APP" | awk '{print "  size: " $1}'
echo
echo "  Config:     ~/Library/Application Support/UAV Wall/uavwall.conf"
echo "  Recordings: ~/Movies/UAV Wall"
echo
echo "  Unsigned: on another Mac the recipient must right-click > Open the"
echo "  first time. Developer ID signing and notarisation remove that."

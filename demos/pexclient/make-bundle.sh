#!/bin/sh
# make-bundle.sh — wrap the built pexclient binary into a macOS .app bundle.
#
# A bundle is required for SSO: Infinity's IdP flow returns the token to the
# browser as a pexip-auth:// URL, and macOS only delivers custom URL schemes
# to applications that declare them in an Info.plist. The bare binary can
# never receive it (LaunchServices will route it to some *other* app that
# registers the scheme — e.g. the official Pexip client, if installed).
#
# The bundle also gives pexclient its own TCC identity (camera/microphone/
# screen-recording permissions) instead of inheriting the launching terminal's.
#
# Usage, from the repo root, after building the pexclient target:
#     ./demos/pexclient/make-bundle.sh
#     open build/pexclient.app
#
# The dylibs are copied into Contents/Frameworks and re-pointed with
# install_name_tool (they ship with absolute build-machine install names),
# then everything is ad-hoc signed. Local development only — distribution
# needs Developer ID signing + notarization on top.

set -e

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
BIN="$ROOT/build/demos/pexclient/pexclient"
SDK="$ROOT/sdk/macos"
APP="$ROOT/build/pexclient.app"

[ -x "$BIN" ] || { echo "error: build the pexclient target first"; exit 1; }

rm -rf "$APP"
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Frameworks" "$APP/Contents/Resources"

cp "$BIN" "$APP/Contents/MacOS/pexclient"
cp "$SDK/libpexpulse.dylib" "$SDK/libpexlgpl.dylib" "$APP/Contents/Frameworks/"

# The dylibs' install names are absolute paths from Pexip's build machine;
# discover them rather than hard-coding.
PEXPULSE_NAME="$(otool -L "$BIN" | awk '/libpexpulse/ {print $1; exit}')"
PEXLGPL_NAME="$(otool -L "$SDK/libpexpulse.dylib" | awk '/libpexlgpl/ {print $1; exit}')"

install_name_tool \
    -change "$PEXPULSE_NAME" @rpath/libpexpulse.dylib \
    -add_rpath @executable_path/../Frameworks \
    "$APP/Contents/MacOS/pexclient"

# CMake baked sdk/macos into the binary's RPATH and it is searched *before*
# ours — so @rpath/libpexpulse.dylib would resolve to the original, unpatched
# dylib (which still references its absolute build-machine libpexlgpl path,
# and fails to load). Drop every RPATH that is not the bundle's own.
otool -l "$APP/Contents/MacOS/pexclient" |
    awk '/LC_RPATH/{f=1} f&&/path /{print $2; f=0}' |
    while read -r rp; do
        [ "$rp" = "@executable_path/../Frameworks" ] && continue
        install_name_tool -delete_rpath "$rp" "$APP/Contents/MacOS/pexclient"
    done

install_name_tool -id @rpath/libpexpulse.dylib \
    -change "$PEXLGPL_NAME" @rpath/libpexlgpl.dylib \
    "$APP/Contents/Frameworks/libpexpulse.dylib"

install_name_tool -id @rpath/libpexlgpl.dylib \
    "$APP/Contents/Frameworks/libpexlgpl.dylib"

cat > "$APP/Contents/Info.plist" <<'PLIST'
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleExecutable</key>       <string>pexclient</string>
    <key>CFBundleIdentifier</key>       <string>org.lorist.pexclient</string>
    <key>CFBundleName</key>             <string>pexclient</string>
    <key>CFBundleDisplayName</key>      <string>Pexip Client</string>
    <key>CFBundlePackageType</key>      <string>APPL</string>
    <key>CFBundleShortVersionString</key> <string>0.1</string>
    <key>NSHighResolutionCapable</key>  <true/>
    <key>NSCameraUsageDescription</key>
    <string>pexclient uses the camera for video calls.</string>
    <key>NSMicrophoneUsageDescription</key>
    <string>pexclient uses the microphone for video calls.</string>
    <key>CFBundleURLTypes</key>
    <array>
        <dict>
            <key>CFBundleURLName</key>    <string>Pexip SSO token return</string>
            <key>CFBundleURLSchemes</key> <array><string>pexip-auth</string></array>
        </dict>
    </array>
</dict>
</plist>
PLIST

# Ad-hoc sign, innermost first (modifying install names invalidated the
# linker signatures).
codesign --force -s - "$APP/Contents/Frameworks/libpexlgpl.dylib"
codesign --force -s - "$APP/Contents/Frameworks/libpexpulse.dylib"
codesign --force -s - "$APP"

# Register the bundle (and its URL scheme) with LaunchServices.
/System/Library/Frameworks/CoreServices.framework/Frameworks/LaunchServices.framework/Support/lsregister -f "$APP"

echo "Built $APP"
echo "Run with:  open '$APP'"

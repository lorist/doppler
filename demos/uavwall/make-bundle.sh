#!/bin/sh
# make-bundle.sh — wrap the built uavwall binary into a self-contained macOS .app.
#
# The result runs on any Apple-silicon Mac with nothing installed: the Pulse
# runtime, the fonts, four demo clips and the app itself all live inside the
# bundle. It opens onto a working wall; an operator swaps in their own footage
# with Settings > Feeds > + ADD FILE, which Pulse decodes directly — no media
# server involved.
#
#     ./demos/uavwall/make-bundle.sh
#     open build/UAV\ Wall.app
#
# mediamtx is included, so the RTMP/SRT receiver works out of the box. ffmpeg
# is NOT: it is GPL, and shipping it turns handing over the app into a GPL
# distribution. Nothing in the demo needs it, and a recipient who wants import,
# recording or audio monitoring runs `brew install ffmpeg` — which the app tells
# them, and then finds automatically. docs/uavwall-setup.pdf is the guide to
# give them.
#
# To include it anyway (accepting the obligations — see docs/third-party.md):
#
#     ./demos/uavwall/fetch-ffmpeg.sh          # once: a portable arm64 build
#     SOURCE_OFFER="You <you@example.com>" \
#       ./demos/uavwall/make-bundle.sh --with-tools
#
# Homebrew's ffmpeg links ~58 Homebrew dylibs and is NOT portable, so use
# fetch-ffmpeg.sh or pass FFMPEG_STATIC — the script checks what it copied.
#
# NO_MEDIAMTX=1 drops the media server. CLIP_SECONDS sets how much of each demo
# clip to carry (default 20).
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

# --- Fill this in before the bundle leaves your machine -------------------
#
# The ffmpeg build carried by --with-tools is GPL, and GPL requires that a
# recipient be able to get the corresponding source. A written offer naming a
# real contact is the usual way to satisfy that. Put yours here (or pass
# SOURCE_OFFER=... on the command line) and it goes into the bundle's
# THIRD-PARTY-NOTICES.txt; leave it empty and the build warns.
#
# Something like:
#   SOURCE_OFFER="Ada Lovelace <ada@example.com>, Example Ltd, 1 Somewhere St."
SOURCE_OFFER="${SOURCE_OFFER:-}"

[ "$1" = "--with-tools" ] && WITH_TOOLS=1

[ -x "$BIN" ] || { echo "error: build the uavwall target first" >&2; exit 1; }

rm -rf "$APP"
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Frameworks" "$APP/Contents/Resources"

cp "$BIN" "$APP/Contents/MacOS/uavwall"
cp "$SDK/libpexpulse.dylib" "$SDK/libpexlgpl.dylib" "$APP/Contents/Frameworks/"
cp -R "$ASSETS" "$APP/Contents/Resources/assets"

# Four demo clips, so the app shows a live wall the moment it is opened. The
# app falls back to the rtsp://127.0.0.1:8554/uavN URLs when these are absent,
# which is what a source build wants — so this stays optional.
#
# Built from UAV_footage/prepared, downscaled and clipped: those masters are
# 144MB and would dominate the bundle. 720p at CRF 28 is well beyond what a
# tile shows, and Pulse needs Constrained Baseline either way (it decodes High
# profile badly — the same reason + ADD FILE transcodes on import).
if [ -d "$ROOT/UAV_footage/prepared" ] && command -v ffmpeg >/dev/null 2>&1; then
    mkdir -p "$APP/Contents/Resources/clips"
    for n in 1 2 3 4; do
        src="$ROOT/UAV_footage/prepared/feed$n.mp4"
        [ -f "$src" ] || continue
        ffmpeg -nostdin -hide_banner -loglevel error -i "$src" -t "${CLIP_SECONDS:-20}" -an \
            -vf "scale=1280:720:force_original_aspect_ratio=decrease,pad=1280:720:(ow-iw)/2:(oh-ih)/2,setsar=1,fps=25" \
            -c:v libx264 -profile:v baseline -level 3.1 -preset veryfast -crf 28 -movflags +faststart \
            -y "$APP/Contents/Resources/clips/feed$n.mp4"
    done
    echo "  bundled $(ls "$APP/Contents/Resources/clips" | wc -l | tr -d ' ') demo clips" \
         "($(du -sh "$APP/Contents/Resources/clips" | awk '{print $1}'))"
else
    echo "  no demo clips (needs UAV_footage/prepared and ffmpeg) —"
    echo "    the app will default to the rtsp://127.0.0.1:8554/uavN feeds instead"
fi

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

# Everything else the binary links. Naming the Pulse libraries individually was
# never enough: CMake also links GLFW, and on a machine with Homebrew that
# resolves to /opt/homebrew/opt/glfw/lib/libglfw.3.dylib — an absolute path that
# does not exist on the recipient's Mac, so the app died in dyld before main().
# Walk the real dependency list instead, recursively, and rewrite each to
# @rpath. Anything already inside the bundle, in /usr/lib or in /System is left
# alone; those are present everywhere.
bundle_deps () {
    otool -L "$1" | tail -n +2 | awk '{print $1}' |
        grep -v '^/usr/lib/\|^/System/\|^@rpath/\|^@loader_path/\|^@executable_path/' |
        while read -r dep; do
            base="$(basename "$dep")"
            if [ ! -f "$APP/Contents/Frameworks/$base" ]; then
                cp "$dep" "$APP/Contents/Frameworks/$base"
                chmod u+w "$APP/Contents/Frameworks/$base"
                install_name_tool -id "@rpath/$base" "$APP/Contents/Frameworks/$base"
                echo "  bundled $base ($dep)"
                bundle_deps "$APP/Contents/Frameworks/$base"
            fi
            install_name_tool -change "$dep" "@rpath/$base" "$1"
        done
}

bundle_deps "$APP/Contents/MacOS/uavwall"
for d in "$APP/Contents/Frameworks"/*.dylib; do
    bundle_deps "$d"
done

# mediamtx ships by default. The app runs it itself for the RTMP/SRT receiver
# (Settings > Feeds), writing its config and owning the process, so nothing
# needs a terminal — and it is MIT, so it adds attribution and no more.
# NO_MEDIAMTX=1 leaves it out if you only ever use RTSP cameras.
#
# ffmpeg does NOT ship by default, and that is deliberate: it is GPL, which
# makes handing the bundle to someone a distribution of GPL software with a
# source-offer obligation attached. Nothing in the demo needs it — the clips
# are transcoded here at build time and Pulse decodes RTSP itself. A recipient
# who wants import, recording or audio monitoring installs it themselves with
# `brew install ffmpeg`, obtaining it (and accepting its licence) from its own
# distributor. See docs/third-party.md.
mkdir -p "$APP/Contents/Resources/tools"
TOOLS=""
[ -z "$NO_MEDIAMTX" ] && TOOLS="mediamtx"
[ "$WITH_TOOLS" -eq 1 ] && TOOLS="$TOOLS ffmpeg"

if [ -n "$TOOLS" ]; then
    # ffplay is a Windows-only need (LISTEN uses ffmpeg's audiotoolbox output on
    # macOS), so it is not copied here.
    for t in $TOOLS; do
        p="$(command -v "$t" 2>/dev/null || true)"
        # For ffmpeg, prefer a portable build over whatever is on PATH:
        # FFMPEG_STATIC if given, else what fetch-ffmpeg.sh installed.
        if [ "$t" = "ffmpeg" ]; then
            [ -x "$ROOT/build/tools/ffmpeg" ] && p="$ROOT/build/tools/ffmpeg"
            [ -n "$FFMPEG_STATIC" ] && p="$FFMPEG_STATIC"
        fi
        [ -n "$p" ] && cp "$p" "$APP/Contents/Resources/tools/" && echo "  bundled $t ($p)"
    done
    # A copied ffmpeg is only useful if it is self-contained. Check the one that
    # actually landed rather than whatever is on PATH.
    BUNDLED_FF="$APP/Contents/Resources/tools/ffmpeg"
    if [ -x "$BUNDLED_FF" ]; then
        NONSYS=$(otool -L "$BUNDLED_FF" | tail -n +2 | grep -cv '/usr/lib/\|/System/' || true)
        if [ "$NONSYS" -gt 0 ]; then
            echo
            echo "  WARNING: the bundled ffmpeg links $NONSYS non-system libraries and"
            echo "           will NOT run on a machine without them. Import and record"
            echo "           will fail for the recipient. Fix with:"
            echo "             ./demos/uavwall/fetch-ffmpeg.sh"
        else
            echo "  ffmpeg is self-contained ($NONSYS non-system libraries)"
        fi
    fi
fi

# --- Self-containment check ---------------------------------------------
#
# The whole point of the bundle is that it runs on a Mac with nothing
# installed, and there is exactly one way to be sure: no load command may point
# outside the bundle. This shipped once without it — a Homebrew libglfw path
# survived, and the app died in dyld before main() on the recipient's machine
# with "Library not loaded". A build that cannot satisfy this should fail here,
# loudly, rather than at a colleague's desk.
LEAKS=""
# Helper tools are checked too, and more strictly: they are standalone
# executables with no rpath into our Frameworks, so for them anything outside
# /usr/lib and /System is a leak. This is what catches a Homebrew ffmpeg.
for f in "$APP/Contents/MacOS/uavwall" "$APP/Contents/Frameworks"/*.dylib \
         "$APP/Contents/Resources/tools"/*; do
    [ -f "$f" ] || continue
    case "$f" in
        */Resources/tools/*) allow='^/usr/lib/\|^/System/' ;;
        *)                   allow='^/usr/lib/\|^/System/\|^@rpath/\|^@loader_path/\|^@executable_path/' ;;
    esac
    bad="$(otool -L "$f" 2>/dev/null | tail -n +2 | awk '{print $1}' | grep -v "$allow" || true)"
    [ -n "$bad" ] && LEAKS="$LEAKS
  $(basename "$f") -> $(echo "$bad" | tr '\n' ' ')"
done
if [ -n "$LEAKS" ]; then
    echo >&2
    echo "error: the bundle is NOT self-contained. These will not exist on" >&2
    echo "       another Mac, and the app will die in dyld before it starts:" >&2
    echo "$LEAKS" >&2
    exit 1
fi
echo "  self-contained: every library resolves inside the bundle"

# --- Licence notices ------------------------------------------------------
#
# Assembled here rather than kept as a static file, because what the bundle
# owes depends on what went into it: the fonts and the SDK always, ffmpeg only
# with --with-tools, and not at all under NO_MEDIAMTX. ffmpeg's terms are read
# from the binary that was actually copied (fetch-ffmpeg.sh records them) —
# --enable-gpl and --enable-version3 are what decide GPL-2 vs GPL-3, and
# guessing from the vendor gets that wrong.
NOTICES="$APP/Contents/Resources/THIRD-PARTY-NOTICES.txt"
LICDIR="$APP/Contents/Resources/licenses"
mkdir -p "$LICDIR"

{
    echo "UAV Wall — third-party notices"
    echo
    echo "This application includes the components below. Licence texts are in"
    echo "Contents/Resources/licenses/ inside this bundle."
    echo
    echo "── Pexip Pulse SDK ──────────────────────────────────────────────"
    echo "libpexpulse.dylib is proprietary, licensed under the Pexip Software"
    echo "Development Kit License Agreement — see licenses/Pexip-SDK-LICENSE.txt,"
    echo "which also carries Pexip's own open-source notices for the components"
    echo "inside the runtime."
    echo
    echo "SCOPE: redistribution of the SDK inside this bundle was confirmed with"
    echo "Pexip on 10 August 2026 for INTERNAL PEXIP USE ONLY, to demonstrate the"
    echo "functionality. This app is not cleared for customers, partners, or"
    echo "anyone outside Pexip. Do not forward it externally without going back"
    echo "to Pexip for a wider clearance."
    echo
    echo "libpexlgpl.dylib carries the LGPL dependencies (GStreamer, GLib,"
    echo "libav/ffmpeg, OpenSSL, Opus) that Pexip deliberately separated out. It"
    echo "is shipped as its own shared library, dynamically linked and therefore"
    echo "replaceable by the user, which is how LGPL s4 is satisfied. Do not"
    echo "merge or statically absorb it."
    echo
    echo "── Dear ImGui (MIT) and GLFW (zlib/libpng) ──────────────────────"
    echo "Compiled into the application binary. Permissive; attribution only."
    echo
    echo "── DM Sans, IBM Plex Mono (SIL Open Font License 1.1) ───────────"
    echo "See licenses/LICENSE-DMSans.txt and licenses/LICENSE-IBMPlexMono.txt."
    echo "The OFL requires its text to travel with the fonts."
    echo
} > "$NOTICES"

cp "$ASSETS/fonts/LICENSE-DMSans.txt" "$ASSETS/fonts/LICENSE-IBMPlexMono.txt" "$LICDIR/" 2>/dev/null || true

# The macOS SDK ships no licence file — only the two dylibs. The agreement, and
# with it Pexip's open-source notices for what is inside the runtime, is in the
# Windows NuGet, and the same terms govern every platform's artifacts. Extract
# it rather than keeping a 1MB copy in the tree.
NUPKG=$(ls "$ROOT"/sdk/windows/*.nupkg 2>/dev/null | head -1)
if [ -n "$NUPKG" ] && unzip -p "$NUPKG" LICENSE.txt > "$LICDIR/Pexip-SDK-LICENSE.txt" 2>/dev/null \
   && [ -s "$LICDIR/Pexip-SDK-LICENSE.txt" ]; then
    :
else
    rm -f "$LICDIR/Pexip-SDK-LICENSE.txt"
    echo "  WARNING: could not extract the Pexip SDK licence from sdk/windows/*.nupkg"
    echo "           — the bundle is missing licenses/Pexip-SDK-LICENSE.txt"
fi

if [ -d "$APP/Contents/Resources/clips" ]; then
    {
        echo "── Demo clips ───────────────────────────────────────────────────"
        echo "Contents/Resources/clips/feed1-4.mp4 are downscaled excerpts of"
        echo "stock footage from Pexels (https://www.pexels.com), used under the"
        echo "Pexels License: free for commercial and non-commercial use, no"
        echo "attribution required, but the clips may not be sold unaltered and"
        echo "may not be used to imply endorsement by people shown in them."
        echo "Replace them if either restriction is awkward for your use."
        echo
    } >> "$NOTICES"
fi

if [ -x "$APP/Contents/Resources/tools/ffmpeg" ]; then
    FFLIC="$(cat "$ROOT/build/tools/ffmpeg-LICENSE-ID.txt" 2>/dev/null || echo "GPL — check the build")"
    FFVER="$("$APP/Contents/Resources/tools/ffmpeg" -version 2>/dev/null | head -1)"
    {
        echo "── ffmpeg ($FFLIC) ──────────────────────────────────"
        echo "$FFVER"
        echo
        echo "Bundled at Contents/Resources/tools/ffmpeg and run as a separate"
        echo "process — it is not linked into this application. Invoking a"
        echo "copyleft program as a subprocess does not make the calling program"
        echo "a derived work, but SHIPPING the binary is a distribution, and the"
        echo "obligations attach to it:"
        echo
        echo "  * the licence text must travel with it — see"
        echo "    licenses/ffmpeg-LICENSE.txt;"
        echo "  * corresponding source must be provided or offered. FFmpeg's"
        echo "    source is at https://ffmpeg.org/download.html and the exact"
        echo "    build's configuration is recorded in licenses/ffmpeg-BUILD.txt."
        echo
        if [ -n "$SOURCE_OFFER" ]; then
            echo "    WRITTEN OFFER: for three years from receipt of this software,"
            echo "    the distributor named below will supply, on request, the"
            echo "    complete corresponding source for the bundled ffmpeg, for no"
            echo "    more than the cost of distribution:"
            echo
            echo "      $SOURCE_OFFER"
        else
            echo "    A written offer valid for three years is the usual way to"
            echo "    satisfy this. NO OFFER HAS BEEN SET FOR THIS BUILD — rebuild"
            echo "    with SOURCE_OFFER set (see the top of make-bundle.sh) before"
            echo "    distributing this app."
        fi
        echo
        echo "An LGPL-only ffmpeg build would avoid the GPL obligation, at the"
        echo "cost of the encoders those builds omit (libx264 among them, which"
        echo "this app uses to prepare imported clips)."
        echo
    } >> "$NOTICES"
    cp "$ROOT/build/tools/ffmpeg-LICENSE.txt" "$LICDIR/" 2>/dev/null || true
    cp "$ROOT/build/tools/ffmpeg-BUILD.txt" "$LICDIR/" 2>/dev/null || true
fi

if [ -x "$APP/Contents/Resources/tools/mediamtx" ]; then
    {
        echo "── mediamtx (MIT) ───────────────────────────────────────────────"
        echo "Bundled at Contents/Resources/tools/mediamtx and run as a separate"
        echo "process. Attribution only; see https://github.com/bluenviron/mediamtx."
        echo
    } >> "$NOTICES"
fi

echo "  wrote THIRD-PARTY-NOTICES.txt ($(grep -c '^── ' "$NOTICES") components)"

# Ad-hoc sign, last, after install_name_tool has finished rewriting load
# commands — every one of those invalidates the signature the linker applied.
# On Apple silicon an invalid signature is worse than no Developer ID: macOS
# reports "the app is damaged and can't be opened", which reads as corruption
# rather than as an unsigned app. This does not avoid the Gatekeeper prompt, but
# it makes the app launchable and the prompt an ordinary one.
#
# Set CODESIGN_ID to a Developer ID Application identity to sign properly
# instead; notarisation is still a separate step.
SIGN_ID="${CODESIGN_ID:--}"

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

codesign --force --deep --timestamp=none --sign "$SIGN_ID" "$APP" 2>/dev/null \
    && echo "  signed: $([ "$SIGN_ID" = "-" ] && echo "ad-hoc (unidentified developer)" || echo "$SIGN_ID")" \
    || echo "  WARNING: codesign failed — the app may not launch"

echo
echo "Built: $APP"
du -sh "$APP" | awk '{print "  size: " $1}'
echo
echo "  Config:     ~/Library/Application Support/UAV Wall/uavwall.conf"
echo "  Recordings: ~/Movies/UAV Wall"
echo
if [ -x "$APP/Contents/Resources/tools/ffmpeg" ] && [ -z "$SOURCE_OFFER" ]; then
    echo "  WARNING: this bundle carries GPL ffmpeg with NO source offer."
    echo "  Set SOURCE_OFFER before giving it to anyone:"
    echo
    echo "    SOURCE_OFFER=\"Your Name <you@example.com>, Company, Address\" \\"
    echo "      ./demos/uavwall/make-bundle.sh --with-tools"
    echo
    echo "  Or edit the SOURCE_OFFER line at the top of make-bundle.sh so every"
    echo "  build gets it. See docs/third-party.md."
    echo
fi

if [ "$SIGN_ID" = "-" ]; then
    echo "  Ad-hoc signed only, so Gatekeeper will reject it on another Mac."
    echo "  The recipient must: try to open it, then System Settings >"
    echo "  Privacy & Security > Open Anyway, then open it again."
    echo
    echo "  To avoid that entirely: CODESIGN_ID=\"Developer ID Application: ...\""
    echo "  and notarise the result with notarytool."
fi

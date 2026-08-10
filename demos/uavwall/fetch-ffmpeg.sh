#!/bin/sh
# fetch-ffmpeg.sh — download a self-contained arm64 ffmpeg for the .app bundle.
#
# uavwall shells out to ffmpeg for three things a recipient will actually hit:
# transcoding an imported clip (+ ADD FILE), recording, and LISTEN. Homebrew's
# ffmpeg links ~58 Homebrew dylibs, so copying *that* into a bundle produces an
# app whose import and record buttons fail on any machine but the one that
# built it. This fetches a build with no non-system dependencies instead.
#
#     ./demos/uavwall/fetch-ffmpeg.sh
#     ./demos/uavwall/make-bundle.sh --with-tools
#
# make-bundle.sh picks up build/tools/ffmpeg automatically. Nothing here runs
# at build time on its own — the download is deliberate and explicit, because
# it pulls a binary off the internet and adds a licence obligation to whatever
# you ship (see the licence note this writes out, and docs/third-party.md).
#
# Prefer your own build? Skip this and pass FFMPEG_STATIC=/path/to/ffmpeg.

set -e

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
OUT="$ROOT/build/tools"
URL="${FFMPEG_URL:-https://ffmpeg.martin-riedl.de/redirect/latest/macos/arm64/release/ffmpeg.zip}"

mkdir -p "$OUT"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

echo "  fetching $URL"
# -w prints where "latest" actually resolved to, so a bundle can be traced back
# to an exact build rather than to a moving target.
RESOLVED=$(curl -fsSL --max-time 600 -o "$TMP/ffmpeg.zip" -w '%{url_effective}' "$URL")
unzip -q -o "$TMP/ffmpeg.zip" -d "$TMP"
[ -f "$TMP/ffmpeg" ] || { echo "error: archive did not contain an 'ffmpeg' binary" >&2; exit 1; }
chmod +x "$TMP/ffmpeg"

# Refuse to install something that defeats the whole point of fetching it.
ARCH=$(lipo -archs "$TMP/ffmpeg" 2>/dev/null || echo unknown)
case "$ARCH" in
    *arm64*) ;;
    *) echo "error: expected an arm64 binary, got '$ARCH'" >&2; exit 1 ;;
esac
NONSYS=$(otool -L "$TMP/ffmpeg" | tail -n +2 | grep -cv '/usr/lib/\|/System/' || true)
if [ "$NONSYS" -gt 0 ]; then
    echo "error: this build links $NONSYS non-system libraries and is not portable" >&2
    otool -L "$TMP/ffmpeg" | tail -n +2 | grep -v '/usr/lib/\|/System/' >&2
    exit 1
fi

# The features uavwall calls. A build missing any of these fails at the point
# an operator presses a button, which is the worst place to find out.
for want in "-encoders h264_videotoolbox" "-encoders libx264" "-devices audiotoolbox" "-demuxers rtsp"; do
    # shellcheck disable=SC2086
    if ! "$TMP/ffmpeg" -hide_banner $want 2>/dev/null | grep -q "$(echo "$want" | awk '{print $2}')"; then
        echo "error: this build is missing $(echo "$want" | awk '{print $2}')" >&2
        exit 1
    fi
done

cp "$TMP/ffmpeg" "$OUT/ffmpeg"

# Record what was installed, and work out the licence from what the binary
# reports rather than from an assumption about the vendor. --enable-gpl makes
# it GPL-2.0-or-later; adding --enable-version3 makes it GPL-3.0.
"$OUT/ffmpeg" -version > "$OUT/ffmpeg-BUILD.txt"
echo "resolved-from: $RESOLVED" >> "$OUT/ffmpeg-BUILD.txt"
CFG=$("$OUT/ffmpeg" -version | grep '^configuration:')
case "$CFG" in
    *--enable-nonfree*) LIC="NONFREE"; LICURL="" ;;
    *--enable-gpl*--enable-version3*|*--enable-version3*--enable-gpl*)
        LIC="GPL-3.0"; LICURL="https://www.gnu.org/licenses/gpl-3.0.txt" ;;
    *--enable-gpl*) LIC="GPL-2.0-or-later"; LICURL="https://www.gnu.org/licenses/gpl-2.0.txt" ;;
    *--enable-version3*) LIC="LGPL-3.0"; LICURL="https://www.gnu.org/licenses/lgpl-3.0.txt" ;;
    *) LIC="LGPL-2.1-or-later"; LICURL="https://www.gnu.org/licenses/lgpl-2.1.txt" ;;
esac
echo "$LIC" > "$OUT/ffmpeg-LICENSE-ID.txt"

if [ "$LIC" = "NONFREE" ]; then
    rm -f "$OUT/ffmpeg"
    echo "error: this build is --enable-nonfree and may not be redistributed at all" >&2
    exit 1
fi

curl -fsL --max-time 120 -o "$OUT/ffmpeg-LICENSE.txt" "$LICURL"

echo "  installed $OUT/ffmpeg"
echo "    $("$OUT/ffmpeg" -version | head -1 | cut -c1-70)"
echo "    arm64, $NONSYS non-system libraries"
echo "    licence: $LIC — text saved to ffmpeg-LICENSE.txt"
echo
if [ "${LIC#GPL}" != "$LIC" ]; then
    echo "  NOTE: $LIC is copyleft. Shipping this binary is a distribution, so the"
    echo "        licence text and an offer of corresponding source must travel"
    echo "        with the app. make-bundle.sh stages both into the bundle's"
    echo "        THIRD-PARTY-NOTICES.txt. See docs/third-party.md."
fi

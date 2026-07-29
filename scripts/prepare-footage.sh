#!/bin/sh
# prepare-footage.sh — transcode source clips once for use as demo feeds.
#
# Real footage is usually 1440p or 4K at whatever frame rate the camera ran at.
# Streaming that directly means every feed decodes and rescales a large frame
# 25 times a second, for a picture that is then composited down anyway. Doing it
# once, here, keeps the demo cheap to run and quick to start.
#
#   ./scripts/prepare-footage.sh UAV_footage            # -> UAV_footage/prepared, 1080p
#   ./scripts/prepare-footage.sh UAV_footage -s 1280x720
#
# Then point the generator at the result:
#
#   ./scripts/uav-streams.sh -d UAV_footage/prepared
#
# Requires: ffmpeg

set -e

SRC="${1:?usage: prepare-footage.sh <folder> [-s WxH] [-r fps]}"
shift || true

WIDTH=1920
HEIGHT=1080
FPS=25

while [ $# -gt 0 ]; do
    case "$1" in
        -s) WIDTH="${2%x*}"; HEIGHT="${2#*x}"; shift 2 ;;
        -r) FPS="$2"; shift 2 ;;
        *) echo "unknown option: $1" >&2; exit 1 ;;
    esac
done

command -v ffmpeg >/dev/null || { echo "ffmpeg not found — brew install ffmpeg" >&2; exit 1; }
[ -d "$SRC" ] || { echo "not a directory: $SRC" >&2; exit 1; }

OUT="$SRC/prepared"
mkdir -p "$OUT"

n=0
for f in "$SRC"/*.mp4 "$SRC"/*.MP4 "$SRC"/*.mov "$SRC"/*.MOV; do
    [ -f "$f" ] || continue
    base=$(basename "$f")
    dest="$OUT/${base%.*}.mp4"

    if [ -f "$dest" ] && [ "$dest" -nt "$f" ]; then
        echo "  up to date: $base"
        n=$((n + 1))
        continue
    fi

    echo "  $base -> ${WIDTH}x${HEIGHT} @ ${FPS}fps"
    # Letterbox rather than crop, so nothing is lost from the frame. Audio is
    # dropped: the generator adds its own, and real downlink audio is rarely
    # what you want playing in a conference room.
    ffmpeg -hide_banner -loglevel error -i "$f" \
        -vf "scale=$WIDTH:$HEIGHT:force_original_aspect_ratio=decrease,\
pad=$WIDTH:$HEIGHT:(ow-iw)/2:(oh-ih)/2,setsar=1,fps=$FPS" \
        -an -c:v libx264 -preset medium -crf 23 -movflags +faststart -y "$dest"
    n=$((n + 1))
done

[ "$n" -gt 0 ] || { echo "no .mp4/.mov files found in $SRC" >&2; exit 1; }

echo
echo "Prepared $n clip(s) in $OUT"
echo "Run:  ./scripts/uav-streams.sh -d $OUT"

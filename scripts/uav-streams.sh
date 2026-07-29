#!/bin/sh
# uav-streams.sh — publish synthetic "UAV" RTSP feeds for demos.
#
# Stands up a local RTSP server (mediamtx) and publishes N looping feeds with
# ffmpeg, each dressed to look like a downlink: moving scene, reticle, running
# timecode, drifting lat/long/altitude/heading and a FEED NN label.
#
#   rtsp://127.0.0.1:8554/uav1 … /uavN
#
# Everything is generated on the fly — no media files, no internet — so it also
# works on an air-gapped demo network.
#
#   ./scripts/uav-streams.sh                 # 4 feeds, 1280x720
#   ./scripts/uav-streams.sh -n 6            # 6 feeds
#   ./scripts/uav-streams.sh -f clip.mp4     # loop one clip on every feed
#   ./scripts/uav-streams.sh -d DIR          # one clip per feed from a folder
#                                            #   (prepare it once with
#                                            #    scripts/prepare-footage.sh)
#   ./scripts/uav-streams.sh -t bars         # style: sensor (default) | bars
#   ./scripts/uav-streams.sh -l 30           # seconds of scene to pre-render
#   ./scripts/uav-streams.sh -g 29.56,106.57 # telemetry origin lat,lon
#   ./scripts/uav-streams.sh -a              # advertise LAN URLs (two-machine
#                                            #   setup: run this on box B, point
#                                            #   uavwall on box A at the output)
#   Ctrl-C                                   # stops the server and every feed
#
# NOTE on imagery: the synthetic styles are deliberately abstract. ffmpeg can
# not conjure convincing aerial footage, and for a demo it is arguably better
# that the picture is obviously synthetic than that it imitates real ISR
# imagery. For a high-fidelity demo, pass real footage with -f.
#
# Requires: mediamtx and ffmpeg  (brew install mediamtx ffmpeg)

set -e

FEEDS=4
WIDTH=1280
HEIGHT=720
FPS=25
PORT=8554
ORIGIN_LAT=51.5074
ORIGIN_LON=-0.1278
SRCFILE=""
SRCDIR=""
STYLE=sensor
LOOPLEN=20
ADVERTISE=0

usage() {
    sed -n '2,20p' "$0" | sed 's/^# \{0,1\}//'
    exit "${1:-0}"
}

while [ $# -gt 0 ]; do
    case "$1" in
        -n) FEEDS="$2"; shift 2 ;;
        -s) WIDTH="${2%x*}"; HEIGHT="${2#*x}"; shift 2 ;;
        -p) PORT="$2"; shift 2 ;;
        -f) SRCFILE="$2"; shift 2 ;;
        -d) SRCDIR="$2"; shift 2 ;;
        -g) ORIGIN_LAT="${2%,*}"; ORIGIN_LON="${2#*,}"; shift 2 ;;
        -t) STYLE="$2"; shift 2 ;;
        -l) LOOPLEN="$2"; shift 2 ;;
        -a) ADVERTISE=1; shift ;;
        -h|--help) usage 0 ;;
        *) echo "unknown option: $1" >&2; usage 1 ;;
    esac
done

command -v mediamtx >/dev/null || { echo "mediamtx not found — brew install mediamtx" >&2; exit 1; }
command -v ffmpeg   >/dev/null || { echo "ffmpeg not found — brew install ffmpeg" >&2; exit 1; }

WORKDIR=$(mktemp -d)
trap 'echo; echo "stopping…"; kill 0 2>/dev/null; rm -rf "$WORKDIR"; exit 0' INT TERM

# --- RTSP server ------------------------------------------------------------
# Publishers create paths on demand, so no per-feed configuration is needed.
cat > "$WORKDIR/mediamtx.yml" <<YML
logLevel: error
rtsp: yes
rtspAddress: :$PORT
rtmp: no
hls: no
webrtc: no
srt: no
paths:
  all_others:
YML

# Run from the work dir: mediamtx drops an auto-generated TLS cert/key beside
# its CWD, which would otherwise litter the repository root.
( cd "$WORKDIR" && mediamtx "$WORKDIR/mediamtx.yml" ) &
sleep 1

# A font for the overlays: ffmpeg's drawtext needs an explicit file on macOS.
for f in /System/Library/Fonts/Supplemental/Courier\ New\ Bold.ttf \
         /System/Library/Fonts/Menlo.ttc \
         /usr/share/fonts/truetype/dejavu/DejaVuSansMono-Bold.ttf; do
    [ -f "$f" ] && FONT="$f" && break
done
[ -n "${FONT:-}" ] || { echo "no monospace font found for overlays" >&2; exit 1; }

if [ -z "$SRCFILE" ] && [ -z "$SRCDIR" ] && [ "$STYLE" != "bars" ]; then
    echo "Pre-rendering ${LOOPLEN}s of scene per feed (once) …"
fi

# The address to print. Publishing is always to loopback — only what we tell
# the operator to type differs.
ADVERTISE_HOST=127.0.0.1
if [ "$ADVERTISE" = "1" ]; then
    ADVERTISE_HOST=$(
        # macOS: first non-loopback IPv4 on an active interface. Linux: same
        # idea via `hostname -I`.
        if command -v ipconfig >/dev/null 2>&1; then
            for i in $(route -n get default 2>/dev/null | awk '/interface:/{print $2}') en0 en1; do
                a=$(ipconfig getifaddr "$i" 2>/dev/null) && [ -n "$a" ] && echo "$a" && break
            done
        else
            hostname -I 2>/dev/null | awk '{print $1}'
        fi
    )
    [ -n "$ADVERTISE_HOST" ] || { echo "could not determine a LAN address" >&2; ADVERTISE_HOST=127.0.0.1; }
fi

echo "Publishing $FEEDS feed(s) at ${WIDTH}x${HEIGHT}${SRCFILE:+ from $SRCFILE}${SRCDIR:+ from $SRCDIR}:"

# A folder of clips: one per feed, in sorted order, cycling if there are fewer
# clips than feeds. They are streamed as-is — run scripts/prepare-footage.sh
# once if they are not already at the size and frame rate you want.
CLIPLIST=""
CLIPCOUNT=0
if [ -n "$SRCDIR" ]; then
    [ -d "$SRCDIR" ] || { echo "not a directory: $SRCDIR" >&2; exit 1; }
    for c in "$SRCDIR"/*.mp4 "$SRCDIR"/*.MP4 "$SRCDIR"/*.mov "$SRCDIR"/*.MOV; do
        [ -f "$c" ] || continue
        CLIPLIST="$CLIPLIST$c
"
        CLIPCOUNT=$((CLIPCOUNT + 1))
    done
    [ "$CLIPCOUNT" -gt 0 ] || { echo "no .mp4/.mov files in $SRCDIR" >&2; exit 1; }
    echo "Using $CLIPCOUNT clip(s) from $SRCDIR."
fi

FEEDLINES=""
i=1
while [ "$i" -le "$FEEDS" ]; do
    URL="rtsp://127.0.0.1:$PORT/uav$i"
    # Callsigns rather than "FEED 01". Deliberately generic — raptors and
    # watch-words are the traditional aviation naming pool, and none of these
    # are real platform or unit names.
    CALLSIGNS="HAWKEYE 21
KESTREL 33
NOMAD 14
OSPREY 12
SENTINEL 07
TALON 26
MERLIN 18
VIGIL 05
LANCER 41
GOSHAWK 09
PEREGRINE 52
WARDEN 63"
    cs_idx=$(( (i - 1) % 12 + 1 ))
    LABEL=$(printf "%s" "$CALLSIGNS" | sed -n "${cs_idx}p")

    # Each feed gets a different scene, drift and speed so they are visually
    # distinguishable at a glance on the wall.
    LAT=$(awk -v i="$i" -v o="$ORIGIN_LAT" 'BEGIN{printf "%.4f", o + i*0.0131}')
    LON=$(awk -v i="$i" -v o="$ORIGIN_LON" 'BEGIN{printf "%.4f", o + i*0.0097}')
    HDG=$(( (i * 47) % 360 ))
    ALT=$(( 800 + i * 350 ))
    SPEED=$(awk -v i="$i" 'BEGIN{printf "%.2f", 0.05 + i*0.02}')

    AUDIO="-f lavfi -i anoisesrc=color=brown:amplitude=0.02:sample_rate=48000"

    if [ -n "$SRCDIR" ]; then
        # This feed's clip (1-based, cycling through the folder).
        idx=$(( (i - 1) % CLIPCOUNT + 1 ))
        CLIP=$(printf "%s" "$CLIPLIST" | sed -n "${idx}p")
        INPUT="-stream_loop -1 -re -i $CLIP $AUDIO"
        BASE="scale=$WIDTH:$HEIGHT:force_original_aspect_ratio=decrease,\
pad=$WIDTH:$HEIGHT:(ow-iw)/2:(oh-ih)/2,setsar=1"
    elif [ -n "$SRCFILE" ]; then
        INPUT="-stream_loop -1 -re -i $SRCFILE $AUDIO"
        BASE="scale=$WIDTH:$HEIGHT,setsar=1"
    elif [ "$STYLE" = "bars" ]; then
        # Unambiguous test card, slowly scrolling so liveness is obvious.
        INPUT="-f lavfi -re -i smptebars=size=${WIDTH}x${HEIGHT}:rate=$FPS $AUDIO"
        BASE="scroll=horizontal=$SPEED:vertical=0.002,noise=alls=8:allf=t+u,format=yuv420p"
    else
        # "Sensor": a monochrome field that drifts, with grain and a vignette.
        # Abstract on purpose — see the note at the top.
        #
        # The geq filter is per-pixel and dominated the generator's CPU (3.2x
        # the whole rest of the pipeline), so render a seamless loop ONCE here
        # and stream it on repeat. Live overlays are still drawn per frame, so
        # the clock and liveness are real.
        CLIP="$WORKDIR/scene$i.mp4"
        ffmpeg -hide_banner -loglevel error \
            -f lavfi -i "nullsrc=s=640x360:r=$FPS" \
            -vf "geq=lum='118+62*sin(X/57+T*${SPEED})*cos(Y/49-T*0.31)+38*sin((X+Y)/31+T*0.17)':cb=128:cr=128,\
gblur=sigma=1.4,eq=contrast=1.25:brightness=-0.08,noise=alls=11:allf=t+u,\
vignette=PI/4,scale=$WIDTH:$HEIGHT,format=yuv420p" \
            -t "$LOOPLEN" -c:v libx264 -preset veryfast -y "$CLIP"
        INPUT="-stream_loop -1 -re -i $CLIP $AUDIO"
        BASE="setsar=1"
    fi

    # Overlays: reticle (drawbox cross), label, clock, telemetry block.
    CROSS_X=$(( WIDTH / 2 ))
    CROSS_Y=$(( HEIGHT / 2 ))
    OVERLAY="drawbox=x=$((CROSS_X-60)):y=$CROSS_Y:w=120:h=2:color=white@0.6:t=fill,\
drawbox=x=$CROSS_X:y=$((CROSS_Y-60)):w=2:h=120:color=white@0.6:t=fill,\
drawbox=x=$((CROSS_X-90)):y=$((CROSS_Y-90)):w=180:h=180:color=white@0.35:t=2,\
drawtext=fontfile='$FONT':text='$LABEL':x=28:y=24:fontsize=28:fontcolor=white@0.9:box=1:boxcolor=black@0.35:boxborderw=8,\
drawtext=fontfile='$FONT':text='%{localtime\\:%Y-%m-%d %H\\\\\\:%M\\\\\\:%S}':x=w-tw-28:y=24:fontsize=22:fontcolor=white@0.85:box=1:boxcolor=black@0.35:boxborderw=8,\
drawtext=fontfile='$FONT':text='LAT ${LAT}  LON ${LON}':x=28:y=h-72:fontsize=20:fontcolor=white@0.85:box=1:boxcolor=black@0.35:boxborderw=6,\
drawtext=fontfile='$FONT':text='ALT ${ALT}ft  HDG ${HDG}  UAV-$i':x=28:y=h-40:fontsize=20:fontcolor=white@0.85:box=1:boxcolor=black@0.35:boxborderw=6"

    # shellcheck disable=SC2086
    ffmpeg -hide_banner -loglevel error $INPUT \
        -vf "$BASE,$OVERLAY" \
        -c:v libx264 -preset veryfast -tune zerolatency -profile:v baseline \
        -pix_fmt yuv420p -g $((FPS * 2)) -b:v 2M -maxrate 2M -bufsize 1M \
        -c:a aac -b:a 64k -ar 48000 -ac 1 \
        -f rtsp -rtsp_transport tcp "$URL" &

    echo "  rtsp://$ADVERTISE_HOST:$PORT/uav$i   ($LABEL)"
    FEEDLINES="${FEEDLINES}${LABEL}|rtsp://$ADVERTISE_HOST:$PORT/uav$i
"
    i=$((i + 1))
done

echo
if [ "$ADVERTISE" = "1" ]; then
    echo "Paste into uavwall-feeds.txt on the operator machine:"
    echo "---8<---"
    printf "%b" "$FEEDLINES"
    echo "--->8---"
    echo
fi
echo "Ctrl-C to stop."
wait

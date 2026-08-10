# uavwall — UAV RTSP feeds into a Pexip VMR

An operator-facing wall for live RTSP downlinks. Pick feeds from the rail,
arrange them on a canvas, and that canvas is pushed into a Pexip conference as
this participant's video — so everyone in the VMR sees the composed picture.

```
┌─────────┬──────────────────────────────────┐
│ FEED 01 │  [1-up] [2x2] [3x3] [PiP]        │  presets
│ FEED 02 │ ┌───────────────┬───────────────┐│
│ FEED 03 │ │               │               ││  send canvas — exactly
│ FEED 04 │ │               │               ││  what the VMR receives
│   ...   │ ├───────────────┼───────────────┤│  (1920x1080)
│         │ │               │               ││
└─────────┴─┴───────────────┴───────────────┘┘
```

* **Feed rail** — one card per configured feed with a live thumbnail, a
  state marker, per-feed resolution/frame rate/throughput, and a **connect
  toggle** so sources can be brought up one at a time.
* **Feed inspector** — select a card and the foot of the rail shows that
  source's URL, transport, uptime and last-frame age, with Reconnect and
  Remove.
* **Presets** — 1-up, 2x2, 3x3 and picture-in-picture arrange whatever is
  placed; drag a tile to move it, drag its corner to resize (16:9 preserved).
* **Saved layouts** — three slots (A/B/C). Click to recall, press-and-hold or
  right-click to store; they persist in `uavwall.conf`.
* **Double-click** a rail card or a tile to punch that feed full screen;
  double-click again to return to the previous layout.
* **Send to VMR** — dial `name@server` (with a PIN if needed) and the canvas is
  pushed into the conference, either as this participant's **main video** or as
  **content** (the presentation stream, shown alongside the participants).
* **Feed-loss alerts** — a connected feed that stops delivering frames raises
  an amber bar with an audible cue; the rail marker, its placement bar and its
  canvas tile all go amber, and everything clears itself when frames resume.
* **Stats** — per-feed rates, plus this process's CPU and memory and, once in
  a conference, the real transmit bitrate, packet loss and RTT from Pulse,
  shown as discrete metric cells along the foot of the window.
* **Listen** — monitor the audio of the selected source, one at a time, from
  the inspector.
* **Send audio** — put one selected feed's audio into the conference alongside
  the canvas.
* **Recording** — capture any feed (stream-copied, so the file is the original
  picture) or the composed canvas to MP4, into `recordings/`.
* **RTMP/SRT receiver** — run a bundled mediamtx from Settings so a wearable or
  phone can push into the wall; it is republished as RTSP and shown like any
  other feed.
* **Registration** — register to Infinity with a username and password so the
  wall can be *dialled into*, search the directory for VMRs and devices, and
  answer incoming calls (including while already in one).
* **Settings** — conference details, registration, send resolution and frame
  rate, RTSP transport and jitter buffer, and an editable feed list. Persisted
  to `uavwall.conf`.

## Build & run

From the repository root, with the Pulse runtime in place (see the
[repository README](../../README.md#1-install-the-pexip-pulse-runtime)):

```bash
cmake -S . -B build -DPEXIP_PREFIX="$(pwd)/sdk/macos"   # macOS
cmake --build build -j --target uavwall
./build/run-uavwall.sh
```

On Windows, build with MSVC from a developer prompt (`vcvars64.bat`) — the Pulse
runtime comes from the in-repo NuGet automatically:

```bat
cmake -S . -B build-win -G Ninja
cmake --build build-win --target uavwall
build-win\run-uavwall.bat
```

Linux should build as-is (the SDK is x86-64 only, and the incoming-call and
feed-loss sounds are silent there). [`docs/porting.md`](../../docs/porting.md)
has the detail on all three, including what differs on Windows: **LISTEN needs
`ffplay`** rather than `ffmpeg`, and canvas recording encodes in software.

### macOS app bundle

[`make-bundle.sh`](make-bundle.sh) wraps the built binary into
`build/UAV Wall.app` — the Pulse runtime, the fonts and four looping demo clips
all inside it. Double-clicking it on any Apple-silicon Mac gives a working wall
with no server, no script and no terminal; the recipient swaps in their own
footage through **Settings › Feeds › + ADD FILE**.

```bash
./demos/uavwall/make-bundle.sh                     # 109MB — this is what you ship
```

That carries the app, the Pulse runtime, the fonts, four demo clips and
**mediamtx** — everything the demo actually needs, all of it MIT or permissive.
Send it with [`docs/uavwall-setup.pdf`](docs/uavwall-setup.pdf), which walks a
non-technical recipient through Gatekeeper, the wall itself, the receiver, and
installing ffmpeg if they want the extras.

**ffmpeg is deliberately not included.** It is GPL, so bundling it makes handing
the app to someone a distribution of GPL software with a source-offer attached —
and nothing in the demo needs it. The clips are transcoded here at build time and
Pulse decodes RTSP itself. It is wanted only for **+ ADD FILE**, recording,
LISTEN and sending feed audio, and a recipient who wants those runs
`brew install ffmpeg`, obtaining it from its own distributor. The app says so at
startup when it is missing, and finds it automatically once installed.

To include it anyway, accepting the obligations:

```bash
./demos/uavwall/fetch-ffmpeg.sh                    # once: a portable arm64 build
SOURCE_OFFER="You <you@example.com>, Pexip" \
  ./demos/uavwall/make-bundle.sh --with-tools      # 172MB
```

The clips are built from `UAV_footage/prepared/feed{1..4}.mp4` if present, cut
to `CLIP_SECONDS` (default 20) at 720p — without them the app falls back to the
`rtsp://127.0.0.1:8554/uavN` URLs, which is what a source build wants.

If you do bundle ffmpeg it must be a *portable* build: Homebrew's links ~58
Homebrew dylibs, so copying that gives the recipient an app whose import and
record buttons fail. `fetch-ffmpeg.sh` downloads a static arm64 build, refuses it
unless `otool` shows zero non-system links and every codec uavwall calls is
present, and records its licence; `make-bundle.sh` then prefers it over anything
on `PATH`. Use your own with `FFMPEG_STATIC=/path/to/ffmpeg`. `NO_MEDIAMTX=1`
drops the media server if you only ever use RTSP cameras.

**Licensing.** The bundle carries `Contents/Resources/THIRD-PARTY-NOTICES.txt`,
generated from what that build actually contains, with the licence texts in
`Contents/Resources/licenses/`. The default build has **no copyleft binaries**:
five components, all permissive, plus the Pexip SDK agreement. Adding
`--with-tools` adds a GPL-3.0 ffmpeg and with it a source-offer obligation —
set `SOURCE_OFFER` and the notice carries a proper three-year written offer,
leave it unset and the build warns you before you hand it over. The demo clips
are Pexels footage; mediamtx is MIT.
[`docs/third-party.md`](../../docs/third-party.md) has the full position.

The bundle is **ad-hoc signed**, so Gatekeeper stops it on another Mac: the
recipient opens it, is refused, allows it once under System Settings › Privacy &
Security › Open Anyway, and opens it again. A Developer ID plus notarisation
(`CODESIGN_ID=…`) is the only way to remove that step.

Feed paths persist as absolute, so moving the app to `/Applications` after first
run would strand them; the app re-points any bundled clip at its own copy on
load.

### Windows app bundle

[`make-bundle.ps1`](make-bundle.ps1) is the same thing for Windows: a flat
portable folder (Windows has no `.app` spine — DLLs beside the exe is the one
layout that loads with no launcher and no PATH edits), staged from the MSVC
build:

```powershell
.\demos\uavwall\make-bundle.ps1 -Zip     # build-win\UAV Wall + UAV Wall.zip
```

Copy the folder (or send the zip) to any Windows 11 x64 machine and
double-click `uavwall.exe` — nothing to install, no terminal. The app finds the
Pulse runtime beside itself and sets `PEX_BASE_PATH` on its own. Everything the
macOS section says about clips, mediamtx, ffmpeg and `-WithTools` (spelled
`--with-tools` there) applies here too, with the Windows differences:

* **Config lives in the folder** (`uavwall.conf` beside the exe — the folder is
  the install), recordings in `%USERPROFILE%\Videos\UAV Wall`. The `clips\`
  directory is also the marker by which the app knows it is running bundled, so
  the script creates it even when there is no footage to fill it.
* **SmartScreen, not Gatekeeper**: an unsigned exe gets the "unrecognised app"
  screen, cleared with **More info › Run anyway**. Authenticode signing is the
  way past that, as Developer ID + notarisation is on the Mac.
* **`-WithTools` also copies ffplay** — LISTEN plays audio through ffplay on
  Windows — and takes the ffmpeg from `PATH` (the `winget install Gyan.FFmpeg`
  full build is a portable single exe, so there is no Windows fetch-ffmpeg
  step). Pass the source offer as `-SourceOffer "You <you@example.com>"`.
* **The receiver wants ffmpeg on Windows.** Phones push High-profile H.264,
  which the Windows Pulse decoder smears ([`docs/porting.md`](../../docs/porting.md)),
  so receiver feeds are pulled through an on-demand Baseline relay — which is
  an ffmpeg. Without one the app falls back to reading the push directly and
  says so in the status bar. For a phone-demo machine: ship `-WithTools`
  (accepting the GPL obligations), or have the recipient run
  `winget install Gyan.FFmpeg` once — the app finds it and the relay arms
  itself.
* **The import-table check replaces the `otool` walk**: the script fails the
  build if any DLL the exe or the bundled DLLs import is missing from both the
  bundle and Windows itself (it needs MSVC's `dumpbin`, and warns rather than
  guesses when that is absent). The VC++ runtime is the one assumed presence —
  near-universal, and a truly bare machine needs Microsoft's `vc_redist.x64.exe`
  once.

**Licensing is the same position as the macOS bundle**: the default build ships
no copyleft binaries. Beyond what the `.app` carries, the Windows runtime adds
the Intel compiler runtime (`libmmd.dll`, `svml_dispmd.dll` — redistributable
support DLLs) and oneTBB (`tbb12.dll`, Apache-2.0); `pexlgpl.dll` is the same
LGPL-isolation library as its dylib twin, shipped as its own replaceable DLL,
which is how LGPL §4 is satisfied. `THIRD-PARTY-NOTICES.txt` at the folder root
is generated from what the build actually contains, licence texts in
`licenses\`.

### RTMP / SRT receiver

Pulse speaks RTSP and only RTSP. A camera suits that — it serves a URL and the
wall pulls it. A wearable or a phone does the opposite: it **pushes**, dialling
out to a server, so something has to be listening and republishing. That is the
whole job of mediamtx, and with `--with-tools` the app carries one and runs it
itself.

**Settings › Feeds › Receiver (RTMP/SRT)**. Switch it on and the panel reports
`LISTENING`, how many devices are provisioned, and the RTMP and SRT addresses
the **next** device should use — with this Mac's LAN address already filled in.

**Several devices at once is the normal case**, so the receiver is a list of
slots rather than one fixed address. Each press of **+ ADD RECEIVER FEED** takes
the next free path — `live/source-01`, `live/source-02`, … — and creates the
matching feed. mediamtx serves any path, so no server-side change is needed per
device. The workflow is: read the `NEXT · RTMP` address, set the device to it,
press **+ ADD RECEIVER FEED**, then connect that feed once the device is
pushing.

`source` is only the default. What pushes in might be a wearable, a UAV, a drone
or a phone, so **Name slots** in the same panel (or `ingest_prefix` in
`uavwall.conf`) sets it — `uav` gives `live/uav-01` and a feed called `UAV 01`.
It is sanitised to lowercase alphanumerics, `-` and `_`, since it goes straight
into a URL path.

**Order does not matter.** A device takes several seconds to finish its RTMP
handshake, and mediamtx serves nothing on the path until it does — so connecting
the feed even slightly early used to fail permanently, with a `no stream is
available` that only a manual Reconnect would clear. A receiver feed now retries
every two seconds instead, showing *waiting for the device to start pushing…*
rather than raising the feed-loss alert, and comes up on its own whenever the
device appears. Switching the feed off, or stopping the receiver, ends the wait.

### Why a connect is probed first

`connect_job_run()` opens a plain socket and sends an RTSP `DESCRIBE` before it
builds anything. This is not an optimisation — it is a crash fix. A failed
connect makes Pulse tear its AVF video sink down on the main dispatch queue, and
doing that repeatedly or concurrently segfaults inside
`_pex_avf_video_sink_set_layer`: **CONNECT ALL across several dead feeds crashed
the app outright**, and so did a retry loop waiting on a device. Never building
the instance removes the whole class of failure.

Three details matter if this is ever touched:

* **Only `404` and an unreachable host count as absent.** A `401` wanting
  credentials, or a hostname rather than an address, returns *unknown* and goes
  through to Pulse unchanged — treating those as absent would strand every
  password-protected camera.
* **It runs on the connect worker, not the UI thread.** It costs a round trip,
  which the UI must never pay.
* **The connect is non-blocking with `poll()`.** `SO_SNDTIMEO` does *not* bound
  `connect()` on macOS — a host that silently drops SYNs takes the full
  75-second TCP timeout, measured. A blocking probe would freeze the window for
  over a minute per unreachable camera.

Once a slot exists, its own push addresses live in **that feed's inspector**,
alongside its URL and uptime — which is where you look when you are working on
one particular device, and it avoids a settings panel that grows a block per
camera. Inspector values are tail-elided to keep the card narrow, which cuts
exactly the host and port a device needs, so **hovering any row shows the full
value and clicking copies it**. The receiver panel's own address lines copy the
same way.

The app writes mediamtx's config itself on every start (beside `uavwall.conf` —
`~/Library/Application Support/UAV Wall/mediamtx.yml` from the Mac bundle, the
bundle folder on Windows, the working directory for a source build; overwritten
each time),
owns the process, and stops it on exit so its listening sockets do not block the
next run. Everything not needed is switched off: HLS, WebRTC, the API and
metrics would all be listeners exposed for no reason, and MoQ additionally
writes a TLS key into the working directory — which for a bundled app is `/`.
Ports are `ingest_rtsp_port` / `ingest_rtmp_port` / `ingest_srt_port` in
`uavwall.conf`, defaulting to 8554 / 1935 / 8890. If one is already taken — most
likely by a mediamtx you started yourself — the receiver reports the bind error
in the panel and switches itself back off.

Two traps worth knowing, both found against real devices:

* **The RTMP path needs two segments.** `/live/wearable` works; `/wearable` is
  rejected outright by Larix and others. mediamtx accepts either, so testing
  with ffmpeg alone never shows it.
* **High-profile H.264 breaks up on Windows** — see
  [`docs/porting.md`](../../docs/porting.md). The Windows receiver relays each
  pushed stream through a Baseline transcode automatically when ffmpeg is
  present, so this is only something to think about when ffmpeg is absent
  (set the device to Baseline profile instead). macOS decodes High cleanly.

### Windows demo kit

[`make-demo-kit.ps1`](make-demo-kit.ps1) stages the whole rig — the app, the
Pulse runtime, ffmpeg/ffplay, mediamtx, and looping demo clips prepared from
up to four MP4s in `sample_videos/` — into one portable folder with a
`start-demo.bat` that brings everything up and tears it down when the wall
window closes. Copy the folder (or the `-Zip` archive) to any Windows 11 x64
box; the target machine needs nothing installed. Clips are transcoded to
Constrained Baseline once at kit-build time, both so publishing is a ~0% CPU
stream-copy and because of the High-profile decode issue in
[`docs/porting.md`](../../docs/porting.md). Registration credentials are
deliberately not shipped; set them in Settings on the target machine.

## Test feeds

There is no need for real drone hardware to demonstrate this.
[`scripts/uav-streams.sh`](../../scripts/uav-streams.sh) stands up a local RTSP
server (mediamtx) and publishes synthetic downlinks with ffmpeg — a drifting
monochrome scene with a reticle, running clock, and per-feed coordinates,
altitude and heading:

```bash
brew install mediamtx ffmpeg
./scripts/uav-streams.sh -n 4       # rtsp://127.0.0.1:8554/uav1 … /uav4
```

Then press **Connect all** in uavwall — the default feed list matches those
URLs, so a fresh checkout demonstrates itself.

Options: `-n` feed count, `-s WxH`, `-t sensor|bars`, `-f clip.mp4` to loop real
footage instead, `-l` seconds of scene to pre-render, `-p` port, `-a` to
advertise LAN URLs, `-S`/`-R` to publish extra feeds over SRT/RTMP, `-i` for
ingest slots real devices push into, `-A` for the audio track.

**Audio.** The synthetic feeds carry an audio track by default, but at −48 dB it
is inaudible — it exists only to prove the track is there. To demonstrate the
wall's **LISTEN** button you want something you can hear, and something that
differs per feed so switching sources is obviously doing something:

| `-A` | What it sounds like | Measured |
| --- | --- | --- |
| `quiet` | brown noise, present but inaudible (default) | −48 dB |
| `rotor` | brown noise with a per-feed tremolo — reads as engine wash | −28 dB |
| `tone` | a clean per-feed pitch, unmistakable when switching | −29 dB |
| `off` | no audio track at all | — |

```bash
./scripts/uav-streams.sh -n 4 -d UAV_footage/prepared -i 1 -a -o -A rotor
```

Pitch and beat rate both vary with the feed index, so `rotor` on feed 1 and feed
3 are distinguishable by ear.

> The synthetic imagery is deliberately abstract. ffmpeg cannot conjure
> convincing aerial footage, and for a demonstration it is arguably better that
> the picture is obviously synthetic than that it imitates real sensor imagery.
> Use `-f` with real footage when fidelity matters.

### The standard demo setup

Four drone downlinks over RTSP plus one wearable pushing in — one command, one
process:

```bash
./scripts/uav-streams.sh -n 4 -d UAV_footage/prepared -i 1 -a -o -A rotor
```

* `-n 4 -d …/prepared` — the four synthetic downlinks, from real footage
* `-i 1` — one ingest slot (`hawkeye-21`) for the wearable
* `-a` — print the LAN address, so a phone has something to point at
* `-o` — no publish credential, which is one less variable at a demo
* `-A rotor` — audible per-feed audio, so **LISTEN** has something to play

The matching feed list in `uavwall.conf`:

```ini
feed=HAWKEYE 21|rtsp://127.0.0.1:8554/uav1
feed=KESTREL 33|rtsp://127.0.0.1:8554/uav2
feed=NOMAD 14|rtsp://127.0.0.1:8554/uav3
feed=OSPREY 12|rtsp://127.0.0.1:8554/uav4
feed=WEARABLE 01|rtsp://127.0.0.1:8554/live
#feed=WEARABLE 01|rtsp://127.0.0.1:8554/live/hawkeye-21
```

Loopback rather than the LAN address throughout: everything runs on this
machine, and a DHCP change would otherwise break every line at once. The
wearable is the same device either way — swap the two `WEARABLE 01` lines to
put it on SRT instead of RTMP:

| | Larix Broadcaster setting |
| --- | --- |
| RTMP | URL `rtmp://<lan-ip>:1935/live/hawkeye-21` → arrives on path `live` |
| SRT | URL `srt://<lan-ip>:8890`, stream ID `publish:live/hawkeye-21` → arrives on `live/hawkeye-21` |

One phone publishes one stream, so only one of the two can be live; the other
shows offline in the rail, which costs nothing.

> **A commented feed does not survive Settings → Save.** `save_config()`
> rebuilds the file from memory and knows only about active feeds, so the
> disabled line above is dropped the moment the app writes the config. Quitting
> is safe — the app leaves a file that changed underneath it alone — but an
> explicit Save is not. To keep both, make it a real feed and simply leave it
> disconnected.

### Using your own video files

**Settings → Feeds → + ADD FILE** points the wall at a clip on disk. No media
server and no RTSP: Pulse decodes the file directly, and it behaves like any
other feed — rail card, canvas tile, recording, LISTEN.

The clip is **transcoded once on import** to Constrained Baseline 1080p, the
same recipe [`scripts/prepare-footage.sh`](../../scripts/prepare-footage.sh)
uses, and cached against the source's size and modification time so adding it
again is instant. The footer shows a `PREPARING` cell while that runs.

That step is not optional. Pulse decodes High-profile H.264 badly — macroblocked
through the video mixer on macOS, around 8fps through the file session — and
libx264's default, along with every phone, camera and editor, produces High. A
clip added without preparing looks like a stream with heavy packet loss.

> This is an SDK-level defect rather than something the demo ought to be working
> around; [`docs/porting.md`](../../docs/porting.md) records the same profile
> problem on Windows. Worth raising with Pexip.

Without ffmpeg the file is added unprepared and the status line says so, rather
than failing silently.

### Real devices pushing in — wearables and body cameras

The generator publishes feeds *to* the wall. Real devices work the other way:
they push, and something has to listen. The same `mediamtx` that serves the
synthetic feeds does that too, so it stays one process either way:

```bash
./scripts/uav-streams.sh -n 4 -S 1 -R 1 # 4 over RTSP + 1 over SRT + 1 over RTMP
./scripts/uav-streams.sh -n 4 -i 2      # 4 synthetic feeds + 2 slots for real devices
./scripts/uav-streams.sh -n 0 -i 6      # devices only, nothing synthetic
./scripts/uav-streams.sh -i 2 -a        # advertise the LAN address to point devices at
```

`-S` and `-R` publish the *same generated picture* over SRT and RTMP instead of
RTSP, so a demo can show all three ingest paths arriving on one wall with no
hardware at all. `-i` is the other half: empty slots that wait for a real device
to push into them.

It prints a push URL per slot in each protocol and the `feed=` lines to paste
into `uavwall.conf`. Whatever a device pushes, the wall pulls back as ordinary
RTSP — nothing in the app changes:

| Protocol | Port | Typical device |
| --- | --- | --- |
| SRT | 8890/udp | LTE-bonded bodycams; the usual choice on a lossy link |
| RTMP | 1935 | most bodycams, phone encoders, OBS |
| RTSP push | 8554 | some IP and tactical cameras |
| WebRTC / WHIP | 8889 | browser and app-based |

Publishing needs a credential (`-u user:pass`, default `uav`/`uav`, or `-o` to
disable); this machine is exempt, since that is where the synthetic publishers
and the wall itself run. A slot with nothing attached simply shows offline in
the rail until a device starts sending.

**Pointing a phone or body camera at it.** Verified with Larix Broadcaster on
Android; the same shape applies to most encoders.

* **RTMP needs two path segments** — `rtmp://host:port/app/stream`. Larix
  refuses a single-segment URL outright ("Can't find rtmp app and stream"),
  which is why the slots are `live/<callsign>` rather than just `<callsign>`.
  mediamtx accepts either, so this only shows up on a real device — testing with
  ffmpeg will not find it.
* **Larix publishes to the app segment**, treating the whole path as the app and
  sending no stream name, so `rtmp://host:1935/live/hawkeye-21` arrives at path
  `live`. Pull it back from `rtsp://host:8554/live`. For several phones at once,
  give each a distinct app segment.
* **Credentials go in the URL for RTMP** (`rtmp://user:pass@host/...`) rather
  than the encoder's separate Login/Password fields, which use RTMP's own auth
  handshake. For SRT they go in the stream ID: `publish:live/<name>:user:pass`.
* Start with `-o` (no credential) for first contact, then add auth once frames
  are arriving — it removes a whole class of ambiguity from the first attempt.
* The free tier of Larix burns a **watermark** into the picture, and a phone
  held upright sends portrait video inside a landscape frame. Both matter in
  front of an audience.

**When a device will not connect**, `-v` puts mediamtx at `logLevel: info`,
which distinguishes the two cases the encoder's own error message cannot:

```bash
./scripts/uav-streams.sh -i 2 -a -o -v
```

Nothing logged means the traffic never arrived — wrong network, client
isolation, or a firewall. A logged connection followed by an error means it
arrived and was refused, which names the cause. A device that connects and
publishes appears as:

```
[RTMP] [conn 10.0.2.12:43058] is publishing to path 'live'
```

**Why a separate service and not the app itself.** Pulse can listen for RTMP —
`pulse_rtmp_session_connect_input()` is a real server with TLS and auth — but
one listener binds one port to one stream, so every device would need its own
port. And Pulse speaks no SRT at all, which is what most LTE-bonded wearables
use. mediamtx takes every protocol on one port each with a path per device, and
re-serves them as the RTSP the wall already consumes.

> **macOS gotcha, already handled in the script.** mediamtx binds a bare
> `:port` as **IPv6-only**, so an SRT handshake from an IPv4 device never
> arrives — and nothing is logged at either end, which makes it look like the
> device is at fault. The generated config binds `0.0.0.0` explicitly.

### Running the generator on a separate machine

Recommended when measuring, or when anyone might look at a task manager during
a demo — the feeds then also arrive over the network, as they would in reality:

```bash
# Box B (the "downlinks")
./scripts/uav-streams.sh -n 4 -a     # prints a ready-to-paste feed list

# Box A (the operator): paste into uavwall-feeds.txt, then
./build/run-uavwall.sh
```

## Configuration

Everything lives in **`uavwall.conf`** in the working directory, editable by
hand or through **Settings** in the app. It is read at startup and written on
exit (and on Save). With no file present, the defaults below are used — which
match what `scripts/uav-streams.sh` publishes, so a fresh checkout
demonstrates itself.

```ini
# Conference to send the composed canvas into.
vmr=ops@example.com
pin=
display_name=UAV Wall

# What the far end receives. 1280x720 roughly halves compositing cost.
canvas=1920x1080
send_fps=30
send_as=main               # main = our video, content = presentation stream

# Registration, so the wall can be dialled into. Leave reg_host empty to
# disable. The host is a domain — Pulse resolves _pexapp._tcp SRV itself.
reg_host=
reg_alias=uavwall@example.com
reg_user=
reg_pass=
reg_auto=false             # register at startup
auto_accept=false          # answer incoming calls without asking

# Feed transport. TCP suits most IP cameras; some only offer UDP.
rtsp_transport=tcp
rtsp_latency_ms=200
autoconnect=false          # connect every feed at startup

# Where recordings are written, relative to the working directory.
record_dir=recordings
audio_delay_ms=0          # delays feed audio when it runs ahead of the canvas

# Interface. ui_scale applies on next start.
ui_scale=1.2
show_stats=true

# Saved layouts, recalled from the A/B/C slots. feed,x,y,w,h per tile.
preset_a=
preset_b=
preset_c=

# One per feed: feed=NAME|URL
feed=FEED 01|rtsp://127.0.0.1:8554/uav1
feed=NORTH RIDGE|rtsp://user:pass@10.0.1.50:554/stream1
```

An older `uavwall-feeds.txt` is migrated automatically the first time.

The settings that matter most:

* **`canvas` and `send_fps`** are the performance levers. Measured on the same
  machine with two feeds: `1920x1080` at 30fps costs 78% of a core, and
  `1280x720` at 15fps costs 48% — for a picture that is indistinguishable in a
  2x2 or 3x3 grid.
* **`send_as`** — `main` replaces this participant's camera, so every endpoint
  shows the wall. `content` sends it on the presentation stream instead, which
  most endpoints display alongside the people rather than in place of one;
  uavwall takes the conference floor automatically in that mode.
* **`rtsp_transport`** — TCP suits most IP cameras, but some only offer UDP and
  will fail to connect over TCP. The reason appears in the feed card's tooltip.
* **`autoconnect`** — worth turning on for a demo machine, so the wall comes up
  live.

Local preview costs nothing worth reclaiming: skipping the GPU upload for
feeds that are not displayed was measured at 112% CPU either way with nine
feeds, because decode and compositing dominate and the upload is near-free on
unified memory. The way to spend less is to connect fewer feeds, or to send a
smaller canvas.

Real cameras usually want credentials in the URL
(`rtsp://user:pass@host/path`).

## Cost and scaling

There is a reproducible benchmark built in, so these numbers can be regenerated
on any candidate machine rather than taken on trust:

```bash
./scripts/uav-streams.sh -n 9                       # feeds to consume
./build/run-uavwall.sh --bench 20 --feeds 9 --grid 3x3
```

It connects every feed, applies the grid, forces the compositor to run as if
sending, discards the first 5 seconds (RTSP connect and decoder warm-up) and
prints one row: feeds, live, average CPU, peak CPU, RSS, composite ms and the
mean per-feed frame rate.

Measured on an **Apple M4 Pro (14 cores)** with 720p25 feeds. CPU is a
percentage of **one** core, as reported by the OS — 100% is one core of
fourteen, not the whole machine:

| Feeds | CPU | RSS | Composite / frame |
|---|---|---|---|
| 1 | 45% | 174 MB | 13.8 ms |
| 4 | 78% | 279 MB | 16.6 ms |
| 9 | 114% | 470 MB | 16.9 ms |

The shape matters more than the absolute figures:

* **A fixed floor of roughly 45%**, almost all of it compositing the 1920x1080
  canvas on the CPU. It is nearly independent of feed count — the same two
  million pixels are written whether they come from one feed or nine.
* **Roughly 8.6% CPU and 37 MB per additional feed**, for decode and texture
  upload. Sixteen feeds extrapolates to about 1.7 cores and 730 MB.
* Idle — feeds connected but nothing being sent — the app sits at a few
  percent, because the canvas is only composited when there is somewhere to
  send it, and then only at the 30fps the send session uses.

> **The benchmark does not include the outgoing encode.** There is no
> conference in bench mode, so H.264 encoding of the 1080p30 output is not
> counted. Budget another 30–60% of a core for a software encoder, materially
> less where Pulse can use hardware encode.

### Choosing a machine

The binding constraint is **single-core speed**, because compositing runs on
one thread. An M4 Pro core does it in 16.9 ms of the 33 ms frame budget; a core
around 1.5x slower needs ~25 ms, which works but leaves little headroom.

For a nine-feed demonstration: an **Intel Core Ultra 5/7 (Meteor Lake or
newer) or 12th-gen-plus i5/i7 mini PC with 16 GB**. Intel additionally lets
Pulse use Quick Sync (the runtime carries VAAPI/QSV support) to offload decode
and encode. Avoid N100-class low-power chips — roughly half the single-core
performance would put compositing alone near the frame budget.

If more headroom is needed, in increasing order of effort: drop the send canvas
to 720p (2.25x less pixel work), lower the send rate to 15–20fps, multithread
the blit across rows, or move compositing to the GPU.

### Cost of the test rig

The generator costs roughly 20–25% of a core per feed: it pre-renders the scene
once and loops it, drawing only the overlays live. (Synthesising every frame
with ffmpeg's `geq` filter cost 5x that.) This is an artefact of the test rig
and disappears when real aircraft supply the feeds — run it on a separate
machine with `-a` when measuring.

## Registration and dial-in

The wall can register to Infinity, which makes it callable: an operator already
in a conference dials `uavwall@your-domain` and the feeds arrive as a
participant, rather than the wall having to join a VMR itself. Dialling out
still works exactly as before — the two are independent.

Turn it on in **Settings → Registration** (host, alias, username, password, and
whether to register at startup); the footer shows the current state. Scope was
decided deliberately:

* **Username/password only — no SSO.** A wall is a fixed installation, not a
  person, so a device credential is the better fit. It also sidesteps two macOS
  problems: SSO needs an `.app` bundle to receive the `pexip-auth://` callback,
  and only one application per machine can own that URL scheme — `pexclient`
  already does.
* **PINs for VMRs**, not SSO-protected ones. A PIN set in Settings is submitted
  automatically on dial-out; anything else prompts the operator. If Infinity
  asks a second time the configured PIN was wrong, so the prompt appears rather
  than the same PIN being resubmitted forever.

What registration enables:

* **Incoming calls.** A banner offers accept/decline, with an audible ring, a
  Dock bounce and a window raise. **Auto-accept** is available for an
  unattended wall.

  **When the wall is already in a call**, the banner instead offers *Disconnect
  and accept* or *Reject* — auto-accept deliberately does not apply here, since
  dropping a conference in progress is the operator's decision. Note the
  ordering hazard behind it: Pulse's incoming callback blocks a worker thread
  until it returns, and leaving a conference is itself asynchronous, so
  accepting means starting the disconnect on the UI thread, waiting (bounded)
  for the conference to reach DISCONNECTED, and only then returning `true`.
* **Directory search.** Typing in the VMR field queries
  `pulse_registrations_query_alias()` and offers matching services and devices;
  picking one fills the field, ready to send. Unregistered, the field is plain
  text entry rather than appearing broken.

**One instance per alias.** Infinity allows a device alias to be registered
once; starting a second uavwall with the same `reg_alias` displaces the first,
and the displaced one then logs

```
Failed to refresh registrations(<alias>) token: Unexpected reply from server.
```

once a second until it gives up. The same thing happens after an unclean stop,
because the registration survives on the server until it expires — so the app
handles SIGINT and SIGTERM and runs its normal shutdown (deregister, stop
recordings, disconnect) rather than dying where it stands. Ctrl-C is safe;
`kill -9` is not, and neither is running two copies against one alias.

Registration belongs to a single Pulse instance, and an incoming call is
answered on that same one — so the long-lived instance that keeps global
GStreamer state alive is also the conference instance, rather than one being
created per call.

## Listening to a feed

Select a source in the rail and press **LISTEN** in the inspector to hear it.
Exclusive by design: starting one stops whatever was playing, because six
downlinks at once is noise rather than information. The case it exists for is
hearing what is happening around a body camera, not drone engine wash.

Like recording, it is an `ffmpeg` child — `-vn -f audiotoolbox` to the default
output — so it needs ffmpeg present, and it opens the URL itself, meaning a feed
can be listened to whether or not it is connected in the wall.

**Pulse cannot do this**, which is worth recording because two plausible
approaches fail in different ways. A feed's RTSP session is bound to MAIN as
that instance's *source* — it is the microphone, not remote media — so:

* connecting a **speaker to MAIN** succeeds and is silent, because it renders
  incoming call audio and there is no call;
* asking for a **selfview audio** data session, mirroring how the video is
  pulled, does not return an error — it **aborts the process** with
  `"Connecting a selfview to audio does not make any sense"`.

Pulse has no notion of monitoring your own input, so the audio never comes back
through the SDK at all.

**LISTEN lags the picture by a few hundred milliseconds, and that is as good as
it gets.** The audiotoolbox output device exposes no buffer control — only
`-list_devices` and `-audio_device_index` — so there is nothing to tune on this
path, and an `ffplay` build with `-sync ext` and every low-latency flag measured
the same by ear. What remains is CoreAudio's own output queue, below anything
either player reaches.

That is acceptable for what LISTEN is for: hearing *what is happening* around a
source — someone shouting, a vehicle approaching — not judging lip sync. It has
no bearing on **what the conference hears**, which travels a different path and
does arrive in sync. Judge sync there, never here.

## Sending feed audio to the VMR

**SEND AUDIO** in the inspector puts the selected feed's audio into the
conference. One source at a time — the canvas header shows which, or `AUDIO
silent` when none is chosen.

One rather than a mix, deliberately. A mix needs per-feed gain, clipping and
drift handling between independent clocks, and four drones at once is not
something anyone wants to listen to. A gallery picks a source; so does this.

How it is put together, and why:

* The PCM comes from **a second decode** — an ffmpeg child writing s16le to a
  pipe — because Pulse will not give us a feed's audio at all (see above).
* A reader thread fills a ring buffer; the render loop drains it and attaches
  the samples to **the same `push_frame` call that carries the canvas**. One
  call site on one thread: the SDK makes no promise about two threads pushing
  frames into one instance.
* The sample count follows **wall clock** since the previous push, so the stream
  keeps real time even though the render loop's cadence is not perfectly even.
  Underrun pads with silence; a stall is capped so it cannot dump a burst.
* The conference input is configured for audio **whether or not a source is
  selected**, and pushes silence when none is. Adding audio later would mean
  renegotiating the session mid-call.

> **Feedback.** The wall sends audio into a conference the operator may also be
> listening to in the same room. Use headphones, or keep the conference muted
> locally — this is a real loop, not a theoretical one.

### Lining audio up with the picture

The two arrive at different times. Video goes through Pulse's RTSP client — a
jitter window (`rtsp_latency_ms`, 200ms by default) plus decode — before it ever
reaches the compositor; the ffmpeg audio path is shorter. So audio runs ahead,
by a few hundred milliseconds.

**Real timestamping is not available.** `PulseDataSessionFrame` is
`{update_config, audio, video}` — there is no PTS field anywhere in the
data-session API, and Pulse stamps frames itself as they arrive. There is no way
to say *this sample belongs at time T*; only when to hand it over.

Judge sync **in the VMR, not through LISTEN** — the local monitor is a separate
ffmpeg pipeline with its own latency, and it is not what the conference hears.
Chasing the monitor's lag will send you after a number that does not exist.

**The dominant term was ffmpeg's own probing**, not anything in this app.
Left at its defaults, ffmpeg reads the input for about three seconds before
emitting a sample — measured at **3.15s, against 0.21s** with
`-fflags nobuffer -flags low_delay -probesize 32 -analyzeduration 0`. Any
alignment control is meaningless beside that, which is exactly how it behaved:
the delay slider appeared to do nothing. Both audio paths — the conference feed
and **LISTEN** — now use those flags.

What is left is `audio_delay_ms`, which holds audio back when it arrives *ahead*
of the canvas, with a live slider in **Settings → Canvas & sending**. It
defaults to **0**, because with the low-latency flags the audio path is the
shorter of the two.

If audio still lags the picture, this control cannot help — it only ever delays.
The lever is the other end: lower `rtsp_latency_ms` so Pulse's video path
buffers less. Delaying video is not practical; it would mean queueing composited
frames at 8MB apiece.

Tune by ear against something with speech or a hand clap in it. A phone is
ideal; drone engine wash tells you nothing about sync.

## Recording

Any feed, and the composed canvas, can be captured to MP4 while the wall runs.
Arm a source with the ring on its rail card, and the canvas with the ring in the
canvas header; the footer shows a REC cell with the elapsed time whenever
anything is recording. Files land in `record_dir` (default `recordings/`,
created if missing) named by callsign and UTC start —
`HAWKEYE-21_20260729T085346Z.mp4`, `CANVAS_20260729T085346Z.mp4`.

Pulse has no recording API — `pulse_file_session.h` is playback only — so each
recording is an `ffmpeg` subprocess. **This makes ffmpeg a runtime dependency**
for the feature alone; without it the record controls are disabled and Settings
→ Recording says so rather than failing at the point of use.

The two shapes are quite different:

* **A feed** is stream-copied: ffmpeg opens the RTSP URL itself, so nothing is
  decoded or re-encoded (measured at **0.3% CPU and ~36 MB** per recording) and
  the file holds the *original* picture rather than the downscaled canvas tile.
  It works whether or not that feed is connected in the wall or placed on the
  canvas. Budget roughly 900 MB/hour at 2 Mbps.
* **The canvas** has no source stream to copy, so composited frames are piped to
  ffmpeg and encoded — `h264_videotoolbox` on macOS, so the encode is in
  hardware and costs about 10% of a core at 1080p30. Recording the canvas forces
  the compositor to run even with no conference up, which is the same ~45% floor
  the sizing table describes.

Three things about this were measured rather than assumed, and each cost a
rewrite:

* **Stopping.** `SIGINT` hangs on an RTSP input — a test sat for two minutes.
  `SIGKILL` leaves an MP4 with no moov atom that nothing will play, and
  fragmented MP4, MPEG-TS and Matroska all failed to survive it in testing.
  Writing `q` to ffmpeg's stdin exits cleanly; the canvas encoder, whose stdin
  carries frames, gets the same result from EOF when the pipe closes. Quitting
  the app stops every recording and waits (bounded at 10s) for the files to
  finalise — so **quit properly rather than force-quitting**, or you lose the
  in-progress captures.
* **Whole frames only.** A 1920x1080 RGBA frame is 8.3 MB and a pipe buffer is
  64 KB. Writing non-blocking from the UI thread tore every frame in half and
  produced a 56-second recording containing 11 usable frames. A writer thread
  now blocks on the pipe on the UI thread's behalf, a few frames deep, and drops
  whole frames if it ever falls behind.
* **The recording keeps its own clock.** The render loop cannot always sustain
  `send_fps` composites a second under load, and a file that is simply short
  plays back fast — 21s of canvas against 25s of the same wall-clock window. The
  cadence is now held independently and the previous picture repeated when a
  frame is late, so duration matches what the operator watched to within 1%.

## Planned

* **SSO-protected VMRs**, if a demonstration ever needs one. The blocker is the
  macOS URL-scheme arbitration above, and the fallback is a script that
  repoints `pexip-auth://` at whichever app is being demonstrated.

## Where things live

```
src/main.cpp   The whole demo: feed instances, compositor, canvas UI, VMR send.
src/Theme.h    Design tokens — Dark Frosted, plus the Instrument additions.
assets/fonts/  DM Sans and IBM Plex Mono (both SIL OFL).
design_handoff_uavwall_instrument/
               The "Instrument" UI spec this build implements, with the HTML
               mock it was measured from.
```

## Interface

The UI follows the **Instrument** direction from
[`design_handoff_uavwall_instrument/`](design_handoff_uavwall_instrument/):
controls collected into four labelled groups (destination, layout, sources,
view) rather than one row of pills; every number in a monospace face; and the
sending state signalled in three places at once — a red strip along the top
edge of the window, an ON AIR readout in the header, and red corner ticks on
the canvas — so it is unmissable from across a room.

## How it works

One `Pulse *` per feed — RTSP in via `pulse_rtsp_session_connect_input()`,
frames pulled back out of a data session — and one more for the conference,
dialled with `pulse_connect_with_rest_async()`. The canvas is composited on the
CPU and pushed in with `pulse_data_session_push_frame()` on MAIN, which is what
makes it appear as this participant's camera. That multi-instance pull/push
pattern comes from [`videowall`](../videowall/), which does the same thing for
a production switcher.

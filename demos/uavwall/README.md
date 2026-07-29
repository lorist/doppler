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
advertise LAN URLs.

> The synthetic imagery is deliberately abstract. ffmpeg cannot conjure
> convincing aerial footage, and for a demonstration it is arguably better that
> the picture is obviously synthetic than that it imitates real sensor imagery.
> Use `-f` with real footage when fidelity matters.

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

Registration belongs to a single Pulse instance, and an incoming call is
answered on that same one — so the long-lived instance that keeps global
GStreamer state alive is also the conference instance, rather than one being
created per call.

## Planned

**Recording.** The operator marks one or more feeds and each is written to its
own MP4, independently of whether that feed is on the canvas.

Pulse cannot do this: `pulse_file_session.h` is playback only, and while the
dylib exports an internal `_pmx_data_session_add_file_output`, no public API
reaches it. The plan is instead one `ffmpeg` subprocess per recorded feed:

```
ffmpeg -rtsp_transport tcp -i <feed url> -c copy -movflags +faststart out.mp4
```

`-c copy` never decodes, so it measured **0.3% CPU and ~36 MB RSS** per
recorder — negligible beside the ~8.6%/feed compositing — and it captures the
original stream rather than the downscaled canvas tile. Budget roughly
900 MB/hour/feed at 2 Mbps.

Stopping is the part that needs care, all of this measured rather than assumed:

* `SIGINT` **hangs** on an RTSP input — a test sat for two minutes.
* `SIGKILL` leaves no moov atom and an unplayable file. Fragmented MP4 does not
  save it: with `+frag_keyframe+empty_moov`, `-frag_duration` and
  `-flush_packets 1`, seven seconds of recording had put between 28 and 1267
  bytes on disk. MPEG-TS and Matroska wrote nothing at all.
* **Writing `q` to stdin exits cleanly** and yields a valid file. So spawn with
  stdin as a pipe, send `q\n`, wait bounded — and stop every recorder on app
  exit, since a force-quit costs the in-progress files either way.

Open: where the toggle lives. A per-feed control in the rail was tried before
for connect/disconnect and got in the way, so the alternative is the Settings
feed list. Note also that this makes `ffmpeg` a **runtime** dependency, where
today it is only needed to generate test feeds — the control should degrade
when it is absent rather than fail silently.

Recording the composed canvas is a separate mechanism, since there is no source
stream to copy: pipe the RGBA buffer to an ffmpeg stdin and encode, which
`h264_videotoolbox` does in hardware on macOS.

## Also planned

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

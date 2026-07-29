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
  green/amber liveness dot and (with stats on) resolution, frame rate and
  throughput.
* **Presets** — 1-up, 2x2, 3x3 and picture-in-picture arrange whatever is
  placed; drag a tile to move it, drag its corner to resize (16:9 preserved).
* **Double-click** a rail card or a tile to punch that feed full screen;
  double-click again to return to the previous layout.
* **Send to VMR** — dial `name@server` (with a PIN if needed) and the canvas is
  pushed into the conference, either as this participant's **main video** or as
  **content** (the presentation stream, shown alongside the participants).
* **Stats** — per-feed rates, plus this process's CPU and memory and, once in
  a conference, the real transmit bitrate, packet loss and RTT from Pulse.
* **Settings** — conference details, send resolution and frame rate, RTSP
  transport and jitter buffer, and an editable feed list. Persisted to
  `uavwall.conf`.

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

# Feed transport. TCP suits most IP cameras; some only offer UDP.
rtsp_transport=tcp
rtsp_latency_ms=200
autoconnect=false          # connect every feed at startup

# Interface. ui_scale applies on next start.
ui_scale=1.2
show_stats=true

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

## Planned

Features to borrow from [`pexclient`](../pexclient/), which already implements
each of them against a live Infinity deployment:

* **Register to Infinity**, with username/password or SSO, so the wall is a
  known device rather than an anonymous guest — and so it can be called.
  pexclient's `start_register()` and its registration-state callback port
  directly.
* **Join SSO-protected VMRs**, including the PIN-then-SSO sequence. Both are
  parked-callback flows in Pulse (the worker thread blocks until the UI
  answers), the same shape as pexclient's PIN and provider-chooser modals.
* **An `.app` bundle**, which is a *prerequisite* for either of the above on
  macOS: Infinity returns the IdP token as a `pexip-auth://` URL, and macOS only
  delivers custom URL schemes to bundles that declare one. See pexclient's
  `make-bundle.sh` and the SSO prerequisites table in its README.

Two implementation notes for whoever picks this up:

* The conference instance must be created with
  `pulse_new_with_internal_sso_handling()` rather than `pulse_new()` — on macOS
  registration fails without it even for password auth. Whether the per-feed
  instances and the keepalive can remain plain `pulse_new()` needs checking,
  since the first instance created performs global initialisation.
* Registration is a property of one Pulse instance. uavwall runs many, so the
  registered identity should live on the conference instance only.

## Where things live

```
src/main.cpp   The whole demo: feed instances, compositor, canvas UI, VMR send.
src/Theme.h    Design tokens, shared with pexclient.
assets/fonts/  DM Sans (SIL OFL).
```

## How it works

One `Pulse *` per feed — RTSP in via `pulse_rtsp_session_connect_input()`,
frames pulled back out of a data session — and one more for the conference,
dialled with `pulse_connect_with_rest_async()`. The canvas is composited on the
CPU and pushed in with `pulse_data_session_push_frame()` on MAIN, which is what
makes it appear as this participant's camera. That multi-instance pull/push
pattern comes from [`videowall`](../videowall/), which does the same thing for
a production switcher.

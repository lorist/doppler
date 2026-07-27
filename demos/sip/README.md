# doppler sip-demo

A tiny demo that places a **SIP** video call using the Pexip **Pulse** C API
as the media engine, with **PJSIP** doing the SIP signalling and **Dear ImGui**
as the application layer.

The companion to the [`doppler`](../doppler/) demo (which talks to a Pexip Infinity conference
directly via Pulse's built-in REST client). Here Pulse never speaks to a
conferencing node itself — we use the two-stage manual setup path so we own
the SDP exchange, and we hand that SDP off to PJSIP which puts it on the
wire in an `INVITE`.

```
┌──────────────────────────────┐
│   ImGui (GLFW + OpenGL3)     │   <-- single field:
│                              │       "SIP URI to call"
│  havard@pexipdemo.com        │
│  [Call]  [Hang up]           │
│                              │
│  State: In call              │
└──────────────┬───────────────┘
               │
               │ 0. pulse_options_set_app_transport(...) - register our
               │    PulseAppPacketCallback so Pulse hands outbound RTP/RTCP
               │    to us instead of writing it to its own UDP sockets.
               │
               │ 1. pulse_setup_stage_1_from_structure(is_sip=true)
               │    -> local SDP offer (ports/IP point at Pulse internals)
               ▼
        ┌──────────────┐                   ┌────────────┐
        │ libpexpulse  │                   │   PJSIP    │
        │  (media)     │                   │ (signalling)
        └──────┬───────┘                   └─────┬──────┘
               │  2. AppTransport binds one UDP   │
               │     socket per m=/RTP+RTCP wire,│
               │     rewrites the offer's m=/c=  │
               │     /a=rtcp lines to advertise  │
               │     OUR ports/IP.               │
               │  3. rewritten offer SDP ──────▶ │
               │                                 │  INVITE → pexipdemo.com
               │                                 │  ◀── 200 OK (answer SDP)
               │  4. answer SDP + Call-ID ◀───── │
               │  5. AppTransport parses answer  │
               │     for remote IP+ports, fills  │
               │     channel→sockaddr table.     │
               │ 6. pulse_setup_stage_2_from_structure
               ▼
        ┌─────────────────────────────────────────────────────┐
        │ media flows:                                        │
        │   Pulse -> PulseAppPacketCallback -> sendto() ----▶ │
        │   recvfrom() -> pulse_app_transport_push() -> Pulse │
        └─────────────────────────────────────────────────────┘
```

## What's in the box

| File                          | Purpose                                                 |
| ----------------------------- | ------------------------------------------------------- |
| `src/main.cpp`                | GLFW + ImGui + Pulse glue, almost identical to the REST demo. |
| `src/sip_ua.h/.cpp`           | Minimal PJSIP wrapper. TCP only, no REGISTER, one outbound call at a time. |
| `src/app_transport.h/.cpp`    | Owns one UDP socket per `PulseAppChannelId` wire; bridges Pulse's app-transport callback to the sockets and the SIP peer. Includes the offer SDP rewriter and the answer SDP remote-endpoint parser. |
| `CMakeLists.txt`              | Build glue. Reuses the parent project's `pexip::pulse` imported target and Dear ImGui FetchContent. |

## Prerequisites

In addition to the [main repository's build deps](../../README.md), you need
PJSIP. **PJSIP is *not* in the Ubuntu 24.04 archive**, so we ship a small
helper script that downloads, builds and installs it under `/usr/local`:

```bash
sudo ./scripts/install-pjsip.sh
```

(The script is idempotent. It installs `libpjproject.pc` into pkg-config's
search path, which is how CMake finds it.)

## Build

The demo is opt-in so the lean default `doppler` build keeps working without
any extra dependencies. From the **repository root**:

```bash
cmake -S . -B build -DBUILD_DOPPLER_SIP=ON
cmake --build build -j --target doppler-sip
./build/run-doppler-sip.sh
```

## Run

Type a SIP URI (`alice@example.com` or `sip:alice@example.com`) and press
**Call**. PJSIP resolves the host, sends `INVITE`, and once the 200 OK
arrives the answer SDP is handed back to Pulse via
`pulse_setup_stage_2_from_structure()`. From that point on media flows
through Pulse exactly as in the REST demo — same camera/mic/speaker
bindings. The two video tiles in the UI ("Remote" and "Self-view") are
fed from Pulse's data-session API (`video/x-raw, format=RGBA`) and
uploaded into GL textures each frame, so you can see what we are
actually receiving without Pulse spawning its own native windows
(which are suppressed via `pulse_options_set_*_window_handle(nullptr)`).

**Hang up** sends a SIP `BYE` and tears the Pulse session down.

## Notes / things to investigate

* **AppTransport bind window**: `pulse_options_set_app_transport()` is
  only accepted while the Pulse session status is `UNINITIALIZED`. The
  named barrier (per `pulse_options.h`) is
  `pulse_setup_stage_1_from_structure` / `_from_response_buffer` - after
  the first stage 1 call, any further (un)bind returns
  `PULSE_ERROR_ALREADY_CONNECTED`. The `connect_default_devices` /
  `data_session_connect_*` / `device_session_connect_*` calls do NOT
  advance the status, so the bind can happen at any point between
  `pulse_new_external_rest()` and the first stage 1. The demo defers
  the entire Pulse bring-up - `pulse_new_external_rest()`, the bind,
  callback registration, native-window suppression, default device
  attach, data-session attach - until the operator presses **Call**
  (see `lazy_pulse_init()` in `src/main.cpp`). Before that, the demo
  does nothing to Pulse at all, so the AppTransport checkbox is a
  pure intent flag: untick it and Pulse genuinely never sees our
  callback. `DOPPLER_SIP_BRIDGE` only seeds the initial checkbox
  state (default = on); set `DOPPLER_SIP_BRIDGE=0` to start with the
  checkbox unticked. The checkbox is interactive up until the first
  Call has been placed - after that Pulse has advanced past
  `UNINITIALIZED` and the checkbox locks for the lifetime of the
  process.
* **App-transport `.so` mismatch**: the channel-aware app-transport API
  (`PulseAppChannelId`, the 4-arg `PulseAppPacketCallback`, etc.) is what
  this demo's `app_transport.cpp` is written against — those headers
  shipped in the repo on May 20 2026. The matching `libpexpulse.so` has
  **not** shipped yet, so the binary will compile and link (the legacy
  symbol names are unchanged) but will **misbehave at runtime** until the
  new `.so` is in place. The link itself is unsound — the old `.so`'s
  `pulse_app_transport_push` is the 3-arg variant.
* **Observed `audio/MUX` outbound ids (why audio stats can stay flat)**:
  if logs show `send_packet {MAIN,audio,MUX} NO MATCHING CHANNEL` while the
  bridge only has split `{MAIN,audio,RTP}` + `{MAIN,audio,RTCP}` channels,
  the callback/channel-id contract is mismatched and packets are dropped
  before they reach the socket counters. In short: yes, app-transport is
  currently surfacing `MUX` for that stream in this runtime combination, and
  yes, this can be treated as a Pulse-side behavior bug (or version-skew)
  relative to the channel-aware headers. The bridge now includes a narrow
  compatibility fallback for this case (classify mux packet as RTP vs RTCP
  and route to the corresponding split channel) so audio can flow while the
  Pulse `.so` side is aligned.
* **Summary to Share with Pulse Developers**
  - **Repro**: start `sip-demo` with app-transport enabled and place a SIP
    call where both audio and video media are successfully negotiated; bridge
    registers split audio channels (`RTP` + `RTCP`) for `MAIN/audio`.
  - **Observed**: outbound callback ids intermittently arrive as
    `{MAIN,audio,MUX}` instead of split `{MAIN,audio,RTP|RTCP}`. Logs show
    `send_packet {MAIN,audio,MUX} NO MATCHING CHANNEL`, and outbound audio
    packet/byte counters can remain flat.
  - **Expected**: with split channel registration for audio, outbound packets
    should be tagged as `RTP` or `RTCP` consistently (or the callback/channel
    contract should clearly define when `MUX` is emitted).
  - **Impact**: packets are dropped before socket send on channel mismatch;
    this can present as one-way or missing audio while video may still flow.
  - **Current bridge mitigation**: for unmatched `MUX` on audio, classify the
    packet payload as RTP vs RTCP and forward to the corresponding split
    channel as a compatibility fallback.
  - **Why this is flagged Pulse-side**: behavior is inconsistent with the
    channel-aware app-transport header contract this demo was built against,
    suggesting runtime `.so` behavior/version skew or a Pulse callback-id bug.
* **Local IPv4 address**: `AppTransport` rewrites the `c=IN IP4` line in
  Pulse's stage-1 offer (Pulse stamps `127.0.0.1` there because, with an
  app-transport set, it no longer does ICE / host-candidate gathering).
  The replacement IP is auto-detected by opening a UDP socket, calling
  `connect()` to `8.8.8.8:53` (no packets sent — this just primes the
  kernel's route lookup) and reading back `getsockname()`, which yields
  the egress interface's address. Set `DOPPLER_SIP_LOCAL_IP=<addr>` to
  override on multi-homed / VPN hosts where the auto-pick is wrong.
* `PulseSetupStage2Config::call_uuid` is set to the SIP **Call-ID** of the
  outgoing dialog. This is a placeholder choice — Pulse's REST path normally
  uses the Infinity-side call UUID. Revisit once we know what Pulse actually
  uses this field for in pure-SIP mode.
* Transport for SIP signalling is TCP only and there's no SIP REGISTER —
  this is a "place a direct INVITE" demo, nothing more. TCP is the
  deliberate choice (Pexip Infinity prefers it, and a video INVITE easily
  exceeds PJSIP's UDP MTU threshold). Add TLS / REGISTER if/when needed.
* **TCP transport idle-timer / long calls**: PJSIP's TCP transport ships
  with the default `PJSIP_TRANSPORT_IDLE_TIME` watchdog (600s). PJSIP
  only ref-counts the transport for the lifetime of individual
  transactions, so once the INVITE transaction completes (typically
  ~32s later, after PJSIP's `Timer J`/`Timer H` cleanup windows
  defined in RFC 3261 §17 — i.e. `64*T1` with `T1 = 500ms`) the
  refcount drops to 0. With no further SIP traffic until the BYE,
  the watchdog destroys the transport mid-call — surfacing as
  `Transport tcp ... is being destroyed due to timeout in idle timer`
  and losing the signalling leg. To prevent this, `sip_ua.cpp` pins the
  dialog to its selected transport via `pjsip_dlg_set_transport()`
  (TPSELECTOR_TRANSPORT) when the call reaches CONFIRMED; that holds a
  transport reference for the dialog's lifetime, which is automatically
  released on DISCONNECTED.
* App-transport is IPv4-only here and supports `a=rtcp-mux` on or off
  per `m=` section. No SRTP / DTLS — the app-transport API surfaces
  plain RTP only.
* The user agent identifies as `doppler-sip/<version>` on both the SIP
  `User-Agent` header and Pulse's application user-agent string.

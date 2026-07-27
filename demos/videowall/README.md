# videowall — a Pulse production switcher (library, many canvases, send-back)

A control-room **production switcher**. You "prepare" any number of **sources** —
cameras, **microphones**, RTSP/RTMP streams, still images, mp4 files, and live
Pexip video-conferences — and each one, once started, joins a **library** down
the left rail. Every source carries a **media type** — **audio**, **video**, or
**both** — which decides where it can go: an audio source feeds the **Audio
Mixer**, a video source can be dropped onto a **canvas**, and a "both" source
does both. From there you point-click-**drag** sources onto one or more
**canvases** and lay them out wherever you like:

* the **Program (Wall)** canvas — one superwide video wall (3480×1080 by default);
* one **Send** canvas per dialled-in conference — exactly what *that* far end
  receives, so two different conferences can be shown two different things, all
  composed from the same library. The Send (and Presentation) bus is a plain
  **1080p** frame (1920×1080) so each endpoint gets a sensible, standard
  resolution rather than the superwide wall geometry;
* an optional **Presentation** canvas paired with each conference, lit up by a
  **Start presentation** button so the far end sees *both* streams.

There is also an **Audio Mixer** that sits **underneath the canvas** (drag the
splitter between them to trade height, so you can watch the wall and ride the
faders at once). It is a classic console with one channel strip per
*audio-capable* source — a live VU meter (fed by Pulse's input audio-level
callback), a gain fader, a three-band EQ of rotary knobs, and noise-suppression
/ mute / solo switches (the switches and knobs are UI today, ready to wire into
the matching Pulse APIs later). The VU meters read each source's **input** audio
level, so a **Microphone** source — which connects a real capture device —
shows live activity; sources with no captured audio simply read as silence.

It is the same idea as the "compositor" mode in
[`pexninja`](../pexninja/), but built the other way round. Where pexninja runs a
**single** Pulse instance and lets Pulse's video mixer composite every source
into one frame, `videowall` runs **one Pulse instance per source** and does the
compositing itself:

```
  ┌── Pulse #1 ──┐   input: camera        selfview ┐
  ┌── Pulse #2 ──┐   input: rtsp://…       selfview ┤   library of active
  ┌── Pulse #3 ──┐   input: image.png      selfview ┼─► sources we paint onto
  ┌── Pulse #4 ──┐   input: clip.mp4       selfview ┤   each canvas at every
  ┌── Pulse #5 ──┐   dial havard@pex…   MAIN (far) ─┘   placement's x/y/w/h
            …                                            (our own compositor)
```

For every *local* source the recipe is uniform: point the Pulse instance's
**input** at the source, then pull its **self-view** back out. The
**video-conference** source is special — it is **both a source and a sink**:

* **As a source** we split `havard@pexipdemo.com` into conference `havard` on
  server `pexipdemo.com`, dial it over REST, and pull the **MAIN** (far-end)
  video out — exactly the inbound-pull path every other source uses.
* **As a sink** it also has outbound canvases we compose and push *back in*: we
  render the Send canvas to an RGBA buffer and push it as that conference's
  **MAIN** input (what the far end sees instead of a camera), and the
  Presentation canvas onto the **PRESENTATION** input when active. This is the
  mirror image of the pull path — `connect_input` + `push_frame` instead of
  `connect_output` + `pull_frame_data` — borrowed from [`gateway`](../gateway/).

## What it highlights

* One `Pulse *` **per source** — `pulse_new()` / `pulse_free()` many times over,
  the multi-instance pattern from [`gateway`](../gateway/) taken to its limit.
* Driving each instance's input per source kind:
  * `pulse_device_session_connect_device` (camera **or microphone** — a video or
    an audio capture device on MAIN)
  * `pulse_rtsp_session_connect_input` + `pulse_rtsp_session_bind_to_content` (RTSP)
  * `pulse_rtmp_session_connect_input` (RTMP)
  * `pulse_video_mix_input_from_file` / `…_from_file_with_loop` (image / mp4)
  * `pulse_connect_with_rest_async` (video-conference)
* Pulling frames out with `pulse_data_session_connect_output` +
  `pulse_data_session_pull_frame_data` (self-view for local sources, MAIN for
  the conference) and compositing them ourselves.
* The **send-back** direction for conferences: composing a canvas on the CPU and
  pushing it in with `pulse_data_session_connect_input` +
  `pulse_data_session_push_frame` on the **MAIN** slot (the far end's video) and
  the **PRESENTATION** slot (the second stream, toggled live).
* Separating **active sources** (the library) from **placements** (where a source
  appears) so the same source can be dropped onto many canvases — even twice onto
  the same one.
* Live audio metering with `pulse_register_device_audio_level_callback` /
  `pulse_deregister_device_audio_level_callback` — one subscription per source,
  driving the VU meters in the **Audio Mixer** (a **Microphone** source connects
  a capture device, so its meter shows real input activity).

## Build & run

From the repository root (install the Pulse runtime first — see the
[repository README](../../README.md#1-install-the-pexip-pulse-runtime)). The
demo builds by default:

```bash
cmake -S . -B build
cmake --build build -j --target videowall
./build/run-videowall.sh
```

(Pass `-DBUILD_VIDEOWALL=OFF` to skip it.)

## Using it

1. In **Prepare a source** (top-left), pick a kind, set its **Media** type
   (audio / video / both — it defaults sensibly per kind), configure it (camera,
   microphone, URL, file path, or `name@server` conference id — plus an optional
   **PIN code** for PIN-protected conferences) and press **Prepare (start)**. For
   **Image** and **MP4** sources a **Browse…** button opens a file dialog so you
   can pick the file off disk. A Pulse instance spins up behind it and — once it
   has signal — it appears in the **Library** below (a live thumbnail for video
   sources, an `(audio)` tile for audio-only ones).
2. **Drag** a video-capable library thumbnail onto any canvas to drop it there
   (audio-only sources are not draggable — they live in the mixer). Then **drag**
   the tile to move it, drag its bottom-right **resize handle** to scale it
   (aspect ratio is kept), or fine-tune position, width and z-order in the
   inspector under the canvas. Drop the same source as many times as you like.
   Each canvas scales to fit entirely in view — even a 1920×1080 send bus or the
   superwide wall — so you never have to scroll to see the whole thing.
3. Use the tabs at the top of the stage to switch buses:
   * **Program (Wall)** — the big video wall (set its size at the top).
   * **`name` (far end)** — one tab per dialled-in conference. Its **Send** bus
     is exactly what that far end receives (a 1080p frame).
   The **Audio Mixer** sits underneath the canvas (not in a tab): a channel strip
   per audio-capable source with a live VU meter, a gain fader, EQ knobs and
   noise-suppression / mute / solo switches. Drag the splitter between the canvas
   and the mixer to trade height between them.
4. In a conference tab, press **Start presentation** to light up a second
   **Presentation** bus — compose it like any other canvas and the far end sees
   *both* streams. **Stop presentation** tears it down.
5. Select a library source to **Stop & remove** it; this also clears every
   placement that referenced it across all canvases.

## Code tour

It is all in [`src/main.cpp`](src/main.cpp), heavily commented:

* `ActiveSource` — one prepared source in the library: its kind, **media type**
  (audio / video / both), config, **its own `Pulse *`**, the GL texture + CPU
  frame we paint, and (for a conference) an `OutboundSink`. `source_has_video` /
  `source_has_audio` read the media type to route it to the canvas, the mixer, or
  both.
* `Placement` / `Canvas` — a lightweight `{source, x, y, w, h}` appearance, and a
  sized list of them. The same `ActiveSource` can back many placements.
* `OutboundSink` — a conference's send side: its Send + Presentation canvases,
  the input data-sessions, and the scratch buffers we composite into.
* `start_source` / `stop_source` — bring a source's Pulse instance up/down, wire
  its input per kind (a camera or microphone capture device, an RTSP/RTMP stream,
  a file mix, or a dialled conference), and (for a conference) open/close the
  outbound sessions.
* `pump_frame` — pull the latest RGBA frame (self-view, or MAIN for a
  conference) into the source's texture **and** a CPU copy.
* `composite_canvas` / `push_canvas` / `pump_outbound` — paint a canvas's
  placements into an RGBA buffer and push it into the conference's MAIN /
  PRESENTATION input.
* `on_source_audio_level` + `mixer_vu_meter` — the audio-level callback that
  feeds each strip's VU meter, and the meter that draws it.
* `file_picker` — a one-slot wrapper around the ImGui-Addons file browser behind
  the **Browse…** buttons for Image / MP4 sources.
* `draw_library_rail` / `draw_editable_canvas` / `draw_audio_mixer` /
  `draw_canvas_tabs` / `draw_stage` — the switcher-style UI: library thumbnails,
  fit-to-view drag-to-place canvases, the audio-mixer console, the tabbed Program
  / Send / Presentation buses, and `draw_stage` which stacks the canvas over the
  mixer with a draggable splitter between them.

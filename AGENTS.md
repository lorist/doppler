# Working with this repo (guide for agents & contributors)

This repository is a **showcase for Pexip Pulse**: a collection of small,
self-contained demo applications, each one highlighting something Pulse can do.
If you are an AI agent (or a human) extending the repo, read this first. It
explains how the repo is wired, how to relate to the Pulse SDK, and the
conventions every demo follows.

The golden rule: **a newcomer should be able to `cd` into any demo, read its
README, run two or three commands, and see it work.** Keep it that way.

---

## 1. Repository layout

```
demos/            One folder per demo. Each is self-contained: source + its own
                  README.md + (for the C/C++ demos) a small CMakeLists.txt.
  doppler/        The flagship one-file video call (REST to Pexip Infinity).
  gateway/        Two Pulse instances bridged via raw data sessions.
  sip/            Pulse as a media engine; PJSIP does the SIP signalling.
  pexninja/       The big reference client — widest Pulse surface of them all.
  android/        Android (Kotlin/Gradle) sample using the Pulse Android SDK.

sdk/              The Pexip Pulse SDK artifacts, by platform.
  linux/          .deb packages (debs/) + their extracted contents (opt/, usr/).
  macos/          libpexpulse.dylib + libpexlgpl.dylib.
  windows/        NuGet packages.

cmake/            Shared CMake helpers (PulseDemo.cmake) used by every demo.
docs/             adding-a-demo.md (contributor guide), DEMO_TEMPLATE/ skeleton,
                  and api/ (the generated Pulse C API reference — open
                  docs/api/index.html).
scripts/          Helper scripts (install-pjsip.sh, env helpers).
CMakeLists.txt    Thin orchestrator: finds shared deps, then add_subdirectory()s
                  each demo behind a BUILD_<DEMO> option.
```

When you add a demo, it goes in `demos/<name>/`. Nothing else moves.

---

## 2. The Pulse SDK, in a nutshell

The C API headers live in `sdk/linux/opt/pexip/include/pexpulse/`. The full
generated reference is in `docs/api/` (`index.html`). The mental model:

* **One handle.** `pulse_new()` returns a `Pulse *`; `pulse_free()` releases it.
  Almost every call takes that handle. A demo can own several handles at once
  (that is exactly what `gateway` does — one per conference leg).
* **Options + callbacks before connecting.** Configure the instance with
  `pulse_options_set_*()` *before* you connect: register callbacks (conference
  state, participant list, media stats, ...) and decide whether Pulse manages
  its own windows or you render the video yourself (pass `NULL` window handles
  to render it yourself, as `doppler` does).
* **Connect / disconnect.** `pulse_connect_with_rest_async()` joins a Pexip
  Infinity conference over REST. There is also a manual two-stage SDP path used
  by `sip` when an external stack owns the signalling.
* **Media flows through "sessions".** Rather than poking at raw devices, you
  attach **data sessions** to logical streams (MAIN, PRESENTATION, SELFVIEW,
  ...). `connect_output` pulls decoded frames *out* for you to render;
  `connect_input` pushes frames *in* from your own source.
* **Async, callback-driven.** Results and state changes arrive on callbacks.
  Keep them quick; hand heavy work (GL upload, format conversion) to your own
  threads, as the demos do.

### Capability → where to look

Every header is named after the capability it exposes. Use this as your map;
when in doubt, **`grep` `demos/pexninja/pexninja.cpp` for the `pulse_*` symbol**
— it almost certainly demonstrates it.

| You want to…                                   | Header (`pexpulse/…`)              | Demo to crib from |
| ---------------------------------------------- | --------------------------------- | ----------------- |
| Create the instance, set options, connect      | `pulse.h`, `pulse_options.h`      | `doppler`         |
| Render incoming / self-view video yourself     | `pulse_data_session.h`            | `doppler`, `gateway` |
| Inject your own audio/video frames             | `pulse_data_session.h`            | `gateway`         |
| Composite multiple inputs into one output      | `pulse_video_mix_session.h`, `pulse_video_mix_input.h` | `doppler` (Twitch mix) |
| Ingest an RTMP / RTSP / RTP / file source       | `pulse_rtmp_session.h`, `pulse_rtsp_session.h`, `pulse_rtp_session.h`, `pulse_file_session.h` | `doppler` (RTMP) |
| Drive SIP signalling yourself (manual SDP)     | `pulse.h`, `pulse_media.h`        | `sip`             |
| Mute, layout, lock, and other conference ops   | `pulse_conference_control.h`, `pulse_participant_control.h` | `pexninja` |
| Roster / participant list                       | `pulse_participant_list.h`        | `pexninja`        |
| Conference / participant events                 | `pulse_conference_event.h`        | `pexninja`        |
| Breakout rooms, annotation, FECC/PTZ            | `pulse_breakout_rooms.h`, `pulse_annotation.h`, `pulse_fecc_types.h`, `pulse_ptz_types.h` | `pexninja` |
| Enumerate / pick cameras, mics, screens         | `pulse_device.h`, `pulse_device_session.h` | `pexninja` |
| Media statistics (bitrate, loss, ...)           | `pulse_media_stats.h`             | `pexninja`        |

> **`pexninja` is the most complete demo** and exercises the widest array of
> Pulse functionality. It is the single best source of working patterns — treat
> it as living documentation.

---

## 3. How the build is wired

The top-level `CMakeLists.txt` is just an orchestrator. It:

1. finds the shared dependencies once — `OpenGL`, `glfw3`, `Threads`, the Pulse
   runtime (`pulse_find_runtime()`), and Dear ImGui (`pulse_declare_imgui()`);
2. exposes a `BUILD_<DEMO>` option per demo; and
3. `add_subdirectory(demos/<name>)` for each enabled demo.

The reusable boilerplate lives in **`cmake/PulseDemo.cmake`**:

| Helper | What it does |
| ------ | ------------ |
| `pulse_find_runtime()` | Locates Pulse, defines the `pexip::pulse` imported target. |
| `pulse_declare_imgui()` | Fetches Dear ImGui, builds the shared `imgui` static lib. |
| `pulse_demo_rpath(<target>)` | Bakes the Pulse lib dir into the target's RPATH. |
| `pulse_demo_launcher(<target> <script>)` | Emits `build/<script>` — a wrapper that sets the lib search path then execs the binary. |

A minimal demo's `CMakeLists.txt` is therefore tiny:

```cmake
add_executable(my_demo src/main.cpp)
target_link_libraries(my_demo PRIVATE imgui pexip::pulse Threads::Threads)
pulse_demo_rpath(my_demo)
pulse_demo_launcher(my_demo run-my-demo.sh)
```

### Runtime linking gotcha (don't fight it)

`libpexpulse.so` has private sibling libraries (`libpexlgpl.so`, `libimf.so`,
`libonnxruntime.so.1`, ...) installed alongside it. The executable's RPATH finds
the *direct* dependency, but the transitive siblings still need the lib dir on
`LD_LIBRARY_PATH` (or `DYLD_LIBRARY_PATH` on macOS). That is the **only** reason
the generated `run-*.sh` launchers exist — always ship one via
`pulse_demo_launcher()` rather than telling users to set the path by hand.

Other things that will bite you if you forget them:

* **Don't relink GLib/GStreamer.** `libpexlgpl` statically links its own copies;
  pulling the system ones into a demo causes a runtime clash. `pexninja` was
  explicitly de-GLib'd for this reason.
* **Pass `NULL` window handles** in `pulse_options_set_*_window_handle()` if you
  intend to render the video yourself; otherwise Pulse spawns its own windows.

---

## 4. Adding a new demo

Follow [`docs/adding-a-demo.md`](docs/adding-a-demo.md). In short:

1. Copy `docs/DEMO_TEMPLATE/` to `demos/<your-demo>/`.
2. Drop your source under `demos/<your-demo>/src/`.
3. Fill in the `CMakeLists.txt` (use the `PulseDemo.cmake` helpers) and the
   `README.md` (what it shows, then build & run in two or three commands).
4. Add a `BUILD_<DEMO>` option + `add_subdirectory()` in the root
   `CMakeLists.txt`. Default it **ON** only if it needs no extra system
   packages beyond GLFW/OpenGL; otherwise default **OFF** and document the deps.
5. Add a row to the demo table in the top-level `README.md`.

Keep new demos **small and focused** — one or two Pulse capabilities each. The
place for breadth is `pexninja`; the place for clarity is everything else.

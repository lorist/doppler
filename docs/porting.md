# Porting the demos to Linux and Windows

What it would actually take to build the C++ demos — [`uavwall`](../demos/uavwall/)
in particular — on a platform other than the Mac they were written on.

Everything below was checked against the SDK artifacts in this repo rather than
assumed; the commands that establish each fact are given so they can be
re-checked when the SDK is updated.

## What the SDK gives you

| Platform | Form | Architecture | Native C API |
| --- | --- | --- | --- |
| macOS | `sdk/macos/*.dylib` | **arm64 only** | yes — but **no headers**, see below |
| Linux | `sdk/linux/debs/*.deb` + extracted tree | **x86-64 only** | yes, with headers |
| Windows | `sdk/windows/*.nupkg` | **win-x64 only** | yes — `pexpulse.lib` + `pexpulse.dll` |

```bash
lipo -archs sdk/macos/libpexpulse.dylib          # arm64
ls sdk/linux/debs/ | sed 's/.*_//' | sort -u     # amd64.deb
unzip -l sdk/windows/*.nupkg | grep -oE 'runtimes/[a-z0-9-]+/' | sort -u
```

**The binding constraint is architecture, not operating system.** There is no
ARM build for Linux or Windows: `sdk/linux/opt/pexip/lib` carries the Intel
compiler runtime (`libimf.so`, `libintlc.so`, `svml`), and the Windows package
ships `libmmd.dll` and `svml_dispmd.dll` for the same reason. That rules out
Raspberry Pi, Graviton, Ampere and Windows-on-ARM regardless of how portable the
application code is. Apple silicon works because the macOS build is a separate
arm64 artifact.

Two things worth knowing about the macOS SDK, because they surprise people:

* **It ships no headers at all.** Every build in this repo — including on macOS —
  compiles against the Linux header tree, via the fallback in
  [`cmake/PulseDemo.cmake`](../cmake/PulseDemo.cmake). So the headers are already
  proven portable; only the libraries differ.
* **The Windows NuGet is not only a .NET binding.** Alongside
  `lib/net8.0/Pexip.Pulse.dll` it carries `build/native/include/*.h` — the same C
  headers — plus `pexpulse.lib`. A native C++ Windows build is supported, and
  [`pexninja`](../demos/pexninja/) already does it under MSVC.

## Where each demo stands

| Demo | macOS | Linux | Windows |
| --- | --- | --- | --- |
| [`doppler`](../demos/doppler/), [`videowall`](../demos/videowall/) | built | should build; see below | needs a port |
| [`uavwall`](../demos/uavwall/) | built | should build; see below | **built** under MSVC — see below |
| [`pexninja`](../demos/pexninja/) | built | built | builds under MSVC via the NuGet |
| [`windows`](../demos/windows/) | — | — | .NET WinForms, uses the managed wrapper |

## Linux

Close to free. The build system already treats Linux as a first-class target:
`PulseDemo.cmake` looks for the runtime under `${PEXIP_PREFIX}` then the in-repo
Linux tree, and RPATH handling is written for ELF. Install the debs per the
[repository README](../README.md#linux-ubuntu-2404) and build as normal.

The application code is C++17 and mostly POSIX, so it moves across intact —
including `uavwall`'s recording layer (`fork`/`execv`/`pipe`/`waitpid`) and its
SIGINT handling. Process metrics already branch: `task_info` on macOS,
`/proc/self/statm` elsewhere.

Two gaps, both already stubbed rather than broken, so the build succeeds and the
behaviour is merely absent:

* **No sound.** `play_ring()` is an empty function off macOS, so the
  incoming-call ring and the feed-loss cue are silent. The visual state — banner,
  amber markers, alert bar — all still works. A few lines of libcanberra, or
  `paplay` on a sound file, closes it.
* **Canvas recording falls back to `libx264`.** The `#if defined(__APPLE__)`
  branch in `start_canvas_recording()` selects `h264_videotoolbox`, which encodes
  1080p30 in hardware for about 10% of a core. Software x264 costs considerably
  more, and on a machine already near its compositing limit that matters. VAAPI
  (`h264_vaapi`) or NVENC (`h264_nvenc`) would restore it on real hardware.

Neither has been exercised — nothing in this repo has been *run* on Linux. The
cheapest useful step is a build on an x86-64 Ubuntu 24.04 box to find out what
actually falls over, rather than trusting this page.

## Windows

**`uavwall` is now ported** — it builds under MSVC and runs. What follows is
what the port actually involved, kept because `doppler` and `videowall` still
need the same treatment and the shape is identical.

The Pulse call, registration and RTSP logic is plain C API and compiled
unchanged. What did not port is the platform plumbing, all of it in the
recording and lifecycle code:

| Area | POSIX | Windows equivalent |
| --- | --- | --- |
| Recording subprocess | `fork` / `execv` / `pipe` / `dup2` | `CreateProcess` + `CreatePipe`, with the child's stdin redirected |
| Stopping a recorder | write `q` to stdin, then `waitpid` | `WriteFile` to the pipe (identical semantics), then `WaitForSingleObject` |
| Last-resort kill | `kill(SIGKILL)` | `TerminateProcess` |
| Clean shutdown | `signal(SIGINT/SIGTERM)` | `SetConsoleCtrlHandler`, or rely on window close |
| Process metrics | `getrusage`, `/proc/self/statm` | `GetProcessTimes`, `GetProcessMemoryInfo` |
| Folder creation | `mkdir` / `access` | `std::filesystem` — portable everywhere, and would delete this row on all three platforms |
| Audible alert | AudioToolbox | `PlaySound` / `MessageBeep` |
| Library resolution | RPATH | DLLs beside the `.exe`; there is no RPATH concept |
| Build | no MSVC branches; `find_library` uses Unix names | NuGet restore, link `pexpulse.lib`, copy `runtimes/win-x64/native/*.dll` next to the binary |

GLFW, Dear ImGui, OpenGL and the two bundled fonts (DM Sans, IBM Plex Mono) are
already cross-platform and need nothing.

### How it was actually done

The process layer never grew a second implementation of its *callers*. Two
choices kept the POSIX shape intact, and both are worth reusing:

* **The pipe handle is wrapped in a CRT descriptor** with `_open_osfhandle()`,
  so `Recorder::in_fd` stays an `int` and the canvas writer thread, the PCM
  reader thread and every `close` are shared verbatim. Only three one-line
  wrappers (`pio_read` / `pio_write` / `pio_close`) differ.
* **The process `HANDLE` lives in the pid slot**, typedef'd as `ProcHandle`.
  Win32 process handles are always small positive values, so the "`> 0` is
  running, `-1` is none" convention the rest of the file relies on survives
  untouched — no call site changed.

Three things were not on the list above and cost the most time:

* **`ffmpeg` has no audio output device on Windows.** `dshow` is capture-only
  and there is no WASAPI/DirectSound muxer, so `-f audiotoolbox` has no
  counterpart at all. **LISTEN** runs `ffplay -nodisp` instead — a second
  runtime dependency, and one that ignores the `q`-on-stdin stop, so the
  monitor uses the existing kill-fallback with its timer brought forward.
* **`pulse_new` needs a different recipe.** The Windows Pulse build does not
  export `pulse_new_with_internal_sso_handling`; it is `pulse_new()` plus
  `pulse_options_set_sso_provider_callbacks()`, which must be present for
  `pulse_register` to work *even for plain password registration*. `uavwall`
  had a guard for this already — spelled `HOST_WINDOWS`, a macro only
  `pexninja`'s CMakeLists ever defines, so on Windows it took the macOS branch
  and failed to link.
* **Closing a descriptor does not release a blocked reader.** The POSIX
  `stop_air` closes the pipe so the reader thread's `read` returns; Windows
  makes no such guarantee, so there the child is terminated *first* and the
  thread leaves on the resulting EOF.

Everything else went as predicted. GLFW, Dear ImGui, OpenGL and the two bundled
fonts (DM Sans, IBM Plex Mono) needed nothing.

### What is different at runtime

* **Canvas recording encodes in software** (`libx264`); there is no
  `h264_videotoolbox` equivalent wired up. `h264_qsv` would restore hardware
  encode on Intel, at the cost of failing on machines without Quick Sync.
* **LISTEN needs `ffplay`** on `PATH`, separately from `ffmpeg`. Absent, the
  button reports it rather than failing silently — the same treatment recording
  already gives a missing `ffmpeg`.
* **`pulse_free` still logs a stale `pin_code_request` callback** at exit. That
  is repo-wide (`pexclient` does the same on every platform), not specific to
  this port.
* **High-profile H.264 feeds break up on Windows.** Diagnosed live against a
  phone (Larix over RTMP → mediamtx → RTSP): the picture smeared as if the
  feed had packet loss, while the same stream played clean in `ffplay` and the
  same rig was clean on the Mac. Eliminated in turn: the bitstream (zero decode
  errors over 10s), frame timing (PTS locked at 33/34ms), the jitter buffer
  (500ms changed nothing), and a real mediamtx quirk found on the way — its
  RTMP→RTSP conversion gives *both* tracks payload type 96 — which turned out
  not to be the cause either. What fixed it: transcoding the feed to
  **Constrained Baseline at the same 1080p**, changing nothing but the
  profile. The Windows Pulse build decodes through DXVA/Media Foundation
  hardware paths (`pexlgpl.dll` carries both); macOS uses VideoToolbox, which
  is why the Mac never showed it. Observed on Intel UHD 770, driver
  32.0.101.7077. Until fixed in the SDK: set the device to **Baseline
  profile** (Larix exposes this directly), or relay through ffmpeg with
  `-profile:v baseline`; the four Baseline test feeds and pexclient's
  conference video are unaffected. Worth reporting to Pexip with this repro.
  uavwall's built-in receiver applies the relay automatically on Windows when
  ffmpeg is present: receiver feeds read `relay/<slot>`, an on-demand Baseline
  round-trip of the pushed `live/<slot>` (which also gives the two tracks
  distinct payload types). Without ffmpeg it falls back to the direct pull and
  the status bar says what to expect.

## Rough effort

* **Linux** — an afternoon, most of it testing, plus the audio stub and a
  hardware encoder for canvas recording.
* **Windows** — estimated at a couple of days; `uavwall` took rather less,
  because `pexclient` had already proven the toolchain, the NuGet plumbing in
  [`cmake/PulseDemo.cmake`](../cmake/PulseDemo.cmake) and the `.bat` launcher.
  With that in place the work was the process layer and little else. `doppler`
  and `videowall` should now be cheaper again — they have no recording layer,
  which was where all the risk sat.

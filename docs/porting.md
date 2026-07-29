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
| [`doppler`](../demos/doppler/), [`videowall`](../demos/videowall/), [`uavwall`](../demos/uavwall/) | built | should build; see below | needs a port |
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

Supported by the SDK, but a genuine port of the application code. The Pulse
call, registration and RTSP logic is plain C API and compiles anywhere; what does
not port is the platform plumbing, most of which is in `uavwall`'s recording and
lifecycle code.

| Area | Today | Windows equivalent |
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

`uavwall` guards its macOS-specific code with `#if defined(__APPLE__)` already,
so the compiler will point at most of the above on the first attempt — the
POSIX headers at the top of `src/main.cpp` (`unistd.h`, `sys/wait.h`,
`sys/stat.h`, `fcntl.h`, `signal.h`) are the list of what needs replacing.

### Suggested order

1. Get it compiling with recording **disabled** — stub `start_feed_recording`,
   `start_canvas_recording` and `reap_recorder` to no-ops. That isolates the
   Pulse and UI layers, which should need almost nothing.
2. Replace `mkdir`/`access` with `std::filesystem` (helps every platform).
3. Port the process layer behind a small `spawn_recorder` / `stop_recorder` /
   `reap_recorder` interface — the three functions the rest of the code already
   goes through, so nothing above them changes.
4. Metrics and the alert sound last; both are cosmetic and the app is usable
   without them.

## Rough effort

* **Linux** — an afternoon, most of it testing, plus the audio stub and a
  hardware encoder for canvas recording.
* **Windows** — a couple of days, concentrated in the process layer and the MSVC
  toolchain. The recording feature carries essentially all of the risk; without
  it, the port is mostly build configuration.

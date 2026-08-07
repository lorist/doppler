# Third-party components

What ends up in a macOS or Windows build of the C++ demos, where it comes from,
and which pieces carry obligations if the result leaves your own machine.

Established by inspecting the artifacts in this repo rather than from memory;
the commands are given so each finding can be re-checked when the SDK or the
dependencies are updated.

> Not legal advice. This is an inventory of what is in the box. Anything
> redistributed to a customer or partner should be reviewed properly, and Pexip
> should confirm the SDK terms and supply their own notices.

## Compiled into the demo binary

| Component | Version | Licence | Source |
| --- | --- | --- | --- |
| Dear ImGui | v1.91.5-docking | MIT | fetched at build time ([`cmake/PulseDemo.cmake`](../cmake/PulseDemo.cmake)) |
| GLFW | 3.4 | zlib/libpng | fetched at build time ([`CMakeLists.txt`](../CMakeLists.txt)) |
| DM Sans | — | SIL OFL 1.1 | `demos/*/assets/fonts/` |
| IBM Plex Mono | — | SIL OFL 1.1 | `demos/uavwall/assets/fonts/` |

All four are permissive. The OFL requires its licence text to travel with the
fonts, which is why `LICENSE-DMSans.txt` and `LICENSE-IBMPlexMono.txt` sit beside
the `.ttf` files — **keep them together when packaging.**

```bash
grep -nE "GIT_REPOSITORY|GIT_TAG" cmake/PulseDemo.cmake CMakeLists.txt
ls demos/uavwall/assets/fonts/
```

## The Pulse runtime

Two libraries, and the split between them is deliberate.

**`libpexpulse.dylib` / `pexpulse.dll` — Pexip's own code.** Proprietary, under
the *Pexip Software Development Kit License Agreement* (`LICENSE.txt` inside the
Windows NuGet; the Android artifacts carry the same). This is the licence that
governs whether you may redistribute the SDK at all, and it is the one to read
before shipping a kit to anyone.

**`libpexlgpl.dylib` / `pexlgpl.dll` — the copyleft dependencies, separated.**
The name is not decoration. Strings in the binary show GStreamer, GLib,
libav/ffmpeg, OpenSSL and Opus:

```bash
strings sdk/macos/libpexlgpl.dylib | grep -oiE "gstreamer|glib|libav|openssl|opus" | sort | uniq -c
```

Pexip has put the LGPL-licensed components in their own shared library so they
are dynamically linked and can be replaced by the end user, which is how LGPL §4
is normally satisfied without affecting the licence of the calling application.

**The practical consequence: keep it a separate shared library.** Do not
statically absorb it, do not merge it into another binary, and ship it as its own
file. That is the arrangement the licensing relies on.

Windows additionally ships Intel runtime — `libmmd.dll`, `svml_dispmd.dll` (Intel
redistributable terms) and `tbb12.dll` (Apache 2.0) — pulled in by the compiler
Pulse was built with, not by anything in this repo.

## Runtime dependencies

Neither is linked. `uavwall` starts them as separate processes.

| Component | Licence | Used for |
| --- | --- | --- |
| mediamtx | MIT | the RTSP/RTMP/SRT server in `scripts/uav-streams.sh` |
| ffmpeg | **depends on the build** | recording, LISTEN, feed generation |

**ffmpeg is the one that needs attention.** Homebrew's build on this machine
reports `--enable-gpl --enable-version3`, making it **GPL-3.0**:

```bash
ffmpeg -version | grep -oE "enable-gpl|enable-version3"
```

Invoking a GPL program as a subprocess does not make your own code a derived
work — that is well-established, and it is why `uavwall` shells out rather than
linking libavcodec. But **distributing the binary is a distribution**, and GPL
obligations attach to it: the licence text must travel with it, and corresponding
source must be provided or offered.

That matters concretely here, because
[`make-demo-kit.ps1`](../demos/uavwall/make-demo-kit.ps1) copies `ffmpeg.exe` and
`ffplay.exe` into the kit (from Gyan.FFmpeg, whose full builds are GPL). **A demo
kit handed to a partner therefore redistributes GPL binaries** and should include
their licence and a source offer. An LGPL-only ffmpeg build would avoid this,
at the cost of the encoders those builds omit.

## If you package any of this

1. Ship `libpexlgpl` / `pexlgpl.dll` as a **separate shared library**.
2. Include the **OFL text** alongside the bundled fonts.
3. Include **ffmpeg's licence and a source offer** if ffmpeg is in the package,
   or swap to an LGPL build.
4. Include the **Pexip SDK licence**, and check with Pexip that redistribution is
   permitted for your case.
5. mediamtx is MIT — attribution only.

Nothing in this repo currently assembles those notices automatically. For a kit
that goes to a third party, a `THIRD-PARTY-NOTICES.txt` staged by the packaging
script would be the natural place.

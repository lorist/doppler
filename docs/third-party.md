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
before shipping a kit to anyone. Redistribution in the macOS bundle was
confirmed with Pexip on 10 August 2026 **for internal Pexip demonstration use
only** — not for customers or partners. Anything wider needs a fresh
conversation.

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

That matters concretely here, because both packaging scripts copy ffmpeg in:
[`make-demo-kit.ps1`](../demos/uavwall/make-demo-kit.ps1) takes `ffmpeg.exe` and
`ffplay.exe` from Gyan.FFmpeg, and [`make-bundle.sh`](../demos/uavwall/make-bundle.sh)
`--with-tools` takes whatever [`fetch-ffmpeg.sh`](../demos/uavwall/fetch-ffmpeg.sh)
installed — currently a **GPL-3.0** arm64 build (`--enable-gpl --enable-version3`).
**A kit or bundle handed to a partner therefore redistributes GPL binaries** and
must include the licence and a source offer.

**The cleanest answer is not to ship ffmpeg at all.** It is invoked as a
subprocess, never linked, and nothing in the demo path needs it: bundled clips
are transcoded when the bundle is built, and RTSP feeds are decoded by Pulse.
It is wanted only for importing your own files, recording, LISTEN, and sending
feed audio to the VMR. A recipient who wants those runs `brew install ffmpeg`
and obtains the software — and accepts its licence — directly from its
distributor. You never convey a GPL binary, so no obligation attaches to your
package at all. uavwall finds a Homebrew ffmpeg automatically and says so at
startup when one is missing. Do not have the app *download* ffmpeg on the user's
behalf; telling them to install it is clean, fetching it for them is not
obviously so.

**If you do bundle it, the internal-only scope softens things, but do not rely
on it.** GPL obligations
attach to *conveying* — handing a copy to someone outside the legal entity that
holds it. Circulating the bundle among Pexip colleagues is arguably not
conveying, so the source offer may not be strictly required for the scope
confirmed above. Keep it anyway: it costs one line, and it is what makes the
bundle safe the first time somebody forwards it to a customer.

`fetch-ffmpeg.sh` reads `--enable-gpl` / `--enable-version3` / `--enable-nonfree`
off the binary it downloaded rather than assuming the vendor's terms, refuses a
`--enable-nonfree` build outright (undistributable), and saves the matching
licence text. `make-bundle.sh` then stages it — see below.

An LGPL-only ffmpeg build would drop the GPL obligation, but it omits libx264,
which uavwall uses to transcode imported clips to Constrained Baseline. That
transcode is not optional: Pulse decodes High-profile H.264 badly. So the
practical choice is GPL-with-a-source-offer, or building an LGPL ffmpeg with a
non-GPL H.264 encoder (`libopenh264`) and changing the recipe.

## Demo clips

`make-bundle.sh` carries four short clips cut from `UAV_footage/`, which came
from **Pexels** — the `:Zone.Identifier` streams on the originals record the
download URLs. The Pexels License allows commercial and non-commercial use with
no attribution, but forbids selling the clips unaltered and using identifiable
people in them to imply endorsement. Neither restriction bites for a demo; both
would matter if the clips were the product.

## If you package any of this

1. Ship `libpexlgpl` / `pexlgpl.dll` as a **separate shared library**.
2. Include the **OFL text** alongside the bundled fonts.
3. Include **ffmpeg's licence and a source offer** if ffmpeg is in the package,
   or swap to an LGPL build.
4. Include the **Pexip SDK licence**, and check with Pexip that redistribution is
   permitted for your case. **Confirmed 10 August 2026 for the macOS bundle,
   internal Pexip demonstration use only.**
5. mediamtx is MIT — attribution only.

**The macOS bundle now does all five automatically.** `make-bundle.sh` writes
`Contents/Resources/THIRD-PARTY-NOTICES.txt` from what actually went into that
build — ffmpeg's section only appears with `--with-tools`, mediamtx's only with
`NO_MEDIAMTX=1` — and copies the licence texts into
`Contents/Resources/licenses/`.

For point 4 it extracts `LICENSE.txt` from `sdk/windows/*.nupkg`, since the
macOS SDK ships no licence file of its own and the same agreement governs every
platform's artifacts. That file is worth knowing about: it is 1MB, and the
agreement proper is only the first ~630 lines. The rest is **Pexip's own
open-source notices** — FreeType, and the GPL/LGPL/Apache/MIT/MPL texts for the
components inside the runtime. Shipping it therefore discharges most of the
`libpexlgpl` obligation as well, which is a better answer than anything written
by hand here.

One thing the script cannot do for you: **the GPL source offer needs a real
contact.** Set `SOURCE_OFFER` (see the top of `make-bundle.sh`) and the notice
carries a proper three-year written offer; leave it unset and the notice says so
and the build warns. Do not edit the notice inside a built `.app` — it is
regenerated each build, and any change invalidates the code signature, which
makes macOS report the app as damaged.

`make-demo-kit.ps1` on Windows still assembles no notices.

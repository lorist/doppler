# pexninja — the full Pulse reference client

`pexninja` is a much fuller Pulse client (~12k LoC, lifted from another repo).
It exercises the **widest array of Pulse functionality** of any demo in this
repo, which makes it the best place to look when you outgrow the smaller demos
and the best source of copy-paste patterns when you build your own.

It uses the same `libpexpulse` as everything else but layers in ImPlot (media
stats plots), gl3w (explicit GL loader) and ImGui-Addons (a file browser).
Because of those extra dependencies it is gated behind a CMake option so the
default build stays lean.

> Looking for a specific Pulse capability? `pexninja.cpp` almost certainly
> demonstrates it. Search it for the relevant `pulse_*` call — see
> [`../../AGENTS.md`](../../AGENTS.md) for a capability → symbol map.

## Build & run

First make sure the Pulse runtime is installed — see the
[repository README](../../README.md#1-install-the-pexip-pulse-runtime). Then
install the extra system dependencies and enable the option:

```bash
sudo apt-get install -y libx11-dev libglfw3-dev libgl1-mesa-dev
cmake -S . -B build -DBUILD_PEXNINJA=ON
cmake --build build -j --target pexninja
./build/run-pexninja.sh
```

`pexninja` no longer depends on GLib or GStreamer. Those were previously
pulled in only for logging and a few string/queue helpers, which now use native
C++/POSIX code. This avoids a runtime clash with the GLib/GStreamer copies that
`libpexlgpl` links statically.

The extra dependencies (ImPlot, gl3w, ImGui-Addons) are fetched at configure
time via `FetchContent`. The Dear ImGui tag is the `-docking` variant because
`pexninja` uses multi-viewport + docking features; the simpler demos keep
building unchanged against the same superset.

## Building on Windows (MSVC + the Pulse NuGet)

Prefer Visual Studio over CMake on Windows? There is a native MSVC project
under [`msvc/`](msvc/) that builds the very same `pexninja.cpp` and links the
Pulse runtime from the **`Pexip.Pulse` NuGet package** shipped in this repo
under [`../../sdk/windows`](../../sdk/windows) — the same package the
[`windows`](../windows/) WinForms demo uses, just consumed by a native C++
target. The other dependencies (ImGui docking, ImPlot, gl3w, ImGui-Addons and
GLFW) are fetched at the same pinned versions used here.

```powershell
cd msvc
# Open pexninja.sln in Visual Studio 2022 and press Build (F5 to Build + Run);
# the first build restores the Pexip.Pulse NuGet and fetches the third-party
# sources automatically. From the command line the equivalent one-liner is:
msbuild -restore pexninja.sln /p:Configuration=Release /p:Platform=x64
```

See [`msvc/README.md`](msvc/README.md) for the details.

## Building on macOS

The repo ships the macOS Pulse libraries under [`../../sdk/macos/`](../../sdk/macos/)
(`libpexpulse.dylib`, `libpexlgpl.dylib`). Install GLFW via Homebrew and enable
the same option; the build links the CoreGraphics, CoreFoundation and AppKit
frameworks instead of X11:

```bash
brew install glfw
cmake -S . -B build -DBUILD_PEXNINJA=ON \
      -DPEXIP_PREFIX="$(pwd)/sdk/macos"
cmake --build build -j --target pexninja
```

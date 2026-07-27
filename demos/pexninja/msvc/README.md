# pexninja on Windows — native MSVC build

This folder is a thin **Visual Studio (MSVC) wrapper** that builds the
[`pexninja`](../) reference client on Windows from the same
[`../pexninja.cpp`](../pexninja.cpp) the CMake build uses.

It is the answer to "can I build pexninja for Windows using the Pulse NuGet?".
The Pulse runtime comes straight from the **`Pexip.Pulse` NuGet package** that
ships in this repo under [`../../../sdk/windows`](../../../sdk/windows) — the
exact same package the managed [`windows`](../../windows/) WinForms demo uses,
but consumed by a *native* C++ target here. The package provides:

* the C headers (`build/native/include/pulse*.h`),
* the `pexpulse.lib` import library, and
* the `win-x64` runtime DLLs (`pexpulse.dll`, `pexlgpl.dll`, `tbb12.dll`, …)
  plus the ONNX `share/models/…` assets.

Everything else pexninja needs (Dear ImGui *docking*, ImPlot, gl3w,
ImGui-Addons and GLFW) is fetched into `third_party/` by `fetch-deps.ps1` at
the **same pinned versions** the [CMakeLists.txt](../CMakeLists.txt) uses, so
the two builds stay in lock-step.

## Prerequisites

* **Visual Studio 2022** with the *Desktop development with C++* workload
  (toolset `v143`), or the Build Tools + `msbuild`.
* **git** and **python** on `PATH` (python is only needed once, to run gl3w's
  generator).
* Internet access for the one-time dependency fetch + NuGet restore.

NuGet itself does **not** have to be installed separately — the Pexip.Pulse
package is consumed as a `<PackageReference>`, which Visual Studio (and
`msbuild -restore`) restores automatically.

## Build

Just open **`pexninja.sln`** in Visual Studio 2022 and press **Build** (or
**F5** to Build + Run). Everything happens for you on that first build:

1. NuGet restores the **`Pexip.Pulse`** package from `..\..\..\sdk\windows`
   (declared as a `PackageReference`; no manual `nuget restore` / unwrap step).
2. The `FetchThirdPartyDeps` MSBuild target runs `fetch-deps.ps1` to stage
   Dear ImGui *docking*, ImPlot, gl3w, ImGui-Addons and GLFW into
   `third_party\` at the same pinned versions the
   [CMakeLists.txt](../CMakeLists.txt) uses.
3. pexninja is compiled and the Pulse runtime DLLs + models are copied next to
   the `.exe`.

Prefer the command line? From a *Developer PowerShell for VS 2022* in this
directory, the single command below restores **and** builds (the
`FetchThirdPartyDeps` target stages the third-party sources as part of it):

```powershell
msbuild -restore pexninja.sln /p:Configuration=Release /p:Platform=x64
```

The produced executable and all of the Pulse runtime DLLs + models are copied
to:

```
build\Release\pexninja.exe
```

> The first build is slower because it fetches the third-party sources and
> restores the NuGet package. Subsequent builds skip both (the fetch target
> is a no-op once `third_party\` is populated).

## Run

```powershell
.\build\Release\pexninja.exe
```

pexninja opens its ImGui window; point it at a Pexip Infinity node and join a
conference exactly as on Linux/macOS. See the
[parent README](../README.md) for what the client can do.

## How it fits together

| Piece | Source | Wired up by |
| ----- | ------ | ----------- |
| Pulse headers + `pexpulse.lib` | `Pexip.Pulse` NuGet | `PackageReference` → `Pexip.Pulse.props` (auto-imported) + an explicit `build\native\include` include dir |
| Pulse runtime DLLs + models | `Pexip.Pulse` NuGet | `CopyPulseRuntime` target → next to the `.exe` |
| ImGui (docking), ImPlot, gl3w, ImGui-Addons | git, pinned | `FetchThirdPartyDeps` target → `fetch-deps.ps1` → `third_party\` |
| GLFW (win-x64) | prebuilt release zip | `FetchThirdPartyDeps` target → `fetch-deps.ps1` → `third_party\glfw\` |

The `Pexip.Pulse` package is referenced with `GeneratePathProperty="true"`, so
its install location is available to the project as `$(PkgPexip_Pulse)` — used
for the explicit Pulse include dir, the `pexpulse.lib` directory and the
runtime-asset copy.

Because the NuGet lays its headers out *flat* (`pulse.h`, not
`pexpulse/pulse.h`), `pexninja.cpp` includes the Pulse header by its bare name
under `HOST_WINDOWS`; on Linux/macOS it keeps using `<pexpulse/pulse.h>`.

The project compiles as **C++20** (`stdcpp20`); the docking-branch ImGui and the
Pulse headers expect C++20 on MSVC, so C++17 produced standard-version warnings.

> Only **x64** is configured — `Pexip.Pulse` ships native binaries for
> `win-x64` only.

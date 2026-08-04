# pexclient — a Pexip Infinity client (macOS & Windows)

`pexclient` is a complete, self-contained video client for Pexip Infinity,
built on the Pulse SDK and styled after the "Dark Frosted" design in
`design_handoff_pexninja_refresh` (DM Sans, glass panels, gradient-mesh
backdrop).

What it does:

* **Registers** to Infinity as a device — enter a domain and Pulse resolves
  the `_pexapp._tcp` DNS SRV records, with failover between nodes.
* **Directory search** — the dial bar autocompletes against the registrar,
  listing both device and conference (VMR) aliases as you type.
* **Calls** — dial anything, receive incoming calls (accept/decline banner),
  PIN-protected conferences (host and guest flows), recents + favourites.
* **Media** — camera/mic/speaker selection with live hot-swap, mute toggles
  before and during calls, floating/hideable self-view. The camera is
  released (light off) whenever you're not in a call.
* **Content** — share a display or window (with floor control); incoming
  presentations take the big pane automatically. Send content and camera as
  **dual streams**, or as a **composition** with the camera as a keyed PiP
  for far ends that render only one stream.
* **Background** — blur, or replace with an image (Pulse segmentation).
* **In-conference** — roster drawer with host controls (mute / camera /
  disconnect per participant), broadcast + direct chat with unread badge,
  live captions, and host conference controls (lock, guest mute policy,
  layout, dial-out, disconnect-all).
* **Virtual reception & DTMF** — extension entry when Infinity routes you via
  an IVR, plus a keypad for sending DTMF into gateway calls.
* **Incoming-call alerting** — ring tone, Dock bounce / taskbar flash and
  window raise.
* **SSO** — IdP sign-in for both conference joins and device registration,
  with a provider chooser. Requires the `.app` bundle — see
  [Single sign-on](#single-sign-on-sso). (macOS only for now: the Windows
  build has no `pexip-auth://` scheme registration yet.)

## Build & run (macOS, Apple Silicon)

One-time setup — the Pulse dylibs ship in this repo under
[`../../sdk/macos/`](../../sdk/macos/), so the only external dependency is
GLFW:

```bash
brew install glfw
cmake -S . -B build -DPEXIP_PREFIX="$(pwd)/sdk/macos"    # from the repo root
cmake --build build -j --target pexclient
```

Then run it via the generated launcher (it puts the Pulse dylibs on the
loader path — don't run the bare binary):

```bash
./build/run-pexclient.sh
```

After code changes, just rebuild the target and rerun:

```bash
cmake --build build -j --target pexclient
```

First build fetches Dear ImGui from GitHub (≈5 MB). macOS will prompt for
camera/microphone permission on first call, and screen-sharing needs Screen
Recording permission for the app you launch from (Terminal, VS Code, …) in
System Settings → Privacy & Security.

## Build & run (Windows, x64)

Prerequisites: Visual Studio 2022 (or the Build Tools) with the C++ workload,
and CMake ≥ 3.18. Everything else is self-contained — the Pulse SDK ships in
this repo as a NuGet package under [`../../sdk/windows/`](../../sdk/windows/)
(extracted automatically at configure time), and GLFW + Dear ImGui are
fetched from GitHub on the first build.

From a "x64 Native Tools" prompt (or any shell where `cl` resolves):

```bat
cmake -S . -B build                          &rem from the repo root
cmake --build build --config Release --target pexclient
```

Then run it via the generated launcher (it puts the Pulse DLLs on `PATH` and
sets `PEX_BASE_PATH` — don't run the bare exe):

```bat
build\run-pexclient.bat
```

Windows asks for camera/microphone permission on first use (Settings →
Privacy & security). Display and window sharing work out of the box — no
extra permission needed.

### Building the .app bundle

Everything above works from the terminal — **except SSO**, which needs a real
application bundle (see below). To build one:

```bash
./demos/pexclient/make-bundle.sh     # after building the pexclient target
open build/pexclient.app
```

The script copies the Pulse dylibs into `Contents/Frameworks`, rewrites their
install names to `@rpath`, writes an `Info.plist` that claims the
`pexip-auth://` URL scheme, ad-hoc signs the result and registers it with
LaunchServices. Rerun it after each rebuild.

The bundle also gets its **own** camera / microphone / screen-recording
permissions rather than inheriting the launching terminal's, so expect fresh
prompts the first time. Ad-hoc signing is fine locally; distributing to other
machines additionally needs Developer ID signing and notarization.

## Configuration

Settings live in `pexclient-config.txt` in the directory you run from,
written automatically by the in-app Settings (gear icon) and Devices panels.
You can also create it by hand:

```ini
ui_scale=1.2                       # UI/text size; 1.0 = the design's own sizing
display_name=Alice                 # shown to other participants
reg_host=example.com               # registration domain (_pexapp SRV) — or blank
reg_alias=alice.desk@example.com   # this device's alias on Infinity
reg_user=alice@example.com         # registration username
reg_pass=secret                    # registration password (plain text!)
reg_sso=false                      # true = sign in via IdP instead (see SSO below)
reg_auto=true                      # register on launch
default_server=conf.example.com    # used for bare aliases when NOT registered
dev_camera=                        # preferred device names; blank = system default
dev_mic=
dev_speaker=
bg_mode=0                          # 0 none, 1 blur, 2 replace with bg_image
bg_image=                          # path to a .png/.jpg for bg_mode=2
content_mode=0                     # 0 dual stream, 1 camera composited into content
pip_size=0.25                      # PiP width as a fraction of the frame
pip_corner=3                       # 0 TL, 1 TR, 2 BL, 3 BR
favorite=Desk|desk@example.com     # one line per favourite
recent=Desk|desk@example.com|10:09|00:08|0   # written by the app
```

> **The password is stored in plain text**, which is why
> `pexclient-config.txt` is gitignored — keep it that way.

Registration is optional: without it you can still dial full addresses
(`alias@domain`), or bare aliases against `default_server`. With it you
additionally get directory search, incoming calls, and bare-alias dialling
via the registrar.

## Single sign-on (SSO)

pexclient supports IdP sign-in for **conference joins** (a VMR with an
identity provider attached) and for **device registration** (`reg_sso=true`,
or the "Authenticate with SSO" checkbox in Settings). Pulse drives the flow:
it hands the client the deployment's provider list, pexclient shows a chooser,
and Pulse then opens the system browser for the IdP login.

Getting this working end-to-end has five prerequisites, only one of which is
in the client. All five were needed against a live Microsoft Entra
deployment:

| Layer | Requirement | Symptom when missing |
|---|---|---|
| Infinity IdP config | The domain clients connect through must be in the identity provider's allowed-domains list. | `SSO Authentication Failed. SSO is not available from this domain` |
| IdP (e.g. Entra) | A redirect URI for that domain. Note the IdP redirects to *Infinity*, never to the client — no client-specific IdP config is needed. | IdP error page mid-flow |
| **The client** | Must run as an **`.app` bundle**. The token comes back as a `pexip-auth://` URL, and macOS only delivers custom URL schemes to bundles that declare them; a bare binary can never receive it. | Browser completes login, client never proceeds |
| macOS LaunchServices | If other Pexip apps are installed (Pexip.app, Infinity Connect) they claim `pexip-auth://` too, and one of them may win. | Browser opens the *wrong* Pexip app |
| Infinity registration | For SSO **registration**, `reg_alias` must match the identity the IdP returns. | Infinity log: `REST device authentication failed … Reason="Alias mismatch"` |

To claim the URL scheme without uninstalling the other apps:

```swift
// swift thisfile.swift
import CoreServices
LSSetDefaultHandlerForURLScheme("pexip-auth" as CFString,
                                "org.lorist.pexclient" as CFString)
```

If Infinity reports `Reason="No checker for twisted.cred.credentials.IAnonymous"`
when registering, SSO device registration is not enabled for that alias at all
— the registrar is password-only and rejected the credential-less attempt.

## Where things live

```
src/main.cpp     The whole client (~3k lines): Pulse glue, UI, call flows.
src/Theme.h      Design tokens from the handoff (colours, radii, DM Sans).
assets/fonts/    DM Sans TTFs (SIL OFL) + licence.
assets/          Pulse backdrop image + the script that generates it.
make-bundle.sh   Wraps the built binary into pexclient.app (needed for SSO).
```

Known gaps: breakout rooms; media statistics are not surfaced in the UI.

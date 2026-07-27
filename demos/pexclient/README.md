# pexclient — a macOS Pexip Infinity client

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
  presentations take the big pane automatically.
* **In-conference** — roster drawer with host controls (mute / camera /
  disconnect per participant), broadcast + direct chat with unread badge,
  live captions, and host conference controls (lock, guest mute policy,
  layout, dial-out, disconnect-all).

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

## Configuration

Settings live in `pexclient-config.txt` in the directory you run from,
written automatically by the in-app Settings (gear icon) and Devices panels.
You can also create it by hand:

```ini
display_name=Alice                 # shown to other participants
reg_host=example.com               # registration domain (_pexapp SRV) — or blank
reg_alias=alice.desk@example.com   # this device's alias on Infinity
reg_user=alice@example.com         # registration username
reg_pass=secret                    # registration password (plain text!)
reg_auto=true                      # register on launch
default_server=conf.example.com    # used for bare aliases when NOT registered
dev_camera=                        # preferred device names; blank = system default
dev_mic=
dev_speaker=
favorite=Desk|desk@example.com     # one line per favourite
recent=Desk|desk@example.com|10:09|00:08|0   # written by the app
```

> **The password is stored in plain text**, which is why
> `pexclient-config.txt` is gitignored — keep it that way.

Registration is optional: without it you can still dial full addresses
(`alias@domain`), or bare aliases against `default_server`. With it you
additionally get directory search, incoming calls, and bare-alias dialling
via the registrar.

## Where things live

```
src/main.cpp     The whole client (~3k lines): Pulse glue, UI, call flows.
src/Theme.h      Design tokens from the handoff (colours, radii, DM Sans).
assets/fonts/    DM Sans TTFs (SIL OFL) + licence.
assets/          Pulse backdrop image + the script that generates it.
```

Known gaps: DTMF keypad and virtual-reception (IVR) extension entry,
breakout rooms.

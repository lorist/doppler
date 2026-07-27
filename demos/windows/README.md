# windows — native Windows (WinForms) demo

A simple **native Windows UI** Pexip client: a tiny .NET 8 **Windows Forms** app
that places a Pexip Infinity call using the **Pexip Pulse** NuGet package that
ships in this repo under [`../../sdk/windows`](../../sdk/windows).

It is the Windows / C# sibling of the [`doppler`](../doppler/) demo. Where that
one uses the native C API plus Dear ImGui, this one drives Pulse through the
managed wrapper bundled inside the NuGet package and puts a small WinForms
window on top: type in a server + conference, press **Connect**, and a log pane
shows the call progress until you press **Hang up**.

The call lifecycle mirrors the C demo exactly:

```
pulse_new()                     -> create a Pulse instance      (on startup)
pulse_options_set_*()           -> register version / state / disconnect callbacks
pulse_connect_with_rest_async() -> Connect button
pulse_disconnect_async()        -> Hang up button
pulse_free()                    -> release the handle           (on window close)
```

Because Pulse delivers its callbacks on its own threads, anything that touches
the UI is marshalled back onto the WinForms thread with `BeginInvoke`.

## What's in the box

| File                | Purpose                                                       |
| ------------------- | ------------------------------------------------------------- |
| `MainForm.cs`       | The whole UI + Pulse call lifecycle, heavily commented.       |
| `PulseNative.cs`    | A few raw `pexpulse.dll` entry points (default devices + video window handles) the managed wrapper doesn't surface. |
| `Program.cs`        | WinForms bootstrap (`Application.Run`).                        |
| `DopplerWin.csproj` | Targets `net8.0-windows` / `win-x64` and references `Pexip.Pulse`. |
| `nuget.config`      | Points NuGet at the in-repo `../../sdk/windows` package folder. |

## Build (on Windows)

You need the [.NET 8 SDK](https://dotnet.microsoft.com/download) with the
Windows Desktop workload (included in the normal Windows SDK installer). The
Pexip.Pulse package only ships native binaries (`pexpulse.dll` and friends) for
`win-x64`, so this targets that runtime.

```powershell
cd demos\windows
dotnet build -c Release
```

`dotnet` restores `Pexip.Pulse` straight from the `sdk\windows` folder in this
repo (see `nuget.config`) and copies all of the native runtime assets
(`pexpulse.dll`, `pexlgpl.dll`, `tbb12.dll`, the ONNX `share\models\…`, …) next
to the produced `doppler-win.exe`, so there is nothing else to install.

> The project sets `<EnableWindowsTargeting>true</EnableWindowsTargeting>` so it
> can also be *restored / compiled* (not run) from a non-Windows machine, e.g.
> in CI.

## Run

Launch the app:

```powershell
.\bin\Release\net8.0-windows\win-x64\doppler-win.exe
```

Then in the window:

1. **Server** — your Pexip Infinity node, e.g. `vc.example.com`
2. **Conference** — the VMR / conference alias, e.g. `meet.alice`
3. **Display name** — how you appear to others (defaults to `Doppler Windows demo`)
4. **PIN** — only if the conference requires one
5. Press **Connect**. The log pane shows progress and state transitions:

```
12:00:00  Pulse initialised. Enter a server + conference and press Connect.
12:00:05  Connecting to vc.example.com / meet.alice as "Alice" ...
12:00:05  Infinity server reports v36.0
12:00:06  progress:  50 %  Connecting media
12:00:06  status: PULSE_CONNECTION_STATUS_CONNECTING (service=..., blocked=False)
12:00:07  status: PULSE_CONNECTION_STATUS_CONNECTED (service=..., blocked=False)
12:00:07  connect: success
```

Press **Hang up** (the Connect button toggles while in a call) to leave, or just
close the window — the app disconnects and frees the Pulse handle on the way out.

## Audio & video

When you connect, the demo attaches the system's **default camera, microphone
and speaker** to the call (`pulse_device_session_connect_system_default` for
each of camera-in, mic-in and speaker-out) — the same thing the C
[`doppler`](../doppler/) demo does. Without those device sessions Pulse has
nothing to capture or play, so the call would connect but stay silent and dark
in both directions.

The video is shown **inside the window**: before connecting, the app points
Pulse's renderers at two WinForms panels with
`pulse_options_set_remote_video_window_handle` (the far-end video, filling the
top of the window) and `pulse_options_set_self_view_window_handle` (your own
camera, picture-in-picture in the bottom-right). This is the WinForms take on
how [`pexninja`](../pexninja/) and `doppler` render video — they pull raw RGBA
frames from a Pulse *data session* and paint them with OpenGL, whereas here we
let Pulse draw straight into native window handles the framework already gives
us. (The two extra entry points live in `PulseNative.cs`; the bundled managed
wrapper doesn't surface them, so the demo p/invokes them directly.)

Presentations / screen-shares are not surfaced in this simple demo (the
presentation window handle is set to `NULL` so Pulse doesn't pop up its own
window). For RTMP ingest, the "Twitch mix" and the other more advanced building
blocks, see the heavily-commented [`doppler`](../doppler/) demo and the larger
[`pexninja`](../pexninja/) reference client.

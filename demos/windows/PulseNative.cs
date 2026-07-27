// PulseNative — a couple of raw native entry points the managed Pexip.Pulse
// wrapper does not (yet) surface, but which the demo needs to get media flowing.
//
// The NuGet wrapper exposes most of the Pulse C API, but two pieces required to
// see and hear anything are missing from it:
//
//   * pulse_device_session_connect_system_default() — binds the operating
//     system's *default* camera / microphone / speaker to the call. Without a
//     device session bound on each direction Pulse has nothing to capture or
//     play, so the far end sees/hears nothing and we get nothing back. This is
//     the exact same call the C `doppler` demo makes in connect_default_devices().
//
//   * pulse_options_set_*_window_handle() — hands Pulse a native window to draw
//     a given video stream into. By default Pulse auto-spawns its own top-level
//     windows for the remote video and self-view; pointing these at our own
//     WinForms panels instead lets the video live *inside* the app window.
//
// These are plain `extern "C"` exports of pexpulse.dll, so we p/invoke them
// directly here, mirroring exactly how the generated wrapper declares its own
// imports (Cdecl calling convention + SafeDirectories so the loader picks up the
// pexpulse.dll that the build copies next to doppler-win.exe).

using System.Runtime.InteropServices;

using Pexip.Pulse.NativeEnums;

namespace DopplerWin;

internal static class PulseNative
{
    private const string Lib = "pexpulse.dll";

    // Attach the OS default device for (media_type, media_direction) to a
    // media-content slot (we only use MAIN here).
    [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
    [DefaultDllImportSearchPaths(DllImportSearchPath.SafeDirectories)]
    internal static extern PulseErrorType pulse_device_session_connect_system_default(
        IntPtr client,
        PulseMediaContent media_content,
        PulseMediaType media_type,
        PulseMediaDirection media_direction);

    // Render the incoming far-end video into window_handle (an HWND), or pass
    // IntPtr.Zero to disable Pulse's own auto-spawned window. Must be called
    // before connecting.
    [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
    [DefaultDllImportSearchPaths(DllImportSearchPath.SafeDirectories)]
    internal static extern PulseErrorType pulse_options_set_remote_video_window_handle(
        IntPtr client, IntPtr window_handle);

    // Render the local camera self-view into window_handle (an HWND), or pass
    // IntPtr.Zero to disable. Must be called before connecting.
    [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
    [DefaultDllImportSearchPaths(DllImportSearchPath.SafeDirectories)]
    internal static extern PulseErrorType pulse_options_set_self_view_window_handle(
        IntPtr client, IntPtr window_handle);

    // Render an incoming presentation/screen-share into window_handle (an HWND),
    // or pass IntPtr.Zero to disable the auto-spawned presentation window.
    [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
    [DefaultDllImportSearchPaths(DllImportSearchPath.SafeDirectories)]
    internal static extern PulseErrorType pulse_options_set_presentation_video_window_handle(
        IntPtr client, IntPtr window_handle);
}

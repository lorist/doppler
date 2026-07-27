// doppler-win — the smallest possible Windows Pexip client.
//
// This is the Windows / .NET sibling of the Linux `doppler` demo in
// ../../src/main.cpp. Instead of the native C API + Dear ImGui, it drives Pulse
// through the managed wrapper that ships inside the Pexip.Pulse NuGet package
// (../../nuget/Pexip.Pulse.*.nupkg), and puts a tiny native Windows Forms UI on
// top of it.
//
// Program.cs is just the WinForms bootstrap; all of the interesting Pulse code
// lives in MainForm.cs.

namespace DopplerWin;

internal static class Program
{
    [STAThread]
    private static void Main()
    {
        ApplicationConfiguration.Initialize();
        Application.Run(new MainForm());
    }
}

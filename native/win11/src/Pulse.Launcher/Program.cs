using System.Diagnostics;
using System.Runtime.InteropServices;

namespace Pulse.Launcher;

public static partial class Program
{
    [STAThread]
    static int Main(string[] args)
    {
        string root = AppContext.BaseDirectory;
        string app = Path.Combine(root, "data", "Pulse.exe");
        if (!File.Exists(app))
        {
            MessageBoxW(0,
                "Pulse can't find its files.\n\n" +
                "Right-click the zip, choose Extract All, then run Pulse.exe from the extracted folder. " +
                "Keep the data and lang folders next to Pulse.exe.",
                "Pulse", 0x10);
            return 1;
        }
        try
        {
            var start = new ProcessStartInfo(app) { WorkingDirectory = Path.GetDirectoryName(app)!, UseShellExecute = false };
            foreach (var a in args) start.ArgumentList.Add(a);
            using var p = Process.Start(start);
            return 0;
        }
        catch (Exception e)
        {
            MessageBoxW(0, $"Pulse could not start.\n\n{e.Message}", "Pulse", 0x10);
            return 1;
        }
    }

    [LibraryImport("user32.dll", StringMarshalling = StringMarshalling.Utf16)]
    private static partial int MessageBoxW(nint hwnd, string text, string caption, uint type);
}

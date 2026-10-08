using System.Diagnostics;
using System.Runtime.InteropServices;

namespace Pulse.Launcher;

[System.Runtime.Versioning.SupportedOSPlatform("windows")]
public static partial class Program
{
    [STAThread]
    static int Main(string[] args)
    {
        if (args.Length == 2 && args[0] == "--apply-update") return ApplyUpdate(AppContext.BaseDirectory, args[1]);

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

    /// <summary>
    /// Runs from the unpacked update. Asks the running Pulse to quit, waits for
    /// every Pulse in the install folder to close, swaps data\, lang\ and
    /// Pulse.exe, then starts Pulse again. If the swap fails, the old files go back.
    /// </summary>
    static int ApplyUpdate(string from, string root)
    {
        root = Path.GetFullPath(root);
        string app = Path.Combine(root, "data", "Pulse.exe");
        try
        {
            if (EventWaitHandle.TryOpenExisting(@"Local\Pulse.Win11.Quit", out var quit)) using (quit) quit.Set();
            var deadline = DateTime.UtcNow.AddSeconds(30);
            while (Running(app))
            {
                if (DateTime.UtcNow > deadline) throw new TimeoutException("Pulse did not close");
                Thread.Sleep(250);
            }

            string stamp = DateTime.UtcNow.Ticks.ToString();
            var moved = new List<(string Old, string Backup)>();
            try
            {
                foreach (var name in new[] { "data", "lang" })
                {
                    string old = Path.Combine(root, name);
                    if (!Directory.Exists(old)) continue;
                    string backup = Path.Combine(root, $"{name}.old-{stamp}");
                    Directory.Move(old, backup);
                    moved.Add((old, backup));
                }
                foreach (var name in new[] { "data", "lang" })
                    CopyDir(Path.Combine(from, name), Path.Combine(root, name));
                File.Copy(Path.Combine(from, "Pulse.exe"), Path.Combine(root, "Pulse.exe"), overwrite: true);
            }
            catch
            {
                foreach (var (old, backup) in moved)
                {
                    try { if (Directory.Exists(old)) Directory.Delete(old, true); Directory.Move(backup, old); }
                    catch (Exception) { }
                }
                throw;
            }
            foreach (var (_, backup) in moved)
            {
                try { Directory.Delete(backup, true); } catch (Exception) { }
            }
            return Start(app, root);
        }
        catch (Exception e)
        {
            MessageBoxW(0, $"Pulse could not update.\n\n{e.Message}", "Pulse", 0x10);
            if (File.Exists(app) && !Running(app)) Start(app, root);
            return 1;
        }
    }

    static bool Running(string app) =>
        Process.GetProcessesByName("Pulse").Any(p =>
        {
            try { return string.Equals(p.MainModule?.FileName, app, StringComparison.OrdinalIgnoreCase); }
            catch (Exception) { return false; }
            finally { p.Dispose(); }
        });

    static int Start(string app, string root)
    {
        using var p = Process.Start(new ProcessStartInfo(app) { WorkingDirectory = Path.Combine(root, "data"), UseShellExecute = false });
        return 0;
    }

    static void CopyDir(string from, string to)
    {
        Directory.CreateDirectory(to);
        foreach (var f in Directory.EnumerateFiles(from)) File.Copy(f, Path.Combine(to, Path.GetFileName(f)), overwrite: true);
        foreach (var d in Directory.EnumerateDirectories(from)) CopyDir(d, Path.Combine(to, Path.GetFileName(d)));
    }

    [LibraryImport("user32.dll", StringMarshalling = StringMarshalling.Utf16)]
    private static partial int MessageBoxW(nint hwnd, string text, string caption, uint type);
}

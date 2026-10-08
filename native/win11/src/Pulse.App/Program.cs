using Microsoft.UI.Dispatching;
using Microsoft.UI.Xaml;
using Pulse.App.Interop;

namespace Pulse.App;

public static class Program
{
    [STAThread]
    static int Main()
    {
        var args = Environment.GetCommandLineArgs().Skip(1).ToArray();
        if (args.Contains("--update-check", StringComparer.OrdinalIgnoreCase) || args.Contains("--update", StringComparer.OrdinalIgnoreCase))
            return CommandLineUpdate(args.Contains("--update", StringComparer.OrdinalIgnoreCase));

        // "Pulse.exe --quit" asks the running copy to close, as the tray menu's Quit does.
        if (Environment.GetCommandLineArgs().Skip(1).Any(a => a.Equals("--quit", StringComparison.OrdinalIgnoreCase)))
        {
            if (EventWaitHandle.TryOpenExisting(QuitEventName, out var quit)) using (quit) quit.Set();
            return 0;
        }

        // One Pulse per sign-in session.
        using var single = new Mutex(true, @"Local\Pulse.Win11.SingleInstance", out bool first);
        if (!first) return 0;

        AppDomain.CurrentDomain.UnhandledException += (_, e) => { if (Started) Log(e.ExceptionObject as Exception); else Fail(e.ExceptionObject as Exception); };
        try
        {
            WinRT.ComWrappersSupport.InitializeComWrappers();
            Application.Start(p =>
            {
                SynchronizationContext.SetSynchronizationContext(new DispatcherQueueSynchronizationContext(DispatcherQueue.GetForCurrentThread()));
                var app = new App();
                // Before the flyout is up, an error means Pulse cannot start. After that,
                // log it and keep the app running rather than vanish from the tray.
                app.UnhandledException += (_, e) => { e.Handled = true; if (Started) Log(e.Exception); else Fail(e.Exception); };
                GC.KeepAlive(app);
            });
        }
        catch (Exception e)
        {
            Fail(e);
            return 1;
        }
        return 0;
    }

    /// <summary>
    /// --update-check prints the running and latest versions. --update also
    /// installs a newer one: the running Pulse is asked to quit, and the new
    /// launcher swaps the files and starts it again. Used by scripts and CI.
    /// </summary>
    static int CommandLineUpdate(bool install)
    {
        try
        {
            var latest = Update.Updater.LatestAsync().GetAwaiter().GetResult();
            string current = Update.Updater.CurrentVersion;
            Console.WriteLine($"current={current} latest={latest.Version}");
            if (!install || Update.Updater.Compare(latest.Version, current) <= 0) return 0;
            var root = Update.Updater.InstallRoot ?? throw new InvalidOperationException("Pulse was not started from its zip folder");
            string files = Update.Updater.DownloadAsync(latest).GetAwaiter().GetResult();
            Update.Updater.StartApply(files, root);
            Console.WriteLine($"installing {latest.Version} into {root}");
            return 0;
        }
        catch (Exception e)
        {
            Console.Error.WriteLine($"update failed: {e.Message}");
            return 1;
        }
    }

    public const string QuitEventName = @"Local\Pulse.Win11.Quit";

    /// <summary>Set once OnLaunched has finished.</summary>
    public static bool Started { get; set; }

    static string LogPath => Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "Pulse", "crash.log");

    public static void Log(Exception? e)
    {
        try
        {
            Directory.CreateDirectory(Path.GetDirectoryName(LogPath)!);
            File.AppendAllText(LogPath, $"{DateTime.Now:u} Pulse {typeof(Program).Assembly.GetName().Version}\r\n{e?.ToString() ?? "Unknown error"}\r\n\r\n");
        }
        catch (Exception) { }
    }

    /// <summary>
    /// A crash should never look like "nothing happened". Write the error to
    /// %LOCALAPPDATA%\Pulse\crash.log, say so in a message box, and quit.
    /// </summary>
    public static void Fail(Exception? e)
    {
        Log(e);
        string log = LogPath;
        // Headless test runs set PULSE_NO_DIALOG so a crash ends the process instead of waiting on a click.
        if (Environment.GetEnvironmentVariable("PULSE_NO_DIALOG") is null)
            Win32.MessageBoxW(0, $"Pulse could not start.\n\n{e?.Message}\n\nDetails: {log}", "Pulse", Win32.MB_ICONERROR);
        Environment.Exit(1);
    }
}

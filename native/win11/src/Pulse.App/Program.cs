using Microsoft.UI.Dispatching;
using Microsoft.UI.Xaml;
using Pulse.App.Interop;

namespace Pulse.App;

public static class Program
{
    [STAThread]
    static int Main()
    {
        // One Pulse per sign-in session.
        using var single = new Mutex(true, @"Local\Pulse.Win11.SingleInstance", out bool first);
        if (!first) return 0;

        AppDomain.CurrentDomain.UnhandledException += (_, e) => Fail(e.ExceptionObject as Exception);
        try
        {
            WinRT.ComWrappersSupport.InitializeComWrappers();
            Application.Start(p =>
            {
                SynchronizationContext.SetSynchronizationContext(new DispatcherQueueSynchronizationContext(DispatcherQueue.GetForCurrentThread()));
                var app = new App();
                app.UnhandledException += (_, e) => { e.Handled = true; Fail(e.Exception); };
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
    /// A crash should never look like "nothing happened". Write the error to
    /// %LOCALAPPDATA%\Pulse\crash.log, say so in a message box, and quit.
    /// </summary>
    public static void Fail(Exception? e)
    {
        string text = e?.ToString() ?? "Unknown error";
        string log = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "Pulse", "crash.log");
        try
        {
            Directory.CreateDirectory(Path.GetDirectoryName(log)!);
            File.AppendAllText(log, $"{DateTime.Now:u} Pulse {typeof(Program).Assembly.GetName().Version}\r\n{text}\r\n\r\n");
        }
        catch (Exception) { }
        Console.Error.WriteLine(text);
        // Headless test runs set PULSE_NO_DIALOG so a crash ends the process instead of waiting on a click.
        if (Environment.GetEnvironmentVariable("PULSE_NO_DIALOG") is null)
            Win32.MessageBoxW(0, $"Pulse could not start.\n\n{e?.Message}\n\nDetails: {log}", "Pulse", Win32.MB_ICONERROR);
        Environment.Exit(1);
    }
}

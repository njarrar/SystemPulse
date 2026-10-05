using Microsoft.UI.Dispatching;
using Microsoft.UI.Xaml;

namespace Pulse.App;

public static class Program
{
    [STAThread]
    static int Main()
    {
        // One Pulse per sign-in session.
        using var single = new Mutex(true, @"Local\Pulse.Win11.SingleInstance", out bool first);
        if (!first) return 0;

        WinRT.ComWrappersSupport.InitializeComWrappers();
        Application.Start(p =>
        {
            SynchronizationContext.SetSynchronizationContext(new DispatcherQueueSynchronizationContext(DispatcherQueue.GetForCurrentThread()));
            var app = new App();
            GC.KeepAlive(app);
        });
        return 0;
    }
}

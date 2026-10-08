using System.Diagnostics;
using Microsoft.UI.Dispatching;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Markup;
using Microsoft.UI.Xaml.XamlTypeInfo;
using Pulse.App.Interop;
using Pulse.App.Localization;
using Pulse.App.Telemetry;
using Pulse.App.Tray;
using Pulse.App.UI;
using Pulse.Core.Formatting;
using Pulse.Core.I18n;
using Pulse.Core.Models;
using Pulse.Core.Presentation;
using Windows.UI.ViewManagement;

namespace Pulse.App;

/// <summary>
/// The tray app. No main window: a notification-area icon (tier 1: live icon
/// and tooltip) opens the flyout (tiers 2 and 3). Built without .xaml files,
/// so the app supplies WinUI's control metadata itself.
/// </summary>
public sealed partial class App : Application, IXamlMetadataProvider, IFlyoutHost
{
    readonly XamlControlsXamlMetaDataProvider _metadata = new();
    readonly NavState _nav = new();
    readonly UISettings _ui = new();
    DispatcherQueue _dispatcher = null!;
    TrayIcon _tray = null!;
    FlyoutWindow _window = null!;
    FlyoutView? _view;
    UiContext? _ctx;
    string _lastTip = "";
    EventWaitHandle? _quitSignal;
    bool _quitting;
    (int Cpu, int Ram, bool Hog, bool Light) _lastIcon = (-1, -1, false, false);

    public Settings Settings { get; } = Settings.Load();
    public LocaleRegistry Locales { get; private set; } = new();
    public Sampler Sampler { get; private set; } = null!;

    public IXamlType GetXamlType(Type type) => _metadata.GetXamlType(type);
    public IXamlType GetXamlType(string fullName) => _metadata.GetXamlType(fullName);
    public XmlnsDefinition[] GetXmlnsDefinitions() => _metadata.GetXmlnsDefinitions();

    protected override void OnLaunched(LaunchActivatedEventArgs args)
    {
        Resources.MergedDictionaries.Add(new XamlControlsResources());
        _dispatcher = DispatcherQueue.GetForCurrentThread();
        Locale.MissingKey = (key, code) => Debug.WriteLine($"[i18n] missing {key} in {code}");
        Locales = Catalog.Load();

        Sampler = new Sampler { Paused = !Settings.LiveUpdates };
        Sampler.Sampled += s => _dispatcher.TryEnqueue(() => OnSample(s));

        _window = new FlyoutWindow();
        _tray = new TrayIcon();
        _tray.Selected += ToggleFlyout;
        _tray.ContextMenu += ShowTrayMenu;
        _ui.ColorValuesChanged += (_, _) => _dispatcher.TryEnqueue(() => { if (Settings.Theme == ThemeChoice.System) Rebuild(); UpdateTray(Sampler.Last, force: true); });

        Rebuild();
        UpdateTray(new Snapshot(), force: true);
        Sampler.Start();

        // Files left in %TEMP% by the update that started this copy.
        Task.Delay(TimeSpan.FromSeconds(30)).ContinueWith(_ => Update.Updater.CleanUp());

        _quitSignal = new EventWaitHandle(false, EventResetMode.AutoReset, Program.QuitEventName);
        ThreadPool.RegisterWaitForSingleObject(_quitSignal, (_, _) => _dispatcher.TryEnqueue(Quit), null, Timeout.Infinite, executeOnlyOnce: true);
        _dispatcher.TryEnqueue(DispatcherQueuePriority.Low, () =>
        {
            _window.ShowAt(_tray.Bounds(), _view?.DesiredHeight() ?? 600);
            // Let the first layout and focus pass run before counting the start as done.
            _dispatcher.TryEnqueue(DispatcherQueuePriority.Low, () => Program.Started = true);
        });
    }

    bool SystemIsDark()
    {
        var bg = _ui.GetColorValue(UIColorType.Background);
        return bg.R + bg.G + bg.B < 384;
    }

    static string SystemLanguage()
    {
        try
        {
            var langs = Windows.System.UserProfile.GlobalizationPreferences.Languages;
            if (langs.Count > 0) return langs[0];
        }
        catch (Exception) { }
        return System.Globalization.CultureInfo.CurrentUICulture.Name;
    }

    bool Dark => Settings.Theme switch { ThemeChoice.Dark => true, ThemeChoice.Light => false, _ => SystemIsDark() };

    /// <summary>The saved choice, or the Windows display language ("Match system"); unknown codes fall back to English.</summary>
    Locale CurrentLocale()
    {
        string? code = Settings.Language ?? SystemLanguage();
        return Locales.Count > 0 ? Locales.Get(code) : new Locale(new LocaleData { Code = "en" });
    }

    /// <summary>Builds the flyout tree again: language, direction or theme changed.</summary>
    void Rebuild()
    {
        var locale = CurrentLocale();
        var theme = new Theme(Dark);
        _ctx = new UiContext(theme, locale, new Fmt(locale, Settings.Fahrenheit));
        _view = new FlyoutView(_ctx, this, _nav);
        var root = new Grid { Background = _window.FallbackFill(theme) };
        root.Children.Add(_view);
        _window.Content = root;
        _window.SetDark(theme.IsDark);
        _view.Update(Sampler.Last);
        ContentHeightChanged();
    }

    void OnSample(Snapshot s)
    {
        UpdateTray(s);
        if (_window.IsOpen) _view?.Update(s);
    }

    void UpdateTray(Snapshot s, bool force = false)
    {
        if (_ctx is null) return;
        string tip = _ctx.X.TrayTooltip(s);
        bool light = IconRenderer.TaskbarIsLight();
        var key = ((int)Math.Round(s.Cpu.TotalPct), (int)Math.Round(s.Memory.LoadPct), Sampler.Hog is not null, light);
        nint icon = 0;
        if (force || key != _lastIcon)
        {
            uint dpi = Win32.GetDpiForWindow(_window.Hwnd);
            icon = IconRenderer.Render(Win32.GetSystemMetricsForDpi(Win32.SM_CXSMICON, dpi == 0 ? 96 : dpi), s.Cpu.TotalPct, s.Memory.LoadPct, Sampler.Hog is not null, light);
            _lastIcon = key;
        }
        if (icon != 0 || force || tip != _lastTip)
        {
            _tray.Update(icon, tip);
            _lastTip = tip;
        }
    }

    void ToggleFlyout()
    {
        if (_window.IsOpen) { _window.Hide(); return; }
        // A click on the icon first deactivates (and hides) the flyout; do not reopen it at once.
        if ((DateTime.UtcNow - _window.HiddenAt).TotalMilliseconds < 300) return;
        _view?.Update(Sampler.Last);
        _window.ShowAt(_tray.Bounds(), _view?.DesiredHeight() ?? 600);
    }

    void ShowTrayMenu(int x, int y)
    {
        if (_ctx is null) return;
        var L = _ctx.L;
        int id = _tray.ShowMenu(x, y,
        [
            (1, L.T("openPulse")),
            (2, L.T("openMonitor", ("monitor", _ctx.X.Monitor))),
            (0, null),
            (4, L.T("checkUpdates")),
            (0, null),
            (3, L.T("quit"))
        ], L.Rtl);
        switch (id)
        {
            case 1: _window.ShowAt(_tray.Bounds(), _view?.DesiredHeight() ?? 600); break;
            case 2: OpenTaskManager(); break;
            case 3: Quit(); break;
            case 4: CheckForUpdates(); break;
        }
    }

    bool _checking;

    /// <summary>Tray menu: asks GitHub for a newer Pulse and, if the user agrees, installs it and restarts.</summary>
    void CheckForUpdates()
    {
        if (_checking || _ctx is null) return;
        _checking = true;
        var L = _ctx.L;
        string current = Update.Updater.CurrentVersion;
        uint dir = L.Rtl ? Win32.MB_RTLREADING | Win32.MB_RIGHT : 0;
        int Ask(string text, uint flags) => Win32.MessageBoxW(_tray.Hwnd, text, "Pulse", flags | dir | Win32.MB_SETFOREGROUND);
        // Off the UI thread: the download can take a while and the tray keeps updating.
        Task.Run(async () =>
        {
            try
            {
                var root = Update.Updater.InstallRoot ?? throw new InvalidOperationException("Pulse was not started from its zip folder");
                var latest = await Update.Updater.LatestAsync();
                if (Update.Updater.Compare(latest.Version, current) <= 0)
                {
                    Ask(L.T("updateNone", ("current", current)), Win32.MB_ICONINFORMATION);
                    return;
                }
                if (Ask(L.T("updateAvailable", ("version", latest.Version), ("current", current)), Win32.MB_YESNO | Win32.MB_ICONINFORMATION) != Win32.IDYES) return;
                string files = await Update.Updater.DownloadAsync(latest);
                Update.Updater.StartApply(files, root);
                _dispatcher.TryEnqueue(Quit);
            }
            catch (Exception e)
            {
                Program.Log(e);
                Ask(L.T("updateFailed", ("error", e.Message)), Win32.MB_ICONERROR);
            }
            finally { _checking = false; }
        });
    }

    // IFlyoutHost ---------------------------------------------------------

    public void SetLanguage(string? code)
    {
        Settings.Language = code;
        Settings.Save();
        Rebuild();
        UpdateTray(Sampler.Last, force: true);
    }

    public void ToggleTheme()
    {
        Settings.Theme = Dark ? ThemeChoice.Light : ThemeChoice.Dark;
        Settings.Save();
        Rebuild();
    }

    public void SetLiveUpdates(bool on)
    {
        Settings.LiveUpdates = on;
        Settings.Save();
        Sampler.Paused = !on;
    }

    public void SetFahrenheit(bool on)
    {
        Settings.Fahrenheit = on;
        Settings.Save();
        Rebuild();
    }

    public void OpenTaskManager()
    {
        try { Process.Start(new ProcessStartInfo("taskmgr.exe") { UseShellExecute = true }); }
        catch (Exception) { }
    }

    public void Quit()
    {
        if (_quitting) return;
        _quitting = true;
        _window.Hide();
        _tray.Dispose();
        Sampler.Dispose();
        Exit();
    }

    // Ended apps: what Pulse ended this session, so Restore can relaunch it.
    readonly List<(string Id, List<string> Paths, bool Demo)> _ended = [];

    public int EndedCount => _ended.Count;

    public bool EndApp(AppSample app)
    {
        if (DemoOverlay.IsDemo(app))
        {
            _ended.Add((app.Id, [], true));
            SetDemoHog(false);
            return true;
        }
        var (ended, _, paths) = ProcessKiller.End(app.Pids);
        if (ended > 0) _ended.Add((app.Id, paths, false));
        return ended > 0;
    }

    public void RestoreEnded()
    {
        foreach (var (_, paths, demo) in _ended)
        {
            if (demo) { Sampler.DemoHog = true; continue; }
            // Relaunch the main image once; helper processes start themselves.
            var path = paths.FirstOrDefault(p => File.Exists(p));
            if (path is null) continue;
            try { Process.Start(new ProcessStartInfo(path) { UseShellExecute = true }); }
            catch (Exception) { }
        }
        _ended.Clear();
    }

    public void SetDemoHog(bool on)
    {
        Sampler.DemoHog = on;
        if (on) _nav.HogDismissedFor = null;
    }

    public void SetDemoCharging(bool on) => Sampler.DemoCharging = on;

    public void ContentHeightChanged()
    {
        if (_view is null) return;
        _dispatcher.TryEnqueue(DispatcherQueuePriority.Low, () =>
        {
            if (_window.IsOpen && _view is not null) _window.Place(_view.DesiredHeight());
        });
    }
}

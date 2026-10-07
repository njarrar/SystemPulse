using Microsoft.UI.Composition.SystemBackdrops;
using Microsoft.UI.Windowing;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Media;
using Pulse.App.Interop;
using Windows.Graphics;
using WinRT.Interop;

namespace Pulse.App.UI;

/// <summary>
/// The flyout window: 420 px wide, no title bar, hidden from Alt+Tab and the
/// taskbar, Desktop Acrylic behind it (Mica, then a solid fill, where acrylic
/// is unavailable), rounded corners from DWM, placed above the tray icon and
/// hidden when it loses focus.
/// </summary>
public sealed unsafe partial class FlyoutWindow : Window
{
    const double Margin = 12;
    readonly AppWindow _app;
    readonly nint _hwnd;
    RECT? _anchor;

    public DateTime HiddenAt { get; private set; } = DateTime.MinValue;
    public bool IsOpen => _app.IsVisible;
    public nint Hwnd => _hwnd;
    public event Action? Closed2;

    public FlyoutWindow()
    {
        Title = "Pulse";
        _hwnd = WindowNative.GetWindowHandle(this);
        _app = AppWindow.GetFromWindowId(Microsoft.UI.Win32Interop.GetWindowIdFromWindow(_hwnd));
        _app.IsShownInSwitchers = false;
        _app.SetIcon(Path.Combine(AppContext.BaseDirectory, "Assets", "Pulse.ico"));
        var presenter = OverlappedPresenter.Create();
        presenter.IsResizable = false;
        presenter.IsMaximizable = false;
        presenter.IsMinimizable = false;
        presenter.IsAlwaysOnTop = true;
        presenter.SetBorderAndTitleBar(true, false);
        _app.SetPresenter(presenter);
        _app.Closing += (_, e) => { e.Cancel = true; Hide(); };

        if (DesktopAcrylicController.IsSupported()) SystemBackdrop = new DesktopAcrylicBackdrop();
        else if (MicaController.IsSupported()) SystemBackdrop = new MicaBackdrop { Kind = MicaKind.Base };

        int round = Win32.DWMWCP_ROUND;
        Win32.DwmSetWindowAttribute(_hwnd, Win32.DWMWA_WINDOW_CORNER_PREFERENCE, &round, sizeof(int));

        Activated += (_, e) =>
        {
            if (e.WindowActivationState == WindowActivationState.Deactivated && _app.IsVisible) Hide();
        };
    }

    public void SetDark(bool dark)
    {
        int v = dark ? 1 : 0;
        Win32.DwmSetWindowAttribute(_hwnd, Win32.DWMWA_USE_IMMERSIVE_DARK_MODE, &v, sizeof(int));
        if (SystemBackdrop is null && Content is FrameworkElement fe)
            fe.RequestedTheme = dark ? ElementTheme.Dark : ElementTheme.Light;
    }

    /// <summary>Solid fill under the content when no system backdrop is available.</summary>
    public Brush? FallbackFill(Theme t) => SystemBackdrop is null ? t.Brush("fly-solid") : null;

    double Scale => Math.Max(1, Win32.GetDpiForWindow(_hwnd) / 96.0);

    public void ShowAt(RECT? trayIcon, double contentHeight)
    {
        _anchor = trayIcon ?? _anchor;
        Place(contentHeight);
        _app.Show(true);
        Activate();
        Win32.SetForegroundWindow(_hwnd);
    }

    /// <summary>Resizes to the content (up to the work area) and keeps the flyout pinned above the tray.</summary>
    public void Place(double contentHeight)
    {
        var anchor = _anchor;
        PointInt32 probe = anchor is RECT r ? new PointInt32((r.Left + r.Right) / 2, (r.Top + r.Bottom) / 2) : new PointInt32(int.MaxValue / 2, int.MaxValue / 2);
        var area = DisplayArea.GetFromPoint(probe, DisplayAreaFallback.Primary).WorkArea;
        double s = Scale;
        int m = (int)Math.Round(Margin * s);
        int w = (int)Math.Round(FlyoutView.Width420 * s);
        int h = (int)Math.Min(Math.Round(contentHeight * s), area.Height - 2 * m);

        int x, y;
        if (anchor is RECT a)
        {
            x = Math.Clamp(a.Right - w + m / 2, area.X + m, area.X + area.Width - w - m);
            bool taskbarOnTop = a.Bottom <= area.Y + 2;
            y = taskbarOnTop ? area.Y + m : area.Y + area.Height - h - m;
        }
        else
        {
            x = area.X + area.Width - w - m;
            y = area.Y + area.Height - h - m;
        }
        _app.MoveAndResize(new RectInt32(x, y, w, h));
    }

    public void Hide()
    {
        _app.Hide();
        HiddenAt = DateTime.UtcNow;
        Closed2?.Invoke();
    }
}

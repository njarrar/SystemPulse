using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Automation;
using Microsoft.UI.Xaml.Automation.Peers;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Input;
using Microsoft.UI.Xaml.Media;
using Pulse.App.Telemetry;
using Pulse.Core.I18n;
using Pulse.Core.Models;
using Pulse.Core.Presentation;
using Windows.System;

namespace Pulse.App.UI;

public enum ViewMode { Overview, Detail, Settings }

/// <summary>What the flyout shows; survives rebuilds for language and theme changes.</summary>
public sealed class NavState
{
    public ViewMode Mode { get; set; } = ViewMode.Overview;
    public DetailKind Kind { get; set; } = DetailKind.Cpu;
    public string? AppId { get; set; }
    public Texts.AppSort Sort { get; set; } = Texts.AppSort.Cpu;
    public string? HogDismissedFor { get; set; }
}

/// <summary>What the flyout asks of the app around it.</summary>
public interface IFlyoutHost
{
    Settings Settings { get; }
    LocaleRegistry Locales { get; }
    Sampler Sampler { get; }
    /// <summary>Switches language live; null follows the Windows display language.</summary>
    void SetLanguage(string? code);
    void ToggleTheme();
    void SetLiveUpdates(bool on);
    void SetFahrenheit(bool on);
    void OpenTaskManager();
    void Quit();
    /// <summary>Ends an app (the demo hog only turns its switch off). True when something ended.</summary>
    bool EndApp(AppSample app);
    int EndedCount { get; }
    /// <summary>Relaunches the apps Pulse ended this session.</summary>
    void RestoreEnded();
    void SetDemoHog(bool on);
    void SetDemoCharging(bool on);
    void ContentHeightChanged();
}

/// <summary>
/// The 420 px flyout, three tiers deep: the overview (tier 2) and a detail
/// view per metric or app (tier 3). The tray tooltip is tier 1. The whole tree
/// is rebuilt when language or theme changes; each sample only updates text,
/// meters and paths.
/// </summary>
public sealed partial class FlyoutView : Grid
{
    public const double Width420 = 420, Pad = 12, Gap = 8;

    readonly UiContext c;
    readonly IFlyoutHost host;
    readonly NavState nav;
    readonly Grid _body = new();
    readonly ScrollViewer _scroll;
    readonly Grid _overlay = new();
    readonly Border _toast;
    readonly TextBlock _toastText;
    DispatcherTimer? _toastTimer;
    Snapshot _snap = new();

    // header
    Border _pill = null!;
    Microsoft.UI.Xaml.Shapes.Ellipse _pillDot = null!;
    TextBlock _pillText = null!;
    Button _gear = null!;

    public FlyoutView(UiContext context, IFlyoutHost host, NavState nav)
    {
        c = context;
        this.host = host;
        this.nav = nav;
        Width = Width420;
        FlowDirection = c.Rtl ? FlowDirection.RightToLeft : FlowDirection.LeftToRight;
        Language = c.L.Code;
        RequestedTheme = c.T.IsDark ? ElementTheme.Dark : ElementTheme.Light;
        Background = c.T.Tint("fly-bg", 55); // a brand tint over the system backdrop
        Padding = new Thickness(Pad);
        RowSpacing = Gap;
        RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });
        RowDefinitions.Add(new RowDefinition { Height = new GridLength(1, GridUnitType.Star) });
        RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });

        Children.Add(BuildHeader().At(0, 0));
        _scroll = new ScrollViewer
        {
            Content = _body,
            VerticalScrollBarVisibility = ScrollBarVisibility.Auto,
            HorizontalScrollBarVisibility = ScrollBarVisibility.Disabled,
            Margin = new Thickness(0)
        };
        Children.Add(_scroll.At(0, 1));
        Children.Add(BuildFooter().At(0, 2));

        _toastText = Ui.Text(c, "", 12, Ui.SemiBold, "ink");
        _toast = new Border
        {
            Background = c.T.Brush("tip-bg"), BorderBrush = c.T.Brush("tip-b"), BorderThickness = new Thickness(1),
            CornerRadius = new CornerRadius(999), Padding = new Thickness(14, 8, 14, 8),
            HorizontalAlignment = HorizontalAlignment.Center, VerticalAlignment = VerticalAlignment.Bottom,
            Margin = new Thickness(0, 0, 0, 44), Visibility = Visibility.Collapsed, Child = _toastText
        };
        AutomationProperties.SetLiveSetting(_toastText, AutomationLiveSetting.Polite);
        Grid.SetRowSpan(_overlay, 3);
        _overlay.Visibility = Visibility.Collapsed;
        Children.Add(_overlay);
        var toastHost = new Grid { IsHitTestVisible = false };
        Grid.SetRowSpan(toastHost, 3);
        toastHost.Children.Add(_toast);
        Children.Add(toastHost);

        KeyDown += OnKey;
        ShowMode();
    }

    void OnKey(object sender, KeyRoutedEventArgs e)
    {
        if (e.Key != VirtualKey.Escape) return;
        if (_overlay.Visibility == Visibility.Visible) { CloseConfirm(); e.Handled = true; }
        else if (nav.Mode != ViewMode.Overview) { Navigate(ViewMode.Overview); e.Handled = true; }
    }

    // ------------------------------------------------------------------
    // Header: logo, status pill, theme, language, settings
    // ------------------------------------------------------------------

    UIElement BuildHeader()
    {
        var g = Ui.Columns("auto auto auto * auto auto auto", 8);
        g.Children.Add(Logo().At(0));
        g.Children.Add(Ui.Text(c, "Pulse", 16, Ui.Bold, "ink", hero: true).At(1));

        _pillDot = Ui.Dot(c.T.Brush("cpu"));
        _pillText = Ui.Text(c, "", 11, Ui.SemiBold, "cpu-ink");
        _pill = Ui.Pill(c, Ui.Row(5, _pillDot, _pillText), c.T.Tint("cpu", 12));
        _pill.MaxWidth = 170;
        AutomationProperties.SetLiveSetting(_pillText, AutomationLiveSetting.Polite);
        g.Children.Add(_pill.At(2));

        var theme = Ui.Button(c, Ui.Glyph(c.T.IsDark ? "" : "", 14, c.T.Brush("ink2")), "card", "card-hover");
        theme.Width = 32; theme.Height = 30; theme.Padding = new Thickness(0);
        string themeName = c.T.IsDark ? c.L.T("aLight") : c.L.T("aDark");
        AutomationProperties.SetName(theme, themeName);
        ToolTipService.SetToolTip(theme, themeName);
        theme.Click += (_, _) => host.ToggleTheme();
        g.Children.Add(theme.At(4));

        g.Children.Add(LanguageSwitcher().At(5));

        _gear = Ui.Button(c, Ui.Glyph("", 14, c.T.Brush("ink2")), "card", "card-hover");
        _gear.Width = 32; _gear.Height = 30; _gear.Padding = new Thickness(0);
        AutomationProperties.SetName(_gear, c.L.T("aSettings"));
        ToolTipService.SetToolTip(_gear, c.L.T("aSettings"));
        _gear.Click += (_, _) => Navigate(nav.Mode == ViewMode.Settings ? ViewMode.Overview : ViewMode.Settings);
        g.Children.Add(_gear.At(6));
        return g;
    }

    UIElement Logo()
    {
        // Five vital bars: CPU, energy, memory, thermal, GPU
        (string Key, double Frac)[] bars = [("cpu", 0.52), ("nrg", 0.79), ("mem", 0.64), ("thm", 1.0), ("gpu", 0.73)];
        var canvas = new Canvas { Width = 18, Height = 14 };
        for (int i = 0; i < bars.Length; i++)
        {
            double h = 14 * bars[i].Frac;
            var bar = new Microsoft.UI.Xaml.Shapes.Rectangle { Width = 2.6, Height = h, RadiusX = 1.3, RadiusY = 1.3, Fill = c.T.Brush(bars[i].Key) };
            Canvas.SetLeft(bar, i * 3.85);
            Canvas.SetTop(bar, 14 - h);
            canvas.Children.Add(bar);
        }
        return new Border
        {
            Width = 28, Height = 28, CornerRadius = new CornerRadius(Theme.RLogo),
            Background = c.T.Brush("badge-bg"), BorderBrush = c.T.Brush("card-b"), BorderThickness = new Thickness(1),
            Child = new Viewbox { Width = 18, Height = 14, Child = canvas, FlowDirection = FlowDirection.LeftToRight }
        };
    }

    /// <summary>Two locales: an "EN | ع" segmented pill. Three or more: a menu (arrow keys, Home, End, Escape work).</summary>
    UIElement LanguageSwitcher()
    {
        var list = host.Locales.List();
        string label = c.L.T("aLang") + ": " + c.L.Name;
        if (list.Count <= 2)
        {
            var row = new StackPanel { Orientation = Orientation.Horizontal, Spacing = 2 };
            foreach (var info in list)
            {
                bool on = info.Code == c.L.Code;
                var text = Ui.Text(c, info.Label, 12, on ? Ui.Bold : Ui.SemiBold, on ? "ink" : "ink2");
                text.FontFamily = new FontFamily(FontStack.ToXaml(host.Locales.Get(info.Code).Fonts("win").Ui, Theme.UiFont));
                var seg = Ui.Button(c, text, on ? "seg-pill" : "track", on ? "seg-pill" : "hover", on ? "card-b" : "track", "ink", 999);
                seg.MinWidth = 32; seg.Height = 24; seg.Padding = new Thickness(8, 0, 8, 0);
                seg.HorizontalContentAlignment = HorizontalAlignment.Center;
                AutomationProperties.SetName(seg, info.Name);
                string code = info.Code;
                seg.Click += (_, _) => { if (code != c.L.Code) host.SetLanguage(code); };
                row.Children.Add(seg);
            }
            var pill = Ui.Pill(c, row, c.T.Brush("track"), new Thickness(2));
            AutomationProperties.SetName(pill, label);
            ToolTipService.SetToolTip(pill, label);
            return pill;
        }

        var menu = new MenuFlyout { Placement = Microsoft.UI.Xaml.Controls.Primitives.FlyoutPlacementMode.BottomEdgeAlignedRight };
        foreach (var info in list)
        {
            var item = new RadioMenuFlyoutItem { Text = info.Label + "  " + info.Name, GroupName = "lang", IsChecked = info.Code == c.L.Code };
            item.FontFamily = new FontFamily(FontStack.ToXaml(host.Locales.Get(info.Code).Fonts("win").Ui, Theme.UiFont));
            item.FlowDirection = info.Dir == "rtl" ? FlowDirection.RightToLeft : FlowDirection.LeftToRight;
            string code = info.Code;
            item.Click += (_, _) => host.SetLanguage(code);
            menu.Items.Add(item);
        }
        var drop = new DropDownButton { Content = c.L.Label, Flyout = menu, Height = 30, Padding = new Thickness(8, 0, 6, 0), FontFamily = c.UiFont, FontSize = 12 };
        Ui.Restyle(drop, c.T.Brush("card"), c.T.Brush("card-hover"), c.T.Brush("card-b"), c.T.Brush("ink"));
        AutomationProperties.SetName(drop, label);
        ToolTipService.SetToolTip(drop, label);
        return drop;
    }

    void UpdateHeader(Snapshot s, AppSample? hog)
    {
        var (text, isHog) = c.X.StatusPill(s, hog is not null && nav.HogDismissedFor != hog.Id ? hog : null);
        _pillText.Text = text;
        string acc = isHog ? "warn" : "cpu";
        _pill.Background = c.T.Tint(acc, isHog ? 16 : 12);
        _pillDot.Fill = c.T.Brush(acc);
        _pillText.Foreground = c.T.Brush(acc + "-ink");
        _gear.BorderBrush = c.T.Brush(nav.Mode == ViewMode.Settings ? "mem" : "card-b");
    }

    // ------------------------------------------------------------------
    // Footer: Open Task Manager, privacy note, Quit
    // ------------------------------------------------------------------

    UIElement BuildFooter()
    {
        var g = Ui.Columns("auto * auto", 8);
        string monitorLabel = c.L.T("openMonitor", ("monitor", c.X.Monitor));
        var openText = Ui.Text(c, monitorLabel, 12, Ui.SemiBold);
        var open = Ui.Button(c, Ui.Row(6, openText, Ui.Glyph("", 11, c.T.Brush("ink2"), mirror: true)), "card", "card-hover");
        open.Height = 30;
        open.MaxWidth = 220;
        AutomationProperties.SetName(open, monitorLabel);
        open.Click += (_, _) =>
        {
            ShowToast(c.L.T("toastMonitor", ("monitor", c.X.Monitor)));
            host.OpenTaskManager();
        };
        g.Children.Add(open.At(0));

        var privacy = Ui.Row(5, Ui.Glyph("", 11, c.T.Brush("ink3")), Ui.Text(c, c.L.T("privacy"), 11, Ui.Regular, "ink3"));
        privacy.HorizontalAlignment = HorizontalAlignment.Center;
        ToolTipService.SetToolTip(privacy, c.L.T("privacy"));
        g.Children.Add(privacy.At(1));

        var quit = Ui.Button(c, Ui.Text(c, c.L.T("quit"), 12, Ui.SemiBold, "ink2"), "track", "hover", "track");
        quit.Background = new SolidColorBrush(Microsoft.UI.Colors.Transparent);
        quit.BorderBrush = quit.Background;
        quit.Height = 30;
        quit.Click += (_, _) => host.Quit();
        g.Children.Add(quit.At(2));
        return g;
    }

    // ------------------------------------------------------------------
    // Navigation and updates
    // ------------------------------------------------------------------

    public void Navigate(ViewMode mode, DetailKind kind = DetailKind.Cpu, string? appId = null)
    {
        nav.Mode = mode;
        if (mode == ViewMode.Detail) { nav.Kind = kind; nav.AppId = appId; }
        ShowMode();
        host.ContentHeightChanged();
    }

    void ShowMode()
    {
        _body.Children.Clear();
        _detail = null;
        _overview = null;
        UIElement view = nav.Mode switch
        {
            ViewMode.Detail => BuildDetail(),
            ViewMode.Settings => BuildSettings(),
            _ => BuildOverview()
        };
        _body.Children.Add(view);
        _scroll.ChangeView(null, 0, null, true);
        Update(_snap);
        // Move keyboard focus into the new view so screen readers follow.
        if (view is FrameworkElement fe) fe.Loaded += (_, _) => FocusManager.TryMoveFocus(FocusNavigationDirection.Next, new FindNextElementOptions { SearchRoot = fe });
    }

    public void Update(Snapshot s)
    {
        _snap = s;
        var hog = host.Sampler.Hog;
        UpdateHeader(s, hog);
        if (nav.Mode == ViewMode.Overview) UpdateOverview(s, hog);
        else if (nav.Mode == ViewMode.Detail) UpdateDetail(s);
    }

    // ------------------------------------------------------------------
    // Confirm dialog and toast
    // ------------------------------------------------------------------

    void RequestEnd(AppSample app)
    {
        _overlay.Children.Clear();
        var scrim = new Border { Background = c.T.Tint("ink", 18), CornerRadius = new CornerRadius(Theme.RFly) };
        scrim.Tapped += (_, _) => CloseConfirm();
        _overlay.Children.Add(scrim);

        string name = c.L.App(app.Name);
        var title = Ui.Text(c, c.L.T("confirmTitle", ("app", name)), 15, Ui.Bold);
        var sub = Ui.Text(c, c.L.T("confirmSub"), 12, Ui.Regular, "ink2");
        sub.TextWrapping = TextWrapping.Wrap;
        sub.TextTrimming = TextTrimming.None;
        var cancel = Ui.Button(c, Ui.Text(c, c.L.T("cancel"), 12, Ui.SemiBold), "card", "card-hover");
        cancel.Height = 32;
        cancel.Click += (_, _) => CloseConfirm();
        var ok = Ui.Button(c, Ui.Text(c, c.L.T("confirmBtn"), 12, Ui.SemiBold, "danger-on"), "danger", "crit", "danger", "danger-on");
        ok.Height = 32;
        ok.Click += (_, _) => EndApp(app);
        var buttons = Ui.Row(8, cancel, ok);
        buttons.HorizontalAlignment = HorizontalAlignment.Right;
        var dialog = new Border
        {
            Background = c.T.Brush("fly-solid"), BorderBrush = c.T.Brush("card-b"), BorderThickness = new Thickness(1),
            CornerRadius = new CornerRadius(Theme.RCard), Padding = new Thickness(16), Margin = new Thickness(24),
            VerticalAlignment = VerticalAlignment.Center,
            Child = Ui.Column(10, title, sub, buttons)
        };
        AutomationProperties.SetName(dialog, title.Text);
        _overlay.Children.Add(dialog);
        _overlay.Visibility = Visibility.Visible;
        cancel.Loaded += (_, _) => cancel.Focus(FocusState.Programmatic);
    }

    void CloseConfirm()
    {
        _overlay.Visibility = Visibility.Collapsed;
        _overlay.Children.Clear();
    }

    void EndApp(AppSample app)
    {
        CloseConfirm();
        if (host.EndApp(app))
        {
            ShowToast(c.L.T("toastEnded", ("app", c.L.App(app.Name))));
            if (nav.Mode == ViewMode.Detail && nav.AppId == app.Id) Navigate(ViewMode.Overview);
        }
        else
        {
            ShowToast(c.L.T("endFailed", ("app", c.L.App(app.Name))));
        }
    }

    public void ShowToast(string text)
    {
        _toastText.Text = text;
        _toast.Visibility = Visibility.Visible;
        _toastTimer?.Stop();
        _toastTimer = new DispatcherTimer { Interval = TimeSpan.FromSeconds(2.8) };
        _toastTimer.Tick += (_, _) => { _toast.Visibility = Visibility.Collapsed; _toastTimer?.Stop(); };
        _toastTimer.Start();
    }

    /// <summary>Header plus content plus footer at 420 px wide, for sizing the window.</summary>
    public double DesiredHeight()
    {
        _scroll.VerticalScrollBarVisibility = ScrollBarVisibility.Disabled;
        Measure(new Windows.Foundation.Size(Width420, double.PositiveInfinity));
        double h = DesiredSize.Height;
        _scroll.VerticalScrollBarVisibility = ScrollBarVisibility.Auto;
        return h;
    }
}

using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Automation;
using Microsoft.UI.Xaml.Automation.Peers;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Media;
using Pulse.Core.Formatting;
using Pulse.Core.Models;
using Pulse.Core.Presentation;
using Windows.UI;

namespace Pulse.App.UI;

public sealed partial class FlyoutView
{
    OverviewParts? _overview;

    /// <summary>Elements the overview updates on every sample.</summary>
    sealed class OverviewParts
    {
        public Border Hog = null!;
        public TextBlock HogTitle = null!, HogSub = null!;
        public AppSample? HogApp;

        public Button CpuCard = null!;
        public TextBlock CpuHero = null!, CpuSub = null!;
        public Border CpuHigh = null!;
        public TextBlock[] CoreLabel = new TextBlock[3], CoreVal = new TextBlock[3];
        public Meter[] CoreMeter = new Meter[3];
        public Sparkline CpuSpark = null!;

        public TextBlock MemHero = null!, MemOf = null!, MemBadge = null!;
        public Border MemBadgeBox = null!;
        public Microsoft.UI.Xaml.Shapes.Ellipse MemBadgeDot = null!;
        public ColumnDefinition[] MemSeg = new ColumnDefinition[4];
        public TextBlock[] MemLegend = new TextBlock[4];
        public Sparkline MemSpark = null!;

        public TextBlock NrgHero = null!, NrgSub = null!, NrgFlow = null!, NrgFlowLabel = null!, NrgHealth = null!;
        public Sparkline NrgSpark = null!;

        public TextBlock ThmHero = null!, ThmLine = null!, ThmNote = null!;
        public Border[] ThmSeg = new Border[4];
        public TextBlock[] ThmLabel = new TextBlock[4];
        public Sparkline ThmSpark = null!;

        public TextBlock GpuName = null!, GpuVal = null!, GpuTemp = null!;
        public Border GpuTempBox = null!;
        public Meter GpuMeter = null!;

        public TextBlock DiskName = null!, DiskUsed = null!, DiskFree = null!;
        public Meter DiskMeter = null!;
        public TextBlock NetName = null!, NetDown = null!, NetUp = null!;
        public FontIcon NetIcon = null!;

        public Button[] SortButtons = new Button[3];
        public AppRow[] Rows = new AppRow[5];
    }

    /// <summary>One row of Top Active Apps: avatar, name, process count, meter, value or End.</summary>
    sealed class AppRow
    {
        public Button Root = null!;
        public Border Avatar = null!;
        public TextBlock Mono = null!, Name = null!, Procs = null!, Metric = null!;
        public Border ProcsBox = null!;
        public Meter Meter = null!;
        public Button End = null!;
        public StackPanel Tip = null!;
        public AppSample? App;
        public bool Hover;
    }

    UIElement BuildOverview()
    {
        var o = _overview = new OverviewParts();
        var col = new StackPanel { Spacing = Gap };
        col.Children.Add(BuildHogBanner(o));

        var grid = new Grid { ColumnSpacing = Gap, RowSpacing = Gap };
        grid.ColumnDefinitions.Add(new ColumnDefinition());
        grid.ColumnDefinitions.Add(new ColumnDefinition());
        grid.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });
        grid.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });
        grid.Children.Add(BuildCpuCard(o).At(0, 0));
        grid.Children.Add(BuildMemCard(o).At(1, 0));
        grid.Children.Add(BuildNrgCard(o).At(0, 1));
        grid.Children.Add(BuildThmCard(o).At(1, 1));
        col.Children.Add(grid);

        col.Children.Add(BuildGpuStrip(o));

        var io = new Grid { ColumnSpacing = Gap };
        io.ColumnDefinitions.Add(new ColumnDefinition());
        io.ColumnDefinitions.Add(new ColumnDefinition());
        io.Children.Add(BuildDiskCard(o).At(0));
        io.Children.Add(BuildNetCard(o).At(1));
        col.Children.Add(io);

        col.Children.Add(BuildTopApps(o));
        return col;
    }

    // Card header: icon, title, optional badge, chevron.
    Grid CardHeader(string glyph, string accent, string title, UIElement? badge = null)
    {
        var g = Ui.Columns("auto * auto auto", 6);
        g.Children.Add(Ui.IconBadge(c, glyph, accent).At(0));
        g.Children.Add(Ui.Text(c, title, 12.5, Ui.SemiBold).At(1));
        if (badge is not null) g.Children.Add(((FrameworkElement)badge).At(2));
        g.Children.Add(Ui.Glyph("", 10, c.T.Brush("ink3"), mirror: true).At(3));
        return g;
    }

    Grid SparkWithLabel(Sparkline spark)
    {
        var g = new Grid { Margin = new Thickness(0, 4, 0, 0) };
        g.Children.Add(spark);
        var label = Ui.Text(c, c.L.T("min1"), 9.5, Ui.Medium, "ink3");
        label.VerticalAlignment = VerticalAlignment.Top;
        label.Margin = new Thickness(0, -2, 0, 0);
        g.Children.Add(label);
        return g;
    }

    UIElement BuildHogBanner(OverviewParts o)
    {
        var g = Ui.Columns("auto * auto auto", 8);
        g.Children.Add(Ui.IconBadge(c, "", "warn", 28).At(0));
        o.HogTitle = Ui.Text(c, "", 12.5, Ui.SemiBold);
        o.HogSub = Ui.Text(c, c.X.HogSub(), 11, Ui.Regular, "ink2");
        g.Children.Add(Ui.Column(1, o.HogTitle, o.HogSub).At(1));
        var end = Ui.Button(c, Ui.Text(c, c.L.T("endApp"), 12, Ui.SemiBold, "warn-ink"), "card", "card-hover", "warn", "warn-ink");
        end.Height = 28;
        end.Click += (_, _) => { if (o.HogApp is not null) RequestEnd(o.HogApp); };
        g.Children.Add(end.At(2));
        var dismiss = Ui.Button(c, Ui.Glyph("", 10, c.T.Brush("ink2")), "track", "hover", "track");
        dismiss.Background = dismiss.BorderBrush = new SolidColorBrush(Microsoft.UI.Colors.Transparent);
        dismiss.Width = 26; dismiss.Height = 26; dismiss.Padding = new Thickness(0);
        AutomationProperties.SetName(dismiss, c.L.T("aDismiss"));
        dismiss.Click += (_, _) => { nav.HogDismissedFor = o.HogApp?.Id; o.Hog.Visibility = Visibility.Collapsed; host.ContentHeightChanged(); };
        g.Children.Add(dismiss.At(3));
        o.Hog = new Border
        {
            Background = c.T.TintGradient("warn", c.T.IsDark ? 18 : 13, "crit", c.T.IsDark ? 12 : 8),
            BorderBrush = c.T.Tint("warn", 34), BorderThickness = new Thickness(1),
            CornerRadius = new CornerRadius(Theme.RCard), Padding = new Thickness(10, 8, 6, 8),
            Child = g, Visibility = Visibility.Collapsed
        };
        AutomationProperties.SetLiveSetting(o.HogTitle, AutomationLiveSetting.Assertive);
        return o.Hog;
    }

    UIElement BuildCpuCard(OverviewParts o)
    {
        o.CpuHigh = Ui.Pill(c, Ui.Text(c, c.L.T("high"), 10.5, Ui.SemiBold, "warn-ink"), c.T.Tint("warn", 16), new Thickness(7, 1, 7, 1));
        o.CpuHero = Ui.Value(c, "", 28, Ui.SemiBold, "ink", hero: true);
        o.CpuSub = Ui.Text(c, "", 11, Ui.Regular, "ink2");
        var rows = new StackPanel { Spacing = 3, Margin = new Thickness(0, 6, 0, 0) };
        for (int i = 0; i < 3; i++)
        {
            var g = Ui.Columns("62 * 34", 6);
            o.CoreLabel[i] = Ui.Text(c, i == 2 ? c.L.T("gpu") : "", 11, Ui.Regular, "ink2").At(0);
            o.CoreMeter[i] = new Meter(c.T.Brush("track"), c.T.Brush(i == 2 ? "gpu" : "cpu"), 4).At(1);
            o.CoreVal[i] = Ui.Value(c, "", 11, Ui.SemiBold).At(2);
            o.CoreVal[i].HorizontalAlignment = HorizontalAlignment.Right;
            g.Children.Add(o.CoreLabel[i]); g.Children.Add(o.CoreMeter[i]); g.Children.Add(o.CoreVal[i]);
            rows.Children.Add(g);
        }
        o.CpuSpark = new Sparkline(c.T.Color("cpu"));
        var body = Ui.Column(2, CardHeader("", "cpu", c.L.T("cpu"), o.CpuHigh), o.CpuHero, o.CpuSub, rows, SparkWithLabel(o.CpuSpark));
        o.CpuCard = Ui.Card(c, body, c.L.T("cpu"), () => Navigate(ViewMode.Detail, DetailKind.Cpu));
        return o.CpuCard;
    }

    UIElement BuildMemCard(OverviewParts o)
    {
        o.MemHero = Ui.Value(c, "", 28, Ui.SemiBold, "ink", hero: true);
        o.MemBadgeDot = Ui.Dot(c.T.Brush("cpu"));
        o.MemBadge = Ui.Text(c, "", 10.5, Ui.SemiBold, "cpu-ink");
        o.MemBadgeBox = Ui.Pill(c, Ui.Row(4, o.MemBadgeDot, o.MemBadge), c.T.Tint("cpu", 12), new Thickness(7, 1, 7, 1));
        o.MemBadgeBox.MaxWidth = 110;
        var heroRow = Ui.Columns("auto *", 8);
        heroRow.Children.Add(o.MemHero.At(0));
        heroRow.Children.Add(o.MemBadgeBox.At(1));
        o.MemBadgeBox.HorizontalAlignment = HorizontalAlignment.Left;
        o.MemOf = Ui.Text(c, "", 11, Ui.Regular, "ink2");

        var bar = new Grid { Height = 6, CornerRadius = new CornerRadius(3), Background = c.T.Brush("track"), Margin = new Thickness(0, 6, 0, 4), ColumnSpacing = 2 };
        string[] segInk = ["mem", "mem", "mem", "track"];
        double[] segAlpha = [100, 55, 30, 0];
        var legend = new StackPanel { Spacing = 1 };
        for (int i = 0; i < 4; i++)
        {
            o.MemSeg[i] = new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) };
            bar.ColumnDefinitions.Add(o.MemSeg[i]);
            if (i < 3) bar.Children.Add(new Border { Background = c.T.Tint(segInk[i], segAlpha[i]) }.At(i));
            var sw = new Border { Width = 7, Height = 7, CornerRadius = new CornerRadius(2), Background = i < 3 ? c.T.Tint("mem", segAlpha[i]) : c.T.Brush("track-strong") };
            o.MemLegend[i] = Ui.Text(c, "", 10.5, Ui.Regular, "ink2");
            legend.Children.Add(Ui.Row(5, sw, o.MemLegend[i]));
        }
        o.MemSpark = new Sparkline(c.T.Color("mem"));
        var body = Ui.Column(2, CardHeader("", "mem", c.L.T("mem")), heroRow, o.MemOf, bar, legend, SparkWithLabel(o.MemSpark));
        return Ui.Card(c, body, c.L.T("mem"), () => Navigate(ViewMode.Detail, DetailKind.Mem));
    }

    UIElement BuildNrgCard(OverviewParts o)
    {
        o.NrgHero = Ui.Value(c, "", 28, Ui.SemiBold, "ink", hero: true);
        o.NrgSub = Ui.Text(c, "", 11, Ui.Regular, "ink2");
        o.NrgFlow = Ui.Value(c, "", 12, Ui.Bold, "nrg-ink");
        o.NrgFlowLabel = Ui.Text(c, "", 11, Ui.Regular, "ink2");
        var flow = Ui.Row(6, Ui.Pill(c, Ui.Row(5, Ui.Dot(c.T.Brush("nrg")), o.NrgFlow), c.T.Tint("nrg", 13), new Thickness(8, 3, 9, 3)), o.NrgFlowLabel);
        flow.Margin = new Thickness(0, 6, 0, 4);
        o.NrgHealth = Ui.Text(c, "", 11, Ui.Regular, "ink2");
        o.NrgSpark = new Sparkline(c.T.Color("nrg"));
        var body = Ui.Column(2, CardHeader("", "nrg", c.L.T("nrg")), o.NrgHero, o.NrgSub, flow, o.NrgHealth, SparkWithLabel(o.NrgSpark));
        return Ui.Card(c, body, c.L.T("nrg"), () => Navigate(ViewMode.Detail, DetailKind.Nrg));
    }

    UIElement BuildThmCard(OverviewParts o)
    {
        o.ThmHero = Ui.Text(c, "", 28, Ui.SemiBold, "ink", hero: true);
        o.ThmLine = Ui.Text(c, "", 11, Ui.Regular, "ink2");
        var segs = new Grid { ColumnSpacing = 4, Margin = new Thickness(0, 6, 0, 2) };
        var labels = new Grid { ColumnSpacing = 4 };
        var names = c.X.ThermalNames();
        for (int i = 0; i < 4; i++)
        {
            segs.ColumnDefinitions.Add(new ColumnDefinition());
            labels.ColumnDefinitions.Add(new ColumnDefinition());
            o.ThmSeg[i] = new Border { Height = 5, CornerRadius = new CornerRadius(2.5), Background = c.T.Brush("track") }.At(i);
            o.ThmLabel[i] = Ui.Text(c, names[i], 10.5, Ui.Medium, "ink3").At(i);
            segs.Children.Add(o.ThmSeg[i]);
            labels.Children.Add(o.ThmLabel[i]);
        }
        o.ThmNote = Ui.Text(c, "", 11, Ui.Regular, "ink3");
        o.ThmNote.Margin = new Thickness(0, 4, 0, 0);
        o.ThmSpark = new Sparkline(c.T.Color("nrg"));
        var body = Ui.Column(2, CardHeader("", "thm", c.L.T("thm")), o.ThmHero, o.ThmLine, segs, labels, o.ThmNote, SparkWithLabel(o.ThmSpark));
        return Ui.Card(c, body, c.L.T("thm"), () => Navigate(ViewMode.Detail, DetailKind.Thm));
    }

    UIElement BuildGpuStrip(OverviewParts o)
    {
        var g = Ui.Columns("auto auto * 84 auto auto", 8);
        g.Children.Add(Ui.IconBadge(c, "", "gpu", 22).At(0));
        g.Children.Add(Ui.Text(c, c.L.T("gpu"), 12, Ui.SemiBold).At(1));
        o.GpuName = Ui.Text(c, "", 11, Ui.Regular, "ink2").At(2);
        o.GpuMeter = new Meter(c.T.Brush("track"), c.T.Brush("gpu"), 4).At(3);
        o.GpuVal = Ui.Value(c, "", 13, Ui.Bold).At(4);
        o.GpuTemp = Ui.Value(c, "", 11, Ui.SemiBold, "gpu-ink");
        o.GpuTempBox = Ui.Pill(c, o.GpuTemp, c.T.Tint("mem", 13), new Thickness(7, 1, 7, 1)).At(5);
        g.Children.Add(o.GpuName); g.Children.Add(o.GpuMeter); g.Children.Add(o.GpuVal); g.Children.Add(o.GpuTempBox);
        return Ui.Card(c, g, c.L.T("gpu"), () => Navigate(ViewMode.Detail, DetailKind.Gpu), 9);
    }

    UIElement BuildDiskCard(OverviewParts o)
    {
        var head = Ui.Columns("auto *", 6);
        head.Children.Add(Ui.IconBadge(c, "", "cpu", 22).At(0));
        o.DiskName = Ui.Text(c, "", 12, Ui.SemiBold).At(1);
        head.Children.Add(o.DiskName);
        var line = Ui.Columns("auto *", 6);
        o.DiskUsed = Ui.Text(c, "", 12.5, Ui.Bold).At(0);
        o.DiskFree = Ui.Text(c, "", 11, Ui.Regular, "ink2").At(1);
        o.DiskFree.HorizontalAlignment = HorizontalAlignment.Right;
        line.Children.Add(o.DiskUsed); line.Children.Add(o.DiskFree);
        o.DiskMeter = new Meter(c.T.Brush("track"), c.T.Gradient("cpu", "nrg"), 5);
        var body = Ui.Column(6, head, line, o.DiskMeter);
        return Ui.Card(c, body, c.L.T("storage"), () => Navigate(ViewMode.Detail, DetailKind.Ssd), 10);
    }

    UIElement BuildNetCard(OverviewParts o)
    {
        var head = Ui.Columns("auto *", 6);
        o.NetIcon = Ui.Glyph("", 12, c.T.Brush("mem-ink"));
        head.Children.Add(new Border { Width = 22, Height = 22, CornerRadius = new CornerRadius(Theme.RBadge), Background = c.T.Tint("mem", 13), Child = o.NetIcon }.At(0));
        o.NetName = Ui.Text(c, "", 12, Ui.SemiBold).At(1);
        head.Children.Add(o.NetName);
        o.NetDown = Ui.Value(c, "", 12.5, Ui.Bold, "mem-ink");
        o.NetUp = Ui.Value(c, "", 12.5, Ui.Bold, "cpu-ink");
        var rates = Ui.Row(12, o.NetDown, o.NetUp);
        rates.FlowDirection = FlowDirection.LeftToRight;
        rates.HorizontalAlignment = c.Rtl ? HorizontalAlignment.Right : HorizontalAlignment.Left;
        var body = Ui.Column(8, head, rates);
        return Ui.Card(c, body, c.L.T("network"), () => Navigate(ViewMode.Detail, DetailKind.Net), 10);
    }

    UIElement BuildTopApps(OverviewParts o)
    {
        var head = Ui.Columns("* auto", 8);
        head.Children.Add(Ui.Text(c, c.L.T("top"), 12.5, Ui.Bold).At(0));
        var seg = new StackPanel { Orientation = Orientation.Horizontal, Spacing = 2 };
        (Texts.AppSort Sort, string Key)[] sorts = [(Texts.AppSort.Cpu, "cpu"), (Texts.AppSort.Mem, "mem"), (Texts.AppSort.Gpu, "gpu")];
        for (int i = 0; i < 3; i++)
        {
            var (sort, key) = sorts[i];
            var b = Ui.Button(c, Ui.Text(c, c.L.T(key), 11, Ui.SemiBold, "ink2"), "track", "hover", "track", "ink2", 999);
            b.Height = 24; b.Padding = new Thickness(10, 0, 10, 0);
            b.Click += (_, _) => { nav.Sort = sort; StyleSort(o); UpdateApps(_snap); };
            o.SortButtons[i] = b;
            seg.Children.Add(b);
        }
        head.Children.Add(Ui.Pill(c, seg, c.T.Brush("track"), new Thickness(2)).At(1));
        StyleSort(o);

        var rows = new StackPanel { Spacing = 0, Margin = new Thickness(0, 6, 0, 0) };
        for (int i = 0; i < 5; i++) rows.Children.Add(BuildAppRow(o, i));
        return Ui.Panel(c, Ui.Column(2, head, rows), 10);
    }

    void StyleSort(OverviewParts o)
    {
        for (int i = 0; i < 3; i++)
        {
            bool on = (int)nav.Sort == i;
            var b = o.SortButtons[i];
            Ui.Restyle(b, c.T.Brush(on ? "seg-pill" : "track"), c.T.Brush(on ? "seg-pill" : "hover"), c.T.Brush(on ? "card-b" : "track"), c.T.Brush(on ? "ink" : "ink2"));
            if (b.Content is TextBlock t) t.Foreground = c.T.Brush(on ? "ink" : "ink2");
            AutomationProperties.SetItemStatus(b, on ? "selected" : "");
        }
    }

    UIElement BuildAppRow(OverviewParts o, int index)
    {
        var r = o.Rows[index] = new AppRow();
        var g = Ui.Columns("24 * 64 60", 8);
        r.Mono = Ui.Text(c, "", 11, Ui.Bold);
        r.Mono.HorizontalAlignment = HorizontalAlignment.Center;
        r.Avatar = new Border { Width = 22, Height = 22, CornerRadius = new CornerRadius(Theme.RMono), Child = r.Mono }.At(0);
        r.Name = Ui.Text(c, "", 12.5, Ui.Medium);
        r.Procs = Ui.Value(c, "", 10, Ui.SemiBold, "ink2");
        r.ProcsBox = new Border { Background = c.T.Brush("track"), CornerRadius = new CornerRadius(4), Padding = new Thickness(5, 0, 5, 0), Child = r.Procs, VerticalAlignment = VerticalAlignment.Center };
        var nameRow = Ui.Columns("auto auto", 6);
        nameRow.Children.Add(r.Name.At(0));
        nameRow.Children.Add(r.ProcsBox.At(1));
        r.Name.MaxWidth = 190;
        g.Children.Add(r.Avatar);
        g.Children.Add(nameRow.At(1));
        r.Meter = new Meter(c.T.Brush("track"), c.T.Brush("cpu"), 4).At(2);
        g.Children.Add(r.Meter);
        r.Metric = Ui.Value(c, "", 12, Ui.SemiBold).At(3);
        r.Metric.HorizontalAlignment = HorizontalAlignment.Right;
        g.Children.Add(r.Metric);
        r.End = Ui.Button(c, Ui.Text(c, c.L.T("end"), 11.5, Ui.SemiBold, "danger"), "card", "card-hover", "card-b", "danger").At(3);
        r.End.Height = 24; r.End.Padding = new Thickness(8, 0, 8, 0);
        r.End.HorizontalAlignment = HorizontalAlignment.Right;
        r.End.Visibility = Visibility.Collapsed;
        r.End.Click += (_, _) => { if (r.App is not null) RequestEnd(r.App); };
        g.Children.Add(r.End);

        r.Root = Ui.Button(c, g, "track", "hover", "track", "ink", Theme.RRow);
        r.Root.Background = r.Root.BorderBrush = new SolidColorBrush(Microsoft.UI.Colors.Transparent);
        Ui.Restyle(r.Root, r.Root.Background, c.T.Brush("hover"), r.Root.BorderBrush, c.T.Brush("ink"));
        r.Root.Height = 34;
        r.Root.Padding = new Thickness(4, 0, 4, 0);
        r.Root.HorizontalAlignment = HorizontalAlignment.Stretch;
        r.Root.Click += (_, _) => { if (r.App is not null) Navigate(ViewMode.Detail, DetailKind.App, r.App.Id); };

        void SetHover(bool on) { r.Hover = on; r.End.Visibility = on && r.App is not null ? Visibility.Visible : Visibility.Collapsed; r.Metric.Opacity = on ? 0 : 1; }
        r.Root.PointerEntered += (_, _) => SetHover(true);
        r.Root.PointerExited += (_, _) => SetHover(false);
        r.Root.GotFocus += (_, _) => SetHover(true);
        r.Root.LostFocus += (_, _) => { if (!r.End.FocusState.HasFlag(FocusState.Keyboard)) SetHover(false); };

        r.Tip = new StackPanel { Spacing = 3, MinWidth = 160 };
        ToolTipService.SetToolTip(r.Root, new ToolTip { Content = r.Tip, Placement = Microsoft.UI.Xaml.Controls.Primitives.PlacementMode.Top });
        return r.Root;
    }

    void UpdateOverview(Snapshot s, AppSample? hog)
    {
        var o = _overview;
        if (o is null) return;
        var X = c.X; var F = c.F; var hist = host.Sampler.History;

        // Hog banner
        o.HogApp = hog;
        bool showHog = hog is not null && nav.HogDismissedFor != hog.Id;
        if (showHog) o.HogTitle.Text = X.HogTitle(hog!);
        var hv = showHog ? Visibility.Visible : Visibility.Collapsed;
        if (o.Hog.Visibility != hv) { o.Hog.Visibility = hv; host.ContentHeightChanged(); }

        // CPU
        bool high = s.Cpu.TotalPct >= 50;
        o.CpuHero.Text = F.Pct(s.Cpu.TotalPct);
        o.CpuHero.Foreground = c.T.Brush(high ? "warn-ink" : "ink");
        o.CpuHigh.Visibility = high ? Visibility.Visible : Visibility.Collapsed;
        o.CpuCard.BorderBrush = high ? c.T.Tint("warn", 34) : c.T.Brush("card-b");
        o.CpuSub.Text = X.UserSys(s.Cpu);
        var rows = X.CoreRows(s.Cpu);
        for (int i = 0; i < 2; i++)
        {
            o.CoreLabel[i].Text = rows[i].Label;
            o.CoreMeter[i].Set(rows[i].Pct);
            o.CoreVal[i].Text = F.Pct(rows[i].Pct);
        }
        o.CoreMeter[1].Fill = c.T.Tint("cpu", 55);
        o.CoreMeter[2].Set(s.Gpu.UtilPct);
        o.CoreVal[2].Text = F.Pct(s.Gpu.UtilPct);
        o.CpuSpark.Set(hist.Spark("cpu"));

        // Memory
        var m = s.Memory;
        o.MemHero.Text = F.Pct(m.LoadPct);
        o.MemOf.Text = X.MemOf(m);
        o.MemBadge.Text = X.MemBadge(m);
        string mAcc = Texts.MemAccent(m);
        o.MemBadgeBox.Background = c.T.Tint(mAcc, 12);
        o.MemBadgeDot.Fill = c.T.Brush(mAcc);
        o.MemBadge.Foreground = c.T.Brush(mAcc + "-ink");
        var legend = X.MemLegend(m);
        for (int i = 0; i < 4; i++)
        {
            o.MemSeg[i].Width = new GridLength(Math.Max(0.001, legend[i].Fraction), GridUnitType.Star);
            o.MemLegend[i].Text = legend[i].Label + " " + legend[i].Value;
        }
        o.MemSpark.Set(hist.Spark("mem"));

        // Energy
        var p = s.Power;
        o.NrgHero.Text = X.EnergyHero(p);
        o.NrgSub.Text = X.EnergySub(p);
        var (flow, flowLabel) = X.EnergyFlow(p);
        o.NrgFlow.Text = flow;
        o.NrgFlowLabel.Text = flowLabel;
        o.NrgHealth.Text = X.HealthLine(p);
        o.NrgSpark.Set(hist.Spark("nrg"));

        // Thermal
        int lv = Texts.ThermalLevel(s.Thermal);
        string ts = Texts.ThermalAccent(lv);
        var names = X.ThermalNames();
        o.ThmHero.Text = s.Thermal.CpuC is null ? Fmt.Dash : names[lv];
        o.ThmLine.Text = X.TempLine(s.Thermal, s.Gpu);
        for (int i = 0; i < 4; i++)
        {
            o.ThmSeg[i].Background = i == lv ? c.T.Brush(ts) : c.T.Brush("track");
            o.ThmLabel[i].Foreground = c.T.Brush(i == lv ? ts + "-ink" : "ink3");
            o.ThmLabel[i].FontWeight = i == lv ? Ui.Bold : Ui.Medium;
        }
        o.ThmNote.Text = X.ThermalNote(s.Thermal);
        o.ThmNote.Foreground = c.T.Brush(lv < 3 ? "ink3" : "crit-ink");
        o.ThmSpark.Set(hist.Spark("thm"));

        // GPU strip
        o.GpuName.Text = string.IsNullOrEmpty(s.Gpu.Name) ? Fmt.Dash : s.Gpu.Name;
        o.GpuMeter.Set(s.Gpu.UtilPct);
        o.GpuVal.Text = F.Pct(s.Gpu.UtilPct);
        o.GpuTemp.Text = F.Temp(s.Gpu.TempC);
        string gl = s.Gpu.TempC is double gt ? (gt < 60 ? "mem" : gt < 80 ? "nrg" : "thm") : "mem";
        o.GpuTempBox.Background = c.T.Tint(gl, 13);
        o.GpuTemp.Foreground = c.T.Brush(gl + "-ink");
        o.GpuTempBox.Visibility = s.Gpu.TempC is null ? Visibility.Collapsed : Visibility.Visible;

        // Storage and network
        o.DiskName.Text = X.DiskName(s.Disk);
        o.DiskUsed.Text = X.DiskUsed(s.Disk);
        o.DiskFree.Text = X.DiskFree(s.Disk);
        o.DiskMeter.Set(s.Disk.UsedPct);
        o.NetName.Text = X.NetName(s.Net);
        o.NetIcon.Glyph = s.Net.Kind == LinkKind.Ethernet ? "" : "";
        o.NetDown.Text = "↓" + F.Rate(s.Net.DownBps);
        o.NetUp.Text = "↑" + F.Rate(s.Net.UpBps);

        UpdateApps(s);
    }

    void UpdateApps(Snapshot s)
    {
        var o = _overview;
        if (o is null) return;
        var hog = host.Sampler.Hog;
        var top = Texts.TopApps(s.Apps, nav.Sort);
        double max = top.Count > 0 ? Math.Max(1e-9, Texts.AppValue(top[0], nav.Sort)) : 1;
        for (int i = 0; i < o.Rows.Length; i++)
        {
            var r = o.Rows[i];
            if (i >= top.Count) { r.Root.Visibility = Visibility.Collapsed; r.App = null; continue; }
            var a = top[i];
            r.App = a;
            r.Root.Visibility = Visibility.Visible;
            string name = c.L.App(a.Name);
            r.Name.Text = name;
            r.Mono.Text = name.Length > 0 ? char.ToUpperInvariant(name[0]).ToString() : "?";
            var (bg, ink) = AvatarColors(a);
            r.Avatar.Background = new SolidColorBrush(bg);
            r.Mono.Foreground = new SolidColorBrush(ink);
            r.ProcsBox.Visibility = a.Processes > 1 ? Visibility.Visible : Visibility.Collapsed;
            r.Procs.Text = "×" + c.F.N(a.Processes);
            bool hogRow = hog is not null && hog.Id == a.Id && nav.Sort == Texts.AppSort.Cpu;
            r.Meter.Set(Texts.AppValue(a, nav.Sort) / max * 100, 3);
            r.Meter.Fill = hogRow ? c.T.Gradient("warn", "crit") : c.T.Brush(nav.Sort switch { Texts.AppSort.Mem => "mem", Texts.AppSort.Gpu => "gpu", _ => "cpu" });
            r.Metric.Text = c.X.AppMetric(a, nav.Sort);
            r.Metric.Foreground = c.T.Brush(hogRow ? "warn-ink" : "ink");
            string endLabel = c.L.T("endNamed", ("app", name));
            AutomationProperties.SetName(r.End, endLabel);
            AutomationProperties.SetName(r.Root, name + ", " + r.Metric.Text);
            r.End.Visibility = r.Hover ? Visibility.Visible : Visibility.Collapsed;

            r.Tip.Children.Clear();
            r.Tip.Children.Add(Ui.Text(c, name, 12, Ui.Bold));
            foreach (var t in c.X.AppTip(a, s.Memory))
            {
                var line = Ui.Columns("* auto", 12);
                line.Children.Add(Ui.Text(c, t.Label, 11, Ui.Regular, "ink2").At(0));
                line.Children.Add(Ui.Value(c, t.Value, 11, Ui.SemiBold).At(1));
                r.Tip.Children.Add(line);
            }
            r.Tip.Children.Add(Ui.Text(c, c.L.T("tipClick"), 10.5, Ui.Medium, "mem-ink"));
        }
    }

    /// <summary>Monogram colours from a hue per app, softer for background processes.</summary>
    (Color Bg, Color Ink) AvatarColors(AppSample a)
    {
        uint h = 2166136261;
        foreach (char ch in a.Id) { h ^= ch; h *= 16777619; }
        double hue = h % 360;
        return c.T.IsDark
            ? (Hsl(hue, 0.35, a.Background ? 0.22 : 0.30), Hsl(hue, 0.65, 0.80))
            : (Hsl(hue, 0.60, a.Background ? 0.94 : 0.88), Hsl(hue, 0.55, a.Background ? 0.38 : 0.32));
    }

    static Color Hsl(double h, double s, double l)
    {
        double C = (1 - Math.Abs(2 * l - 1)) * s, X = C * (1 - Math.Abs(h / 60 % 2 - 1)), m = l - C / 2;
        (double r, double g, double b) = h switch
        {
            < 60 => (C, X, 0d), < 120 => (X, C, 0d), < 180 => (0d, C, X), < 240 => (0d, X, C), < 300 => (X, 0d, C), _ => (C, 0d, X)
        };
        return Microsoft.UI.ColorHelper.FromArgb(255, (byte)((r + m) * 255), (byte)((g + m) * 255), (byte)((b + m) * 255));
    }
}

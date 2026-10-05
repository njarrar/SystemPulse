using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Automation;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Input;
using Microsoft.UI.Xaml.Media;
using Microsoft.UI.Xaml.Shapes;
using Pulse.App.Telemetry;
using Pulse.Core.Models;
using Pulse.Core.Presentation;

namespace Pulse.App.UI;

public sealed partial class FlyoutView
{
    const double ChartW = 372, ChartH = 140;

    DetailParts? _detail;

    sealed class DetailParts
    {
        public TextBlock Title = null!, Hero = null!, Sub = null!, Stat = null!;
        public Microsoft.UI.Xaml.Shapes.Path LineA = null!, AreaA = null!, LineB = null!, AreaB = null!;
        public Line Cursor = null!;
        public Ellipse DotA = null!, DotB = null!;
        public Border Pill = null!;
        public TextBlock PillText = null!;
        public TextBlock[] TileLabel = new TextBlock[6], TileValue = new TextBlock[6];
        public Button? End;
        public int? Hover;
        public DetailText? Last;
        public double[] A = [], B = [];
        public AppSample? App;
    }

    UIElement BuildDetail()
    {
        var d = _detail = new DetailParts();
        var col = new StackPanel { Spacing = Gap };

        // Back, title, End App for apps
        var head = Ui.Columns("auto * auto", 8);
        var back = Ui.Button(c, Ui.Glyph("", 12, c.T.Brush("ink"), mirror: true), "card", "card-hover");
        back.Width = 30; back.Height = 30; back.Padding = new Thickness(0);
        AutomationProperties.SetName(back, c.L.T("back"));
        ToolTipService.SetToolTip(back, c.L.T("back"));
        back.Click += (_, _) => Navigate(ViewMode.Overview);
        head.Children.Add(back.At(0));
        d.Title = Ui.Text(c, "", 14, Ui.Bold).At(1);
        head.Children.Add(d.Title);
        if (nav.Kind == DetailKind.App)
        {
            d.End = Ui.Button(c, Ui.Text(c, c.L.T("endApp"), 12, Ui.SemiBold, "danger-on"), "danger", "crit", "danger", "danger-on").At(2);
            d.End.Height = 30;
            d.End.Click += (_, _) => { if (d.App is not null) RequestEnd(d.App); };
            head.Children.Add(d.End);
        }
        col.Children.Add(head);

        d.Hero = Ui.Value(c, "", 32, Ui.SemiBold, "ink", hero: true);
        d.Hero.HorizontalAlignment = HorizontalAlignment.Left;
        d.Sub = Ui.Text(c, "", 12, Ui.Regular, "ink2");
        col.Children.Add(Ui.Column(0, d.Hero, d.Sub));

        col.Children.Add(Ui.Panel(c, BuildChart(d), 12));

        var tiles = new Grid { ColumnSpacing = Gap, RowSpacing = Gap };
        for (int i = 0; i < 3; i++) tiles.ColumnDefinitions.Add(new ColumnDefinition());
        tiles.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });
        tiles.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });
        for (int i = 0; i < 6; i++)
        {
            d.TileLabel[i] = Ui.Text(c, "", 11, Ui.Regular, "ink3");
            d.TileValue[i] = Ui.Value(c, "", 13, Ui.SemiBold);
            d.TileValue[i].HorizontalAlignment = HorizontalAlignment.Left;
            var tile = Ui.Panel(c, Ui.Column(2, d.TileLabel[i], d.TileValue[i]), 10).At(i % 3, i / 3);
            tiles.Children.Add(tile);
        }
        col.Children.Add(tiles);
        return col;
    }

    UIElement BuildChart(DetailParts d)
    {
        var head = Ui.Columns("* auto", 8);
        d.Stat = Ui.Text(c, "", 11.5, Ui.SemiBold, "ink2").At(0);
        head.Children.Add(d.Stat);
        head.Children.Add(Ui.Text(c, c.L.T("last10"), 11, Ui.Regular, "ink3").At(1));

        var canvas = new Canvas { Width = ChartW, Height = ChartH, Background = new SolidColorBrush(Microsoft.UI.Colors.Transparent) };
        canvas.Children.Add(new Line { X1 = 0, X2 = ChartW, Y1 = ChartH / 2, Y2 = ChartH / 2, Stroke = c.T.Brush("hair"), StrokeThickness = 1, StrokeDashArray = { 2, 4 } });
        d.AreaB = new Microsoft.UI.Xaml.Shapes.Path();
        d.LineB = new Microsoft.UI.Xaml.Shapes.Path { StrokeThickness = 2 };
        d.AreaA = new Microsoft.UI.Xaml.Shapes.Path();
        d.LineA = new Microsoft.UI.Xaml.Shapes.Path { StrokeThickness = 2 };
        canvas.Children.Add(d.AreaB); canvas.Children.Add(d.LineB);
        canvas.Children.Add(d.AreaA); canvas.Children.Add(d.LineA);
        d.Cursor = new Line { Y1 = 0, Y2 = ChartH, Stroke = c.T.Brush("track-strong"), StrokeThickness = 1, Visibility = Visibility.Collapsed };
        d.DotA = new Ellipse { Width = 8, Height = 8, StrokeThickness = 2, Fill = c.T.Brush("fly-solid"), Visibility = Visibility.Collapsed };
        d.DotB = new Ellipse { Width = 8, Height = 8, StrokeThickness = 2, Fill = c.T.Brush("fly-solid"), Visibility = Visibility.Collapsed };
        canvas.Children.Add(d.Cursor); canvas.Children.Add(d.DotA); canvas.Children.Add(d.DotB);
        d.PillText = Ui.Text(c, "", 11, Ui.SemiBold);
        d.Pill = new Border
        {
            Background = c.T.Brush("tip-bg"), BorderBrush = c.T.Brush("tip-b"), BorderThickness = new Thickness(1),
            CornerRadius = new CornerRadius(999), Padding = new Thickness(8, 2, 8, 2), Child = d.PillText, Visibility = Visibility.Collapsed
        };
        canvas.Children.Add(d.Pill);

        // The chart keeps time running left to right in RTL locales too.
        var host = new Grid { FlowDirection = FlowDirection.LeftToRight, Height = ChartH, Margin = new Thickness(0, 6, 0, 2) };
        host.Children.Add(new Viewbox { Stretch = Stretch.Fill, Child = canvas });
        host.PointerMoved += (s, e) =>
        {
            var g = (FrameworkElement)s;
            double fr = Math.Clamp(e.GetCurrentPoint(g).Position.X / Math.Max(1, g.ActualWidth), 0, 1);
            int n = Math.Max(1, d.A.Length);
            d.Hover = (int)Math.Round(fr * (n - 1));
            DrawCursor(d);
        };
        host.PointerExited += (_, _) => { d.Hover = null; DrawCursor(d); };

        var axis = Ui.Columns("* * *");
        axis.FlowDirection = FlowDirection.LeftToRight;
        var a0 = Ui.Text(c, c.L.T("ago10"), 10, Ui.Regular, "ink3").At(0);
        var a1 = Ui.Text(c, c.L.T("ago5"), 10, Ui.Regular, "ink3").At(1);
        a1.HorizontalAlignment = HorizontalAlignment.Center;
        var a2 = Ui.Text(c, c.L.T("now"), 10, Ui.Regular, "ink3").At(2);
        a2.HorizontalAlignment = HorizontalAlignment.Right;
        axis.Children.Add(a0); axis.Children.Add(a1); axis.Children.Add(a2);

        var legend = new StackPanel();
        if (nav.Kind is DetailKind.Ssd or DetailKind.Net)
        {
            var dt = c.X.Detail(nav.Kind, _snap);
            legend = Ui.Row(12,
                Ui.Row(5, Ui.Dot(c.T.Brush(dt.Accent)), Ui.Text(c, dt.ALabel, 11, Ui.Medium, "ink2")),
                Ui.Row(5, Ui.Dot(c.T.Brush(dt.Accent2)), Ui.Text(c, dt.BLabel, 11, Ui.Medium, "ink2")));
        }
        return Ui.Column(2, head, legend, host, axis);
    }

    void UpdateDetail(Snapshot s)
    {
        var d = _detail;
        if (d is null) return;
        AppSample? app = null;
        if (nav.Kind == DetailKind.App)
        {
            app = s.Apps.FirstOrDefault(a => a.Id == nav.AppId) ?? d.App;
            if (app is null) return;
            d.App = app;
        }
        var dt = c.X.Detail(nav.Kind, s, app);
        d.Last = dt;
        d.Title.Text = dt.Title;
        d.Hero.Text = dt.Hero;
        d.Sub.Text = dt.Sub;
        for (int i = 0; i < 6 && i < dt.Tiles.Count; i++)
        {
            d.TileLabel[i].Text = dt.Tiles[i].Label;
            d.TileValue[i].Text = dt.Tiles[i].Value;
        }

        var hist = host.Sampler.History;
        (string a, string? b) keys = nav.Kind switch
        {
            DetailKind.Cpu => ("cpu", null), DetailKind.Mem => ("mem", null), DetailKind.Nrg => ("nrg", null),
            DetailKind.Thm => ("thm", null), DetailKind.Gpu => ("gpu", null),
            DetailKind.Ssd => ("ssd.r", "ssd.w"), DetailKind.Net => ("net.d", "net.u"),
            _ => ("app:" + nav.AppId, null)
        };
        d.A = hist.Timeline(keys.a);
        d.B = keys.b is null ? [] : hist.Timeline(keys.b);
        d.Stat.Text = c.X.ChartStat(dt, d.A, keys.b is null ? null : d.B);

        var colA = c.T.Color(dt.Accent);
        StyleSeries(d.LineA, d.AreaA, colA, top: true);
        if (dt.Split)
        {
            var (pa, pb) = ChartMath.Split(d.A, d.B, ChartW, ChartH);
            d.LineA.Data = Sparkline.Geometry(pa, null, ChartH);
            d.AreaA.Data = Sparkline.Geometry(pa, ChartH / 2, ChartH);
            StyleSeries(d.LineB, d.AreaB, c.T.Color(dt.Accent2), top: false);
            d.LineB.Data = Sparkline.Geometry(pb, null, ChartH);
            d.AreaB.Data = Sparkline.Geometry(pb, ChartH / 2, ChartH);
        }
        else
        {
            var pa = ChartMath.Detail(d.A, ChartW, ChartH, dt.ZeroBased);
            d.LineA.Data = Sparkline.Geometry(pa, null, ChartH);
            d.AreaA.Data = Sparkline.Geometry(pa, ChartH, ChartH);
            d.LineB.Data = d.AreaB.Data = null;
        }
        DrawCursor(d);
    }

    static void StyleSeries(Microsoft.UI.Xaml.Shapes.Path line, Microsoft.UI.Xaml.Shapes.Path area, Windows.UI.Color color, bool top)
    {
        line.Stroke = new SolidColorBrush(color);
        var strong = color; strong.A = (byte)(255 * 0.28);
        var clear = color; clear.A = 0;
        area.Fill = new LinearGradientBrush
        {
            StartPoint = new Windows.Foundation.Point(0, top ? 0 : 1), EndPoint = new Windows.Foundation.Point(0, top ? 1 : 0),
            GradientStops = { new GradientStop { Color = strong, Offset = 0 }, new GradientStop { Color = clear, Offset = 1 } }
        };
    }

    void DrawCursor(DetailParts d)
    {
        var vis = d.Hover is not null && d.A.Length > 1 && d.Last is not null ? Visibility.Visible : Visibility.Collapsed;
        d.Cursor.Visibility = d.DotA.Visibility = d.Pill.Visibility = vis;
        d.DotB.Visibility = vis == Visibility.Visible && d.Last!.Split ? Visibility.Visible : Visibility.Collapsed;
        if (vis == Visibility.Collapsed) return;
        var dt = d.Last!;
        int n = d.A.Length, i = Math.Clamp(d.Hover!.Value, 0, n - 1);
        double x = i / (double)(n - 1) * ChartW;
        d.Cursor.X1 = d.Cursor.X2 = x;
        Pt a, b = default;
        if (dt.Split) { var (pa, pb) = ChartMath.Split(d.A, d.B.Length == n ? d.B : d.A, ChartW, ChartH); a = pa[i]; b = pb[i]; }
        else a = ChartMath.Detail(d.A, ChartW, ChartH, dt.ZeroBased)[i];
        d.DotA.Stroke = c.T.Brush(dt.Accent);
        Canvas.SetLeft(d.DotA, a.X - 4); Canvas.SetTop(d.DotA, a.Y - 4);
        if (dt.Split) { d.DotB.Stroke = c.T.Brush(dt.Accent2); Canvas.SetLeft(d.DotB, b.X - 4); Canvas.SetTop(d.DotB, b.Y - 4); }

        int secondsAgo = (n - 1 - i) * (int)TimeSpan.FromSeconds(5).TotalSeconds;
        d.PillText.Text = c.X.ScrubLabel(dt, secondsAgo, d.A[i], dt.Split && d.B.Length == n ? d.B[i] : null);
        // The pill tracks the cursor across [16%, 84%] and shifts by its own width, so it never leaves the chart.
        d.Pill.Measure(new Windows.Foundation.Size(double.PositiveInfinity, double.PositiveInfinity));
        double pw = d.Pill.DesiredSize.Width, frac = Math.Clamp(i / (double)(n - 1), 0.16, 0.84);
        Canvas.SetLeft(d.Pill, Math.Clamp(frac * ChartW - frac * pw, 0, ChartW - pw));
        Canvas.SetTop(d.Pill, 2);
    }
}

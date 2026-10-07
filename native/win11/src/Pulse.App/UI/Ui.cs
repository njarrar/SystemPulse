using Microsoft.UI.Text;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Automation;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Media;
using Microsoft.UI.Xaml.Shapes;
using Pulse.Core.Formatting;
using Pulse.Core.I18n;
using Pulse.Core.Presentation;
using Windows.Foundation;
using Windows.UI.Text;

namespace Pulse.App.UI;

/// <summary>Everything a view needs to draw: tokens, locale, text builders and fonts.</summary>
public sealed class UiContext(Theme theme, Locale locale, Fmt fmt)
{
    public Theme T { get; } = theme;
    public Locale L { get; } = locale;
    public Fmt F { get; } = fmt;
    public Texts X { get; } = new(locale, fmt);
    public bool Rtl => L.Rtl;
    public FontFamily UiFont { get; } = new(FontStack.ToXaml(locale.Fonts("win").Ui, Theme.UiFont));
    public FontFamily HeroFont { get; } = new(FontStack.ToXaml(locale.Fonts("win").Hero, Theme.HeroFont));
    public FontFamily LatinFont { get; } = new(Theme.UiFont);
    public static readonly FontFamily Icons = new("Segoe Fluent Icons, Segoe MDL2 Assets");
}

public static class Ui
{
    public static readonly FontWeight Regular = FontWeights.Normal, Medium = FontWeights.Medium, SemiBold = FontWeights.SemiBold, Bold = FontWeights.Bold;

    /// <summary>Single-line text that ends in an ellipsis and shows the full text in a tooltip when it does.</summary>
    public static TextBlock Text(UiContext c, string text, double size, FontWeight weight, string ink = "ink", bool hero = false)
    {
        var tb = new TextBlock
        {
            Text = text,
            FontSize = size,
            FontWeight = weight,
            FontFamily = hero ? c.HeroFont : c.UiFont,
            Foreground = c.T.Brush(ink),
            TextTrimming = TextTrimming.CharacterEllipsis,
            TextWrapping = TextWrapping.NoWrap,
            VerticalAlignment = VerticalAlignment.Center,
            IsTextScaleFactorEnabled = true
        };
        tb.IsTextTrimmedChanged += (s, _) => ToolTipService.SetToolTip(s, s.IsTextTrimmed ? s.Text : null);
        return tb;
    }

    /// <summary>
    /// A number with its unit or sign ("−15.2 W", "1.6 MB/s"). Always laid out
    /// left to right so signs and units keep their place in RTL; Segoe UI's
    /// figures are tabular, so values do not jitter as they change.
    /// </summary>
    public static TextBlock Value(UiContext c, string text, double size, FontWeight weight, string ink = "ink", bool hero = false)
    {
        var tb = Text(c, text, size, weight, ink, hero);
        tb.FlowDirection = FlowDirection.LeftToRight;
        if (!hero) tb.FontFamily = c.LatinFont;
        return tb;
    }

    public static FontIcon Glyph(string glyph, double size, Brush brush, bool mirror = false) => new()
    {
        Glyph = glyph,
        FontFamily = UiContext.Icons,
        FontSize = size,
        Foreground = brush,
        MirroredWhenRightToLeft = mirror,
        IsTextScaleFactorEnabled = false
    };

    /// <summary>Icon on a tinted rounded square, as in the card headers.</summary>
    public static Border IconBadge(UiContext c, string glyph, string accent, double box = 24)
    {
        return new Border
        {
            Width = box, Height = box, CornerRadius = new CornerRadius(Theme.RBadge),
            Background = c.T.Tint(accent, 13),
            Child = Glyph(glyph, box * 0.55, c.T.Brush(accent + "-ink"))
        };
    }

    public static Border Pill(UiContext c, UIElement child, Brush bg, Thickness? pad = null) => new()
    {
        CornerRadius = new CornerRadius(999),
        Background = bg,
        Padding = pad ?? new Thickness(8, 2, 8, 2),
        VerticalAlignment = VerticalAlignment.Center,
        Child = child
    };

    public static Ellipse Dot(Brush b, double d = 6) => new() { Width = d, Height = d, Fill = b, VerticalAlignment = VerticalAlignment.Center };

    public static StackPanel Row(double spacing, params UIElement[] children)
    {
        var sp = new StackPanel { Orientation = Orientation.Horizontal, Spacing = spacing, VerticalAlignment = VerticalAlignment.Center };
        foreach (var ch in children) sp.Children.Add(ch);
        return sp;
    }

    public static StackPanel Column(double spacing, params UIElement[] children)
    {
        var sp = new StackPanel { Orientation = Orientation.Vertical, Spacing = spacing };
        foreach (var ch in children) sp.Children.Add(ch);
        return sp;
    }

    public static Grid Columns(string spec, double spacing = 0)
    {
        var g = new Grid { ColumnSpacing = spacing };
        foreach (var part in spec.Split(' '))
        {
            GridLength len = part == "auto" ? GridLength.Auto
                : part.EndsWith('*') ? new GridLength(part.Length == 1 ? 1 : double.Parse(part[..^1], System.Globalization.CultureInfo.InvariantCulture), GridUnitType.Star)
                : new GridLength(double.Parse(part, System.Globalization.CultureInfo.InvariantCulture));
            g.ColumnDefinitions.Add(new ColumnDefinition { Width = len });
        }
        return g;
    }

    public static T At<T>(this T e, int column, int row = 0, int columnSpan = 1) where T : UIElement
    {
        var fe = (FrameworkElement)(object)e;
        Grid.SetColumn(fe, column);
        Grid.SetRow(fe, row);
        if (columnSpan > 1) Grid.SetColumnSpan(fe, columnSpan);
        return e;
    }

    /// <summary>
    /// A flat button in Pulse colours. Uses lightweight styling resource keys,
    /// so pointer-over, pressed, focus and automation still come from WinUI.
    /// </summary>
    public static Button Button(UiContext c, object content, string bg, string hoverBg, string border = "card-b", string ink = "ink", double radius = Theme.RBtn)
    {
        var b = new Button
        {
            Content = content,
            CornerRadius = new CornerRadius(radius),
            Padding = new Thickness(10, 4, 10, 4),
            HorizontalContentAlignment = HorizontalAlignment.Stretch,
            VerticalContentAlignment = VerticalAlignment.Stretch,
            FontFamily = c.UiFont,
            FontSize = 12,
            FontWeight = SemiBold,
            MinWidth = 0,
            MinHeight = 0
        };
        Restyle(b, c.T.Brush(bg), c.T.Brush(hoverBg), c.T.Brush(border), c.T.Brush(ink));
        return b;
    }

    public static void Restyle(Control b, Brush bg, Brush hover, Brush border, Brush ink)
    {
        b.Background = bg;
        b.BorderBrush = border;
        b.Foreground = ink;
        b.BorderThickness = new Thickness(1);
        var r = b.Resources;
        r["ButtonBackground"] = bg; r["ButtonBackgroundPointerOver"] = hover; r["ButtonBackgroundPressed"] = hover;
        r["ButtonBorderBrush"] = border; r["ButtonBorderBrushPointerOver"] = border; r["ButtonBorderBrushPressed"] = border;
        r["ButtonForeground"] = ink; r["ButtonForegroundPointerOver"] = ink; r["ButtonForegroundPressed"] = ink;
    }

    /// <summary>A clickable card: a styled Button so it gets keyboard focus, Enter/Space and a name for screen readers.</summary>
    public static Button Card(UiContext c, UIElement content, string name, Action onClick, double pad = 12)
    {
        var b = Button(c, content, "card", "card-hover", "card-b", "ink", Theme.RCard);
        b.Padding = new Thickness(pad);
        b.HorizontalAlignment = HorizontalAlignment.Stretch;
        b.VerticalAlignment = VerticalAlignment.Stretch;
        AutomationProperties.SetName(b, name);
        b.Click += (_, _) => onClick();
        return b;
    }

    public static Border Panel(UiContext c, UIElement content, double pad = 12) => new()
    {
        Background = c.T.Brush("card"),
        BorderBrush = c.T.Brush("card-b"),
        BorderThickness = new Thickness(1),
        CornerRadius = new CornerRadius(Theme.RCard),
        Padding = new Thickness(pad),
        Child = content
    };
}

/// <summary>A rounded meter whose fill grows from the start edge (the right edge in RTL).</summary>
public sealed partial class Meter : Grid
{
    readonly ColumnDefinition _fill = new(), _rest = new();
    readonly Border _bar;

    public Meter(Brush track, Brush fill, double height = 4)
    {
        Height = height;
        CornerRadius = new CornerRadius(height / 2);
        Background = track;
        ColumnDefinitions.Add(_fill);
        ColumnDefinitions.Add(_rest);
        _bar = new Border { Background = fill, CornerRadius = new CornerRadius(height / 2) };
        Children.Add(_bar);
        VerticalAlignment = VerticalAlignment.Center;
        Set(0);
    }

    public void Set(double pct, double minPct = 2)
    {
        double v = Math.Clamp(pct, minPct, 100);
        _fill.Width = new GridLength(v, GridUnitType.Star);
        _rest.Width = new GridLength(100 - v, GridUnitType.Star);
    }

    public Brush Fill { get => _bar.Background; set => _bar.Background = value; }
}

/// <summary>
/// A smooth line with a soft area under it, drawn in a fixed coordinate space
/// and stretched to fit. Time runs left to right in every locale.
/// </summary>
public sealed partial class Sparkline : Grid
{
    readonly Microsoft.UI.Xaml.Shapes.Path _line = new() { StrokeThickness = 1.6, StrokeLineJoin = PenLineJoin.Round };
    readonly Microsoft.UI.Xaml.Shapes.Path _area = new();
    readonly double _w, _h;

    public Sparkline(Windows.UI.Color color, double w = 172, double h = 28, double height = 28)
    {
        _w = w; _h = h;
        Height = height;
        FlowDirection = FlowDirection.LeftToRight;
        _line.Stroke = new SolidColorBrush(color);
        var top = color; top.A = (byte)(255 * 0.28);
        var bottom = color; bottom.A = 0;
        _area.Fill = new LinearGradientBrush
        {
            StartPoint = new Point(0, 0), EndPoint = new Point(0, 1),
            GradientStops = { new GradientStop { Color = top, Offset = 0 }, new GradientStop { Color = bottom, Offset = 1 } }
        };
        var canvas = new Canvas { Width = w, Height = h };
        canvas.Children.Add(_area);
        canvas.Children.Add(_line);
        Children.Add(new Viewbox { Stretch = Stretch.Fill, Child = canvas });
    }

    public void Set(IReadOnlyList<double> values)
    {
        var pts = ChartMath.Spark(values, _w, _h);
        _line.Data = Geometry(pts, null, _h);
        _area.Data = Geometry(pts, _h, _h);
    }

    /// <summary>Smoothed path through the points; with <paramref name="closeAt"/> the shape is closed down to that y.</summary>
    public static PathGeometry Geometry(IReadOnlyList<Pt> pts, double? closeAt, double h)
    {
        var geo = new PathGeometry();
        if (pts.Count < 2) return geo;
        var fig = new PathFigure { StartPoint = new Point(pts[0].X, pts[0].Y), IsClosed = closeAt is not null, IsFilled = closeAt is not null };
        foreach (var b in ChartMath.Smooth(pts))
            fig.Segments.Add(new BezierSegment { Point1 = new Point(b.C1.X, b.C1.Y), Point2 = new Point(b.C2.X, b.C2.Y), Point3 = new Point(b.End.X, b.End.Y) });
        if (closeAt is double y)
        {
            fig.Segments.Add(new LineSegment { Point = new Point(pts[^1].X, y) });
            fig.Segments.Add(new LineSegment { Point = new Point(pts[0].X, y) });
        }
        geo.Figures.Add(fig);
        return geo;
    }
}

using System.Globalization;
using Microsoft.UI;
using Microsoft.UI.Xaml.Media;
using Windows.UI;

namespace Pulse.App.UI;

/// <summary>
/// Part 1 colour tokens for Windows 11, light and dark, taken from the
/// prototype (pulse.component.js LIGHT / DARK). Radii follow its "win" platform:
/// flyout 12, card 8, button 6, badge 6, row 6.
/// </summary>
public sealed class Theme
{
    public const double RFly = 12, RCard = 8, RBtn = 6, RBadge = 6, RRow = 6, RMono = 6, RTip = 8, RLogo = 8;
    public const string HeroFont = "Segoe UI Variable Display, Segoe UI";
    public const string UiFont = "Segoe UI Variable Text, Segoe UI";

    static readonly Dictionary<string, string> Light = new()
    {
        ["fly-bg"] = "rgba(245,248,246,0.82)", ["fly-solid"] = "#F5F8F6", ["fly-border"] = "rgba(14,30,25,0.08)",
        ["card"] = "rgba(255,255,255,0.78)", ["card-hover"] = "rgba(255,255,255,0.96)", ["card-b"] = "rgba(14,30,25,0.06)",
        ["ink"] = "#0E1E19", ["ink2"] = "#4B635B", ["ink3"] = "#5E746C", ["track"] = "rgba(14,30,25,0.07)", ["track-strong"] = "rgba(14,30,25,0.2)",
        ["hover"] = "rgba(14,30,25,0.045)", ["hair"] = "rgba(14,30,25,0.1)", ["press"] = "rgba(14,30,25,0.1)",
        ["tip-bg"] = "rgba(255,255,255,0.94)", ["tip-b"] = "rgba(14,30,25,0.06)", ["seg-pill"] = "#FFFFFF", ["badge-bg"] = "#EEF7F2",
        ["cpu"] = "#10B981", ["mem"] = "#0EA5E9", ["nrg"] = "#F59E0B", ["thm"] = "#F43F5E", ["gpu"] = "#14B8A6", ["warn"] = "#F97316", ["crit"] = "#EF4444",
        ["cpu-ink"] = "#047857", ["mem-ink"] = "#0369A1", ["nrg-ink"] = "#B45309", ["thm-ink"] = "#BE123C", ["gpu-ink"] = "#0F766E", ["warn-ink"] = "#C2410C", ["crit-ink"] = "#B91C1C",
        ["danger"] = "#DC2626", ["danger-on"] = "#FFFFFF"
    };

    static readonly Dictionary<string, string> Dark = new()
    {
        ["fly-bg"] = "rgba(12,21,18,0.80)", ["fly-solid"] = "#0F1A16", ["fly-border"] = "rgba(167,243,208,0.12)",
        ["card"] = "rgba(255,255,255,0.055)", ["card-hover"] = "rgba(255,255,255,0.09)", ["card-b"] = "rgba(255,255,255,0.08)",
        ["ink"] = "#ECFDF5", ["ink2"] = "rgba(236,253,245,0.68)", ["ink3"] = "rgba(236,253,245,0.52)", ["track"] = "rgba(255,255,255,0.08)", ["track-strong"] = "rgba(255,255,255,0.22)",
        ["hover"] = "rgba(255,255,255,0.05)", ["hair"] = "rgba(255,255,255,0.1)", ["press"] = "rgba(255,255,255,0.14)",
        ["tip-bg"] = "rgba(20,32,28,0.94)", ["tip-b"] = "rgba(255,255,255,0.08)", ["seg-pill"] = "rgba(255,255,255,0.16)", ["badge-bg"] = "rgba(255,255,255,0.08)",
        ["cpu"] = "#34D399", ["mem"] = "#38BDF8", ["nrg"] = "#FBBF24", ["thm"] = "#FB7185", ["gpu"] = "#2DD4BF", ["warn"] = "#FB923C", ["crit"] = "#F87171",
        ["cpu-ink"] = "#34D399", ["mem-ink"] = "#38BDF8", ["nrg-ink"] = "#FBBF24", ["thm-ink"] = "#FB7185", ["gpu-ink"] = "#2DD4BF", ["warn-ink"] = "#FDBA74", ["crit-ink"] = "#FCA5A5",
        ["danger"] = "#F87171", ["danger-on"] = "#2A0E00"
    };

    readonly Dictionary<string, SolidColorBrush> _brushes = new(StringComparer.Ordinal);

    public bool IsDark { get; }

    public Theme(bool dark) => IsDark = dark;

    public Color Color(string token)
    {
        var table = IsDark ? Dark : Light;
        return table.TryGetValue(token, out var css) ? Parse(css) : Colors.Magenta;
    }

    public SolidColorBrush Brush(string token)
    {
        if (!_brushes.TryGetValue(token, out var b)) _brushes[token] = b = new SolidColorBrush(Color(token));
        return b;
    }

    /// <summary>The colour at a percentage of its alpha: the prototype's color-mix(..., transparent).</summary>
    public SolidColorBrush Tint(string token, double pct)
    {
        string key = token + "@" + pct.ToString(CultureInfo.InvariantCulture);
        if (_brushes.TryGetValue(key, out var b)) return b;
        var c = Color(token);
        c.A = (byte)Math.Round(c.A * pct / 100);
        return _brushes[key] = new SolidColorBrush(c);
    }

    /// <summary>Horizontal gradient; FlowDirection RTL on an ancestor mirrors it.</summary>
    public static LinearGradientBrush Gradient(Color from, Color to) => new()
    {
        StartPoint = new Windows.Foundation.Point(0, 0.5),
        EndPoint = new Windows.Foundation.Point(1, 0.5),
        GradientStops = { new GradientStop { Color = from, Offset = 0 }, new GradientStop { Color = to, Offset = 1 } }
    };

    public LinearGradientBrush Gradient(string from, string to) => Gradient(Color(from), Color(to));

    public LinearGradientBrush TintGradient(string from, double pa, string to, double pb)
    {
        Color a = Color(from), b = Color(to);
        a.A = (byte)Math.Round(a.A * pa / 100);
        b.A = (byte)Math.Round(b.A * pb / 100);
        return Gradient(a, b);
    }

    public static Color Parse(string css)
    {
        css = css.Trim();
        if (css.StartsWith('#'))
        {
            uint v = uint.Parse(css.AsSpan(1), NumberStyles.HexNumber, CultureInfo.InvariantCulture);
            return css.Length == 9
                ? ColorHelper.FromArgb((byte)(v & 0xFF), (byte)(v >> 24), (byte)(v >> 16), (byte)(v >> 8))
                : ColorHelper.FromArgb(255, (byte)(v >> 16), (byte)(v >> 8), (byte)v);
        }
        if (css.StartsWith("rgba(", StringComparison.Ordinal))
        {
            var p = css[5..^1].Split(',');
            byte Ch(int i) => byte.Parse(p[i].Trim(), CultureInfo.InvariantCulture);
            double a = double.Parse(p[3].Trim(), CultureInfo.InvariantCulture);
            return ColorHelper.FromArgb((byte)Math.Round(a * 255), Ch(0), Ch(1), Ch(2));
        }
        return Colors.Magenta;
    }
}

namespace Pulse.Core.Presentation;

public readonly record struct Pt(double X, double Y);

/// <summary>One cubic Bézier segment: two control points and the end point.</summary>
public readonly record struct Bezier(Pt C1, Pt C2, Pt End);

/// <summary>Chart geometry shared with the prototype: Catmull-Rom smoothing and value scaling.</summary>
public static class ChartMath
{
    /// <summary>Catmull-Rom to Bézier, same as the prototype's smooth().</summary>
    public static List<Bezier> Smooth(IReadOnlyList<Pt> p)
    {
        var segs = new List<Bezier>(Math.Max(0, p.Count - 1));
        for (int i = 0; i < p.Count - 1; i++)
        {
            Pt p0 = i > 0 ? p[i - 1] : p[i], p1 = p[i], p2 = p[i + 1], p3 = i + 2 < p.Count ? p[i + 2] : p2;
            segs.Add(new Bezier(
                new Pt(p1.X + (p2.X - p0.X) / 6, p1.Y + (p2.Y - p0.Y) / 6),
                new Pt(p2.X - (p3.X - p1.X) / 6, p2.Y - (p3.Y - p1.Y) / 6),
                p2));
        }
        return segs;
    }

    /// <summary>Sparkline points (card footers): padded min/max so a flat line sits low.</summary>
    public static Pt[] Spark(IReadOnlyList<double> v, double w, double h)
    {
        if (v.Count == 0) return [];
        if (v.Count == 1) return [new Pt(0, h / 2), new Pt(w, h / 2)];
        double mn = v.Min(), mx = v.Max(), rg = Math.Max(mx - mn, 0.5);
        double lo = mn - rg * 0.25, hi = mx + rg * 0.35;
        var pts = new Pt[v.Count];
        for (int i = 0; i < v.Count; i++)
            pts[i] = new Pt(i / (double)(v.Count - 1) * w, h - 2 - (v[i] - lo) / (hi - lo) * (h - 4));
        return pts;
    }

    /// <summary>Single-series detail chart points (372 x 140 in the prototype).</summary>
    public static Pt[] Detail(IReadOnlyList<double> v, double w, double h, bool zeroBased)
    {
        if (v.Count == 0) return [];
        double mx = v.Max(), mn = v.Min();
        double lo = zeroBased ? 0 : Math.Max(0, mn - (mx - mn) * 0.8);
        double hi = mx + (mx - lo) * 0.2;
        if (hi - lo < 1e-9) hi = lo + 1;
        int n = Math.Max(2, v.Count);
        var pts = new Pt[v.Count];
        for (int i = 0; i < v.Count; i++)
            pts[i] = new Pt(i / (double)(n - 1) * w, h - 6 - (v[i] - lo) / (hi - lo) * (h - 34));
        return pts;
    }

    /// <summary>Split chart: series A grows up from the middle, series B grows down.</summary>
    public static (Pt[] A, Pt[] B) Split(IReadOnlyList<double> a, IReadOnlyList<double> b, double w, double h)
    {
        double c = h / 2;
        double ma = Math.Max(1e-9, (a.Count > 0 ? a.Max() : 0) * 1.12), mb = Math.Max(1e-9, (b.Count > 0 ? b.Max() : 0) * 1.12);
        Pt[] Map(IReadOnlyList<double> s, double max, int sign)
        {
            int n = Math.Max(2, s.Count);
            var r = new Pt[s.Count];
            for (int i = 0; i < s.Count; i++) r[i] = new Pt(i / (double)(n - 1) * w, c + sign * (1 + s[i] / max * (c - 22)));
            return r;
        }
        return (Map(a, ma, -1), Map(b, mb, 1));
    }
}

/// <summary>Turns a CSS font stack from a locale file into a XAML FontFamily list.</summary>
public static class FontStack
{
    static readonly HashSet<string> Generic = new(StringComparer.OrdinalIgnoreCase)
    { "system-ui", "sans-serif", "serif", "monospace", "ui-sans-serif", "ui-serif", "ui-monospace", "ui-rounded", "cursive", "fantasy", "-apple-system", "BlinkMacSystemFont" };

    /// <summary>"\"IBM Plex Sans Arabic\", Tahoma, sans-serif" + "Segoe UI Variable Text" gives "IBM Plex Sans Arabic, Tahoma, Segoe UI Variable Text".</summary>
    public static string ToXaml(string? cssStack, string platformDefault)
    {
        if (string.IsNullOrWhiteSpace(cssStack)) return platformDefault;
        var names = cssStack.Split(',')
            .Select(x => x.Trim().Trim('"', '\'').Trim())
            .Where(x => x.Length > 0 && !Generic.Contains(x))
            .ToList();
        foreach (var d in platformDefault.Split(',').Select(x => x.Trim()))
            if (d.Length > 0 && !names.Contains(d, StringComparer.OrdinalIgnoreCase)) names.Add(d);
        return string.Join(", ", names);
    }
}

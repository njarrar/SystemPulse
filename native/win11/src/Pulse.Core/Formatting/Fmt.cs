using Pulse.Core.I18n;

namespace Pulse.Core.Formatting;

/// <summary>
/// Unit formatting for the flyout. Digits come from the locale; units stay
/// Latin and are never translated. Callers show these values in left-to-right
/// runs, so signs and units keep their order in RTL layouts.
/// </summary>
public sealed class Fmt(Locale locale, bool fahrenheit = false)
{
    public const string Minus = "−";
    public const string Dash = "—";

    public Locale Locale { get; } = locale;
    public bool Fahrenheit { get; } = fahrenheit;

    public string N(double x, int d = 0) => Locale.Num(x, d);

    /// <summary>"68%" or with decimals "51.0%".</summary>
    public string Pct(double x, int d = 0) => N(d == 0 ? Math.Round(x, MidpointRounding.AwayFromZero) : x, d) + "%";
    public string Pct(double? x, int d = 0) => x is double v ? Pct(v, d) : Dash;

    public string Temp(double celsius, int d = 0) =>
        Fahrenheit ? N(celsius * 9 / 5 + 32, d) + "°F" : N(celsius, d) + "°C";
    public string Temp(double? celsius, int d = 0) => celsius is double c ? Temp(c, d) : Dash;

    public string Watts(double w) => N(w, 1) + " W";
    public string WattsShort(double w) => N(w, 1) + "W";

    /// <summary>Signed power flow: "−15.2 W" on battery, "+48 W" when charging.</summary>
    public string Flow(double signedWatts) =>
        signedWatts >= 0 ? "+" + N(signedWatts, signedWatts >= 10 ? 0 : 1) + " W" : Minus + Watts(-signedWatts);

    /// <summary>Byte counts: "812 MB", "9.2 GB", "248 GB", "1.82 TB".</summary>
    public string Bytes(double bytes)
    {
        const double K = 1024, M = K * 1024, G = M * 1024, T = G * 1024;
        if (bytes < M) return N(bytes / K, 0) + " KB";
        if (bytes < G) return N(bytes / M, bytes < 10 * M ? 1 : 0) + " MB";
        if (bytes < 100 * G) return N(bytes / G, 1) + " GB";
        if (bytes < T) return N(bytes / G, 0) + " GB";
        return N(bytes / T, 2) + " TB";
    }

    /// <summary>Memory of one app: MB below 1 GB, else GB with two decimals.</summary>
    public string AppMem(double bytes)
    {
        const double M = 1024 * 1024, G = M * 1024;
        return bytes < 0.25 * G ? N(bytes / M, bytes < 10 * M ? 1 : 0) + " MB" : N(bytes / G, 2) + " GB";
    }

    /// <summary>GB with a fixed number of decimals, no unit ("9.2").</summary>
    public string Gb(double bytes, int d = 1) => N(bytes / (1024.0 * 1024 * 1024), d);

    /// <summary>Transfer rate in bytes per second: "124 KB/s", "1.6 MB/s".</summary>
    public string Rate(double bytesPerSec)
    {
        const double K = 1024, M = K * 1024, G = M * 1024;
        if (bytesPerSec < M) return N(bytesPerSec / K, 0) + " KB/s";
        if (bytesPerSec < G) return N(bytesPerSec / M, 1) + " MB/s";
        return N(bytesPerSec / G, 2) + " GB/s";
    }

    /// <summary>Link speed in bits per second: "866 Mbps", "2.4 Gbps".</summary>
    public string LinkSpeed(double bitsPerSec)
    {
        if (bitsPerSec >= 1e9) return N(bitsPerSec / 1e9, bitsPerSec % 1e9 == 0 ? 0 : 1) + " Gbps";
        if (bitsPerSec >= 1e6) return N(bitsPerSec / 1e6, 0) + " Mbps";
        return N(bitsPerSec / 1e3, 0) + " kbps";
    }

    public string Hz(double hz) => hz >= 1e9 ? N(hz / 1e9, 2) + " GHz" : N(hz / 1e6, 0) + " MHz";

    public string Dbm(int dbm) => (dbm < 0 ? Minus : "") + N(Math.Abs(dbm)) + " dBm";

    public string Rpm(double rpm) => N(rpm) + " RPM";

    /// <summary>Duration through the locale ("2h 40m", "52m").</summary>
    public string Duration(TimeSpan t)
    {
        int total = (int)Math.Max(0, Math.Round(t.TotalMinutes));
        int h = total / 60, m = total % 60;
        return h > 0
            ? Locale.T("durHM", ("h", (double)h), ("m", (double)m))
            : Locale.T("durM", ("m", (double)m));
    }
}

using Pulse.Core.Formatting;
using Pulse.Core.I18n;
using Pulse.Core.Models;

namespace Pulse.Core.Presentation;

public enum DetailKind { Cpu, Mem, Nrg, Thm, Gpu, Ssd, Net, App }

/// <summary>Accent token names, as in the prototype: cpu, mem, nrg, thm, gpu, warn, crit.</summary>
public static class Accent
{
    public const string Cpu = "cpu", Mem = "mem", Nrg = "nrg", Thm = "thm", Gpu = "gpu", Warn = "warn", Crit = "crit";
}

public sealed record Tile(string Label, string Value);

public sealed record DetailText
{
    public required DetailKind Kind { get; init; }
    public required string Title { get; init; }
    public required string Hero { get; init; }
    public required string Sub { get; init; }
    public required IReadOnlyList<Tile> Tiles { get; init; }
    public required string Accent { get; init; }
    public string Accent2 { get; init; } = "";
    public bool Split { get; init; }
    /// <summary>Single-series charts start at zero when true.</summary>
    public bool ZeroBased { get; init; }
    public string ALabel { get; init; } = "";
    public string BLabel { get; init; } = "";
    public string Unit { get; init; } = "";
    public required Func<double, string> FormatA { get; init; }
    public Func<double, string>? FormatB { get; init; }
    public string? AppId { get; init; }
}

/// <summary>
/// Builds every string the tray tooltip (tier 1), the overview (tier 2) and the
/// detail views (tier 3) show. Pure: same snapshot and locale give the same text.
/// </summary>
public sealed class Texts(Locale l, Fmt f)
{
    public Locale L { get; } = l;
    public Fmt F { get; } = f;

    public const double HogThresholdPct = 50;

    // -------------------------------------------------------------------
    // Shared pieces
    // -------------------------------------------------------------------

    public static int ThermalLevel(ThermalSample t)
    {
        if (t.Throttled) return 3;
        double c = t.CpuC ?? 0;
        return c < 48 ? 0 : c < 70 ? 1 : c < 90 ? 2 : 3;
    }

    public static string ThermalAccent(int level) => level switch { 0 => Accent.Mem, 1 => Accent.Nrg, 2 => Accent.Thm, _ => Accent.Crit };

    public string[] ThermalNames() => [L.T("cool"), L.T("warm"), L.T("hot"), L.T("throttled")];

    public string ThermalNote(ThermalSample t) => ThermalLevel(t) < 3 ? L.T("zero") : L.T("throttling");

    public string CoresLabel(CpuSample c)
    {
        var parts = new List<string?> { L.Plural("core", c.PhysicalCores > 0 ? c.PhysicalCores : c.LogicalProcessors) };
        if (c.LogicalProcessors > 0 && c.LogicalProcessors != c.PhysicalCores) parts.Add(L.Plural("thread", c.LogicalProcessors));
        return L.Join(parts);
    }

    /// <summary>The two core rows of the CPU card: P/E cores on hybrid CPUs, user/kernel otherwise.</summary>
    public (string Label, double Pct)[] CoreRows(CpuSample c) => c.Hybrid
        ? [(L.Hw("coreP"), c.PerfPct), (L.Hw("coreE"), c.EffPct ?? 0)]
        : [(L.Hw("user"), c.UserPct), (L.Hw("kernel"), c.SystemPct)];

    public string NetName(NetSample n) => n.Kind switch
    {
        LinkKind.WiFi => "Wi-Fi",
        LinkKind.Ethernet => "Ethernet",
        _ => string.IsNullOrEmpty(n.Alias) ? Fmt.Dash : n.Alias
    };

    public string DiskName(DiskSample d) => L.Hw("localDisk", ("drive", d.Root));

    public string Monitor => L.Hw("taskManager");

    // -------------------------------------------------------------------
    // Tier 1: tray tooltip
    // -------------------------------------------------------------------

    /// <summary>"CPU 68% · RAM 58% · ⚡ 15.2W · ↓ 1.6 MB/s". Watts show a dash when no battery reports a rate.</summary>
    public string TrayTooltip(Snapshot s)
    {
        var parts = new List<string?>
        {
            L.T("barCpu") + " " + F.Pct(s.Cpu.TotalPct),
            L.T("barRam") + " " + F.Pct(s.Memory.LoadPct)
        };
        parts.Add("⚡ " + (s.Power.RateWatts is double w ? F.WattsShort(Math.Abs(w)) : Fmt.Dash + "W"));
        parts.Add("↓ " + F.Rate(s.Net.DownBps));
        return L.Join(parts);
    }

    // -------------------------------------------------------------------
    // Tier 2: overview
    // -------------------------------------------------------------------

    /// <summary>Header pill: "Hog Alert · 53%" or "System Calm · 15.2W" (CPU % when no battery rate).</summary>
    public (string Text, bool Hog) StatusPill(Snapshot s, AppSample? hog)
    {
        if (hog is not null)
            return (L.T("pillValue", ("label", L.T("hogPill")), ("value", F.Pct(hog.CpuPct))), true);
        string value = s.Power.RateWatts is double w ? F.WattsShort(Math.Abs(w)) : F.Pct(s.Cpu.TotalPct);
        return (L.T("pillValue", ("label", L.T("calm")), ("value", value)), false);
    }

    public string HogTitle(AppSample hog) => L.T("hogTitle", ("app", L.App(hog.Name)), ("pct", F.Pct(hog.CpuPct)));
    public string HogSub() => L.T("hogSub", ("pct", F.Pct(HogThresholdPct)));

    public string UserSys(CpuSample c) => L.T("userSys", ("user", F.Pct(c.UserPct)), ("sys", F.Pct(c.SystemPct)));

    public string MemOf(MemorySample m) =>
        L.T("memOf", ("used", F.Gb(m.UsedBytes) + " GB"), ("total", F.Gb(m.TotalBytes, 0) + " GB"));

    /// <summary>"Normal pressure", "High pressure" or "Critical pressure".</summary>
    public string MemBadge(MemorySample m) => m.Pressure switch
    {
        MemoryPressure.Critical => L.T("pressureCritical"),
        MemoryPressure.High => L.T("pressureHigh"),
        _ => L.T("pressure")
    };

    /// <summary>Short form for tiles: "Normal", "High", "Critical".</summary>
    public string MemPressureShort(MemorySample m) => m.Pressure switch
    {
        MemoryPressure.Critical => L.T("pressureCriticalShort"),
        MemoryPressure.High => L.T("pressureHighShort"),
        _ => L.T("pressureShort")
    };

    /// <summary>Accent of the memory badge: green, orange, red.</summary>
    public static string MemAccent(MemorySample m) => m.Pressure switch
    {
        MemoryPressure.Critical => Accent.Crit,
        MemoryPressure.High => Accent.Warn,
        _ => Accent.Cpu
    };

    /// <summary>Legend of the memory bar: App, System, Compressed, Free.</summary>
    public (string Label, string Value, double Fraction)[] MemLegend(MemorySample m)
    {
        double total = Math.Max(1, m.TotalBytes);
        return
        [
            (L.T("segApp"), F.Gb(m.AppBytes) + " GB", m.AppBytes / total),
            (L.Hw("system"), F.Gb(m.SystemBytes) + " GB", m.SystemBytes / total),
            (L.Hw("compressed"), F.Gb(m.CompressedBytes) + " GB", m.CompressedBytes / total),
            (L.T("segFree"), F.Gb(m.AvailableBytes) + " GB", m.AvailableBytes / total)
        ];
    }

    public string EnergyHero(PowerSample p) => p.HasBattery ? F.Pct(p.PercentRemaining) : Fmt.Dash;

    public string EnergySub(PowerSample p)
    {
        if (!p.HasBattery) return L.T("noBattery");
        if (p.Charging) return p.TimeToFull is TimeSpan t ? L.T("fullIn", ("time", F.Duration(t))) : L.T("charging");
        if (p.OnAc) return L.T("acPower");
        return p.TimeLeft is TimeSpan l ? L.T("timeLeft", ("time", F.Duration(l))) : L.T("onBatt");
    }

    /// <summary>Flow pill: "−15.2 W" with "on battery", or "+48 W" with "charging".</summary>
    public (string Value, string Label) EnergyFlow(PowerSample p)
    {
        string label = !p.HasBattery ? L.T("acPower") : p.Charging ? L.T("charging") : p.OnAc ? L.T("acPower") : L.T("onBatt");
        return (p.RateWatts is double w ? F.Flow(w) : Fmt.Dash, label);
    }

    public string HealthLine(PowerSample p)
    {
        if (!p.HasBattery || p.HealthPct is not double h) return "";
        return p.CycleCount is int c && c > 0
            ? L.T("health", ("pct", F.Pct(h)), ("cycles", L.Plural("cycle", c)))
            : L.Join(L.T("tHealth") + " " + F.Pct(h));
    }

    public string TempLine(ThermalSample t, GpuSample g) => L.T("tempLine", ("cpu", F.Temp(t.CpuC)), ("gpu", F.Temp(g.TempC)));

    public string DiskUsed(DiskSample d) => L.T("used", ("pct", F.Pct(d.UsedPct)));
    public string DiskFree(DiskSample d) => L.T("free", ("value", F.Bytes(d.FreeBytes)));

    public string VideoMemory(GpuSample g)
    {
        if (g.Integrated || g.DedicatedBudget == 0)
            return L.Hw("vramShared", ("size", $"{F.Gb(g.SharedUsed)} / {F.Gb(g.SharedBudget, 0)} GB"));
        return $"{F.Gb(g.DedicatedUsed)} / {F.Gb(g.DedicatedBudget, 0)} GB";
    }

    public string NetSub(NetSample n)
    {
        if (!n.Connected) return Fmt.Dash;
        if (n.Kind == LinkKind.WiFi)
        {
            string detail = n.BandGHz is double b ? F.N(b, b % 1 == 0 ? 0 : 1) + " GHz" : Fmt.Dash;
            string state = n.RssiDbm is int r && r >= -67 ? L.Hw("strongSignal") : L.Hw("connected");
            return L.Hw("netSub", ("link", n.WifiStandard ?? "Wi-Fi"), ("detail", detail), ("state", state));
        }
        string speed = n.LinkBitsPerSec is double s ? F.LinkSpeed(s) : Fmt.Dash;
        return L.Hw("netSub", ("link", NetName(n)), ("detail", speed), ("state", L.Hw("connected")));
    }

    public string NetToday(NetSample n) => $"↓ {F.Bytes(n.TodayDown)} · ↑ {F.Bytes(n.TodayUp)}";

    // -------------------------------------------------------------------
    // Top apps
    // -------------------------------------------------------------------

    public enum AppSort { Cpu, Mem, Gpu }

    public static IReadOnlyList<AppSample> TopApps(IEnumerable<AppSample> apps, AppSort sort, int count = 5) => sort switch
    {
        AppSort.Mem => apps.OrderByDescending(a => a.MemBytes).Take(count).ToList(),
        AppSort.Gpu => apps.OrderByDescending(a => a.GpuPct).ThenByDescending(a => a.CpuPct).Take(count).ToList(),
        _ => apps.OrderByDescending(a => a.CpuPct).Take(count).ToList()
    };

    public string AppMetric(AppSample a, AppSort sort) => sort switch
    {
        AppSort.Mem => F.AppMem(a.MemBytes),
        AppSort.Gpu => F.Pct(a.GpuPct),
        _ => F.Pct(a.CpuPct, 1)
    };

    public static double AppValue(AppSample a, AppSort sort) => sort switch
    {
        AppSort.Mem => a.MemBytes,
        AppSort.Gpu => a.GpuPct,
        _ => a.CpuPct
    };

    /// <summary>Rows of the hover tooltip on an app.</summary>
    public IReadOnlyList<Tile> AppTip(AppSample a, MemorySample m)
    {
        double busy = Math.Max(0.0001, a.UserCpuPct + a.KernelCpuPct);
        double user = a.UserCpuPct * 100 / busy;
        return
        [
            new(L.T("tipUserKernel"), $"{F.Pct(user)} · {F.Pct(100 - user)}"),
            new(L.T("tipRam"), F.Pct(m.TotalBytes == 0 ? 0 : a.MemBytes * 100.0 / m.TotalBytes, 1)),
            new(L.T("tipGpu"), F.Pct(a.GpuPct)),
            new(L.T("tipProc"), F.N(a.Processes))
        ];
    }

    // -------------------------------------------------------------------
    // Tier 3: detail views
    // -------------------------------------------------------------------

    public DetailText Detail(DetailKind kind, Snapshot s, AppSample? app = null)
    {
        Tile T(string label, string value) => new(label, value);
        switch (kind)
        {
            case DetailKind.Cpu:
            {
                var c = s.Cpu;
                var rows = CoreRows(c);
                return new DetailText
                {
                    Kind = kind, Accent = Accent.Cpu, ZeroBased = true, Title = L.T("cpu"), Hero = F.Pct(c.TotalPct),
                    Sub = L.T("cpuDetailSub", ("user", F.Pct(c.UserPct)), ("sys", F.Pct(c.SystemPct)), ("cores", CoresLabel(c))),
                    Unit = L.T("unitCpu"), FormatA = x => F.Pct(x),
                    Tiles =
                    [
                        T(L.T("tUser"), F.Pct(c.UserPct)), T(L.T("tSystem"), F.Pct(c.SystemPct)), T(L.T("tIdle"), F.Pct(Math.Max(0, 100 - Math.Round(c.TotalPct)))),
                        c.Hybrid ? T(rows[0].Label, F.Pct(rows[0].Pct)) : T(L.T("tThreads"), F.N(c.LogicalProcessors)),
                        c.Hybrid ? T(rows[1].Label, F.Pct(rows[1].Pct)) : T(L.T("cpu"), L.Plural("core", c.PhysicalCores)),
                        T(L.T("tLoad"), c.LoadAverage is double la ? F.N(la, 2) : Fmt.Dash)
                    ]
                };
            }
            case DetailKind.Mem:
            {
                var m = s.Memory;
                var leg = MemLegend(m);
                return new DetailText
                {
                    Kind = kind, Accent = Accent.Mem, Title = L.T("mem"), Hero = F.Pct(m.LoadPct),
                    Sub = L.T("memInUse", ("used", F.Gb(m.UsedBytes) + " GB"), ("total", F.Gb(m.TotalBytes, 0) + " GB")),
                    Unit = L.T("unitRam"), FormatA = x => F.Pct(x),
                    Tiles =
                    [
                        T(leg[0].Label, leg[0].Value), T(leg[1].Label, leg[1].Value), T(leg[2].Label, leg[2].Value), T(leg[3].Label, leg[3].Value),
                        T(L.Hw("committed"), $"{F.Gb(m.CommitBytes)} / {F.Gb(m.CommitLimitBytes, 0)} GB"),
                        T(L.Hw("pressure"), MemPressureShort(m))
                    ]
                };
            }
            case DetailKind.Nrg:
            {
                var p = s.Power;
                string sub = !p.HasBattery ? L.T("noBattery")
                    : p.Charging ? (p.TimeToFull is TimeSpan tf ? L.T("chargingFullIn", ("time", F.Duration(tf))) : L.T("charging"))
                    : p.TimeLeft is TimeSpan tl && !p.OnAc ? L.T("onBattLeft", ("time", F.Duration(tl))) : EnergyFlow(p).Label;
                string timeLeft = p.Charging ? (p.TimeToFull is TimeSpan f2 ? L.T("fullIn", ("time", F.Duration(f2))) : Fmt.Dash)
                    : p.TimeLeft is TimeSpan l2 ? F.Duration(l2) : Fmt.Dash;
                string cap = p.FullChargeCapacityWh is double full && p.RemainingCapacityWh is double rem
                    ? $"{F.N(rem, 1)} / {F.N(full, 1)} Wh" : Fmt.Dash;
                string source = !p.HasBattery || p.OnAc ? L.T("acPower") : L.T("tBattery");
                return new DetailText
                {
                    Kind = kind, Accent = Accent.Nrg, ZeroBased = true, Title = L.T("nrg"), Hero = EnergyHero(p), Sub = sub,
                    FormatA = F.Watts,
                    Tiles =
                    [
                        T(L.T("tDraw"), p.RateWatts is double w ? F.Flow(w) : Fmt.Dash), T(L.T("tTimeLeft"), timeLeft),
                        T(L.T("tHealth"), F.Pct(p.HealthPct)), T(L.T("tCycles"), p.CycleCount is int cc ? F.N(cc) : Fmt.Dash),
                        T(L.T("tCapacity"), cap), T(L.T("tSource"), source)
                    ]
                };
            }
            case DetailKind.Thm:
            {
                var t = s.Thermal;
                int lv = ThermalLevel(t);
                return new DetailText
                {
                    Kind = kind, Accent = ThermalAccent(lv), Title = L.T("thm"), Hero = F.Temp(t.CpuC),
                    Sub = L.Join(ThermalNames()[lv], ThermalNote(t)), FormatA = x => F.Temp(x, 1),
                    Tiles =
                    [
                        T(L.T("tCpuDie"), F.Temp(t.CpuC, 1)), T(L.T("gpu"), F.Temp(s.Gpu.TempC, 1)), T(L.T("tStorage"), F.Temp(s.Disk.TempC)),
                        T(L.T("tBattery"), F.Temp(s.Power.TemperatureC)), T(L.T("tFans"), s.Gpu.FanRpm is double rpm ? F.Rpm(rpm) : Fmt.Dash),
                        T(L.T("tThrottling"), F.Pct(t.ThrottlePct))
                    ]
                };
            }
            case DetailKind.Gpu:
            {
                var g = s.Gpu;
                return new DetailText
                {
                    Kind = kind, Accent = Accent.Gpu, ZeroBased = true, Title = L.T("gpu"), Hero = F.Pct(g.UtilPct),
                    Sub = L.Join(g.Name, F.Temp(g.TempC)), Unit = L.T("unitGpu"), FormatA = x => F.Pct(x),
                    Tiles =
                    [
                        T(L.T("util"), F.Pct(g.UtilPct)), T(L.T("tTemperature"), F.Temp(g.TempC, 1)), T(L.T("tVideoMem"), VideoMemory(g)),
                        T(L.T("tCoreClock"), g.ClockHz is double hz ? F.Hz(hz) : Fmt.Dash),
                        T(L.T("tPower"), F.Pct(g.PowerPct)), T(L.T("tHotspot"), Fmt.Dash)
                    ]
                };
            }
            case DetailKind.Ssd:
            {
                var d = s.Disk;
                return new DetailText
                {
                    Kind = kind, Accent = Accent.Cpu, Accent2 = Accent.Nrg, Split = true, Title = L.T("storage"), Hero = F.Pct(d.UsedPct),
                    Sub = L.Join(DiskName(d), DiskFree(d)), ALabel = L.T("read"), BLabel = L.T("write"),
                    FormatA = F.Rate, FormatB = F.Rate,
                    Tiles =
                    [
                        T(L.T("tUsed"), F.Bytes(d.UsedBytes)), T(L.T("tFree"), F.Bytes(d.FreeBytes)), T(L.T("read"), F.Rate(d.ReadBps)),
                        T(L.T("write"), F.Rate(d.WriteBps)), T(L.T("tHealth"), Fmt.Dash), T(L.T("tFormat"), string.IsNullOrEmpty(d.FileSystem) ? Fmt.Dash : d.FileSystem)
                    ]
                };
            }
            case DetailKind.Net:
            {
                var n = s.Net;
                var sig = n.Kind == LinkKind.WiFi
                    ? T(L.Hw("signal"), n.RssiDbm is int r ? F.Dbm(r) : Fmt.Dash)
                    : T(L.Hw("linkSpeed"), n.LinkBitsPerSec is double ls ? F.LinkSpeed(ls) : Fmt.Dash);
                return new DetailText
                {
                    Kind = kind, Accent = Accent.Mem, Accent2 = Accent.Cpu, Split = true, Title = L.T("network"), Hero = F.Rate(n.DownBps),
                    Sub = NetSub(n), ALabel = L.T("down"), BLabel = L.T("up"),
                    FormatA = x => "↓ " + F.Rate(x), FormatB = x => "↑ " + F.Rate(x),
                    Tiles =
                    [
                        T(L.T("down"), F.Rate(n.DownBps)), T(L.T("up"), F.Rate(n.UpBps)),
                        T(L.T("tInterface"), n.WifiStandard ?? (string.IsNullOrEmpty(n.Description) ? Fmt.Dash : n.Description)),
                        sig, T(L.T("tLatency"), Fmt.Dash), T(L.T("tToday"), NetToday(n))
                    ]
                };
            }
            default:
            {
                var a = app ?? throw new ArgumentNullException(nameof(app));
                bool hog = a.CpuPct > HogThresholdPct;
                return new DetailText
                {
                    Kind = kind, Accent = hog ? Accent.Warn : Accent.Cpu, ZeroBased = true, Title = L.App(a.Name), Hero = F.Pct(a.CpuPct, 1),
                    Sub = L.Join(L.Plural("process", a.Processes), L.Plural("thread", a.Threads), "PID " + a.MainPid),
                    Unit = L.T("unitOfCpu"), FormatA = x => F.Pct(x, 1), AppId = a.Id,
                    Tiles =
                    [
                        T(L.T("cpu"), F.Pct(a.CpuPct, 1)), T(L.T("mem"), F.AppMem(a.MemBytes)), T(L.T("gpu"), F.Pct(a.GpuPct)),
                        T(L.T("tipProc"), F.N(a.Processes)), T(L.T("tThreads"), F.N(a.Threads)), T("PID", a.MainPid.ToString(System.Globalization.CultureInfo.InvariantCulture))
                    ]
                };
            }
        }
    }

    /// <summary>"Peak 72% · Avg 41%" for single charts, "Peak ↓ 3.1 MB/s · ↑ 410 KB/s" for split charts.</summary>
    public string ChartStat(DetailText d, double[] a, double[]? b)
    {
        if (a.Length == 0) return "";
        if (!d.Split || b is null || b.Length == 0)
            return L.T("peakAvg", ("peak", d.FormatA(a.Max())), ("avg", d.FormatA(a.Average())));
        return L.T("peakSplit", ("a", d.FormatA(a.Max())), ("b", (d.FormatB ?? d.FormatA)(b.Max())));
    }

    /// <summary>Label of the chart scrub pill: "2m 15s ago · 41%".</summary>
    public string ScrubLabel(DetailText d, int secondsAgo, double a, double? b)
    {
        int m = secondsAgo / 60, sec = secondsAgo % 60;
        string ago = secondsAgo == 0 ? L.T("now")
            : m == 0 ? L.T("agoS", ("s", (double)sec))
            : sec != 0 ? L.T("agoMS", ("m", (double)m), ("s", (double)sec))
            : L.T("agoM", ("m", (double)m));
        string val = d.Split && b is double bv
            ? L.Join(d.FormatA(a), (d.FormatB ?? d.FormatA)(bv))
            : d.FormatA(a) + (d.Unit.Length > 0 ? " " + d.Unit : "");
        return L.Join(ago, val);
    }
}

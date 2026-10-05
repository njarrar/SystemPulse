using Pulse.Core.Formatting;
using Pulse.Core.History;
using Pulse.Core.Models;
using Pulse.Core.Presentation;
using Xunit;

namespace Pulse.Core.Tests;

public class FmtTests
{
    static Fmt En(bool f = false) => new(Data.Registry().Get("en"), f);

    [Fact]
    public void Units_stay_latin_and_signs_use_true_minus()
    {
        var f = En();
        Assert.Equal("68%", f.Pct(67.6));
        Assert.Equal("51.0%", f.Pct(51.0, 1));
        Assert.Equal("−15.2 W", f.Flow(-15.24));
        Assert.Equal("+48 W", f.Flow(48));
        Assert.Equal("52°C", f.Temp(52.2));
        Assert.Equal("126°F", En(true).Temp(52.2));
        Assert.Equal("—", f.Temp((double?)null));
        Assert.Equal("−54 dBm", f.Dbm(-54));
    }

    [Theory]
    [InlineData(126_976, "124 KB/s")]
    [InlineData(1_677_722, "1.6 MB/s")]
    [InlineData(2_400_000_000, "2.24 GB/s")]
    public void Rates(double bps, string want) => Assert.Equal(want, En().Rate(bps));

    [Theory]
    [InlineData(266_287_972_352d, "248 GB")]
    [InlineData(9_878_424_780d, "9.2 GB")]
    [InlineData(851_443_712d, "812 MB")]
    [InlineData(2_001_111_000_000d, "1.82 TB")]
    public void Bytes(double b, string want) => Assert.Equal(want, En().Bytes(b));

    [Fact]
    public void Durations_go_through_the_locale()
    {
        Assert.Equal("2h 40m", En().Duration(TimeSpan.FromMinutes(160)));
        Assert.Equal("52m", En().Duration(TimeSpan.FromMinutes(52)));
        var ar = new Fmt(Data.Registry().Get("ar"));
        Assert.Contains('⁨', ar.Duration(TimeSpan.FromMinutes(160)));
    }
}

public class HistoryTests
{
    [Fact]
    public void Ring_keeps_the_newest_values_in_order()
    {
        var r = new RingSeries(3);
        foreach (var v in new[] { 1d, 2, 3, 4, 5 }) r.Push(v);
        Assert.Equal([3d, 4, 5], r.ToArray());
        Assert.Equal(5, r.Last);
    }

    [Fact]
    public void Timeline_averages_buckets_and_fills_gaps()
    {
        var t0 = new DateTimeOffset(2026, 10, 5, 9, 0, 0, TimeSpan.Zero);
        var tl = new Timeline(TimeSpan.FromSeconds(5), 121);
        for (int s = 0; s < 5; s++) tl.Add(t0.AddSeconds(s), s < 3 ? 10 : 20); // bucket 1: avg 14
        tl.Add(t0.AddSeconds(16), 40); // closes bucket 1, repeats it for the empty buckets 2 and 3
        Assert.Equal([14d, 14, 14, 40], tl.Points());
        for (int s = 0; s < 2000; s += 5) tl.Add(t0.AddSeconds(20 + s), 1);
        Assert.Equal(121, tl.Points().Length);
    }

    [Fact]
    public void Daily_counter_resets_at_midnight_and_survives_counter_resets()
    {
        var c = new DailyCounter();
        c.Update(new DateTime(2026, 10, 5, 23, 59, 0), 1000, 100);
        c.Update(new DateTime(2026, 10, 5, 23, 59, 30), 3000, 300);
        Assert.Equal(2000UL, c.Down);
        c.Update(new DateTime(2026, 10, 6, 0, 0, 5), 3500, 350);
        Assert.Equal(500UL, c.Down);
        c.Update(new DateTime(2026, 10, 6, 0, 0, 10), 10, 10); // adapter reset
        Assert.Equal(500UL, c.Down);
        c.Update(new DateTime(2026, 10, 6, 0, 0, 15), 110, 20);
        Assert.Equal(600UL, c.Down);
        Assert.Equal(60UL, c.Up);
    }

    [Fact]
    public void Rate_meter_divides_by_elapsed_time()
    {
        var m = new RateMeter();
        Assert.Equal(0, m.Update(1000, 0, 1000));
        Assert.Equal(2000, m.Update(3000, 1000, 1000));
    }

    [Fact]
    public void Cpu_math_splits_performance_and_efficiency_cores()
    {
        // 4 logical processors: two P (class 1), two E (class 0). 1 s = 10,000,000 ticks.
        var prev = new CoreTimes[4];
        var cur = new[]
        {
            new CoreTimes(Idle: 2_000_000, Kernel: 4_000_000, User: 6_000_000), // 80% busy
            new CoreTimes(2_000_000, 4_000_000, 6_000_000),                      // 80%
            new CoreTimes(8_000_000, 9_000_000, 1_000_000),                      // 20%
            new CoreTimes(8_000_000, 9_000_000, 1_000_000)                       // 20%
        };
        var s = CpuMath.Compute(prev, cur, [1, 1, 0, 0], physicalCores: 4);
        Assert.Equal(50, s.TotalPct, 6);
        Assert.Equal(80, s.PerfPct, 6);
        Assert.Equal(20, s.EffPct!.Value, 6);
        Assert.Equal(2, s.PerfCores);
        Assert.Equal(2, s.EffCores);
        Assert.Equal(35, s.UserPct, 6);   // 14 of 40 M ticks
        Assert.Equal(15, s.SystemPct, 6); // kernel minus idle: 6 of 40 M ticks
        Assert.True(s.Hybrid);

        var flat = CpuMath.Compute(prev, cur, [0, 0, 0, 0], 4);
        Assert.False(flat.Hybrid);
        Assert.Equal(50, flat.PerfPct, 6);
    }

    [Fact]
    public void Process_share_is_of_the_whole_machine()
    {
        var (total, user, kernel) = CpuMath.ProcessPct(new ProcTimes(0, 0), new ProcTimes(10_000_000, 30_000_000), 10_000_000, 8);
        Assert.Equal(50, total, 6);
        Assert.Equal(37.5, user, 6);
        Assert.Equal(12.5, kernel, 6);
    }

    [Fact]
    public void Hog_needs_two_minutes_above_half_the_cpu()
    {
        var h = new HogDetector();
        var t0 = DateTimeOffset.UnixEpoch;
        var hot = new[] { new AppSample { Id = "msmpeng", Name = "Antimalware Service", CpuPct = 53 } };
        var cool = new[] { new AppSample { Id = "msmpeng", Name = "Antimalware Service", CpuPct = 12 } };
        Assert.Null(h.Update(t0, hot));
        Assert.Null(h.Update(t0.AddSeconds(119), hot));
        Assert.Equal("msmpeng", h.Update(t0.AddSeconds(120), hot)?.Id);
        Assert.Null(h.Update(t0.AddSeconds(121), cool));
        Assert.Null(h.Update(t0.AddSeconds(122), hot)); // timer starts over
    }

    [Fact]
    public void Load_average_damps_over_a_minute()
    {
        var la = new LoadAverage();
        Assert.Equal(4, la.Update(4, TimeSpan.FromSeconds(1)));
        double v = 0;
        for (int i = 0; i < 60; i++) v = la.Update(0, TimeSpan.FromSeconds(1));
        Assert.InRange(v, 4 / Math.E - 0.01, 4 / Math.E + 0.01);
    }
}

public class TextsTests
{
    static Snapshot Sample() => new()
    {
        At = DateTimeOffset.UnixEpoch,
        Cpu = new CpuSample { TotalPct = 68, UserPct = 45, SystemPct = 23, PerfPct = 83, EffPct = 39, PerfCores = 6, EffCores = 8, PhysicalCores = 14, LogicalProcessors = 20, LoadAverage = 6.07 },
        Memory = new MemorySample { TotalBytes = 16UL << 30, AvailableBytes = (ulong)(6.8 * (1UL << 30)), SystemBytes = 2UL << 30, CompressedBytes = (ulong)(1.2 * (1UL << 30)), CommitBytes = (ulong)(11.2 * (1UL << 30)), CommitLimitBytes = 24UL << 30 },
        Power = new PowerSample { HasBattery = true, PercentRemaining = 84, RateWatts = -15.2, TimeLeft = TimeSpan.FromMinutes(160), DesignCapacityWh = 72.6, FullChargeCapacityWh = 69, RemainingCapacityWh = 58, CycleCount = 118 },
        Thermal = new ThermalSample { CpuC = 52, PassiveLimitPct = 100, PerfLimitPct = 100 },
        Gpu = new GpuSample { Name = "Intel Arc Graphics", UtilPct = 14, TempC = 44, Integrated = true, SharedUsed = (ulong)(2.1 * (1UL << 30)), SharedBudget = 8UL << 30 },
        Disk = new DiskSample { Root = "C:", FileSystem = "NTFS", TotalBytes = 512UL << 30, FreeBytes = 248UL << 30 },
        Net = new NetSample { Kind = LinkKind.WiFi, Connected = true, DownBps = 1.6 * 1024 * 1024, UpBps = 124 * 1024, WifiStandard = "Wi-Fi 6E", BandGHz = 5, RssiDbm = -54 },
        Apps = [new AppSample { Id = "msmpeng", Name = "Antimalware Service", CpuPct = 51, UserCpuPct = 40, KernelCpuPct = 11, MemBytes = 420UL << 20, Pids = [5124], Threads = 52 }]
    };

    static Texts En() { var l = Data.Registry().Get("en"); return new Texts(l, new Fmt(l)); }
    static Texts Ar() { var l = Data.Registry().Get("ar"); return new Texts(l, new Fmt(l)); }

    [Fact]
    public void Tier_one_tooltip_has_cpu_ram_watts_and_download()
    {
        Assert.Equal("CPU 68% · RAM 58% · ⚡ 15.2W · ↓ 1.6 MB/s", En().TrayTooltip(Sample()));
        Assert.True(En().TrayTooltip(Sample()).Length < 128); // NOTIFYICONDATA.szTip limit
    }

    [Fact]
    public void Tier_one_tooltip_keeps_the_watts_slot_without_a_battery()
    {
        var s = Sample() with { Power = new PowerSample() };
        Assert.Equal("CPU 68% · RAM 58% · ⚡ —W · ↓ 1.6 MB/s", En().TrayTooltip(s));
    }

    [Fact]
    public void Demo_overlay_adds_a_hog_and_charging_over_real_data()
    {
        var real = Sample() with { Apps = [], Cpu = Sample().Cpu with { TotalPct = 16 }, Power = Sample().Power with { Charging = false } };
        var demo = DemoOverlay.Apply(real, hog: true, charging: true);
        var hog = Assert.Single(demo.Apps, DemoOverlay.IsDemo);
        Assert.Empty(hog.Pids); // never a real process
        Assert.Equal(68, demo.Cpu.TotalPct, 6);
        Assert.True(demo.Power.Charging);
        Assert.Equal("Full in 52m", En().EnergySub(demo.Power));
        Assert.Equal(("+48 W", "charging"), En().EnergyFlow(demo.Power));
        Assert.Same(real, DemoOverlay.Apply(real, false, false));
        Assert.Equal(16, real.Cpu.TotalPct); // the real snapshot is untouched
    }

    [Fact]
    public void Memory_pressure_has_three_levels()
    {
        var t = En();
        var m = Sample().Memory;
        Assert.Equal("Normal pressure", t.MemBadge(m));
        var high = m with { AvailableBytes = (ulong)(0.08 * m.TotalBytes) };
        Assert.Equal("High pressure", t.MemBadge(high));
        Assert.Equal("High", t.MemPressureShort(high));
        Assert.Equal("warn", Texts.MemAccent(high));
        var crit = m with { AvailableBytes = (ulong)(0.03 * m.TotalBytes) };
        Assert.Equal("Critical pressure", t.MemBadge(crit));
        Assert.Equal("Critical", t.Detail(DetailKind.Mem, Sample() with { Memory = crit }).Tiles[5].Value);
        Assert.Equal("crit", Texts.MemAccent(crit));
    }

    [Fact]
    public void Desktops_without_a_battery_say_so()
    {
        var t = En();
        var s = Sample() with { Power = new PowerSample { OnAc = true } };
        Assert.Equal("No battery", t.EnergySub(s.Power));
        Assert.Equal(("—", "On AC power"), t.EnergyFlow(s.Power));
        var d = t.Detail(DetailKind.Nrg, s);
        Assert.Equal("No battery", d.Sub);
        Assert.Equal("On AC power", d.Tiles[5].Value);
        var plugged = Sample().Power with { OnAc = true, Charging = false };
        Assert.Equal("On AC power", t.EnergySub(plugged));
    }

    [Fact]
    public void New_settings_and_toast_strings_exist_in_every_locale()
    {
        var r = Data.Registry();
        foreach (var info in r.List())
        {
            var l = r.Get(info.Code);
            foreach (var k in new[] { "language", "languageSub", "langSystem", "endFailed", "acPower", "noBattery" })
                Assert.True(l.Data.Strings.ContainsKey(k), $"{info.Code} lacks {k}");
        }
        Assert.Equal("Couldn't end Defender", r.Get("en").T("endFailed", ("app", "Defender")));
        Assert.Contains("\u2068Defender\u2069", r.Get("ar").T("endFailed", ("app", "Defender")));
    }

    [Fact]
    public void Overview_strings_match_the_prototype()
    {
        var t = En();
        var s = Sample();
        Assert.Equal(("Hog Alert · 51%", true), t.StatusPill(s, s.Apps[0]));
        Assert.Equal(("System Calm · 15.2W", false), t.StatusPill(s, null));
        Assert.Equal("Antimalware Service is using 51% of total CPU", t.HogTitle(s.Apps[0]));
        Assert.Equal("User 45% · Sys 23%", t.UserSys(s.Cpu));
        Assert.Equal("9.2 GB of 16 GB", t.MemOf(s.Memory));
        Assert.Equal("Normal pressure", t.MemBadge(s.Memory));
        Assert.Equal("2h 40m left", t.EnergySub(s.Power));
        Assert.Equal(("−15.2 W", "on battery"), t.EnergyFlow(s.Power));
        Assert.Equal("Health 95% · 118 cycles", t.HealthLine(s.Power));
        Assert.Equal("CPU 52°C · GPU 44°C", t.TempLine(s.Thermal, s.Gpu));
        Assert.Equal("Local Disk (C:)", t.DiskName(s.Disk));
        Assert.Equal("248 GB free", t.DiskFree(s.Disk));
        Assert.Equal("Wi-Fi 6E · 5 GHz · Strong signal", t.NetSub(s.Net));
        Assert.Equal("14 cores · 20 threads", t.CoresLabel(s.Cpu));
        Assert.Equal("2.1 / 8 GB shared", t.VideoMemory(s.Gpu));
        Assert.Equal(["P-Cores", "E-Cores"], t.CoreRows(s.Cpu).Select(x => x.Label));
    }

    [Fact]
    public void Every_detail_view_has_six_tiles()
    {
        var t = En();
        var s = Sample();
        foreach (var k in Enum.GetValues<DetailKind>())
        {
            var d = t.Detail(k, s, s.Apps[0]);
            Assert.Equal(6, d.Tiles.Count);
            Assert.False(string.IsNullOrEmpty(d.Title));
        }
        var app = t.Detail(DetailKind.App, s, s.Apps[0]);
        Assert.Equal("1 process · 52 threads · PID 5124", app.Sub);
        Assert.Equal("warn", app.Accent);
    }

    [Fact]
    public void Arabic_overview_isolates_values()
    {
        var t = Ar();
        var s = Sample();
        Assert.Contains("⁨52%⁩", t.TempLine(s.Thermal, s.Gpu).Replace("°C", "%"));
        Assert.StartsWith("⁨", t.TrayTooltip(s));
    }

    [Fact]
    public void Scrub_label_counts_back_in_minutes_and_seconds()
    {
        var t = En();
        var d = t.Detail(DetailKind.Cpu, Sample());
        Assert.Equal("Now · 41% CPU", t.ScrubLabel(d, 0, 41, null));
        Assert.Equal("2m 15s ago · 41% CPU", t.ScrubLabel(d, 135, 41, null));
        Assert.Equal("Peak 90% · Avg 50%", t.ChartStat(d, [10, 50, 90], null));
    }

    [Fact]
    public void Thermal_levels_follow_the_prototype_thresholds()
    {
        Assert.Equal(0, Texts.ThermalLevel(new ThermalSample { CpuC = 33 }));
        Assert.Equal(1, Texts.ThermalLevel(new ThermalSample { CpuC = 52 }));
        Assert.Equal(2, Texts.ThermalLevel(new ThermalSample { CpuC = 75 }));
        Assert.Equal(3, Texts.ThermalLevel(new ThermalSample { CpuC = 60, PassiveLimitPct = 80 }));
    }

    [Fact]
    public void Font_stacks_drop_generic_families()
    {
        Assert.Equal("IBM Plex Sans Arabic, Tahoma, Segoe UI Variable Text",
            FontStack.ToXaml("\"IBM Plex Sans Arabic\", Tahoma, sans-serif", "Segoe UI Variable Text"));
        Assert.Equal("Segoe UI Variable Text", FontStack.ToXaml(null, "Segoe UI Variable Text"));
    }

    [Fact]
    public void Chart_geometry_matches_the_prototype_shape()
    {
        var pts = ChartMath.Spark([1, 2, 3], 172, 28);
        Assert.Equal(3, pts.Length);
        Assert.Equal(0, pts[0].X);
        Assert.Equal(172, pts[^1].X);
        Assert.True(pts[0].Y > pts[^1].Y);
        Assert.Equal(2, ChartMath.Smooth(pts).Count);
        var (a, b) = ChartMath.Split([1, 2], [3, 4], 372, 140);
        Assert.True(a.All(p => p.Y < 70) && b.All(p => p.Y > 70));
    }
}

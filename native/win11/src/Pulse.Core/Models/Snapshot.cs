namespace Pulse.Core.Models;

// Plain data the Windows samplers fill and the flyout reads. A null field
// means the machine did not report that value; the UI shows a dash.

public sealed record CpuSample
{
    public double TotalPct { get; init; }
    public double UserPct { get; init; }
    public double SystemPct { get; init; }
    /// <summary>Average load of the performance cores (highest efficiency class).</summary>
    public double PerfPct { get; init; }
    /// <summary>Average load of the efficiency cores. Null on CPUs with one core class.</summary>
    public double? EffPct { get; init; }
    public int PerfCores { get; init; }
    public int EffCores { get; init; }
    public int PhysicalCores { get; init; }
    public int LogicalProcessors { get; init; }
    public double[] PerCorePct { get; init; } = [];
    /// <summary>One-minute load average, built from busy CPUs plus the ready queue.</summary>
    public double? LoadAverage { get; init; }
    public bool Hybrid => EffPct is not null;
}

public sealed record MemorySample
{
    public ulong TotalBytes { get; init; }
    public ulong AvailableBytes { get; init; }
    public ulong UsedBytes => TotalBytes > AvailableBytes ? TotalBytes - AvailableBytes : 0;
    /// <summary>Kernel paged plus nonpaged pool.</summary>
    public ulong SystemBytes { get; init; }
    /// <summary>Working set of the Memory Compression process.</summary>
    public ulong CompressedBytes { get; init; }
    public ulong CommitBytes { get; init; }
    public ulong CommitLimitBytes { get; init; }
    public double LoadPct => TotalBytes == 0 ? 0 : UsedBytes * 100.0 / TotalBytes;
    public ulong AppBytes => UsedBytes > SystemBytes + CompressedBytes ? UsedBytes - SystemBytes - CompressedBytes : 0;
    /// <summary>
    /// Critical when under 5% of RAM is free or commit is over 95% of its limit;
    /// high under 10% free or over 90% commit; normal otherwise.
    /// </summary>
    public MemoryPressure Pressure =>
        TotalBytes == 0 ? MemoryPressure.Normal
        : AvailableBytes < TotalBytes / 20 || (CommitLimitBytes > 0 && CommitBytes > CommitLimitBytes * 0.95) ? MemoryPressure.Critical
        : AvailableBytes < TotalBytes / 10 || (CommitLimitBytes > 0 && CommitBytes > CommitLimitBytes * 0.9) ? MemoryPressure.High
        : MemoryPressure.Normal;
    public bool HighPressure => Pressure != MemoryPressure.Normal;
}

public enum MemoryPressure { Normal, High, Critical }

public sealed record PowerSample
{
    public bool HasBattery { get; init; }
    public bool OnAc { get; init; }
    public bool Charging { get; init; }
    public double? PercentRemaining { get; init; }
    /// <summary>Battery power flow in watts: negative while discharging, positive while charging.</summary>
    public double? RateWatts { get; init; }
    public TimeSpan? TimeLeft { get; init; }
    public TimeSpan? TimeToFull { get; init; }
    public double? DesignCapacityWh { get; init; }
    public double? FullChargeCapacityWh { get; init; }
    public double? RemainingCapacityWh { get; init; }
    public int? CycleCount { get; init; }
    public double? HealthPct => DesignCapacityWh is > 0 && FullChargeCapacityWh is double f ? Math.Min(100, f * 100 / DesignCapacityWh.Value) : null;
    public string? Chemistry { get; init; }
    /// <summary>Battery pack temperature, when the battery driver reports it.</summary>
    public double? TemperatureC { get; init; }
}

public sealed record ThermalSample
{
    /// <summary>Hottest ACPI thermal zone, used as the CPU die reading.</summary>
    public double? CpuC { get; init; }
    /// <summary>Lowest passive limit across zones, 100 = no throttling.</summary>
    public double? PassiveLimitPct { get; init; }
    /// <summary>Processor performance limit from firmware or the OS, 100 = none.</summary>
    public double? PerfLimitPct { get; init; }
    public bool Throttled => (PassiveLimitPct is double p && p < 100) || (PerfLimitPct is double l && l < 95);
    public double ThrottlePct => Math.Max(0, 100 - Math.Min(PassiveLimitPct ?? 100, PerfLimitPct ?? 100));
}

public sealed record GpuSample
{
    public string Name { get; init; } = "";
    public double UtilPct { get; init; }
    public double? TempC { get; init; }
    public ulong DedicatedUsed { get; init; }
    public ulong DedicatedBudget { get; init; }
    public ulong SharedUsed { get; init; }
    public ulong SharedBudget { get; init; }
    public bool Integrated { get; init; }
    public double? ClockHz { get; init; }
    public double? FanRpm { get; init; }
    /// <summary>Board power as a percent of its limit, when the driver reports it.</summary>
    public double? PowerPct { get; init; }
}

public sealed record DiskSample
{
    public string Root { get; init; } = "C:";
    public string FileSystem { get; init; } = "";
    public ulong TotalBytes { get; init; }
    public ulong FreeBytes { get; init; }
    public ulong UsedBytes => TotalBytes > FreeBytes ? TotalBytes - FreeBytes : 0;
    public double UsedPct => TotalBytes == 0 ? 0 : UsedBytes * 100.0 / TotalBytes;
    public double ReadBps { get; init; }
    public double WriteBps { get; init; }
    public double? TempC { get; init; }
}

public enum LinkKind { None, Ethernet, WiFi, Cellular, Other }

public sealed record NetSample
{
    public LinkKind Kind { get; init; }
    public string Alias { get; init; } = "";
    public string Description { get; init; } = "";
    public bool Connected { get; init; }
    public double DownBps { get; init; }
    public double UpBps { get; init; }
    public double? LinkBitsPerSec { get; init; }
    /// <summary>"Wi-Fi 6E" and similar, from the PHY type and band.</summary>
    public string? WifiStandard { get; init; }
    public double? BandGHz { get; init; }
    public int? RssiDbm { get; init; }
    public ulong TodayDown { get; init; }
    public ulong TodayUp { get; init; }
}

public sealed record AppSample
{
    /// <summary>Image name without ".exe", the grouping key.</summary>
    public required string Id { get; init; }
    public required string Name { get; init; }
    public int[] Pids { get; init; } = [];
    public double CpuPct { get; init; }
    public double UserCpuPct { get; init; }
    public double KernelCpuPct { get; init; }
    public ulong MemBytes { get; init; }
    public double GpuPct { get; init; }
    public int Threads { get; init; }
    public int Processes => Pids.Length;
    public bool Background { get; init; }
    public int MainPid => Pids.Length > 0 ? Pids[0] : 0;
}

public sealed record Snapshot
{
    public DateTimeOffset At { get; init; }
    public CpuSample Cpu { get; init; } = new();
    public MemorySample Memory { get; init; } = new();
    public PowerSample Power { get; init; } = new();
    public ThermalSample Thermal { get; init; } = new();
    public GpuSample Gpu { get; init; } = new();
    public DiskSample Disk { get; init; } = new();
    public NetSample Net { get; init; } = new();
    public IReadOnlyList<AppSample> Apps { get; init; } = [];
}

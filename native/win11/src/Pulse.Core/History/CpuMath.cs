using Pulse.Core.Models;

namespace Pulse.Core.History;

/// <summary>Cumulative times of one logical processor, in 100 ns units, as NT reports them.</summary>
/// <remarks>NT counts idle time inside kernel time, so busy kernel time is Kernel - Idle.</remarks>
public readonly record struct CoreTimes(long Idle, long Kernel, long User);

/// <summary>Cumulative CPU time of one process, in 100 ns units.</summary>
public readonly record struct ProcTimes(long Kernel, long User);

public static class CpuMath
{
    /// <summary>
    /// Builds a CPU sample from two readings of every logical processor.
    /// <paramref name="efficiencyClass"/> holds GetSystemCpuSetInformation's
    /// EfficiencyClass per logical processor: the highest class marks
    /// performance cores; when every core shares one class the CPU is not hybrid.
    /// </summary>
    public static CpuSample Compute(CoreTimes[] prev, CoreTimes[] cur, byte[]? efficiencyClass, int physicalCores, double? loadAverage = null)
    {
        int n = Math.Min(prev.Length, cur.Length);
        var per = new double[n];
        double busySum = 0, userSum = 0, sysSum = 0, totalSum = 0;
        for (int i = 0; i < n; i++)
        {
            long idle = Math.Max(0, cur[i].Idle - prev[i].Idle);
            long kernel = Math.Max(0, cur[i].Kernel - prev[i].Kernel);
            long user = Math.Max(0, cur[i].User - prev[i].User);
            long total = kernel + user;
            long sys = Math.Max(0, kernel - idle);
            per[i] = total > 0 ? Math.Clamp((sys + user) * 100.0 / total, 0, 100) : 0;
            busySum += sys + user; userSum += user; sysSum += sys; totalSum += total;
        }
        double totalPct = totalSum > 0 ? Math.Clamp(busySum * 100 / totalSum, 0, 100) : 0;
        double userPct = totalSum > 0 ? userSum * 100 / totalSum : 0;
        double sysPct = totalSum > 0 ? sysSum * 100 / totalSum : 0;

        double perf = totalPct;
        double? eff = null;
        int pCount = n, eCount = 0;
        if (efficiencyClass is { Length: > 0 } ec && ec.Length >= n)
        {
            byte top = 0;
            for (int i = 0; i < n; i++) top = Math.Max(top, ec[i]);
            bool hybrid = false;
            for (int i = 0; i < n; i++) if (ec[i] != top) { hybrid = true; break; }
            if (hybrid)
            {
                double ps = 0, es = 0; pCount = 0;
                for (int i = 0; i < n; i++)
                {
                    if (ec[i] == top) { ps += per[i]; pCount++; }
                    else { es += per[i]; eCount++; }
                }
                perf = pCount > 0 ? ps / pCount : 0;
                eff = eCount > 0 ? es / eCount : 0;
            }
        }
        return new CpuSample
        {
            TotalPct = totalPct, UserPct = userPct, SystemPct = sysPct,
            PerfPct = perf, EffPct = eff, PerfCores = pCount, EffCores = eCount,
            PhysicalCores = physicalCores, LogicalProcessors = n, PerCorePct = per, LoadAverage = loadAverage
        };
    }

    /// <summary>Share of total machine CPU a process used between two readings.</summary>
    public static (double Total, double User, double Kernel) ProcessPct(ProcTimes prev, ProcTimes cur, long elapsed100ns, int logicalProcessors)
    {
        if (elapsed100ns <= 0 || logicalProcessors <= 0) return (0, 0, 0);
        double cap = (double)elapsed100ns * logicalProcessors;
        double u = Math.Max(0, cur.User - prev.User) * 100 / cap;
        double k = Math.Max(0, cur.Kernel - prev.Kernel) * 100 / cap;
        return (Math.Min(100, u + k), u, k);
    }
}

/// <summary>
/// Exponentially damped one-minute load average, the way Unix computes it,
/// fed with busy CPUs plus threads waiting in the ready queue.
/// </summary>
public sealed class LoadAverage
{
    double? _value;
    public double? Value => _value;

    public double Update(double runnable, TimeSpan interval)
    {
        double a = Math.Exp(-interval.TotalSeconds / 60.0);
        _value = _value is double v ? v * a + runnable * (1 - a) : runnable;
        return _value.Value;
    }
}

/// <summary>
/// Raises a hog alert when one app stays above a share of total CPU for a
/// set time (50% for 2 min by default), and clears it once the app drops below.
/// </summary>
public sealed class HogDetector(double thresholdPct = 50, TimeSpan? hold = null)
{
    readonly Dictionary<string, DateTimeOffset> _since = new(StringComparer.OrdinalIgnoreCase);

    public double ThresholdPct { get; } = thresholdPct;
    public TimeSpan Hold { get; } = hold ?? TimeSpan.FromMinutes(2);

    /// <summary>Feeds one reading; returns the hog app, if any.</summary>
    public AppSample? Update(DateTimeOffset at, IReadOnlyList<AppSample> apps)
    {
        var seen = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        AppSample? hog = null;
        foreach (var a in apps)
        {
            if (a.CpuPct <= ThresholdPct) continue;
            seen.Add(a.Id);
            if (!_since.TryGetValue(a.Id, out var start)) _since[a.Id] = start = at;
            if (at - start >= Hold && (hog is null || a.CpuPct > hog.CpuPct)) hog = a;
        }
        foreach (var id in _since.Keys.Where(k => !seen.Contains(k)).ToList()) _since.Remove(id);
        return hog;
    }
}

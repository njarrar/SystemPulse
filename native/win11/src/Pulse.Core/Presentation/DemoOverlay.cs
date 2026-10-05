using Pulse.Core.Models;

namespace Pulse.Core.Presentation;

/// <summary>
/// The Settings switches "Simulate CPU hog" and "Charging" lay demo values over
/// the real snapshot. Everything else stays real. The simulated hog is not a
/// real process: ending it only turns the simulation off.
/// </summary>
public static class DemoOverlay
{
    public const string HogId = "pulse-demo-hog";
    public const string HogName = "Antimalware Service";
    public const double HogPct = 52;
    public const double ChargeWatts = 48;
    public static readonly TimeSpan FullIn = TimeSpan.FromMinutes(52);
    public const string Adapter = "USB-C 96 W";

    public static bool IsDemo(AppSample a) => a.Id == HogId;

    public static Snapshot Apply(Snapshot s, bool hog, bool charging)
    {
        if (!hog && !charging) return s;
        var r = s;
        if (hog)
        {
            var app = new AppSample
            {
                Id = HogId, Name = HogName, Pids = [], CpuPct = HogPct, UserCpuPct = HogPct * 0.8, KernelCpuPct = HogPct * 0.2,
                MemBytes = 420UL << 20, Threads = 52, Background = true
            };
            double total = Math.Min(100, s.Cpu.TotalPct + HogPct);
            double add = total - s.Cpu.TotalPct;
            r = r with
            {
                Apps = [.. s.Apps, app],
                Cpu = s.Cpu with
                {
                    TotalPct = total,
                    UserPct = s.Cpu.UserPct + add * 0.8,
                    SystemPct = s.Cpu.SystemPct + add * 0.2,
                    PerfPct = Math.Min(100, s.Cpu.PerfPct + add * 1.2),
                    EffPct = s.Cpu.EffPct is double e ? Math.Min(100, e + add * 0.6) : null
                },
                Thermal = s.Thermal with { CpuC = Math.Max(s.Thermal.CpuC ?? 0, 52) }
            };
        }
        if (charging)
        {
            var p = r.Power;
            r = r with
            {
                Power = p with
                {
                    HasBattery = true, OnAc = true, Charging = true, RateWatts = ChargeWatts,
                    PercentRemaining = p.PercentRemaining ?? 84, TimeToFull = FullIn, TimeLeft = null
                }
            };
        }
        return r;
    }
}

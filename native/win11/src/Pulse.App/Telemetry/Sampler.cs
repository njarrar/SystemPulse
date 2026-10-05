using Pulse.App.Interop;
using Pulse.Core.History;
using Pulse.Core.Models;
using Pulse.Core.Presentation;

namespace Pulse.App.Telemetry;

/// <summary>Sparklines (60 one-second points) and 10-minute timelines (121 five-second points).</summary>
public sealed class HistoryStore
{
    public const int SparkPoints = 60, TimelinePoints = 121;
    static readonly TimeSpan Bucket = TimeSpan.FromSeconds(5);

    readonly Dictionary<string, RingSeries> _spark = new(StringComparer.Ordinal);
    readonly Dictionary<string, Timeline> _timeline = new(StringComparer.Ordinal);
    readonly object _lock = new();

    public void Add(Snapshot s, IEnumerable<AppSample> trackedApps)
    {
        lock (_lock)
        {
            Push(s.At, "cpu", s.Cpu.TotalPct);
            Push(s.At, "mem", s.Memory.LoadPct);
            Push(s.At, "nrg", Math.Abs(s.Power.RateWatts ?? 0));
            Push(s.At, "thm", s.Thermal.CpuC ?? 0);
            Push(s.At, "gpu", s.Gpu.UtilPct);
            Push(s.At, "ssd.r", s.Disk.ReadBps);
            Push(s.At, "ssd.w", s.Disk.WriteBps);
            Push(s.At, "net.d", s.Net.DownBps);
            Push(s.At, "net.u", s.Net.UpBps);
            foreach (var a in trackedApps) Push(s.At, "app:" + a.Id, a.CpuPct);
        }
    }

    void Push(DateTimeOffset at, string key, double v)
    {
        if (!_spark.TryGetValue(key, out var r)) _spark[key] = r = new RingSeries(SparkPoints);
        r.Push(v);
        if (!_timeline.TryGetValue(key, out var t)) _timeline[key] = t = new Timeline(Bucket, TimelinePoints);
        t.Add(at, v);
    }

    public double[] Spark(string key)
    {
        lock (_lock) return _spark.TryGetValue(key, out var r) ? r.ToArray() : [];
    }

    public double[] Timeline(string key)
    {
        lock (_lock) return _timeline.TryGetValue(key, out var t) ? t.Points() : [];
    }
}

/// <summary>
/// Reads every sensor once a second on a background thread and hands each
/// snapshot to the UI thread. A failing reader leaves its part empty and
/// never stops the others.
/// </summary>
public sealed class Sampler : IDisposable
{
    /// <summary>Polling period from the spec: every 1.5 s.</summary>
    public static readonly TimeSpan Interval = TimeSpan.FromSeconds(1.5);

    readonly PdhSet _pdh = new();
    readonly CpuReader _cpu;
    readonly ProcessReader _proc;
    readonly ThermalReader _thermal;
    readonly PowerReader _power = new();
    readonly GpuReader _gpu = new();
    readonly DiskReader _disk = new();
    readonly NetReader _net = new();
    readonly HogDetector _hog = new();
    readonly CancellationTokenSource _stop = new();
    Snapshot _last = new(), _raw = new();
    volatile bool _paused, _demoHog, _demoCharging;

    public HistoryStore History { get; } = new();
    public Snapshot Last => _last;
    public AppSample? Hog { get; private set; }
    public event Action<Snapshot>? Sampled;

    public Sampler()
    {
        _cpu = new CpuReader(_pdh);
        _proc = new ProcessReader(_pdh);
        _thermal = new ThermalReader(_pdh);
    }

    public bool Paused { get => _paused; set => _paused = value; }

    /// <summary>Settings demo switches, laid over real readings (see DemoOverlay).</summary>
    public bool DemoHog { get => _demoHog; set { _demoHog = value; Refresh(); } }
    public bool DemoCharging { get => _demoCharging; set { _demoCharging = value; Refresh(); } }

    /// <summary>Re-applies the demo overlay to the last real reading and notifies listeners, without a new sample.</summary>
    public void Refresh()
    {
        var snap = DemoOverlay.Apply(_raw, _demoHog, _demoCharging);
        _last = snap;
        Hog = snap.Apps.FirstOrDefault(DemoOverlay.IsDemo) ?? _realHog;
        Sampled?.Invoke(snap);
    }

    AppSample? _realHog;

    public void Start()
    {
        var thread = new Thread(Loop) { IsBackground = true, Name = "Pulse sampler", Priority = ThreadPriority.BelowNormal };
        thread.Start();
    }

    void Loop()
    {
        _pdh.Collect(); // PDH rates need two collections
        using var timer = new PeriodicTimer(Interval);
        do
        {
            if (_paused) continue;
            try { Tick(); }
            catch (Exception) { /* keep sampling */ }
        } while (!_stop.IsCancellationRequested && timer.WaitForNextTickAsync(_stop.Token).AsTask().GetAwaiter().GetResult());
    }

    void Tick()
    {
        _pdh.Collect();
        var at = DateTimeOffset.Now;
        var cpu = Try(() => _cpu.Read(_pdh, Interval), new CpuSample());
        var apps = Try(() => _proc.Read(_pdh, Math.Max(1, cpu.LogicalProcessors)), (IReadOnlyList<AppSample>)[]);
        var snap = new Snapshot
        {
            At = at,
            Cpu = cpu,
            Apps = apps,
            Memory = Try(() => MemoryReader.Read(_proc.CompressedBytes), new MemorySample()),
            Power = Try(_power.Read, new PowerSample()),
            Thermal = Try(() => _thermal.Read(_pdh), new ThermalSample()),
            Gpu = Try(() => _gpu.Read(_pdh), new GpuSample()),
            Disk = Try(_disk.Read, new DiskSample()),
            Net = Try(_net.Read, new NetSample())
        };
        _raw = snap;
        _realHog = _hog.Update(at, apps);
        snap = DemoOverlay.Apply(snap, _demoHog, _demoCharging);
        Hog = snap.Apps.FirstOrDefault(DemoOverlay.IsDemo) ?? _realHog; // the demo hog shows at once
        var tracked = snap.Apps.OrderByDescending(a => a.CpuPct).Take(24);
        History.Add(snap, tracked);
        _last = snap;
        Sampled?.Invoke(snap);
    }

    static T Try<T>(Func<T> read, T fallback)
    {
        try { return read(); }
        catch (Exception) { return fallback; }
    }

    public void Dispose()
    {
        _stop.Cancel();
        _gpu.Dispose();
        _net.Dispose();
        _pdh.Dispose();
    }
}

public static class ProcessKiller
{
    /// <summary>
    /// TerminateProcess on every PID of an app. Returns how many ended, how many
    /// refused (protected or elevated), and the image paths, for relaunching later.
    /// </summary>
    public static (int Ended, int Refused, List<string> Paths) End(IEnumerable<int> pids)
    {
        int ok = 0, refused = 0;
        var paths = new List<string>();
        foreach (var pid in pids)
        {
            if (Win32.ProcessImagePath(pid) is string path && !paths.Contains(path, StringComparer.OrdinalIgnoreCase)) paths.Add(path);
            nint h = Win32.OpenProcess(Win32.PROCESS_TERMINATE, false, (uint)pid);
            if (h == 0) { refused++; continue; }
            try { if (Win32.TerminateProcess(h, 1)) ok++; else refused++; }
            finally { Win32.CloseHandle(h); }
        }
        return (ok, refused, paths);
    }
}

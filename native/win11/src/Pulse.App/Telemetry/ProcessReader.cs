using System.Diagnostics;
using System.Globalization;
using System.Runtime.InteropServices;
using Pulse.App.Interop;
using Pulse.Core.History;
using Pulse.Core.Models;

namespace Pulse.App.Telemetry;

/// <summary>
/// Every process from one NtQuerySystemInformation(SystemProcessInformation)
/// call, grouped by image name the way Task Manager groups apps. CPU share
/// comes from kernel and user time deltas; memory is the private working set;
/// GPU share comes from the "GPU Engine" counters (busiest engine per process).
/// </summary>
public sealed unsafe class ProcessReader
{
    public const string GpuEngineCounter = @"\GPU Engine(*)\Utilization Percentage";
    const string MemoryCompression = "Memory Compression";

    readonly Dictionary<(int Pid, long Created), ProcTimes> _prev = new();
    readonly Dictionary<string, string> _displayNames = new(StringComparer.OrdinalIgnoreCase);
    HashSet<int> _windowed = [];
    long _lastStamp;
    uint _cap;
    int _tick;

    public ProcessReader(PdhSet pdh) => pdh.Add(GpuEngineCounter);

    public ulong CompressedBytes { get; private set; }

    public IReadOnlyList<AppSample> Read(PdhSet pdh, int logicalProcessors)
    {
        long stamp = Stopwatch.GetTimestamp();
        long elapsed100ns = _lastStamp == 0 ? 0 : (long)((stamp - _lastStamp) * (10_000_000.0 / Stopwatch.Frequency));
        _lastStamp = stamp;
        if (_tick++ % 5 == 0) _windowed = WindowedPids();
        var gpu = GpuByPid(pdh);

        byte* buf = NtDll.Query(NtDll.SystemProcessInformation, ref _cap, out _);
        if (buf == null) return [];
        var groups = new Dictionary<string, Group>(StringComparer.OrdinalIgnoreCase);
        var seen = new HashSet<(int, long)>();
        try
        {
            uint off = 0;
            while (true)
            {
                var p = (SYSTEM_PROCESS_INFORMATION*)(buf + off);
                int pid = (int)p->UniqueProcessId;
                if (pid != 0)
                {
                    string image = p->ImageName.Buffer == null ? "System" : new string(p->ImageName.Buffer, 0, p->ImageName.Length / 2);
                    var key = (pid, p->CreateTime);
                    seen.Add(key);
                    var cur = new ProcTimes(p->KernelTime, p->UserTime);
                    var share = _prev.TryGetValue(key, out var before) && elapsed100ns > 0
                        ? CpuMath.ProcessPct(before, cur, elapsed100ns, logicalProcessors)
                        : (0, 0, 0);
                    _prev[key] = cur;

                    if (image.Equals(MemoryCompression, StringComparison.OrdinalIgnoreCase)) CompressedBytes = p->WorkingSetSize;
                    string id = image.EndsWith(".exe", StringComparison.OrdinalIgnoreCase) ? image[..^4] : image;
                    if (!groups.TryGetValue(id, out var g)) groups[id] = g = new Group(id);
                    g.Pids.Add(pid);
                    g.Cpu += share.Item1; g.User += share.Item2; g.Kernel += share.Item3;
                    g.Mem += (ulong)Math.Max(0, p->WorkingSetPrivateSize);
                    g.Threads += (int)p->NumberOfThreads;
                    g.Gpu = Math.Max(g.Gpu, gpu.GetValueOrDefault(pid));
                    g.Windowed |= _windowed.Contains(pid);
                }
                if (p->NextEntryOffset == 0) break;
                off += p->NextEntryOffset;
            }
        }
        finally { NativeMemory.Free(buf); }

        foreach (var k in _prev.Keys.Where(k => !seen.Contains(k)).ToList()) _prev.Remove(k);

        int lookups = 0;
        var list = new List<AppSample>(groups.Count);
        foreach (var g in groups.Values)
        {
            g.Pids.Sort();
            list.Add(new AppSample
            {
                Id = g.Id,
                Name = DisplayName(g, ref lookups),
                Pids = g.Pids.ToArray(),
                CpuPct = g.Cpu, UserCpuPct = g.User, KernelCpuPct = g.Kernel,
                MemBytes = g.Mem, GpuPct = Math.Min(100, g.Gpu), Threads = g.Threads,
                Background = !g.Windowed
            });
        }
        return list;
    }

    /// <summary>FileDescription from the image's version info ("Microsoft Edge"), cached; at most a few new lookups per tick.</summary>
    string DisplayName(Group g, ref int lookups)
    {
        if (_displayNames.TryGetValue(g.Id, out var name)) return name;
        if (lookups++ > 8) return g.Id;
        name = g.Id;
        try
        {
            foreach (var pid in g.Pids)
            {
                var path = Win32.ProcessImagePath(pid);
                if (path is null) continue;
                var desc = FileVersionInfo.GetVersionInfo(path).FileDescription?.Trim();
                if (!string.IsNullOrEmpty(desc) && desc.Length <= 60) name = desc;
                break;
            }
        }
        catch (Exception) { }
        _displayNames[g.Id] = name;
        return name;
    }

    /// <summary>Busiest GPU engine per process, from instance names like "pid_1234_luid_..._engtype_3D".</summary>
    static Dictionary<int, double> GpuByPid(PdhSet pdh)
    {
        var perEngine = new Dictionary<(int, string), double>();
        foreach (var (name, value) in pdh.Array(GpuEngineCounter))
        {
            if (!name.StartsWith("pid_", StringComparison.Ordinal)) continue;
            int us = name.IndexOf('_', 4);
            if (us < 0 || !int.TryParse(name.AsSpan(4, us - 4), NumberStyles.None, CultureInfo.InvariantCulture, out int pid)) continue;
            int et = name.IndexOf("engtype_", StringComparison.Ordinal);
            string engine = et >= 0 ? name[(et + 8)..] : "";
            perEngine[(pid, engine)] = perEngine.GetValueOrDefault((pid, engine)) + value;
        }
        var result = new Dictionary<int, double>();
        foreach (var ((pid, _), v) in perEngine) result[pid] = Math.Max(result.GetValueOrDefault(pid), v);
        return result;
    }

    // Processes with a visible, unowned, titled top-level window count as apps;
    // everything else is a background process.
    static HashSet<int>? s_collect;

    [UnmanagedCallersOnly]
    static int EnumProc(nint hwnd, nint _)
    {
        if (Win32.IsWindowVisible(hwnd) && Win32.GetWindow(hwnd, 4 /* GW_OWNER */) == 0 && Win32.GetWindowTextLengthW(hwnd) > 0)
        {
            uint pid;
            Win32.GetWindowThreadProcessId(hwnd, &pid);
            s_collect?.Add((int)pid);
        }
        return 1;
    }

    static HashSet<int> WindowedPids()
    {
        var set = new HashSet<int>();
        s_collect = set;
        try { Win32.EnumWindows(&EnumProc, 0); }
        finally { s_collect = null; }
        return set;
    }

    sealed class Group(string id)
    {
        public string Id = id;
        public List<int> Pids = [];
        public double Cpu, User, Kernel, Gpu;
        public ulong Mem;
        public int Threads;
        public bool Windowed;
    }
}

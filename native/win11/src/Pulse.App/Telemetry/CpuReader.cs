using System.Runtime.InteropServices;
using Pulse.App.Interop;
using Pulse.Core.History;
using Pulse.Core.Models;

namespace Pulse.App.Telemetry;

/// <summary>
/// Per-core load from NtQuerySystemInformation(SystemProcessorPerformanceInformation),
/// P/E core classes from GetSystemCpuSetInformation's EfficiencyClass, and a
/// one-minute load average from busy CPUs plus the ready queue.
/// Reads processor group 0 (up to 64 logical processors).
/// </summary>
public sealed unsafe class CpuReader
{
    public const string QueueCounter = @"\System\Processor Queue Length";

    readonly byte[] _effClass;
    readonly int _physicalCores;
    readonly LoadAverage _load = new();
    CoreTimes[]? _prev;
    uint _cap;

    public CpuReader(PdhSet pdh)
    {
        (_effClass, _physicalCores) = ReadCpuSets();
        pdh.Add(QueueCounter);
    }

    static (byte[] Eff, int Cores) ReadCpuSets()
    {
        uint len = 0;
        Win32.GetSystemCpuSetInformation(null, 0, &len, 0, 0);
        if (len == 0) return ([], Environment.ProcessorCount);
        byte* buf = (byte*)NativeMemory.Alloc(len);
        try
        {
            if (!Win32.GetSystemCpuSetInformation(buf, len, &len, 0, 0)) return ([], Environment.ProcessorCount);
            var eff = new Dictionary<int, byte>();
            var cores = new HashSet<(ushort, byte)>();
            for (uint off = 0; off < len;)
            {
                uint size = *(uint*)(buf + off);
                if (size == 0) break;
                int type = *(int*)(buf + off + 4);
                if (type == 0) // CpuSetInformation
                {
                    ushort group = *(ushort*)(buf + off + 12);
                    byte lp = buf[off + 14], core = buf[off + 15], ec = buf[off + 18];
                    cores.Add((group, core));
                    if (group == 0) eff[lp] = ec;
                }
                off += size;
            }
            var arr = new byte[eff.Count == 0 ? 0 : eff.Keys.Max() + 1];
            foreach (var (lp, ec) in eff) arr[lp] = ec;
            return (arr, Math.Max(1, cores.Count));
        }
        finally { NativeMemory.Free(buf); }
    }

    public CpuSample Read(PdhSet pdh, TimeSpan interval)
    {
        byte* buf = NtDll.Query(NtDll.SystemProcessorPerformanceInformation, ref _cap, out uint len);
        if (buf == null) return new CpuSample();
        CoreTimes[] cur;
        try
        {
            int n = (int)(len / (uint)sizeof(SYSTEM_PROCESSOR_PERFORMANCE_INFORMATION));
            var p = (SYSTEM_PROCESSOR_PERFORMANCE_INFORMATION*)buf;
            cur = new CoreTimes[n];
            for (int i = 0; i < n; i++) cur[i] = new CoreTimes(p[i].IdleTime, p[i].KernelTime, p[i].UserTime);
        }
        finally { NativeMemory.Free(buf); }

        var prev = _prev ?? cur;
        _prev = cur;
        var sample = CpuMath.Compute(prev, cur, _effClass, _physicalCores);
        double queue = pdh.Value(QueueCounter) ?? 0;
        double load = _load.Update(sample.TotalPct / 100 * sample.LogicalProcessors + queue, interval);
        return sample with { LoadAverage = load };
    }
}

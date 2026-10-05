using System.Diagnostics;
using Pulse.App.Interop;
using Pulse.Core.Models;

namespace Pulse.App.Telemetry;

/// <summary>
/// The primary hardware adapter. DXGI gives its name and IDXGIAdapter3::QueryVideoMemoryInfo
/// its memory use; D3DKMTQueryStatistics gives per-node running time, from which
/// utilization is the busiest node's share of wall time (the method Task Manager
/// uses). D3DKMTQueryAdapterInfo adds temperature, fan and power on WDDM 2.4+
/// drivers that report them. If the node query fails, utilization falls back to
/// the "GPU Engine" counters.
/// </summary>
public sealed unsafe class GpuReader : IDisposable
{
    void* _factory;
    void* _adapter3;
    uint _kmt;
    LUID _luid;
    uint _nodes;
    long[] _prevRun = [];
    long _prevStamp;
    bool _nodeQueryWorks = true;

    public string Name { get; } = "";
    public bool Integrated { get; }

    public GpuReader()
    {
        try
        {
            Guid iid = Gpu.IID_IDXGIFactory1;
            void* f;
            if (Gpu.CreateDXGIFactory1(&iid, &f) < 0) return;
            _factory = f;
            for (uint i = 0; ; i++)
            {
                void* a;
                if (Gpu.EnumAdapters1(_factory, i, &a) < 0) break;
                DXGI_ADAPTER_DESC1 d;
                if (Gpu.GetDesc1(a, &d) >= 0 && (d.Flags & Gpu.DXGI_ADAPTER_FLAG_SOFTWARE) == 0)
                {
                    Name = Win32.FromFixed(d.Description, 128).Trim();
                    Integrated = d.DedicatedVideoMemory < 512UL * 1024 * 1024;
                    _luid = d.AdapterLuid;
                    void* a3;
                    if (Gpu.QueryInterface(a, Gpu.IID_IDXGIAdapter3, &a3) >= 0) _adapter3 = a3;
                    Gpu.Release(a);
                    break;
                }
                Gpu.Release(a);
            }
            if (Name.Length == 0) return;

            var open = new D3DKMT_OPENADAPTERFROMLUID { AdapterLuid = _luid };
            if (Gpu.D3DKMTOpenAdapterFromLuid(&open) >= 0) _kmt = open.hAdapter;

            byte* q = stackalloc byte[Gpu.QS_SIZE];
            new Span<byte>(q, Gpu.QS_SIZE).Clear();
            *(int*)(q + Gpu.QS_TYPE) = Gpu.D3DKMT_QUERYSTATISTICS_ADAPTER;
            *(LUID*)(q + Gpu.QS_LUID) = _luid;
            if (Gpu.D3DKMTQueryStatistics(q) >= 0) _nodes = *(uint*)(q + Gpu.QS_RESULT + 4); // ADAPTER_INFORMATION.NodeCount
            _prevRun = new long[_nodes];
        }
        catch (Exception) { }
    }

    public GpuSample Read(PdhSet pdh)
    {
        if (Name.Length == 0) return new GpuSample();
        double util = NodeUtilization() ?? EngineUtilization(pdh);

        ulong dedUsed = 0, dedBudget = 0, shUsed = 0, shBudget = 0;
        if (_adapter3 != null)
        {
            DXGI_QUERY_VIDEO_MEMORY_INFO local, nonLocal;
            if (Gpu.QueryVideoMemoryInfo(_adapter3, 0, 0, &local) >= 0) { dedUsed = local.CurrentUsage; dedBudget = local.Budget; }
            if (Gpu.QueryVideoMemoryInfo(_adapter3, 0, 1, &nonLocal) >= 0) { shUsed = nonLocal.CurrentUsage; shBudget = nonLocal.Budget; }
        }
        if (Integrated) { shUsed += dedUsed; shBudget = Math.Max(shBudget, dedBudget); dedUsed = dedBudget = 0; }

        double? temp = null, fan = null, power = null, clock = null;
        if (_kmt != 0)
        {
            D3DKMT_ADAPTER_PERFDATA perf = default;
            var qa = new D3DKMT_QUERYADAPTERINFO { hAdapter = _kmt, Type = Gpu.KMTQAITYPE_ADAPTERPERFDATA, pPrivateDriverData = &perf, PrivateDriverDataSize = (uint)sizeof(D3DKMT_ADAPTER_PERFDATA) };
            if (Gpu.D3DKMTQueryAdapterInfo(&qa) >= 0)
            {
                if (perf.Temperature is > 0 and < 1500) temp = perf.Temperature / 10.0;
                if (perf.FanRPM is > 0 and < 20000) fan = perf.FanRPM;
                if (perf.Power is > 0 and <= 1000) power = perf.Power / 10.0;
            }
            D3DKMT_NODE_PERFDATA node = default; // node 0 is the 3D engine on most adapters
            var qn = new D3DKMT_QUERYADAPTERINFO { hAdapter = _kmt, Type = Gpu.KMTQAITYPE_NODEPERFDATA, pPrivateDriverData = &node, PrivateDriverDataSize = (uint)sizeof(D3DKMT_NODE_PERFDATA) };
            if (Gpu.D3DKMTQueryAdapterInfo(&qn) >= 0 && node.Frequency is > 1_000_000 and < 10_000_000_000) clock = node.Frequency;
        }

        return new GpuSample
        {
            Name = Name, Integrated = Integrated, UtilPct = Math.Clamp(util, 0, 100), TempC = temp, FanRpm = fan, PowerPct = power, ClockHz = clock,
            DedicatedUsed = dedUsed, DedicatedBudget = dedBudget, SharedUsed = shUsed, SharedBudget = shBudget
        };
    }

    double? NodeUtilization()
    {
        if (!_nodeQueryWorks || _nodes == 0) return null;
        long now = Stopwatch.GetTimestamp();
        double wall100ns = _prevStamp == 0 ? 0 : (now - _prevStamp) * (10_000_000.0 / Stopwatch.Frequency);
        _prevStamp = now;
        byte* q = stackalloc byte[Gpu.QS_SIZE];
        double best = 0;
        bool any = false;
        for (uint n = 0; n < _nodes; n++)
        {
            new Span<byte>(q, Gpu.QS_SIZE).Clear();
            *(int*)(q + Gpu.QS_TYPE) = Gpu.D3DKMT_QUERYSTATISTICS_NODE;
            *(LUID*)(q + Gpu.QS_LUID) = _luid;
            *(uint*)(q + Gpu.QS_QUERY) = n; // QueryNode.NodeId
            if (Gpu.D3DKMTQueryStatistics(q) < 0) continue;
            any = true;
            long run = *(long*)(q + Gpu.QS_RESULT); // NodeInformation.GlobalInformation.RunningTime
            if (wall100ns > 0 && run >= _prevRun[n]) best = Math.Max(best, (run - _prevRun[n]) * 100.0 / wall100ns);
            _prevRun[n] = run;
        }
        if (!any) { _nodeQueryWorks = false; return null; }
        return best <= 100.5 ? best : null; // a wrong layout would give nonsense: use the counters instead
    }

    double EngineUtilization(PdhSet pdh)
    {
        // Sum per engine type across processes, then the busiest engine type.
        string luid = $"luid_0x{(uint)_luid.HighPart:X8}_0x{_luid.LowPart:X8}";
        var perEngine = new Dictionary<string, double>(StringComparer.Ordinal);
        foreach (var (name, v) in pdh.Array(ProcessReader.GpuEngineCounter))
        {
            if (!name.Contains(luid, StringComparison.OrdinalIgnoreCase)) continue;
            int et = name.IndexOf("engtype_", StringComparison.Ordinal);
            string engine = et >= 0 ? name[(et + 8)..] : "";
            perEngine[engine] = perEngine.GetValueOrDefault(engine) + v;
        }
        return perEngine.Count == 0 ? 0 : perEngine.Values.Max();
    }

    public void Dispose()
    {
        if (_kmt != 0) { var c = new D3DKMT_CLOSEADAPTER { hAdapter = _kmt }; Gpu.D3DKMTCloseAdapter(&c); _kmt = 0; }
        Gpu.Release(_adapter3); _adapter3 = null;
        Gpu.Release(_factory); _factory = null;
    }
}

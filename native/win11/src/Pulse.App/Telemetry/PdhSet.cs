using System.Runtime.InteropServices;
using Pulse.App.Interop;

namespace Pulse.App.Telemetry;

/// <summary>
/// One PDH query holding every counter Pulse reads, collected once per tick.
/// Counter paths use English names (PdhAddEnglishCounterW), so they work on
/// any display language. Missing counters are skipped.
/// </summary>
public sealed unsafe class PdhSet : IDisposable
{
    nint _query;
    readonly Dictionary<string, nint> _counters = new(StringComparer.Ordinal);

    public PdhSet()
    {
        nint q;
        if (Pdh.PdhOpenQueryW(null, 0, &q) == 0) _query = q;
    }

    public bool Add(string path)
    {
        if (_query == 0 || _counters.ContainsKey(path)) return _counters.ContainsKey(path);
        nint c;
        if (Pdh.PdhAddEnglishCounterW(_query, path, 0, &c) != 0) return false;
        _counters[path] = c;
        return true;
    }

    public void Collect()
    {
        if (_query != 0) Pdh.PdhCollectQueryData(_query);
    }

    public double? Value(string path)
    {
        if (!_counters.TryGetValue(path, out var c)) return null;
        uint type;
        PDH_FMT_COUNTERVALUE v;
        if (Pdh.PdhGetFormattedCounterValue(c, Pdh.PDH_FMT_DOUBLE | Pdh.PDH_FMT_NOCAP100, &type, &v) != 0 || v.CStatus > 1) return null;
        return v.DoubleValue;
    }

    /// <summary>Every instance of a wildcard counter as (instance name, value).</summary>
    public List<(string Name, double Value)> Array(string path)
    {
        var list = new List<(string, double)>();
        if (!_counters.TryGetValue(path, out var c)) return list;
        uint size = 0, count = 0;
        int st = Pdh.PdhGetFormattedCounterArrayW(c, Pdh.PDH_FMT_DOUBLE | Pdh.PDH_FMT_NOCAP100, &size, &count, null);
        if (st != Pdh.PDH_MORE_DATA || size == 0) return list;
        byte* buf = (byte*)NativeMemory.Alloc(size);
        try
        {
            if (Pdh.PdhGetFormattedCounterArrayW(c, Pdh.PDH_FMT_DOUBLE | Pdh.PDH_FMT_NOCAP100, &size, &count, buf) != 0) return list;
            var items = (PDH_FMT_COUNTERVALUE_ITEM_W*)buf;
            for (uint i = 0; i < count; i++)
            {
                if (items[i].FmtValue.CStatus > 1) continue;
                list.Add((new string(items[i].szName), items[i].FmtValue.DoubleValue));
            }
        }
        finally { NativeMemory.Free(buf); }
        return list;
    }

    public void Dispose()
    {
        if (_query != 0) Pdh.PdhCloseQuery(_query);
        _query = 0;
    }
}

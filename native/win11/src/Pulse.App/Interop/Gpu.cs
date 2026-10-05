using System.Runtime.InteropServices;

namespace Pulse.App.Interop;

[StructLayout(LayoutKind.Sequential)]
public struct LUID { public uint LowPart; public int HighPart; }

[StructLayout(LayoutKind.Sequential)]
public unsafe struct DXGI_ADAPTER_DESC1
{
    public fixed char Description[128];
    public uint VendorId, DeviceId, SubSysId, Revision;
    public nuint DedicatedVideoMemory, DedicatedSystemMemory, SharedSystemMemory;
    public LUID AdapterLuid;
    public uint Flags;
}

[StructLayout(LayoutKind.Sequential)]
public struct DXGI_QUERY_VIDEO_MEMORY_INFO
{
    public ulong Budget, CurrentUsage, AvailableForReservation, CurrentReservation;
}

[StructLayout(LayoutKind.Sequential)]
public struct D3DKMT_OPENADAPTERFROMLUID
{
    public LUID AdapterLuid;
    public uint hAdapter;
}

[StructLayout(LayoutKind.Sequential)]
public unsafe struct D3DKMT_QUERYADAPTERINFO
{
    public uint hAdapter;
    public int Type;
    public void* pPrivateDriverData;
    public uint PrivateDriverDataSize;
}

[StructLayout(LayoutKind.Sequential)]
public struct D3DKMT_CLOSEADAPTER { public uint hAdapter; }

/// <summary>D3DKMT_ADAPTER_PERFDATA (WDDM 2.4+). Temperature in deci-°C, Power in tenths of a percent.</summary>
[StructLayout(LayoutKind.Sequential)]
public struct D3DKMT_ADAPTER_PERFDATA
{
    public uint PhysicalAdapterIndex;
    public ulong MemoryFrequency, MaxMemoryFrequency, MaxMemoryFrequencyOC, MemoryBandwidth, PCIEBandwidth;
    public uint FanRPM, Power, Temperature;
    public byte PowerStateOverride;
}

/// <summary>D3DKMT_NODE_PERFDATA (WDDM 2.4+). Frequencies in Hz.</summary>
[StructLayout(LayoutKind.Sequential)]
public struct D3DKMT_NODE_PERFDATA
{
    public uint NodeOrdinal, PhysicalAdapterIndex;
    public ulong Frequency, MaxFrequency, MaxFrequencyOC;
    public uint Voltage, VoltageMax, VoltageMaxOC;
    public ulong MaxTransitionLatency;
}

public static unsafe partial class Gpu
{
    public static readonly Guid IID_IDXGIFactory1 = new("770aae78-f26f-4dba-a829-253c83d1b387");
    public static readonly Guid IID_IDXGIAdapter3 = new("645967a4-1392-4310-a798-8053ce3e93fd");
    public const uint DXGI_ADAPTER_FLAG_SOFTWARE = 2;
    public const int DXGI_ERROR_NOT_FOUND = unchecked((int)0x887A0002);

    // D3DKMT_QUERYSTATISTICS: Type (0), AdapterLuid (4), hProcess (8 bytes at 0x10), the
    // result union at 0x18, and the query parameters (QueryNode.NodeId) at 0x320.
    // The struct is 0x328 bytes on 64-bit Windows; the buffer below is larger to be safe.
    public const int QS_SIZE = 0x400, QS_TYPE = 0x0, QS_LUID = 0x4, QS_RESULT = 0x18, QS_QUERY = 0x320;
    public const int D3DKMT_QUERYSTATISTICS_ADAPTER = 0, D3DKMT_QUERYSTATISTICS_NODE = 5;
    public const int KMTQAITYPE_NODEPERFDATA = 61, KMTQAITYPE_ADAPTERPERFDATA = 62;

    [LibraryImport("dxgi.dll")]
    public static partial int CreateDXGIFactory1(Guid* riid, void** factory);

    [LibraryImport("gdi32.dll")]
    public static partial int D3DKMTOpenAdapterFromLuid(D3DKMT_OPENADAPTERFROMLUID* open);

    [LibraryImport("gdi32.dll")]
    public static partial int D3DKMTCloseAdapter(D3DKMT_CLOSEADAPTER* close);

    [LibraryImport("gdi32.dll")]
    public static partial int D3DKMTQueryStatistics(void* stats);

    [LibraryImport("gdi32.dll")]
    public static partial int D3DKMTQueryAdapterInfo(D3DKMT_QUERYADAPTERINFO* info);

    // COM through the vtable, so Native AOT needs no COM interop.
    static void** Vtbl(void* obj) => *(void***)obj;

    public static uint Release(void* obj) =>
        obj == null ? 0 : ((delegate* unmanaged[Stdcall]<void*, uint>)Vtbl(obj)[2])(obj);

    public static int QueryInterface(void* obj, Guid iid, void** result) =>
        ((delegate* unmanaged[Stdcall]<void*, Guid*, void**, int>)Vtbl(obj)[0])(obj, &iid, result);

    /// <summary>IDXGIFactory1::EnumAdapters1 (vtable slot 12).</summary>
    public static int EnumAdapters1(void* factory, uint index, void** adapter) =>
        ((delegate* unmanaged[Stdcall]<void*, uint, void**, int>)Vtbl(factory)[12])(factory, index, adapter);

    /// <summary>IDXGIAdapter1::GetDesc1 (vtable slot 10).</summary>
    public static int GetDesc1(void* adapter, DXGI_ADAPTER_DESC1* desc) =>
        ((delegate* unmanaged[Stdcall]<void*, DXGI_ADAPTER_DESC1*, int>)Vtbl(adapter)[10])(adapter, desc);

    /// <summary>IDXGIAdapter3::QueryVideoMemoryInfo (vtable slot 14). Group 0 = local, 1 = non-local.</summary>
    public static int QueryVideoMemoryInfo(void* adapter3, uint node, int group, DXGI_QUERY_VIDEO_MEMORY_INFO* info) =>
        ((delegate* unmanaged[Stdcall]<void*, uint, int, DXGI_QUERY_VIDEO_MEMORY_INFO*, int>)Vtbl(adapter3)[14])(adapter3, node, group, info);
}

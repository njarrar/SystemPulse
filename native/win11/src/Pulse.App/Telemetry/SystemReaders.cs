using System.Diagnostics;
using Pulse.App.Interop;
using Pulse.Core.History;
using Pulse.Core.Models;

namespace Pulse.App.Telemetry;

/// <summary>RAM from GlobalMemoryStatusEx, commit and kernel pools from GetPerformanceInfo.</summary>
public static unsafe class MemoryReader
{
    public static MemorySample Read(ulong compressedBytes)
    {
        var ms = new MEMORYSTATUSEX { dwLength = (uint)sizeof(MEMORYSTATUSEX) };
        if (!Win32.GlobalMemoryStatusEx(&ms)) return new MemorySample();
        var pi = new PERFORMANCE_INFORMATION { cb = (uint)sizeof(PERFORMANCE_INFORMATION) };
        bool havePi = Win32.GetPerformanceInfo(&pi, pi.cb);
        ulong page = havePi ? pi.PageSize : 4096;
        return new MemorySample
        {
            TotalBytes = ms.ullTotalPhys,
            AvailableBytes = ms.ullAvailPhys,
            SystemBytes = havePi ? (ulong)(pi.KernelPaged + pi.KernelNonpaged) * page : 0,
            CompressedBytes = compressedBytes,
            CommitBytes = havePi ? (ulong)pi.CommitTotal * page : ms.ullTotalPageFile - ms.ullAvailPageFile,
            CommitLimitBytes = havePi ? (ulong)pi.CommitLimit * page : ms.ullTotalPageFile
        };
    }
}

/// <summary>
/// ACPI thermal zones through the "Thermal Zone Information" counter set (the
/// WMI Win32_PerfFormattedData_Counters_ThermalZoneInformation provider, read
/// through PDH so no admin rights or COM are needed), plus the processor
/// performance limit. Many firmwares expose only a board or skin sensor here.
/// </summary>
public sealed class ThermalReader
{
    const string HighPrecision = @"\Thermal Zone Information(*)\High Precision Temperature"; // tenths of kelvin
    const string Kelvin = @"\Thermal Zone Information(*)\Temperature";
    const string Passive = @"\Thermal Zone Information(*)\% Passive Limit";
    const string PerfLimit = @"\Processor Information(_Total)\% Performance Limit";

    public ThermalReader(PdhSet pdh)
    {
        pdh.Add(HighPrecision);
        pdh.Add(Kelvin);
        pdh.Add(Passive);
        pdh.Add(PerfLimit);
    }

    public ThermalSample Read(PdhSet pdh)
    {
        double? hottest = null;
        foreach (var (_, v) in pdh.Array(HighPrecision))
            if (v > 0) hottest = Math.Max(hottest ?? double.MinValue, v / 10 - 273.15);
        if (hottest is null)
            foreach (var (_, v) in pdh.Array(Kelvin))
                if (v > 0) hottest = Math.Max(hottest ?? double.MinValue, v - 273.15);
        double? passive = null;
        foreach (var (_, v) in pdh.Array(Passive)) passive = Math.Min(passive ?? 100, v);
        return new ThermalSample
        {
            CpuC = hottest is double c && c > -50 && c < 150 ? c : null,
            PassiveLimitPct = passive,
            PerfLimitPct = pdh.Value(PerfLimit)
        };
    }
}

/// <summary>System drive size from GetDiskFreeSpaceExW, throughput from IOCTL_DISK_PERFORMANCE.</summary>
public sealed unsafe class DiskReader
{
    const uint IOCTL_DISK_PERFORMANCE = 0x00070020;

    readonly string _root;
    readonly string _fs = "";
    readonly RateMeter _read = new(), _write = new();

    public DiskReader()
    {
        string drive = (Environment.GetEnvironmentVariable("SystemDrive") ?? "C:").TrimEnd('\\');
        _root = drive;
        char* fs = stackalloc char[64];
        if (Win32.GetVolumeInformationW(drive + "\\", null, 0, null, null, null, fs, 64)) _fs = new string(fs);
    }

    public DiskSample Read()
    {
        ulong free, total, totalFree;
        if (!Win32.GetDiskFreeSpaceExW(_root + "\\", &free, &total, &totalFree)) { free = total = 0; }
        double rd = 0, wr = 0;
        nint h = Win32.CreateFileW(@"\\.\" + _root, 0, Win32.FILE_SHARE_READ | Win32.FILE_SHARE_WRITE, 0, Win32.OPEN_EXISTING, 0, 0);
        if (h != Win32.INVALID_HANDLE_VALUE)
        {
            try
            {
                // DISK_PERFORMANCE: BytesRead at 0, BytesWritten at 8 (88 bytes in all).
                byte* perf = stackalloc byte[128];
                uint ret;
                if (Win32.DeviceIoControl(h, IOCTL_DISK_PERFORMANCE, null, 0, perf, 128, &ret, 0) && ret >= 16)
                {
                    long now = Stopwatch.GetTimestamp();
                    rd = _read.Update((ulong)*(long*)perf, now, Stopwatch.Frequency);
                    wr = _write.Update((ulong)*(long*)(perf + 8), now, Stopwatch.Frequency);
                }
            }
            finally { Win32.CloseHandle(h); }
        }
        return new DiskSample { Root = _root, FileSystem = _fs, TotalBytes = total, FreeBytes = totalFree, ReadBps = rd, WriteBps = wr };
    }
}

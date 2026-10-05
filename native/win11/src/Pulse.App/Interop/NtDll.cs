using System.Runtime.InteropServices;

namespace Pulse.App.Interop;

/// <summary>SYSTEM_PROCESSOR_PERFORMANCE_INFORMATION (class 8). Times in 100 ns units; kernel time includes idle.</summary>
[StructLayout(LayoutKind.Sequential)]
public struct SYSTEM_PROCESSOR_PERFORMANCE_INFORMATION
{
    public long IdleTime, KernelTime, UserTime, DpcTime, InterruptTime;
    public uint InterruptCount;
}

[StructLayout(LayoutKind.Sequential)]
public unsafe struct UNICODE_STRING
{
    public ushort Length, MaximumLength;
    public char* Buffer;
}

/// <summary>SYSTEM_PROCESS_INFORMATION (class 5), the fixed part before the thread array. Same layout on x64 and ARM64.</summary>
[StructLayout(LayoutKind.Sequential)]
public struct SYSTEM_PROCESS_INFORMATION
{
    public uint NextEntryOffset;
    public uint NumberOfThreads;
    public long WorkingSetPrivateSize;
    public uint HardFaultCount;
    public uint NumberOfThreadsHighWatermark;
    public ulong CycleTime;
    public long CreateTime;
    public long UserTime;
    public long KernelTime;
    public UNICODE_STRING ImageName;
    public int BasePriority;
    public nint UniqueProcessId;
    public nint InheritedFromUniqueProcessId;
    public uint HandleCount;
    public uint SessionId;
    public nuint UniqueProcessKey;
    public nuint PeakVirtualSize;
    public nuint VirtualSize;
    public uint PageFaultCount;
    public nuint PeakWorkingSetSize;
    public nuint WorkingSetSize;
    public nuint QuotaPeakPagedPoolUsage;
    public nuint QuotaPagedPoolUsage;
    public nuint QuotaPeakNonPagedPoolUsage;
    public nuint QuotaNonPagedPoolUsage;
    public nuint PagefileUsage;
    public nuint PeakPagefileUsage;
    public nuint PrivatePageCount;
    public long ReadOperationCount, WriteOperationCount, OtherOperationCount;
    public long ReadTransferCount, WriteTransferCount, OtherTransferCount;
}

public static unsafe partial class NtDll
{
    public const int SystemProcessInformation = 5;
    public const int SystemProcessorPerformanceInformation = 8;
    public const int STATUS_INFO_LENGTH_MISMATCH = unchecked((int)0xC0000004);

    [LibraryImport("ntdll.dll")]
    public static partial int NtQuerySystemInformation(int infoClass, void* info, uint length, uint* returned);

    /// <summary>Calls NtQuerySystemInformation, growing the buffer until it fits. Caller frees with NativeMemory.Free.</summary>
    public static byte* Query(int infoClass, ref uint capacity, out uint length)
    {
        if (capacity == 0) capacity = 256 * 1024;
        while (true)
        {
            byte* buf = (byte*)NativeMemory.Alloc(capacity);
            uint ret = 0;
            int status = NtQuerySystemInformation(infoClass, buf, capacity, &ret);
            if (status == STATUS_INFO_LENGTH_MISMATCH)
            {
                NativeMemory.Free(buf);
                capacity = Math.Max(capacity * 2, ret + 64 * 1024);
                continue;
            }
            if (status < 0)
            {
                NativeMemory.Free(buf);
                length = 0;
                return null;
            }
            length = ret;
            return buf;
        }
    }
}

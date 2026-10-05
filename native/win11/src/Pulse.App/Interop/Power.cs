using System.Runtime.InteropServices;

namespace Pulse.App.Interop;

/// <summary>SYSTEM_BATTERY_STATE from CallNtPowerInformation(SystemBatteryState). Capacities in mWh, rate in mW.</summary>
[StructLayout(LayoutKind.Sequential)]
public unsafe struct SYSTEM_BATTERY_STATE
{
    public byte AcOnLine, BatteryPresent, Charging, Discharging;
    public fixed byte Spare1[3];
    public byte Tag;
    public uint MaxCapacity, RemainingCapacity;
    public int Rate;
    public uint EstimatedTime, DefaultAlert1, DefaultAlert2;
}

[StructLayout(LayoutKind.Sequential)]
public struct BATTERY_QUERY_INFORMATION
{
    public uint BatteryTag;
    public int InformationLevel;
    public int AtRate;
}

[StructLayout(LayoutKind.Sequential)]
public unsafe struct BATTERY_INFORMATION
{
    public uint Capabilities;
    public byte Technology;
    public fixed byte Reserved[3];
    public fixed byte Chemistry[4];
    public uint DesignedCapacity, FullChargedCapacity, DefaultAlert1, DefaultAlert2, CriticalBias, CycleCount;
}

[StructLayout(LayoutKind.Sequential)]
public struct BATTERY_WAIT_STATUS
{
    public uint BatteryTag, Timeout, PowerState, LowCapacity, HighCapacity;
}

[StructLayout(LayoutKind.Sequential)]
public struct BATTERY_STATUS
{
    public uint PowerState, Capacity, Voltage;
    public int Rate;
}

[StructLayout(LayoutKind.Sequential)]
public struct SP_DEVICE_INTERFACE_DATA
{
    public uint cbSize;
    public Guid InterfaceClassGuid;
    public uint Flags;
    public nuint Reserved;
}

public static unsafe partial class Power
{
    public const int SystemBatteryState = 5;
    public const uint IOCTL_BATTERY_QUERY_TAG = 0x294040, IOCTL_BATTERY_QUERY_INFORMATION = 0x294044, IOCTL_BATTERY_QUERY_STATUS = 0x29404C;
    public const int BatteryInformation = 0, BatteryTemperature = 2;
    public const uint BATTERY_CAPACITY_RELATIVE = 0x40000000;
    public const uint BATTERY_POWER_ON_LINE = 1, BATTERY_DISCHARGING = 2, BATTERY_CHARGING = 4;
    public const int BATTERY_UNKNOWN_RATE = unchecked((int)0x80000000);
    public const uint DIGCF_PRESENT = 0x2, DIGCF_DEVICEINTERFACE = 0x10;

    /// <summary>GUID_DEVCLASS_BATTERY, also the battery device interface class.</summary>
    public static readonly Guid GUID_DEVCLASS_BATTERY = new("72631e54-78a4-11d0-bcf7-00aa00b7b32a");

    [LibraryImport("powrprof.dll")]
    public static partial int CallNtPowerInformation(int level, void* input, uint inSize, void* output, uint outSize);

    [LibraryImport("setupapi.dll", SetLastError = true)]
    public static partial nint SetupDiGetClassDevsW(Guid* classGuid, char* enumerator, nint parent, uint flags);

    [LibraryImport("setupapi.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    public static partial bool SetupDiEnumDeviceInterfaces(nint set, void* devInfo, Guid* classGuid, uint index, SP_DEVICE_INTERFACE_DATA* data);

    [LibraryImport("setupapi.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    public static partial bool SetupDiGetDeviceInterfaceDetailW(nint set, SP_DEVICE_INTERFACE_DATA* data, void* detail, uint detailSize, uint* required, void* devInfo);

    [LibraryImport("setupapi.dll")]
    [return: MarshalAs(UnmanagedType.Bool)]
    public static partial bool SetupDiDestroyDeviceInfoList(nint set);

    /// <summary>Device paths of every battery, for the battery IOCTLs.</summary>
    public static List<string> BatteryPaths()
    {
        var paths = new List<string>();
        Guid g = GUID_DEVCLASS_BATTERY;
        nint set = SetupDiGetClassDevsW(&g, null, 0, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
        if (set == Win32.INVALID_HANDLE_VALUE || set == 0) return paths;
        try
        {
            for (uint i = 0; i < 8; i++)
            {
                var did = new SP_DEVICE_INTERFACE_DATA { cbSize = (uint)sizeof(SP_DEVICE_INTERFACE_DATA) };
                if (!SetupDiEnumDeviceInterfaces(set, null, &g, i, &did)) break;
                uint required = 0;
                SetupDiGetDeviceInterfaceDetailW(set, &did, null, 0, &required, null);
                if (required == 0) continue;
                byte* buf = (byte*)NativeMemory.AllocZeroed(required);
                try
                {
                    // SP_DEVICE_INTERFACE_DETAIL_DATA_W.cbSize is 8 on 64-bit builds.
                    *(uint*)buf = (uint)(IntPtr.Size == 8 ? 8 : 6);
                    if (SetupDiGetDeviceInterfaceDetailW(set, &did, buf, required, null, null))
                        paths.Add(new string((char*)(buf + 4)));
                }
                finally { NativeMemory.Free(buf); }
            }
        }
        finally { SetupDiDestroyDeviceInfoList(set); }
        return paths;
    }
}

[StructLayout(LayoutKind.Sequential)]
public struct PDH_FMT_COUNTERVALUE
{
    public uint CStatus;
    public double DoubleValue;
}

[StructLayout(LayoutKind.Sequential)]
public unsafe struct PDH_FMT_COUNTERVALUE_ITEM_W
{
    public char* szName;
    public PDH_FMT_COUNTERVALUE FmtValue;
}

public static unsafe partial class Pdh
{
    public const uint PDH_FMT_DOUBLE = 0x00000200, PDH_FMT_NOCAP100 = 0x00008000;
    public const int PDH_MORE_DATA = unchecked((int)0x800007D2);

    [LibraryImport("pdh.dll", StringMarshalling = StringMarshalling.Utf16)]
    public static partial int PdhOpenQueryW(string? dataSource, nuint userData, nint* query);

    [LibraryImport("pdh.dll", StringMarshalling = StringMarshalling.Utf16)]
    public static partial int PdhAddEnglishCounterW(nint query, string path, nuint userData, nint* counter);

    [LibraryImport("pdh.dll")]
    public static partial int PdhCollectQueryData(nint query);

    [LibraryImport("pdh.dll")]
    public static partial int PdhGetFormattedCounterValue(nint counter, uint format, uint* type, PDH_FMT_COUNTERVALUE* value);

    [LibraryImport("pdh.dll")]
    public static partial int PdhGetFormattedCounterArrayW(nint counter, uint format, uint* bufferSize, uint* itemCount, void* items);

    [LibraryImport("pdh.dll")]
    public static partial int PdhCloseQuery(nint query);
}

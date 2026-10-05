using Pulse.App.Interop;
using Pulse.Core.Models;

namespace Pulse.App.Telemetry;

/// <summary>
/// Battery level, rate and time from CallNtPowerInformation(SystemBatteryState)
/// every tick; design and full-charge capacity, cycle count and temperature from
/// the battery IOCTLs once a minute.
/// </summary>
public sealed unsafe class PowerReader
{
    List<string>? _paths;
    DateTime _infoAt = DateTime.MinValue;
    double? _designWh, _fullWh, _tempC;
    int? _cycles;
    string? _chemistry;

    public PowerSample Read()
    {
        SYSTEM_BATTERY_STATE st;
        if (Power.CallNtPowerInformation(Power.SystemBatteryState, null, 0, &st, (uint)sizeof(SYSTEM_BATTERY_STATE)) != 0)
            return new PowerSample { OnAc = true };
        if (st.BatteryPresent == 0) return new PowerSample { OnAc = st.AcOnLine != 0 };

        if (DateTime.UtcNow - _infoAt > TimeSpan.FromMinutes(1)) { _infoAt = DateTime.UtcNow; ReadInfo(); }

        double? rateW = st.Rate == Power.BATTERY_UNKNOWN_RATE || st.Rate == 0 ? null : st.Rate / 1000.0;
        double? pct = st.MaxCapacity > 0 ? Math.Min(100, st.RemainingCapacity * 100.0 / st.MaxCapacity) : null;
        bool charging = st.Charging != 0;
        TimeSpan? left = null, toFull = null;
        if (!charging && st.Discharging != 0)
        {
            if (st.EstimatedTime != 0xFFFFFFFF && st.EstimatedTime > 0) left = TimeSpan.FromSeconds(st.EstimatedTime);
            else if (rateW is double r && r < 0) left = TimeSpan.FromHours(st.RemainingCapacity / 1000.0 / -r);
        }
        if (charging && rateW is double cr && cr > 0 && st.MaxCapacity > st.RemainingCapacity)
            toFull = TimeSpan.FromHours((st.MaxCapacity - st.RemainingCapacity) / 1000.0 / cr);

        return new PowerSample
        {
            HasBattery = true,
            OnAc = st.AcOnLine != 0,
            Charging = charging,
            PercentRemaining = pct,
            RateWatts = rateW,
            TimeLeft = left,
            TimeToFull = toFull,
            DesignCapacityWh = _designWh,
            FullChargeCapacityWh = _fullWh ?? (st.MaxCapacity > 0 ? st.MaxCapacity / 1000.0 : null),
            RemainingCapacityWh = st.RemainingCapacity / 1000.0,
            CycleCount = _cycles,
            Chemistry = _chemistry,
            TemperatureC = _tempC
        };
    }

    void ReadInfo()
    {
        try
        {
            _paths ??= Power.BatteryPaths();
            foreach (var path in _paths)
            {
                nint h = Win32.CreateFileW(path, Win32.GENERIC_READ | Win32.GENERIC_WRITE, Win32.FILE_SHARE_READ | Win32.FILE_SHARE_WRITE, 0, Win32.OPEN_EXISTING, 0, 0);
                if (h == Win32.INVALID_HANDLE_VALUE) continue;
                try
                {
                    uint wait = 0, tag = 0, ret;
                    if (!Win32.DeviceIoControl(h, Power.IOCTL_BATTERY_QUERY_TAG, &wait, 4, &tag, 4, &ret, 0) || tag == 0) continue;

                    var q = new BATTERY_QUERY_INFORMATION { BatteryTag = tag, InformationLevel = Power.BatteryInformation };
                    BATTERY_INFORMATION bi;
                    if (Win32.DeviceIoControl(h, Power.IOCTL_BATTERY_QUERY_INFORMATION, &q, (uint)sizeof(BATTERY_QUERY_INFORMATION), &bi, (uint)sizeof(BATTERY_INFORMATION), &ret, 0)
                        && (bi.Capabilities & Power.BATTERY_CAPACITY_RELATIVE) == 0)
                    {
                        _designWh = bi.DesignedCapacity > 0 ? bi.DesignedCapacity / 1000.0 : null;
                        _fullWh = bi.FullChargedCapacity > 0 ? bi.FullChargedCapacity / 1000.0 : null;
                        _cycles = bi.CycleCount > 0 ? (int)bi.CycleCount : null;
                        _chemistry = new string((sbyte*)bi.Chemistry, 0, 4).TrimEnd('\0', ' ');
                    }

                    q.InformationLevel = Power.BatteryTemperature;
                    uint tenthsK;
                    _tempC = Win32.DeviceIoControl(h, Power.IOCTL_BATTERY_QUERY_INFORMATION, &q, (uint)sizeof(BATTERY_QUERY_INFORMATION), &tenthsK, 4, &ret, 0) && tenthsK > 2000
                        ? tenthsK / 10.0 - 273.15 : null;
                    return; // first battery that answers
                }
                finally { Win32.CloseHandle(h); }
            }
        }
        catch (Exception) { }
    }
}

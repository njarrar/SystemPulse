using System.Diagnostics;
using Pulse.App.Interop;
using Pulse.Core.History;
using Pulse.Core.Models;

namespace Pulse.App.Telemetry;

/// <summary>
/// The interface that carries the default route (GetBestInterface is a routing
/// table lookup; nothing is sent), with throughput from GetIfTable2 counters.
/// For Wi-Fi, WlanQueryInterface adds RSSI, PHY type and link rate, and the BSS
/// list adds the channel frequency. Windows 11 24H2 may hide BSS details from
/// apps without location access; those fields then show a dash.
/// </summary>
public sealed unsafe class NetReader : IDisposable
{
    readonly RateMeter _down = new(), _up = new();
    readonly DailyCounter _today = new();
    nint _wlan;
    uint _lastIndex;
    int _tick;
    double? _bandGHz;

    public NetReader()
    {
        uint ver;
        nint h;
        if (Net.WlanOpenHandle(2, null, &ver, &h) == 0) _wlan = h;
    }

    public NetSample Read()
    {
        void* table;
        if (Net.GetIfTable2(&table) != 0) return new NetSample();
        try
        {
            uint n = *(uint*)table;
            var rows = (MIB_IF_ROW2*)((byte*)table + 8);
            uint best = 0;
            Net.GetBestInterface(0x08080808, &best); // route lookup only
            MIB_IF_ROW2* pick = null;
            for (uint i = 0; i < n; i++)
                if (rows[i].InterfaceIndex == best && rows[i].OperStatus == Net.IfOperStatusUp) { pick = &rows[i]; break; }
            if (pick == null)
                for (uint i = 0; i < n; i++)
                {
                    var r = &rows[i];
                    bool hardware = (r->InterfaceAndOperStatusFlags & 1) != 0;
                    if (hardware && r->OperStatus == Net.IfOperStatusUp && r->Type != Net.IF_TYPE_SOFTWARE_LOOPBACK) { pick = r; break; }
                }
            if (pick == null) return new NetSample { Connected = false };

            if (pick->InterfaceIndex != _lastIndex) { _lastIndex = pick->InterfaceIndex; _bandGHz = null; }
            long now = Stopwatch.GetTimestamp();
            double down = _down.Update(pick->InOctets, now, Stopwatch.Frequency);
            double up = _up.Update(pick->OutOctets, now, Stopwatch.Frequency);
            _today.Update(DateTime.Now, pick->InOctets, pick->OutOctets);

            var kind = pick->Type switch
            {
                Net.IF_TYPE_IEEE80211 => LinkKind.WiFi,
                Net.IF_TYPE_ETHERNET_CSMACD => LinkKind.Ethernet,
                Net.IF_TYPE_WWANPP or Net.IF_TYPE_WWANPP2 => LinkKind.Cellular,
                _ => LinkKind.Other
            };
            var sample = new NetSample
            {
                Kind = kind,
                Alias = Win32.FromFixed(pick->Alias, 257),
                Description = Win32.FromFixed(pick->Description, 257),
                Connected = true,
                DownBps = down,
                UpBps = up,
                LinkBitsPerSec = pick->ReceiveLinkSpeed is > 0 and < ulong.MaxValue ? pick->ReceiveLinkSpeed : null,
                TodayDown = _today.Down,
                TodayUp = _today.Up
            };
            return kind == LinkKind.WiFi ? AddWifi(sample, pick->InterfaceGuid) : sample;
        }
        finally { Net.FreeMibTable(table); }
    }

    NetSample AddWifi(NetSample s, Guid iface)
    {
        if (_wlan == 0) return s;
        int? rssi = null;
        int phy = 0;
        double? rate = null;
        byte* bssid = stackalloc byte[6];
        bool haveBssid = false;
        uint size;
        void* data;
        int vt;
        if (Net.WlanQueryInterface(_wlan, &iface, Net.wlan_intf_opcode_rssi, null, &size, &data, &vt) == 0)
        {
            rssi = *(int*)data;
            Net.WlanFreeMemory(data);
        }
        if (Net.WlanQueryInterface(_wlan, &iface, Net.wlan_intf_opcode_current_connection, null, &size, &data, &vt) == 0)
        {
            var c = (WLAN_CONNECTION_ATTRIBUTES*)data;
            phy = c->wlanAssociationAttributes.dot11PhyType;
            if (c->wlanAssociationAttributes.ulRxRate > 0) rate = c->wlanAssociationAttributes.ulRxRate * 1000.0; // kbps
            for (int i = 0; i < 6; i++) { bssid[i] = c->wlanAssociationAttributes.dot11Bssid[i]; haveBssid |= bssid[i] != 0; }
            Net.WlanFreeMemory(data);
        }
        if (haveBssid && (_bandGHz is null || _tick % 10 == 0)) _bandGHz = BandFromBssList(iface, bssid) ?? _bandGHz;
        _tick++;

        return s with
        {
            RssiDbm = rssi is < 0 and > -120 ? rssi : null,
            LinkBitsPerSec = rate ?? s.LinkBitsPerSec,
            BandGHz = _bandGHz,
            WifiStandard = Standard(phy, _bandGHz)
        };
    }

    double? BandFromBssList(Guid iface, byte* bssid)
    {
        void* list;
        if (Net.WlanGetNetworkBssList(_wlan, &iface, null, 1 /* infrastructure */, 0, null, &list) != 0) return null;
        try
        {
            uint count = *((uint*)list + 1);
            var entries = (WLAN_BSS_ENTRY*)((byte*)list + 8);
            for (uint i = 0; i < count; i++)
            {
                bool same = true;
                for (int b = 0; b < 6 && same; b++) same = entries[i].dot11Bssid[b] == bssid[b];
                if (!same) continue;
                double mhz = entries[i].ulChCenterFrequency / 1000.0;
                return mhz < 3000 ? 2.4 : mhz < 5925 ? 5 : 6;
            }
            return null;
        }
        finally { Net.WlanFreeMemory(list); }
    }

    static string? Standard(int phy, double? band) => phy switch
    {
        Net.dot11_phy_type_eht => "Wi-Fi 7",
        Net.dot11_phy_type_he => band == 6 ? "Wi-Fi 6E" : "Wi-Fi 6",
        Net.dot11_phy_type_vht => "Wi-Fi 5",
        Net.dot11_phy_type_ht => "Wi-Fi 4",
        Net.dot11_phy_type_erp or Net.dot11_phy_type_hrdsss or Net.dot11_phy_type_ofdm => "Wi-Fi",
        _ => null
    };

    public void Dispose()
    {
        if (_wlan != 0) Net.WlanCloseHandle(_wlan, null);
        _wlan = 0;
    }
}

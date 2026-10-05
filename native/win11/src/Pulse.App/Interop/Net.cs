using System.Runtime.InteropServices;

namespace Pulse.App.Interop;

/// <summary>MIB_IF_ROW2 from GetIfTable2 (1352 bytes).</summary>
[StructLayout(LayoutKind.Sequential)]
public unsafe struct MIB_IF_ROW2
{
    public ulong InterfaceLuid;
    public uint InterfaceIndex;
    public Guid InterfaceGuid;
    public fixed char Alias[257];
    public fixed char Description[257];
    public uint PhysicalAddressLength;
    public fixed byte PhysicalAddress[32];
    public fixed byte PermanentPhysicalAddress[32];
    public uint Mtu;
    public uint Type;
    public int TunnelType;
    public int MediaType;
    public int PhysicalMediumType;
    public int AccessType;
    public int DirectionType;
    public byte InterfaceAndOperStatusFlags;
    public int OperStatus;
    public int AdminStatus;
    public int MediaConnectState;
    public Guid NetworkGuid;
    public int ConnectionType;
    public ulong TransmitLinkSpeed, ReceiveLinkSpeed;
    public ulong InOctets, InUcastPkts, InNUcastPkts, InDiscards, InErrors, InUnknownProtos, InUcastOctets, InMulticastOctets, InBroadcastOctets;
    public ulong OutOctets, OutUcastPkts, OutNUcastPkts, OutDiscards, OutErrors, OutUcastOctets, OutMulticastOctets, OutBroadcastOctets, OutQLen;
}

[StructLayout(LayoutKind.Sequential)]
public unsafe struct DOT11_SSID
{
    public uint uSSIDLength;
    public fixed byte ucSSID[32];
}

[StructLayout(LayoutKind.Sequential)]
public unsafe struct WLAN_INTERFACE_INFO
{
    public Guid InterfaceGuid;
    public fixed char strInterfaceDescription[256];
    public int isState;
}

[StructLayout(LayoutKind.Sequential)]
public unsafe struct WLAN_ASSOCIATION_ATTRIBUTES
{
    public DOT11_SSID dot11Ssid;
    public int dot11BssType;
    public fixed byte dot11Bssid[6];
    public int dot11PhyType;
    public uint uDot11PhyIndex;
    public uint wlanSignalQuality;
    public uint ulRxRate, ulTxRate;
}

[StructLayout(LayoutKind.Sequential)]
public unsafe struct WLAN_CONNECTION_ATTRIBUTES
{
    public int isState;
    public int wlanConnectionMode;
    public fixed char strProfileName[256];
    public WLAN_ASSOCIATION_ATTRIBUTES wlanAssociationAttributes;
    // WLAN_SECURITY_ATTRIBUTES follows; Pulse does not read it.
}

/// <summary>WLAN_BSS_ENTRY (360 bytes).</summary>
[StructLayout(LayoutKind.Sequential)]
public unsafe struct WLAN_BSS_ENTRY
{
    public DOT11_SSID dot11Ssid;
    public uint uPhyId;
    public fixed byte dot11Bssid[6];
    public int dot11BssType;
    public int dot11BssPhyType;
    public int lRssi;
    public uint uLinkQuality;
    public byte bInRegDomain;
    public ushort usBeaconPeriod;
    public ulong ullTimestamp, ullHostTimestamp;
    public ushort usCapabilityInformation;
    public uint ulChCenterFrequency;
    public uint wlanRateSetLength;
    public fixed ushort usRateSet[126];
    public uint ulIeOffset, ulIeSize;
}

public static unsafe partial class Net
{
    public const uint IF_TYPE_ETHERNET_CSMACD = 6, IF_TYPE_IEEE80211 = 71, IF_TYPE_WWANPP = 243, IF_TYPE_WWANPP2 = 244, IF_TYPE_SOFTWARE_LOOPBACK = 24;
    public const int IfOperStatusUp = 1;
    public const uint wlan_intf_opcode_current_connection = 7, wlan_intf_opcode_channel_number = 8, wlan_intf_opcode_rssi = 0x10000102;
    public const int dot11_phy_type_ofdm = 4, dot11_phy_type_hrdsss = 5, dot11_phy_type_erp = 6, dot11_phy_type_ht = 7, dot11_phy_type_vht = 8, dot11_phy_type_he = 10, dot11_phy_type_eht = 11;

    [LibraryImport("iphlpapi.dll")]
    public static partial int GetIfTable2(void** table);

    [LibraryImport("iphlpapi.dll")]
    public static partial void FreeMibTable(void* memory);

    [LibraryImport("iphlpapi.dll")]
    public static partial int GetBestInterface(uint destAddr, uint* bestIfIndex);

    [LibraryImport("wlanapi.dll")]
    public static partial uint WlanOpenHandle(uint clientVersion, void* reserved, uint* negotiatedVersion, nint* client);

    [LibraryImport("wlanapi.dll")]
    public static partial uint WlanCloseHandle(nint client, void* reserved);

    [LibraryImport("wlanapi.dll")]
    public static partial uint WlanEnumInterfaces(nint client, void* reserved, void** list);

    [LibraryImport("wlanapi.dll")]
    public static partial uint WlanQueryInterface(nint client, Guid* iface, uint opcode, void* reserved, uint* dataSize, void** data, int* valueType);

    [LibraryImport("wlanapi.dll")]
    public static partial uint WlanGetNetworkBssList(nint client, Guid* iface, DOT11_SSID* ssid, int bssType, int securityEnabled, void* reserved, void** list);

    [LibraryImport("wlanapi.dll")]
    public static partial void WlanFreeMemory(void* memory);
}

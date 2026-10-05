#if canImport(AppKit)
import CoreWLAN
import Darwin
import Foundation
import PulseCore

/// Throughput from getifaddrs link counters, Wi-Fi details from CoreWLAN.
/// Pulse makes no network requests of its own.
final class NetworkReader {
    private var last: [String: (rx: UInt32, tx: UInt32)] = [:]
    private var lastTime: TimeInterval? = nil
    private var day = Calendar.current.startOfDay(for: Date())
    private var todayDown: Double = 0
    private var todayUp: Double = 0

    func read(at time: TimeInterval) -> NetworkReading {
        var n = NetworkReading()
        var counters: [String: (rx: UInt32, tx: UInt32)] = [:]
        var running: [String] = []

        var ifap: UnsafeMutablePointer<ifaddrs>? = nil
        if getifaddrs(&ifap) == 0, let first = ifap {
            var ptr: UnsafeMutablePointer<ifaddrs>? = first
            while let p = ptr {
                let ifa = p.pointee
                let name = String(cString: ifa.ifa_name)
                let flags = Int32(ifa.ifa_flags)
                // Physical links only: en0, en1, ... (not utun, awdl, bridge or loopback).
                if name.hasPrefix("en"), (flags & IFF_UP) != 0, (flags & IFF_LOOPBACK) == 0,
                   let addr = ifa.ifa_addr {
                    if addr.pointee.sa_family == UInt8(AF_LINK), let data = ifa.ifa_data {
                        let d = data.assumingMemoryBound(to: if_data.self).pointee
                        counters[name] = (d.ifi_ibytes, d.ifi_obytes)
                    } else if addr.pointee.sa_family == UInt8(AF_INET) || addr.pointee.sa_family == UInt8(AF_INET6),
                              (flags & IFF_RUNNING) != 0, !running.contains(name) {
                        running.append(name)
                    }
                }
                ptr = ifa.ifa_next
            }
            freeifaddrs(ifap)
        }

        if let lt = lastTime {
            let dt = max(0.001, time - lt)
            var rx: Double = 0, tx: Double = 0
            for (name, c) in counters {
                guard let prev = last[name] else { continue }
                // if_data counters are 32-bit and wrap; &- gives the right delta.
                rx += Double(c.rx &- prev.rx)
                tx += Double(c.tx &- prev.tx)
            }
            n.downRate = rx / dt
            n.upRate = tx / dt
            let today = Calendar.current.startOfDay(for: Date())
            if today != day { day = today; todayDown = 0; todayUp = 0 }
            todayDown += rx
            todayUp += tx
        }
        last = counters
        lastTime = time
        n.todayDown = todayDown
        n.todayUp = todayUp

        if let wifi = CWWiFiClient.shared().interface(), wifi.powerOn(), wifi.rssiValue() != 0 {
            n.kind = .wifi
            n.interface = wifi.interfaceName ?? "en0"
            n.rssi = wifi.rssiValue()
            n.noise = wifi.noiseMeasurement()
            n.txRateMbps = wifi.transmitRate()
            let bandRaw = wifi.wlanChannel()?.channelBand.rawValue ?? 0
            n.band = [1: "2.4 GHz", 2: "5 GHz", 3: "6 GHz"][bandRaw]
            // CWPHYMode raw values: 4 = 802.11n, 5 = ac, 6 = ax, 7 = be.
            switch wifi.activePHYMode().rawValue {
            case 7: n.standard = "Wi-Fi 7"
            case 6: n.standard = bandRaw == 3 ? "Wi-Fi 6E" : "Wi-Fi 6"
            case 5: n.standard = "Wi-Fi 5"
            case 4: n.standard = "Wi-Fi 4"
            default: n.standard = "Wi-Fi"
            }
        } else if let wired = running.sorted().first {
            n.kind = .ethernet
            n.interface = wired
            n.standard = "Ethernet"
        }
        return n
    }
}
#endif

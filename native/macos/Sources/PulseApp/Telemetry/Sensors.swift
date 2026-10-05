#if canImport(AppKit)
import CPulseSensors
import Foundation
import PulseCore

/// Temperatures, fans and system power. Apple silicon reports temperatures
/// through IOHID sensors; Intel Macs through SMC keys. Fans and total system
/// power come from the SMC on both.
final class Sensors {
    static let shared = Sensors()

    private var cpuKeys: [String] = []
    private var gpuKeys: [String] = []
    private var storageKeys: [String] = []
    private var batteryKeys: [String] = []
    private var fanCount = 0
    private var hasPSTR = false
    private var discovered = false

    static func responsiblePid(_ pid: Int32) -> Int32 { pulse_responsible_pid(pid) }

    private func smc(_ key: String) -> Double? {
        var v: Double = 0
        return pulse_smc_read(key, &v) == 0 ? v : nil
    }

    /// Lists SMC keys once and keeps those that hold plausible values.
    private func discover() {
        discovered = true
        guard pulse_smc_open() == 0 else { return }
        let count = pulse_smc_key_count()
        var buf = [CChar](repeating: 0, count: 5)
        if count > 0 {
            for i in 0..<count {
                guard pulse_smc_key_at(Int32(i), &buf) == 0 else { continue }
                let key = String(cString: buf)
                guard key.count == 4, key.hasPrefix("T") else { continue }
                guard let v = smc(key), v > 5, v < 130 else { continue }
                let p2 = String(key.prefix(2))
                switch p2 {
                case "Tp", "Te", "TC": cpuKeys.append(key)      // Apple silicon P/E clusters, Intel CPU
                case "Tg", "TG": gpuKeys.append(key)
                case "TH": storageKeys.append(key)
                case "TB": batteryKeys.append(key)
                default: break
                }
            }
        }
        fanCount = Int(smc("FNum") ?? 0)
        hasPSTR = smc("PSTR") != nil
    }

    private func mean(_ keys: [String]) -> Double? {
        let vals = keys.compactMap { smc($0) }.filter { $0 > 5 && $0 < 130 }
        return vals.isEmpty ? nil : vals.reduce(0, +) / Double(vals.count)
    }

    /// Returns the thermal reading and the GPU hotspot (hottest GPU sensor).
    func read() -> (ThermalReading, gpuHotspot: Double?) {
        if !discovered { discover() }
        var t = ThermalReading()
        var hotspot: Double? = nil
        var hid = PulseHIDTemps()
        if pulse_hid_read(&hid) > 0 {
            if hid.cpu > 0 { t.cpu = hid.cpu }
            if hid.gpu > 0 { t.gpu = hid.gpu; hotspot = hid.gpuMax }
            if hid.storage > 0 { t.storage = hid.storage }
            if hid.battery > 0 { t.battery = hid.battery }
        }
        if t.cpu == nil { t.cpu = mean(cpuKeys) }
        if t.gpu == nil {
            t.gpu = mean(gpuKeys)
            hotspot = gpuKeys.compactMap { smc($0) }.filter { $0 > 5 && $0 < 130 }.max()
        }
        if t.storage == nil { t.storage = mean(storageKeys) }
        if t.battery == nil { t.battery = mean(batteryKeys) }
        if fanCount > 0 {
            t.fanCount = fanCount
            t.fanRPM = (0..<fanCount).compactMap { smc("F\($0)Ac") }
        } else if discovered {
            t.fanCount = 0
        }
        t.osThermalState = ProcessInfo.processInfo.thermalState.rawValue
        return (t, hotspot)
    }

    /// Total system power in watts (SMC "PSTR"), when the Mac reports it.
    func systemPower() -> Double? {
        if !discovered { discover() }
        guard hasPSTR, let w = smc("PSTR"), w > 0, w < 1000 else { return nil }
        return w
    }
}
#endif

#if canImport(AppKit)
import Foundation
import IOKit
import IOKit.ps
import PulseCore

enum IORegistry {
    static func properties(_ entry: io_registry_entry_t) -> [String: Any]? {
        var props: Unmanaged<CFMutableDictionary>? = nil
        guard IORegistryEntryCreateCFProperties(entry, &props, kCFAllocatorDefault, 0) == KERN_SUCCESS,
              let dict = props?.takeRetainedValue() as? [String: Any] else { return nil }
        return dict
    }

    /// Calls `body` for every service matching an IOKit class name.
    static func forEach(matching className: String, _ body: (io_registry_entry_t) -> Void) {
        var iter: io_iterator_t = 0
        guard IOServiceGetMatchingServices(kIOMainPortDefault, IOServiceMatching(className), &iter) == KERN_SUCCESS else { return }
        defer { IOObjectRelease(iter) }
        while true {
            let entry = IOIteratorNext(iter)
            if entry == 0 { break }
            body(entry)
            IOObjectRelease(entry)
        }
    }

    static func children(of entry: io_registry_entry_t, _ body: (io_registry_entry_t) -> Void) {
        var iter: io_iterator_t = 0
        guard IORegistryEntryGetChildIterator(entry, "IOService", &iter) == KERN_SUCCESS else { return }
        defer { IOObjectRelease(iter) }
        while true {
            let child = IOIteratorNext(iter)
            if child == 0 { break }
            body(child)
            IOObjectRelease(child)
        }
    }

    /// Total bytes read and written by every block storage driver since boot.
    static func blockStorageBytes() -> (read: UInt64, write: UInt64) {
        var r: UInt64 = 0, w: UInt64 = 0
        forEach(matching: "IOBlockStorageDriver") { entry in
            guard let stats = properties(entry)?["Statistics"] as? [String: Any] else { return }
            r &+= (stats["Bytes (Read)"] as? NSNumber)?.uint64Value ?? 0
            w &+= (stats["Bytes (Write)"] as? NSNumber)?.uint64Value ?? 0
        }
        return (r, w)
    }

    static func string(_ v: Any?) -> String? {
        if let s = v as? String { return s }
        if let d = v as? Data { return String(decoding: d.prefix { $0 != 0 }, as: UTF8.self) }
        return nil
    }
}

/// Battery and power: IOPSCopyPowerSourcesInfo for state and time estimates,
/// AppleSmartBattery for health, cycles, voltage and current.
final class PowerReader {
    func read() -> PowerReading {
        var p = PowerReading()
        let blob = IOPSCopyPowerSourcesInfo().takeRetainedValue()
        let list = (IOPSCopyPowerSourcesList(blob).takeRetainedValue() as NSArray) as [AnyObject]
        for ps in list {
            guard let desc = IOPSGetPowerSourceDescription(blob, ps as CFTypeRef)?.takeUnretainedValue() as? [String: Any] else { continue }
            guard (desc[kIOPSTypeKey] as? String) == kIOPSInternalBatteryType else { continue }
            p.hasBattery = true
            if let cur = desc[kIOPSCurrentCapacityKey] as? Double, let max = desc[kIOPSMaxCapacityKey] as? Double, max > 0 {
                p.percent = cur / max * 100
            }
            p.charging = (desc[kIOPSIsChargingKey] as? Bool) ?? false
            p.externalConnected = (desc[kIOPSPowerSourceStateKey] as? String) == kIOPSACPowerValue
            if let t = desc[kIOPSTimeToEmptyKey] as? Int, t > 0 { p.minutesToEmpty = t }
            if let t = desc[kIOPSTimeToFullChargeKey] as? Int, t > 0 { p.minutesToFull = t }
        }
        if !p.hasBattery {
            p.externalConnected = true
        }

        let service = IOServiceGetMatchingService(kIOMainPortDefault, IOServiceMatching("AppleSmartBattery"))
        if service != 0 {
            defer { IOObjectRelease(service) }
            if let b = IORegistry.properties(service) {
                func num(_ k: String) -> NSNumber? { b[k] as? NSNumber }
                let volts = (num("Voltage")?.doubleValue ?? 0) / 1000
                let amps = Double(num("InstantAmperage")?.int64Value ?? num("Amperage")?.int64Value ?? 0) / 1000
                if volts > 0 { p.batteryWatts = volts * amps }
                p.cycles = num("CycleCount")?.intValue
                let design = num("DesignCapacity")?.doubleValue
                let maxRaw = num("AppleRawMaxCapacity")?.doubleValue ?? num("NominalChargeCapacity")?.doubleValue
                if let d = design, d > 0, let m = maxRaw, m > 0 {
                    p.healthPercent = min(100, m / d * 100)
                    let v = (num("DesignVoltage") ?? num("Voltage"))?.doubleValue ?? 0
                    if v > 0 {
                        p.designWh = d * v / 1_000_000
                        p.capacityWh = m * v / 1_000_000
                    }
                }
                if let adapter = b["AdapterDetails"] as? [String: Any] {
                    if let w = (adapter["Watts"] as? NSNumber)?.doubleValue, w > 0 { p.adapterWatts = w }
                    p.adapterName = IORegistry.string(adapter["Name"]) ?? IORegistry.string(adapter["Description"])
                }
                if let ext = b["ExternalConnected"] as? Bool { p.externalConnected = ext }
                if let ch = b["IsCharging"] as? Bool { p.charging = ch }
            }
        }
        if let w = Sensors.shared.systemPower() { p.systemWatts = w }
        return p
    }
}

/// GPU utilization from IOAccelerator PerformanceStatistics, and per-process
/// GPU time from the accelerator's user clients.
final class GPUReader {
    private(set) var processGPUTime: [Int32: UInt64] = [:]

    func read() -> GPUReading {
        var g = GPUReading()
        var best: Double = -1
        var times: [Int32: UInt64] = [:]
        IORegistry.forEach(matching: "IOAccelerator") { entry in
            guard let props = IORegistry.properties(entry) else { return }
            let perf = props["PerformanceStatistics"] as? [String: Any] ?? [:]
            let util = (perf["Device Utilization %"] as? NSNumber)?.doubleValue
                ?? (perf["GPU Activity(%)"] as? NSNumber)?.doubleValue ?? 0
            if util > best {
                best = util
                g.utilization = util
                if let model = IORegistry.string(props["model"]) { g.name = model }
                g.cores = (props["gpu-core-count"] as? NSNumber)?.intValue
                if let mem = (perf["In use system memory"] as? NSNumber)?.doubleValue {
                    g.memoryInUse = mem
                    g.unifiedMemory = true
                } else if let vram = (perf["vramUsedBytes"] as? NSNumber)?.doubleValue {
                    g.memoryInUse = vram
                }
            }
            IORegistry.children(of: entry) { child in
                guard let c = IORegistry.properties(child),
                      let creator = c["IOUserClientCreator"] as? String,
                      let pid = GPUReader.pid(fromCreator: creator),
                      let usage = c["AppUsage"] as? [[String: Any]] else { return }
                let ns = usage.reduce(UInt64(0)) { $0 &+ ((($1["accumulatedGPUTime"] as? NSNumber)?.uint64Value) ?? 0) }
                times[pid, default: 0] &+= ns
            }
        }
        if g.name == "GPU", let brand = Sysctl.string("machdep.cpu.brand_string") { g.name = brand }
        processGPUTime = times
        return g
    }

    /// "pid 812, Safari" -> 812
    static func pid(fromCreator s: String) -> Int32? {
        guard s.hasPrefix("pid ") else { return nil }
        let digits = s.dropFirst(4).prefix { $0.isNumber }
        return Int32(digits)
    }
}
#endif

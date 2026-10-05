import Foundation

// Platform-neutral telemetry records. The macOS readers in PulseApp fill
// them; nil means "this Mac does not report it".

public struct CPUReading: Equatable, Sendable {
    public var total: Double = 0          // percent of all logical CPUs
    public var user: Double = 0
    public var system: Double = 0
    public var perCore: [Double] = []
    public var performanceCores: Int = 0  // hw.perflevel0.logicalcpu
    public var efficiencyCores: Int = 0   // hw.perflevel1.logicalcpu
    public var pClusterLoad: Double? = nil
    public var eClusterLoad: Double? = nil
    public var load1: Double = 0
    public var physicalCores: Int = 0
    public var logicalCores: Int = 0
    public init() {}
}

public enum MemoryPressure: Int, Sendable {
    case normal = 1, warning = 2, critical = 4
}

public struct MemoryReading: Equatable, Sendable {
    public var total: Double = 0
    public var app: Double = 0
    public var wired: Double = 0
    public var compressed: Double = 0
    public var cached: Double = 0
    public var swapUsed: Double = 0
    public var swapTotal: Double = 0
    public var pressure: MemoryPressure = .normal
    public init() {}

    public var used: Double { app + wired + compressed }
    public var free: Double { max(0, total - used) }
    public var usedPercent: Double { total > 0 ? used / total * 100 : 0 }
}

public struct PowerReading: Equatable, Sendable {
    public var hasBattery = false
    public var percent: Double? = nil
    public var charging = false
    public var externalConnected = false
    public var batteryWatts: Double? = nil      // + charging, − discharging
    public var systemWatts: Double? = nil       // SMC PSTR when present
    public var adapterWatts: Double? = nil
    public var adapterName: String? = nil
    public var minutesToEmpty: Int? = nil
    public var minutesToFull: Int? = nil
    public var healthPercent: Double? = nil
    public var cycles: Int? = nil
    public var capacityWh: Double? = nil
    public var designWh: Double? = nil
    public init() {}

    /// Draw shown in the UI: system power when known, else battery discharge.
    public var drawWatts: Double? {
        if let s = systemWatts, s > 0 { return s }
        if let b = batteryWatts { return abs(b) }
        return nil
    }
}

public struct ThermalReading: Equatable, Sendable {
    public var cpu: Double? = nil
    public var gpu: Double? = nil
    public var storage: Double? = nil
    public var battery: Double? = nil
    public var fanCount: Int? = nil
    public var fanRPM: [Double] = []
    public var osThermalState: Int = 0       // ProcessInfo.ThermalState raw value
    public init() {}
}

public struct GPUReading: Equatable, Sendable {
    public var name: String = "GPU"
    public var cores: Int? = nil
    public var utilization: Double = 0
    public var memoryInUse: Double? = nil
    public var unifiedMemory = false
    public init() {}
}

public struct DiskReading: Equatable, Sendable {
    public var volumeName: String = ""
    public var format: String = ""
    public var total: Double = 0
    public var free: Double = 0
    public var readRate: Double = 0
    public var writeRate: Double = 0
    public init() {}
    public var used: Double { max(0, total - free) }
    public var usedPercent: Double { total > 0 ? used / total * 100 : 0 }
}

public enum LinkKind: Sendable { case wifi, ethernet, other, none }

public struct NetworkReading: Equatable, Sendable {
    public var kind: LinkKind = .none
    public var interface: String = ""        // "en0"
    public var standard: String? = nil       // "Wi-Fi 6E"
    public var band: String? = nil           // "5 GHz"
    public var rssi: Int? = nil              // dBm
    public var noise: Int? = nil
    public var txRateMbps: Double? = nil
    public var downRate: Double = 0
    public var upRate: Double = 0
    public var todayDown: Double = 0
    public var todayUp: Double = 0
    public init() {}
}

/// One row in Top Active Apps: an app with its helper processes, or a
/// background process group that belongs to no app.
public struct AppUsage: Equatable, Sendable, Identifiable {
    public var id: String
    public var name: String
    public var pid: Int32
    public var pids: [Int32]
    public var processCount: Int
    public var threads: Int
    public var cpu: Double        // percent of total CPU
    public var memory: Double     // bytes
    public var gpu: Double        // percent of GPU time
    public var isApp: Bool        // backed by an NSRunningApplication
    public var bundleID: String?

    public init(id: String, name: String, pid: Int32, pids: [Int32], processCount: Int, threads: Int,
                cpu: Double, memory: Double, gpu: Double, isApp: Bool, bundleID: String? = nil) {
        self.id = id; self.name = name; self.pid = pid; self.pids = pids; self.processCount = processCount
        self.threads = threads; self.cpu = cpu; self.memory = memory; self.gpu = gpu; self.isApp = isApp; self.bundleID = bundleID
    }
}

public struct Snapshot: Equatable, Sendable {
    public var time: TimeInterval = 0
    public var cpu = CPUReading()
    public var memory = MemoryReading()
    public var power = PowerReading()
    public var thermal = ThermalReading()
    public var gpu = GPUReading()
    public var disk = DiskReading()
    public var network = NetworkReading()
    public var apps: [AppUsage] = []
    public init() {}
}

// MARK: - Process grouping

/// One process as libproc reports it.
public struct ProcessSample: Equatable, Sendable {
    public var pid: Int32
    public var ppid: Int32
    public var responsiblePid: Int32?
    public var name: String
    public var cpuTimeNs: UInt64    // user + system, nanoseconds
    public var residentBytes: UInt64
    public var threads: Int
    public var gpuTimeNs: UInt64

    public init(pid: Int32, ppid: Int32, responsiblePid: Int32? = nil, name: String, cpuTimeNs: UInt64,
                residentBytes: UInt64, threads: Int, gpuTimeNs: UInt64 = 0) {
        self.pid = pid; self.ppid = ppid; self.responsiblePid = responsiblePid; self.name = name
        self.cpuTimeNs = cpuTimeNs; self.residentBytes = residentBytes; self.threads = threads; self.gpuTimeNs = gpuTimeNs
    }
}

/// A running GUI app (from NSRunningApplication).
public struct AppIdentity: Equatable, Sendable {
    public var pid: Int32
    public var name: String
    public var bundleID: String?
    public init(pid: Int32, name: String, bundleID: String?) { self.pid = pid; self.name = name; self.bundleID = bundleID }
}

/// Groups processes under the app responsible for them, the way Activity
/// Monitor's hierarchical view does, and turns time deltas into percentages.
public struct ProcessGrouper {
    private var lastCPU: [Int32: UInt64] = [:]
    private var lastGPU: [Int32: UInt64] = [:]
    private var lastTime: TimeInterval? = nil

    public init() {}

    /// The app pid a process belongs to: its responsible pid when that is an
    /// app, else the first app found walking up the parent chain.
    public static func owner(of pid: Int32, in byPid: [Int32: ProcessSample], apps: Set<Int32>) -> Int32? {
        if apps.contains(pid) { return pid }
        if let r = byPid[pid]?.responsiblePid, apps.contains(r) { return r }
        var cur = byPid[pid]?.ppid ?? 0
        var hops = 0
        while cur > 1 && hops < 32 {
            if apps.contains(cur) { return cur }
            if let r = byPid[cur]?.responsiblePid, apps.contains(r) { return r }
            cur = byPid[cur]?.ppid ?? 0
            hops += 1
        }
        return nil
    }

    /// Returns usage rows. `cpuCount` is the number of logical CPUs, so 100%
    /// means every core is busy.
    public mutating func update(processes: [ProcessSample], apps: [AppIdentity], cpuCount: Int, at time: TimeInterval) -> [AppUsage] {
        let byPid = Dictionary(processes.map { ($0.pid, $0) }, uniquingKeysWith: { a, _ in a })
        let appByPid = Dictionary(apps.map { ($0.pid, $0) }, uniquingKeysWith: { a, _ in a })
        let appPids = Set(appByPid.keys)
        let elapsed = lastTime.map { max(0.001, time - $0) }

        struct Acc { var pids: [Int32] = []; var threads = 0; var cpuNs: Double = 0; var gpuNs: Double = 0; var mem: Double = 0 }
        var groups: [String: Acc] = [:]
        var meta: [String: (name: String, pid: Int32, isApp: Bool, bundle: String?)] = [:]
        var nextCPU: [Int32: UInt64] = [:], nextGPU: [Int32: UInt64] = [:]

        for p in processes {
            nextCPU[p.pid] = p.cpuTimeNs
            nextGPU[p.pid] = p.gpuTimeNs
            let dCPU = lastCPU[p.pid].map { p.cpuTimeNs >= $0 ? Double(p.cpuTimeNs - $0) : 0 } ?? 0
            let dGPU = lastGPU[p.pid].map { p.gpuTimeNs >= $0 ? Double(p.gpuTimeNs - $0) : 0 } ?? 0
            let key: String
            if let owner = ProcessGrouper.owner(of: p.pid, in: byPid, apps: appPids), let a = appByPid[owner] {
                key = "app:\(owner)"
                meta[key] = (a.name, owner, true, a.bundleID)
            } else {
                key = "proc:" + p.name
                if meta[key] == nil || p.pid < meta[key]!.pid { meta[key] = (p.name, p.pid, false, nil) }
            }
            var g = groups[key] ?? Acc()
            g.pids.append(p.pid)
            g.threads += p.threads
            g.cpuNs += dCPU
            g.gpuNs += dGPU
            g.mem += Double(p.residentBytes)
            groups[key] = g
        }
        lastCPU = nextCPU
        lastGPU = nextGPU
        lastTime = time

        return groups.map { key, g in
            let m = meta[key]!
            let cpu = elapsed.map { g.cpuNs / ($0 * 1e9 * Double(max(1, cpuCount))) * 100 } ?? 0
            let gpu = elapsed.map { g.gpuNs / ($0 * 1e9) * 100 } ?? 0
            return AppUsage(id: m.bundle ?? key, name: m.name, pid: m.pid, pids: g.pids.sorted(), processCount: g.pids.count,
                            threads: g.threads, cpu: min(100, cpu), memory: g.mem, gpu: min(100, gpu), isApp: m.isApp, bundleID: m.bundle)
        }.sorted { $0.cpu > $1.cpu }
    }
}

/// Splits CPU ticks into busy percentages (host_processor_info deltas).
public struct CPUTicks: Equatable, Sendable {
    public var user: UInt64
    public var system: UInt64
    public var idle: UInt64
    public var nice: UInt64
    public init(user: UInt64, system: UInt64, idle: UInt64, nice: UInt64) {
        self.user = user; self.system = system; self.idle = idle; self.nice = nice
    }

    /// (user%, system%, total%) between two readings.
    public static func load(from a: CPUTicks, to b: CPUTicks) -> (user: Double, system: Double, total: Double) {
        func d(_ x: UInt64, _ y: UInt64) -> Double { y >= x ? Double(y - x) : 0 }
        let u = d(a.user, b.user) + d(a.nice, b.nice), s = d(a.system, b.system), i = d(a.idle, b.idle)
        let all = u + s + i
        guard all > 0 else { return (0, 0, 0) }
        return (u / all * 100, s / all * 100, (u + s) / all * 100)
    }
}

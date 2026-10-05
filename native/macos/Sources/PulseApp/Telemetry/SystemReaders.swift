#if canImport(AppKit)
import Darwin
import Foundation
import PulseCore

// CPU and memory through Mach and sysctl.

enum Sysctl {
    static func int(_ name: String) -> Int? {
        var v: Int64 = 0
        var size = MemoryLayout<Int64>.size
        guard sysctlbyname(name, &v, &size, nil, 0) == 0 else { return nil }
        switch size {
        case 4: return Int(Int32(truncatingIfNeeded: v))
        default: return Int(v)
        }
    }

    static func string(_ name: String) -> String? {
        var size = 0
        guard sysctlbyname(name, nil, &size, nil, 0) == 0, size > 0 else { return nil }
        var buf = [CChar](repeating: 0, count: size)
        guard sysctlbyname(name, &buf, &size, nil, 0) == 0 else { return nil }
        return String(cString: buf)
    }
}

final class CPUReader {
    private let host = mach_host_self()
    private var previous: [CPUTicks] = []
    let performanceCores: Int
    let efficiencyCores: Int
    let logical: Int
    let physical: Int

    init() {
        logical = Sysctl.int("hw.logicalcpu") ?? ProcessInfo.processInfo.activeProcessorCount
        physical = Sysctl.int("hw.physicalcpu") ?? logical
        if (Sysctl.int("hw.nperflevels") ?? 1) > 1 {
            // perflevel0 is the fastest cluster (P), perflevel1 the efficient one (E).
            performanceCores = Sysctl.int("hw.perflevel0.logicalcpu") ?? 0
            efficiencyCores = Sysctl.int("hw.perflevel1.logicalcpu") ?? 0
        } else {
            performanceCores = 0
            efficiencyCores = 0
        }
    }

    func read() -> CPUReading {
        var r = CPUReading()
        r.performanceCores = performanceCores
        r.efficiencyCores = efficiencyCores
        r.logicalCores = logical
        r.physicalCores = physical

        var cpuCount: natural_t = 0
        var info: processor_info_array_t? = nil
        var infoCount: mach_msg_type_number_t = 0
        let kr = host_processor_info(host, PROCESSOR_CPU_LOAD_INFO, &cpuCount, &info, &infoCount)
        guard kr == KERN_SUCCESS, let info = info else { return r }
        defer {
            vm_deallocate(mach_task_self_, vm_address_t(UInt(bitPattern: info)),
                          vm_size_t(Int(infoCount) * MemoryLayout<integer_t>.stride))
        }
        let stride = Int(CPU_STATE_MAX)
        var ticks: [CPUTicks] = []
        for i in 0..<Int(cpuCount) {
            func tick(_ state: Int32) -> UInt64 { UInt64(UInt32(bitPattern: info[i * stride + Int(state)])) }
            ticks.append(CPUTicks(user: tick(CPU_STATE_USER), system: tick(CPU_STATE_SYSTEM),
                                  idle: tick(CPU_STATE_IDLE), nice: tick(CPU_STATE_NICE)))
        }
        defer { previous = ticks }
        guard previous.count == ticks.count else { return r }

        var sumU = 0.0, sumS = 0.0
        for (a, b) in zip(previous, ticks) {
            let l = CPUTicks.load(from: a, to: b)
            r.perCore.append(l.total)
            sumU += l.user
            sumS += l.system
        }
        let n = Double(max(1, ticks.count))
        r.user = sumU / n
        r.system = sumS / n
        r.total = min(100, r.user + r.system)

        // Apple silicon numbers the efficiency cores first.
        if efficiencyCores > 0, performanceCores > 0, r.perCore.count >= efficiencyCores + performanceCores {
            let e = r.perCore[0..<efficiencyCores]
            let p = r.perCore[efficiencyCores..<(efficiencyCores + performanceCores)]
            r.eClusterLoad = e.reduce(0, +) / Double(e.count)
            r.pClusterLoad = p.reduce(0, +) / Double(p.count)
        }
        var loads = [Double](repeating: 0, count: 3)
        if getloadavg(&loads, 3) > 0 { r.load1 = loads[0] }
        return r
    }
}

final class MemoryReader {
    private let host = mach_host_self()
    private let total: Double = {
        var v: UInt64 = 0
        var size = MemoryLayout<UInt64>.size
        sysctlbyname("hw.memsize", &v, &size, nil, 0)
        return Double(v)
    }()

    func read() -> MemoryReading {
        var m = MemoryReading()
        m.total = total
        var stats = vm_statistics64()
        var count = mach_msg_type_number_t(MemoryLayout<vm_statistics64_data_t>.stride / MemoryLayout<integer_t>.stride)
        let kr = withUnsafeMutablePointer(to: &stats) { ptr in
            ptr.withMemoryRebound(to: integer_t.self, capacity: Int(count)) { host_statistics64(host, HOST_VM_INFO64, $0, &count) }
        }
        if kr == KERN_SUCCESS {
            let page = Double(vm_kernel_page_size)
            let internalPages = Int64(stats.internal_page_count), purgeable = Int64(stats.purgeable_count)
            // The same split Activity Monitor shows.
            m.app = Double(max(0, internalPages - purgeable)) * page
            m.wired = Double(stats.wire_count) * page
            m.compressed = Double(stats.compressor_page_count) * page
            m.cached = Double(Int64(stats.external_page_count) + purgeable) * page
        }

        var swap = xsw_usage()
        var size = MemoryLayout<xsw_usage>.size
        if sysctlbyname("vm.swapusage", &swap, &size, nil, 0) == 0 {
            m.swapUsed = Double(swap.xsu_used)
            m.swapTotal = Double(swap.xsu_total)
        }

        var level: Int32 = 1
        var lsize = MemoryLayout<Int32>.size
        if sysctlbyname("kern.memorystatus_vm_pressure_level", &level, &lsize, nil, 0) == 0 {
            m.pressure = MemoryPressure(rawValue: Int(level)) ?? .normal
        }
        return m
    }
}

/// Volume size through statfs and disk throughput through IOBlockStorageDriver.
final class DiskReader {
    private var lastBytes: (read: UInt64, write: UInt64, time: TimeInterval)? = nil
    private lazy var volumeName: String = FileManager.default.displayName(atPath: "/")

    func read(at time: TimeInterval) -> DiskReading {
        var d = DiskReading()
        d.volumeName = volumeName
        var s = statfs()
        // On APFS the Data volume shares the container with the sealed system volume.
        if statfs("/System/Volumes/Data", &s) == 0 || statfs("/", &s) == 0 {
            d.total = Double(s.f_blocks) * Double(s.f_bsize)
            d.free = Double(s.f_bavail) * Double(s.f_bsize)
            let fs = withUnsafePointer(to: &s.f_fstypename) {
                $0.withMemoryRebound(to: CChar.self, capacity: Int(MFSTYPENAMELEN)) { String(cString: $0) }
            }
            d.format = fs.uppercased()
        }
        let bytes = IORegistry.blockStorageBytes()
        if let last = lastBytes {
            let dt = max(0.001, time - last.time)
            d.readRate = Double(bytes.read &- last.read) / dt
            d.writeRate = Double(bytes.write &- last.write) / dt
            if bytes.read < last.read || bytes.write < last.write { d.readRate = 0; d.writeRate = 0 }
        }
        lastBytes = (bytes.read, bytes.write, time)
        return d
    }
}

/// Process list through libproc.
final class ProcessReader {
    private struct Static { var ppid: Int32; var name: String; var responsible: Int32? }
    private var cache: [Int32: Static] = [:]
    private let timebase: (numer: UInt64, denom: UInt64) = {
        var tb = mach_timebase_info_data_t()
        mach_timebase_info(&tb)
        return (UInt64(max(1, tb.numer)), UInt64(max(1, tb.denom)))
    }()

    func read(gpuTime: [Int32: UInt64]) -> [ProcessSample] {
        let estimate = proc_listallpids(nil, 0)
        guard estimate > 0 else { return [] }
        var pids = [pid_t](repeating: 0, count: Int(estimate) + 64)
        let got = pids.withUnsafeMutableBytes { proc_listallpids($0.baseAddress, Int32($0.count)) }
        guard got > 0 else { return [] }

        var out: [ProcessSample] = []
        var seen = Set<Int32>()
        for pid in pids.prefix(Int(got)) where pid > 0 {
            var ti = proc_taskinfo()
            let tsize = Int32(MemoryLayout<proc_taskinfo>.size)
            // Fails for processes owned by other users; those are left out.
            guard proc_pidinfo(pid, PROC_PIDTASKINFO, 0, &ti, tsize) == tsize else { continue }
            seen.insert(pid)
            let st = cache[pid] ?? staticInfo(pid)
            cache[pid] = st
            // pti_total_* are Mach time units on Apple silicon, nanoseconds on Intel.
            let ticks = ti.pti_total_user &+ ti.pti_total_system
            let ns = ticks / timebase.denom * timebase.numer + (ticks % timebase.denom) * timebase.numer / timebase.denom
            out.append(ProcessSample(pid: pid, ppid: st.ppid, responsiblePid: st.responsible, name: st.name,
                                     cpuTimeNs: ns, residentBytes: ti.pti_resident_size,
                                     threads: Int(ti.pti_threadnum), gpuTimeNs: gpuTime[pid] ?? 0))
        }
        cache = cache.filter { seen.contains($0.key) }
        return out
    }

    private func staticInfo(_ pid: Int32) -> Static {
        var bi = proc_bsdinfo()
        let bsize = Int32(MemoryLayout<proc_bsdinfo>.size)
        var ppid: Int32 = 0
        var name = ""
        if proc_pidinfo(pid, PROC_PIDTBSDINFO, 0, &bi, bsize) == bsize {
            ppid = Int32(bitPattern: bi.pbi_ppid)
            name = withUnsafePointer(to: &bi.pbi_name) {
                $0.withMemoryRebound(to: CChar.self, capacity: Int(2 * MAXCOMLEN)) { String(cString: $0) }
            }
            if name.isEmpty {
                name = withUnsafePointer(to: &bi.pbi_comm) {
                    $0.withMemoryRebound(to: CChar.self, capacity: Int(MAXCOMLEN)) { String(cString: $0) }
                }
            }
        }
        if name.isEmpty {
            var buf = [CChar](repeating: 0, count: 256)
            if proc_name(pid, &buf, UInt32(buf.count)) > 0 { name = String(cString: buf) }
        }
        let r = Sensors.responsiblePid(pid)
        return Static(ppid: ppid, name: name.isEmpty ? "pid \(pid)" : name, responsible: r > 0 && r != pid ? r : nil)
    }
}
#endif

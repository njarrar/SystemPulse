#if canImport(AppKit)
import Foundation
import PulseCore

/// Reads every source once per tick on a private serial queue.
final class TelemetryEngine {
    private let queue = DispatchQueue(label: "pulse.telemetry", qos: .utility)
    private let cpu = CPUReader()
    private let memory = MemoryReader()
    private let power = PowerReader()
    private let gpu = GPUReader()
    private let disk = DiskReader()
    private let network = NetworkReader()
    private let processes = ProcessReader()
    private var grouper = ProcessGrouper()

    var logicalCPUs: Int { cpu.logical }

    /// Samples off the main thread and calls back on the main thread.
    func sample(apps: [AppIdentity], completion: @escaping (Snapshot, Double?) -> Void) {
        queue.async { [self] in
            let now = ProcessInfo.processInfo.systemUptime
            var s = Snapshot()
            s.time = now
            s.cpu = cpu.read()
            s.memory = memory.read()
            s.gpu = gpu.read()
            let (thermal, hotspot) = Sensors.shared.read()
            s.thermal = thermal
            s.power = power.read()
            s.disk = disk.read(at: now)
            s.network = network.read(at: now)
            let procs = processes.read(gpuTime: gpu.processGPUTime)
            s.apps = grouper.update(processes: procs, apps: apps, cpuCount: cpu.logical, at: now)
            DispatchQueue.main.async { completion(s, hotspot) }
        }
    }
}
#endif

import Foundation

/// The Settings demo switches (Part 1.3) laid over real telemetry:
/// "Simulate CPU hog" pushes the busiest app above 50% of total CPU and
/// raises total CPU, power draw and CPU temperature the way the prototype
/// does; "Charging" shows the charging state. Apps Pulse ended are removed
/// at once, before the next sample would drop them.
public struct DemoOverlay: Equatable, Sendable {
    public var hog = false
    public var charging = false

    /// Prototype targets: the hog holds about 52% of total CPU, draw rises
    /// by 5.4 W and the CPU warms by 19 °C (33 -> 52).
    public static let hogShare = 52.0
    public static let hogWatts = 5.4
    public static let hogTempRise = 19.0
    public static let chargeWatts = 48.0
    public static let chargeMinutes = 52
    public static let adapter = "USB-C 96 W"

    public init(hog: Bool = false, charging: Bool = false) {
        self.hog = hog
        self.charging = charging
    }

    public struct Result: Equatable, Sendable {
        public var snapshot: Snapshot
        /// The app the demo hog runs on, when the hog switch is on.
        public var hogID: String?
    }

    public func apply(_ raw: Snapshot, ended: Set<Int32> = []) -> Result {
        var s = raw
        // Apps Pulse ended leave the list, and their CPU leaves the total.
        if !ended.isEmpty {
            let gone = s.apps.filter { app in app.pids.contains { ended.contains($0) } }
            let freed = gone.reduce(0) { $0 + $1.cpu }
            s.apps.removeAll { app in app.pids.contains { ended.contains($0) } }
            s.cpu.total = max(0, s.cpu.total - freed)
            s.cpu.user = max(0, s.cpu.user - freed * 0.66)
            s.cpu.system = max(0, s.cpu.system - freed * 0.34)
        }

        var hogID: String? = nil
        if hog, let pick = s.apps.filter({ $0.isApp }).max(by: { $0.cpu < $1.cpu }) ?? s.apps.max(by: { $0.cpu < $1.cpu }) {
            // A small wobble so the readout stays alive, like the prototype's jitter.
            let target = DemoOverlay.hogShare + 1.2 * sin(raw.time / 3)
            let boost = max(0, target - pick.cpu)
            if let i = s.apps.firstIndex(where: { $0.id == pick.id }) { s.apps[i].cpu += boost }
            s.cpu.total = min(100, s.cpu.total + boost)
            s.cpu.user = min(100, s.cpu.user + boost * 0.66)
            s.cpu.system = min(100, s.cpu.system + boost * 0.34)
            if let p = s.cpu.pClusterLoad { s.cpu.pClusterLoad = min(100, p + boost * 1.22) }
            if let e = s.cpu.eClusterLoad { s.cpu.eClusterLoad = min(100, e + boost * 0.58) }
            if let w = s.power.systemWatts { s.power.systemWatts = w + DemoOverlay.hogWatts }
            if let b = s.power.batteryWatts, b <= 0 { s.power.batteryWatts = b - DemoOverlay.hogWatts }
            if let c = s.thermal.cpu { s.thermal.cpu = c + DemoOverlay.hogTempRise }
            hogID = pick.id
        }

        if charging {
            s.power.hasBattery = true
            s.power.percent = s.power.percent ?? 84
            if !raw.power.charging || s.power.minutesToFull == nil { s.power.minutesToFull = DemoOverlay.chargeMinutes }
            s.power.charging = true
            s.power.externalConnected = true
            s.power.batteryWatts = DemoOverlay.chargeWatts
            s.power.adapterWatts = 96
            s.power.adapterName = DemoOverlay.adapter
        }

        s.apps.sort { $0.cpu > $1.cpu }
        return Result(snapshot: s, hogID: hogID)
    }
}

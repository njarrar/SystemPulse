#if canImport(AppKit)
import AppKit
import PulseCore
import SwiftUI

/// Command-line checks used by CI and for screenshots:
///   Pulse --probe          print two rounds of real telemetry and exit
///   Pulse --render DIR     render the flyout to PNG files (light, dark, en, ar) and exit
@MainActor
enum DevTools {
    static func probe() {
        let engine = TelemetryEngine()
        engine.sample(apps: []) { _, _ in
            Task { @MainActor in
                try? await Task.sleep(nanoseconds: 1_500_000_000)
                let apps = NSWorkspace.shared.runningApplications
                    .filter { $0.activationPolicy == .regular }
                    .map { AppIdentity(pid: $0.processIdentifier, name: $0.localizedName ?? "?", bundleID: $0.bundleIdentifier) }
                engine.sample(apps: apps) { s, hotspot in
                    print(DevTools.report(s, hotspot: hotspot))
                    exit(0)
                }
            }
        }
    }

    nonisolated static func report(_ s: Snapshot, hotspot: Double?) -> String {
        func o<T>(_ v: T?) -> String { v.map { "\($0)" } ?? "n/a" }
        var lines: [String] = []
        lines.append("cpu      total=\(fmt(s.cpu.total))% user=\(fmt(s.cpu.user))% sys=\(fmt(s.cpu.system))% cores=\(s.cpu.logicalCores) P=\(s.cpu.performanceCores) E=\(s.cpu.efficiencyCores) Pload=\(o(s.cpu.pClusterLoad.map(fmt))) Eload=\(o(s.cpu.eClusterLoad.map(fmt))) load1=\(fmt(s.cpu.load1))")
        let m = s.memory
        lines.append("memory   total=\(gb(m.total)) app=\(gb(m.app)) wired=\(gb(m.wired)) compressed=\(gb(m.compressed)) swap=\(gb(m.swapUsed))/\(gb(m.swapTotal)) pressure=\(m.pressure)")
        let p = s.power
        lines.append("power    battery=\(p.hasBattery) pct=\(o(p.percent.map(fmt))) charging=\(p.charging) ac=\(p.externalConnected) batteryW=\(o(p.batteryWatts.map(fmt))) systemW=\(o(p.systemWatts.map(fmt))) health=\(o(p.healthPercent.map(fmt))) cycles=\(o(p.cycles))")
        let t = s.thermal
        lines.append("thermal  cpu=\(o(t.cpu.map(fmt))) gpu=\(o(t.gpu.map(fmt))) hotspot=\(o(hotspot.map(fmt))) storage=\(o(t.storage.map(fmt))) battery=\(o(t.battery.map(fmt))) fans=\(o(t.fanCount)) rpm=\(t.fanRPM.map(fmt)) state=\(t.osThermalState)")
        lines.append("gpu      name=\(s.gpu.name) cores=\(o(s.gpu.cores)) util=\(fmt(s.gpu.utilization))% mem=\(o(s.gpu.memoryInUse.map(gb)))")
        lines.append("disk     \(s.disk.volumeName) \(s.disk.format) used=\(fmt(s.disk.usedPercent))% free=\(gb(s.disk.free)) read=\(fmt(s.disk.readRate / 1e6))MB/s write=\(fmt(s.disk.writeRate / 1e6))MB/s")
        let n = s.network
        lines.append("network  kind=\(n.kind) if=\(n.interface) std=\(o(n.standard)) band=\(o(n.band)) rssi=\(o(n.rssi)) down=\(fmt(n.downRate / 1e3))KB/s up=\(fmt(n.upRate / 1e3))KB/s")
        lines.append("apps     \(s.apps.count) groups")
        for a in s.apps.prefix(5) {
            lines.append("         \(a.name) cpu=\(fmt(a.cpu))% mem=\(gb(a.memory)) gpu=\(fmt(a.gpu))% procs=\(a.processCount) threads=\(a.threads) app=\(a.isApp)")
        }
        return lines.joined(separator: "\n")
    }

    nonisolated private static func fmt(_ x: Double) -> String { NumberText.fixed(x, 1) }
    nonisolated private static func gb(_ x: Double) -> String { NumberText.fixed(x / 1_073_741_824, 2) + "G" }

    /// Waits for a few samples, then writes PNGs of the flyout.
    static func render(store: PulseStore, to dir: URL) {
        try? FileManager.default.createDirectory(at: dir, withIntermediateDirectories: true)
        Task { @MainActor in
            try? await Task.sleep(nanoseconds: 6_000_000_000)
            let shots: [(String, String, ColorScheme, Route)] = [
                ("overview-en-light", "en", .light, .overview),
                ("overview-en-dark", "en", .dark, .overview),
                ("overview-ar-light", "ar", .light, .overview),
                ("overview-ar-dark", "ar", .dark, .overview),
                ("detail-cpu-en-light", "en", .light, .detail(.cpu)),
                ("detail-thm-ar-light", "ar", .light, .detail(.thm)),
                ("detail-net-en-dark", "en", .dark, .detail(.net)),
                ("settings-en-light", "en", .light, .settings),
                ("settings-ar-dark", "ar", .dark, .settings),
            ]
            for (name, code, scheme, route) in shots where store.registry.codes.contains(code) {
                store.localeCode = code
                store.route = route
                let view = FlyoutRoot(store: store, live: false)
                    .environment(\.colorScheme, scheme)
                    .frame(width: 420)
                let renderer = ImageRenderer(content: view)
                renderer.scale = 2
                renderer.proposedSize = ProposedViewSize(width: 420, height: nil)
                if let img = renderer.nsImage, let tiff = img.tiffRepresentation,
                   let rep = NSBitmapImageRep(data: tiff), let png = rep.representation(using: .png, properties: [:]) {
                    let url = dir.appendingPathComponent(name + ".png")
                    try? png.write(to: url)
                    print("wrote \(url.path)")
                }
            }
            exit(0)
        }
    }
}
#endif

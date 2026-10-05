#if canImport(AppKit)
import AppKit
import Combine
import PulseCore
import SwiftUI

enum DetailKind: Equatable, Hashable {
    case cpu, mem, nrg, thm, gpu, ssd, net
    case app(String)
}

enum Route: Equatable {
    case overview
    case detail(DetailKind)
    case settings
}

enum SortKey: String, CaseIterable { case cpu, mem, gpu }

/// An app Pulse ended, with what it takes to start it again.
struct EndedApp: Equatable {
    var name: String
    var bundleID: String?
    var bundleURL: URL?
    /// Pids still alive; they are hidden until the process exits.
    var pids: [Int32]
}

/// App state: telemetry, histories, locale and settings.
@MainActor
final class PulseStore: ObservableObject {
    static let interval: TimeInterval = 1.5

    // Telemetry
    @Published private(set) var snapshot = Snapshot()
    @Published private(set) var gpuHotspot: Double? = nil
    private(set) var history: [String: MetricHistory] = [:]
    private(set) var appHistory: [String: MetricHistory] = [:]
    @Published private(set) var hasData = false

    // Locale
    let registry: LocaleRegistry
    @Published private(set) var lc: PulseLocale
    /// The locale in use. Set it through `setLanguage` to keep the choice.
    @Published var localeCode: String { didSet { applyLocale() } }
    /// The saved language choice; nil means "Match system".
    @Published private(set) var languageChoice: String?

    // Settings
    @Published var fahrenheit: Bool { didSet { defaults.set(fahrenheit, forKey: "fahrenheit") } }
    @Published var live: Bool { didSet { defaults.set(live, forKey: "live"); scheduleTimer() } }
    /// nil follows the system; "light" or "dark" overrides it.
    @Published var themeOverride: String? { didSet { defaults.set(themeOverride, forKey: "theme") } }

    // Navigation and transient UI
    @Published var route: Route = .overview
    @Published var sort: SortKey = .cpu
    @Published var hoveredApp: String? = nil
    @Published private(set) var hogID: String? = nil
    @Published var hogDismissed = false
    @Published var confirm: AppUsage? = nil
    @Published private(set) var toast: String? = nil
    /// Apps Pulse ended, kept so Settings can relaunch them.
    @Published private(set) var endedApps: [EndedApp] = []
    var endedCount: Int { endedApps.count }

    // Demo switches (Settings): drawn over real data, never saved.
    @Published var simHog = false { didSet { if simHog != oldValue { if simHog { hogDismissed = false }; publish(record: true) } } }
    @Published var simCharging = false { didSet { if simCharging != oldValue { publish(record: true) } } }
    @Published var contentHeight: CGFloat = 600

    private let defaults = UserDefaults.standard
    private let engine = TelemetryEngine()
    private var hog = HogDetector(threshold: 50, window: 120)
    private var timer: Timer? = nil
    private var toastTask: Task<Void, Never>? = nil
    private var sampling = false
    /// The last real sample, before the demo overlay and ended apps.
    private var raw = Snapshot()
    private var detectedHog: String? = nil

    init() {
        let ud = UserDefaults.standard
        let reg = PulseStore.loadRegistry()
        let saved = ud.string(forKey: "locale").flatMap { reg.codes.contains($0) ? $0 : nil }
        let code = saved ?? PulseStore.systemCode(reg)
        registry = reg
        languageChoice = saved
        localeCode = code
        lc = (try? reg.get(code)) ?? (try! PulseLocale(LocaleData(code: "en")))
        fahrenheit = ud.bool(forKey: "fahrenheit")
        live = ud.object(forKey: "live") as? Bool ?? true
        themeOverride = ud.string(forKey: "theme")
        tick()
        scheduleTimer()
        // Follow a change to the system language while "Match system" is on.
        NotificationCenter.default.addObserver(forName: NSLocale.currentLocaleDidChangeNotification, object: nil, queue: .main) { [weak self] _ in
            Task { @MainActor in
                guard let self, self.languageChoice == nil else { return }
                self.localeCode = PulseStore.systemCode(self.registry)
            }
        }
    }

    // MARK: locale

    /// Reads the generated String Catalog: from Contents/Resources in the
    /// .app bundle, or from the SwiftPM resource bundle under `swift run`.
    static func loadRegistry() -> LocaleRegistry {
        var url = Bundle.main.url(forResource: "Localizable", withExtension: "xcstrings")
        if url == nil { url = Bundle.module.url(forResource: "Localizable", withExtension: "xcstrings") }
        if let u = url, let data = try? Data(contentsOf: u), let locales = try? XCStrings.read(data), !locales.isEmpty {
            return LocaleRegistry(locales)
        }
        NSLog("Pulse: Localizable.xcstrings is missing; UI shows string keys")
        return LocaleRegistry([LocaleData(code: "en")])
    }

    private func applyLocale() {
        if let l = try? registry.get(localeCode) { lc = l }
    }

    /// The first system language the app has, else English.
    static func systemCode(_ reg: LocaleRegistry) -> String {
        for pref in Locale.preferredLanguages {
            var parts = pref.split(whereSeparator: { $0 == "-" || $0 == "_" }).map(String.init)
            while !parts.isEmpty {
                let c = parts.joined(separator: "-")
                if reg.codes.contains(c) { return c }
                parts.removeLast()
            }
        }
        return reg.codes.contains("en") ? "en" : reg.resolve(nil)
    }

    /// Picks a language (nil follows the system), switches live and saves it.
    func setLanguage(_ code: String?) {
        let pick = code.flatMap { registry.codes.contains($0) ? $0 : nil }
        languageChoice = pick
        if let p = pick { defaults.set(p, forKey: "locale") } else { defaults.removeObject(forKey: "locale") }
        localeCode = pick ?? PulseStore.systemCode(registry)
    }

    var locales: [LocaleSummary] { registry.list() }
    var fmt: PulseFormat { PulseFormat(lc, fahrenheit: fahrenheit) }
    func t(_ key: String, _ p: [String: Arg] = [:]) -> String { lc.t(key, p) }
    func hw(_ key: String, _ p: [String: Arg] = [:]) -> String { lc.hw(key, p) }

    func cycleLocale() {
        let codes = registry.codes
        guard let i = codes.firstIndex(of: lc.code), codes.count > 1 else { return }
        setLanguage(codes[(i + 1) % codes.count])
    }

    // MARK: sampling

    private func scheduleTimer() {
        timer?.invalidate()
        timer = nil
        guard live else { return }
        let t = Timer(timeInterval: PulseStore.interval, repeats: true) { [weak self] _ in
            Task { @MainActor in self?.tick() }
        }
        t.tolerance = 0.2
        RunLoop.main.add(t, forMode: .common)
        timer = t
    }

    func tick() {
        guard !sampling else { return }
        sampling = true
        let apps = NSWorkspace.shared.runningApplications
            .filter { $0.activationPolicy == .regular && !$0.isTerminated }
            .map { AppIdentity(pid: $0.processIdentifier, name: $0.localizedName ?? $0.bundleIdentifier ?? "pid \($0.processIdentifier)", bundleID: $0.bundleIdentifier) }
        engine.sample(apps: apps) { [weak self] snap, hotspot in
            Task { @MainActor in self?.ingest(snap, hotspot: hotspot) }
        }
    }

    private func record(_ key: String, _ value: Double?, at time: TimeInterval) {
        guard let v = value, v.isFinite else { return }
        history[key, default: MetricHistory()].add(v, at: time)
    }

    private func ingest(_ s: Snapshot, hotspot: Double?) {
        sampling = false
        gpuHotspot = hotspot
        raw = s
        // Drop ended apps from the list once they are really gone.
        let alive = Set(s.apps.flatMap { $0.pids })
        for i in endedApps.indices { endedApps[i].pids = endedApps[i].pids.filter { alive.contains($0) } }
        publish(record: true, fresh: true)
        hasData = hasData || s.cpu.perCore.count > 0
    }

    /// Applies the demo overlay and ended apps to the last real sample and
    /// publishes it. Switch, End and Restore call this at once, so CPU,
    /// power, temperature and sparklines move even while polling is paused.
    private func publish(record: Bool, fresh: Bool = false) {
        let ended = Set(endedApps.flatMap { $0.pids })
        let r = DemoOverlay(hog: simHog, charging: simCharging).apply(raw, ended: ended)
        let s = r.snapshot
        let t = s.time
        // The first CPU reading has no previous ticks to compare with.
        if record && (hasData || s.cpu.perCore.count > 0) {
            record("cpu", s.cpu.total, at: t)
            record("mem", s.memory.usedPercent, at: t)
            record("nrg", s.power.drawWatts, at: t)
            record("thm", s.thermal.cpu, at: t)
            record("gpu", s.gpu.utilization, at: t)
            record("ssdR", s.disk.readRate, at: t)
            record("ssdW", s.disk.writeRate, at: t)
            record("netD", s.network.downRate, at: t)
            record("netU", s.network.upRate, at: t)
            let ids = Set(s.apps.prefix(40).map { $0.id })
            for a in s.apps.prefix(40) { appHistory[a.id, default: MetricHistory()].add(a.cpu, at: t) }
            appHistory = appHistory.filter { ids.contains($0.key) }
        }
        if fresh {
            let realApps = DemoOverlay().apply(raw, ended: ended).snapshot.apps
            detectedHog = hog.update(Dictionary(realApps.map { ($0.id, $0.cpu) }, uniquingKeysWith: { Swift.max($0, $1) }), at: t)
        }
        // The demo hog shows its banner at once; a real hog needs 2 minutes.
        let newHog = r.hogID ?? detectedHog
        if newHog != hogID {
            hogID = newHog
            if newHog != nil { hogDismissed = false }
        }
        snapshot = s
    }

    // MARK: derived values

    var hogApp: AppUsage? {
        guard let id = hogID else { return nil }
        return snapshot.apps.first { $0.id == id }
    }

    func app(_ id: String) -> AppUsage? { snapshot.apps.first { $0.id == id } }

    var topApps: [AppUsage] {
        let key = sort
        return Array(snapshot.apps.sorted { a, b in
            switch key {
            case .cpu: return a.cpu > b.cpu
            case .mem: return a.memory > b.memory
            case .gpu: return a.gpu == b.gpu ? a.cpu > b.cpu : a.gpu > b.gpu
            }
        }.prefix(5))
    }

    func values(_ key: String) -> [Double] { history[key]?.sparkValues ?? [] }
    func chart(_ key: String, live: Double?) -> [Double] { history[key]?.chartValues(live: live) ?? (live.map { [$0] } ?? []) }
    func appChart(_ id: String) -> [Double] { appHistory[id]?.chartValues(live: app(id)?.cpu) ?? [] }

    var thermalStage: ThermalStage {
        ThermalStage(celsius: snapshot.thermal.cpu ?? 0, osThermalState: snapshot.thermal.osThermalState)
    }

    // MARK: actions

    func open(_ r: Route) { route = r; hoveredApp = nil }
    func back() { route = .overview }

    func requestEnd(_ app: AppUsage) {
        guard app.isApp else { return }
        confirm = app
    }

    func confirmEnd() {
        guard let a = confirm else { return }
        confirm = nil
        let name = lc.app(a.name)
        guard let running = NSRunningApplication(processIdentifier: a.pid), !running.isTerminated else {
            showToast(t("endFailed", ["app": .text(name)]))
            return
        }
        let url = running.bundleURL
        let bundleID = running.bundleIdentifier ?? a.bundleID
        guard running.terminate() else {
            showToast(t("endFailed", ["app": .text(name)]))
            return
        }
        endedApps.append(EndedApp(name: a.name, bundleID: bundleID, bundleURL: url, pids: a.pids))
        // A hog that was just ended stops counting toward the alert.
        if hogID == a.id { hogDismissed = false }
        if case .detail(.app(a.id)) = route { route = .overview }
        publish(record: true)
        showToast(t("toastEnded", ["app": .text(name)]))
    }

    /// Relaunches every app Pulse ended, from the bundle URL saved at End.
    func restoreApps() {
        guard !endedApps.isEmpty else { return }
        let ws = NSWorkspace.shared
        for e in endedApps {
            let url = e.bundleURL ?? e.bundleID.flatMap { ws.urlForApplication(withBundleIdentifier: $0) }
            guard let u = url else { continue }
            let cfg = NSWorkspace.OpenConfiguration()
            cfg.activates = false
            ws.openApplication(at: u, configuration: cfg) { _, err in
                if let err { NSLog("Pulse: could not relaunch %@: %@", u.path, err.localizedDescription) }
            }
        }
        endedApps = []
        publish(record: true)
        showToast(t("toastRestored"))
        // Pick up the relaunched apps without waiting for the next poll.
        Task { @MainActor [weak self] in
            try? await Task.sleep(nanoseconds: 700_000_000)
            self?.tick()
        }
    }

    func openActivityMonitor() {
        let monitor = hw("activityMonitor")
        showToast(t("toastMonitor", ["monitor": .text(monitor)]))
        let ws = NSWorkspace.shared
        let url = ws.urlForApplication(withBundleIdentifier: "com.apple.ActivityMonitor")
            ?? URL(fileURLWithPath: "/System/Applications/Utilities/Activity Monitor.app")
        ws.openApplication(at: url, configuration: NSWorkspace.OpenConfiguration()) { _, _ in }
    }

    func showToast(_ text: String) {
        toastTask?.cancel()
        toast = text
        toastTask = Task { @MainActor [weak self] in
            try? await Task.sleep(nanoseconds: 2_800_000_000)
            if !Task.isCancelled { self?.toast = nil }
        }
    }

    // MARK: Tier 1 (menu bar readouts)

    /// "CPU 64%  RAM 58%  ⚡15.1W  ↓ 1.7 MB/s"
    func statusParts() -> [(text: String, alert: Bool)] {
        let f = fmt, s = snapshot
        var parts: [(text: String, alert: Bool)] = [
            ("\(t("barCpu")) \(f.pct(s.cpu.total))", hogApp != nil && !hogDismissed),
            ("\(t("barRam")) \(f.pct(s.memory.usedPercent))", s.memory.pressure != .normal),
        ]
        parts.append(("⚡" + (s.power.drawWatts.map { f.wattsShort($0) } ?? "—"), false))
        parts.append(("↓ " + f.rate(s.network.downRate), false))
        return parts
    }
}
#endif

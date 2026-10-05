#if canImport(AppKit)
import AppKit
import PulseCore
import SwiftUI

/// Everything a Tier 3 view shows for one metric or app.
struct DetailModel {
    var accent: Accent
    var accent2: Accent
    var title: String
    var hero: String
    var sub: String
    var a: [Double]
    var b: [Double]? = nil
    var aLabel = ""
    var bLabel = ""
    var fromZero = false
    var fa: (Double) -> String
    var fb: (Double) -> String = { _ in "" }
    var unit = ""
    var tiles: [(String, String)]
    var app: AppUsage? = nil
}

@MainActor
enum DetailBuilder {
    static let dash = "—"

    static func build(_ store: PulseStore, _ kind: DetailKind) -> DetailModel {
        let s = store.snapshot, f = store.fmt, lc = store.lc
        let t = { (k: String, p: [String: Arg]) in store.t(k, p) }
        switch kind {
        case .cpu:
            let c = s.cpu
            let cores = lc.plural("core", c.logicalCores)
            return DetailModel(
                accent: .cpu, accent2: .cpu, title: t("cpu", [:]), hero: f.pct(c.total),
                sub: t("cpuDetailSub", ["user": .text(f.pct(c.user)), "sys": .text(f.pct(c.system)), "cores": .text(cores)]),
                a: store.chart("cpu", live: c.total), fromZero: true, fa: { f.pct($0) }, unit: t("unitCpu", [:]),
                tiles: [
                    (t("tUser", [:]), f.pct(c.user)), (t("tSystem", [:]), f.pct(c.system)),
                    (t("tIdle", [:]), f.pct(max(0, 100 - c.total))),
                    (c.pClusterLoad != nil ? store.hw("coreP") : store.hw("user"), f.pct(c.pClusterLoad ?? c.user)),
                    (c.eClusterLoad != nil ? store.hw("coreE") : store.hw("kernel"), f.pct(c.eClusterLoad ?? c.system)),
                    (t("tLoad", [:]), f.n(c.load1, 2)),
                ])
        case .mem:
            let m = s.memory
            let gb = { (x: Double) in f.gib(x, 1) + " GB" }
            let pressure: String = {
                switch m.pressure {
                case .normal: return store.hw("normal")
                case .critical: return t("pressureCritical", [:])
                default: return t("pressureHigh", [:])
                }
            }()
            return DetailModel(
                accent: .mem, accent2: .mem, title: t("mem", [:]), hero: f.pct(m.usedPercent),
                sub: t("memInUse", ["used": .text(gb(m.used)), "total": .text(f.gib(m.total, 0) + " GB")]),
                a: store.chart("mem", live: m.usedPercent), fa: { f.pct($0) }, unit: t("unitRam", [:]),
                tiles: [
                    (t("segApp", [:]), gb(m.app)), (store.hw("wired"), gb(m.wired)),
                    (store.hw("compressed"), gb(m.compressed)), (t("segFree", [:]), gb(m.free)),
                    (store.hw("swapUsed"), m.swapTotal > 0 ? f.storage(m.swapUsed) + " / " + f.storage(m.swapTotal) : f.storage(m.swapUsed)),
                    (store.hw("pressure"), pressure),
                ])
        case .nrg:
            let p = s.power
            let hero = p.percent.map { f.pct($0) } ?? p.drawWatts.map { f.watts($0) } ?? dash
            var sub = EnergyText.subtitle(store) ?? ""
            if p.charging, let m = p.minutesToFull { sub = t("chargingFullIn", ["time": .text(f.duration(minutes: m))]) }
            else if p.hasBattery, !p.externalConnected, let m = p.minutesToEmpty { sub = t("onBattLeft", ["time": .text(f.duration(minutes: m))]) }
            let draw = (p.batteryWatts ?? p.drawWatts).map { EnergyText.flow(store, watts: $0) } ?? dash
            let left: String = p.charging ? (p.minutesToFull.map { t("fullIn", ["time": .text(f.duration(minutes: $0))]) } ?? dash)
                : (p.minutesToEmpty.map { f.duration(minutes: $0) } ?? dash)
            let source = !p.hasBattery ? t("acPower", [:])
                : p.externalConnected
                ? (p.adapterName ?? p.adapterWatts.map { f.n($0) + " W" } ?? store.hw("connected"))
                : t("tBattery", [:])
            let capacity: String = {
                guard let c = p.capacityWh else { return dash }
                if let d = p.designWh { return f.n(c, 1) + " / " + f.n(d, 1) + " Wh" }
                return f.n(c, 1) + " Wh"
            }()
            return DetailModel(
                accent: .nrg, accent2: .nrg, title: t("nrg", [:]), hero: hero, sub: sub,
                a: store.chart("nrg", live: p.drawWatts), fromZero: true, fa: { f.watts($0) },
                tiles: [
                    (t("tDraw", [:]), draw), (t("tTimeLeft", [:]), p.hasBattery ? left : t("noBattery", [:])),
                    (t("tHealth", [:]), p.healthPercent.map { f.pct($0) } ?? dash),
                    (t("tCycles", [:]), p.cycles.map { f.n(Double($0)) } ?? dash),
                    (t("tCapacity", [:]), capacity), (t("tSource", [:]), source),
                ])
        case .thm:
            let th = s.thermal, stage = store.thermalStage
            let fans: String = {
                guard let n = th.fanCount else { return dash }
                if n == 0 { return store.hw("fanless") }
                let rpm = th.fanRPM.max() ?? 0
                return rpm < 1 ? store.hw("fansOff", ["rpm": .text(f.rpm(0))]) : f.rpm(rpm)
            }()
            let throttling = th.osThermalState >= 2 ? t("throttled", [:]) : f.pct(0)
            return DetailModel(
                accent: stage.accent, accent2: stage.accent, title: t("thm", [:]), hero: th.cpu.map { f.temp($0) } ?? dash,
                sub: lc.join([t(stage.key, [:]), stage == .throttled ? t("throttling", [:]) : t("zero", [:])]),
                a: store.chart("thm", live: th.cpu), fa: { f.temp($0, 1) },
                tiles: [
                    (t("tCpuDie", [:]), th.cpu.map { f.temp($0, 1) } ?? dash), (t("gpu", [:]), th.gpu.map { f.temp($0, 1) } ?? dash),
                    (t("tStorage", [:]), th.storage.map { f.temp($0) } ?? dash), (t("tBattery", [:]), th.battery.map { f.temp($0) } ?? dash),
                    (t("tFans", [:]), fans), (t("tThrottling", [:]), throttling),
                ])
        case .gpu:
            let g = s.gpu, temp = s.thermal.gpu
            let vram: String = g.memoryInUse.map { m in
                g.unifiedMemory ? store.hw("vramUnified", ["size": .text(f.storage(m))]) : store.hw("vramShared", ["size": .text(f.storage(m))])
            } ?? dash
            return DetailModel(
                accent: .gpu, accent2: .gpu, title: t("gpu", [:]), hero: f.pct(g.utilization),
                sub: lc.join([GPUText.name(g, lc), temp.map { f.temp($0) }]),
                a: store.chart("gpu", live: g.utilization), fromZero: true, fa: { f.pct($0) }, unit: t("unitGpu", [:]),
                tiles: [
                    (t("util", [:]), f.pct(g.utilization)), (t("tTemperature", [:]), temp.map { f.temp($0, 1) } ?? dash),
                    (t("tVideoMem", [:]), vram), (t("tCoreClock", [:]), dash), (t("tPower", [:]), dash),
                    (t("tHotspot", [:]), store.gpuHotspot.map { f.temp($0, 1) } ?? dash),
                ])
        case .ssd:
            let d = s.disk
            return DetailModel(
                accent: .cpu, accent2: .nrg, title: t("storage", [:]), hero: f.pct(d.usedPercent),
                sub: lc.join([d.volumeName, t("free", ["value": .text(f.storage(d.free))])]),
                a: store.chart("ssdR", live: d.readRate), b: store.chart("ssdW", live: d.writeRate),
                aLabel: t("read", [:]), bLabel: t("write", [:]), fa: { f.rate($0) }, fb: { f.rate($0) },
                tiles: [
                    (t("tUsed", [:]), f.storage(d.used)), (t("tFree", [:]), f.storage(d.free)),
                    (t("read", [:]), f.rate(d.readRate)), (t("write", [:]), f.rate(d.writeRate)),
                    (t("tCapacity", [:]), f.storage(d.total)), (t("tFormat", [:]), d.format),
                ])
        case .net:
            let n = s.network
            let state = (n.rssi ?? -100) >= -60 ? store.hw("strongSignal") : store.hw("connected")
            let sub: String = n.kind == .none ? dash
                : store.hw("netSub", ["link": .text(n.standard ?? n.interface), "detail": .text(n.band ?? n.interface), "state": .text(state)])
            let signal: (String, String) = n.rssi.map { (store.hw("signal"), f.n(Double($0)).replacingOccurrences(of: "-", with: "−") + " dBm") }
                ?? (store.hw("linkSpeed"), n.txRateMbps.map { f.n($0) + " Mbps" } ?? dash)
            return DetailModel(
                accent: .mem, accent2: .cpu, title: t("network", [:]), hero: f.rate(n.downRate), sub: sub,
                a: store.chart("netD", live: n.downRate), b: store.chart("netU", live: n.upRate),
                aLabel: t("down", [:]), bLabel: t("up", [:]), fa: { "↓ " + f.rate($0) }, fb: { "↑ " + f.rate($0) },
                tiles: [
                    (t("down", [:]), f.rate(n.downRate)), (t("up", [:]), f.rate(n.upRate)),
                    (t("tInterface", [:]), lc.join([n.standard, n.interface.isEmpty ? nil : n.interface])),
                    signal,
                    (store.hw("linkSpeed"), n.txRateMbps.map { f.n($0) + " Mbps" } ?? dash),
                    (t("tToday", [:]), "↓ " + f.storage(n.todayDown) + " · ↑ " + f.storage(n.todayUp)),
                ])
        case .app(let id):
            let app = store.app(id)
            let hog = store.hogApp?.id == id
            let name = app.map { lc.app($0.name) } ?? id
            return DetailModel(
                accent: hog ? .warn : .cpu, accent2: hog ? .warn : .cpu, title: name, hero: f.pct(app?.cpu ?? 0, 1),
                sub: app.map { lc.join([lc.plural("process", $0.processCount), lc.plural("thread", $0.threads), "PID \($0.pid)"]) } ?? dash,
                a: store.appChart(id), fromZero: true, fa: { f.pct($0, 1) }, unit: t("unitOfCpu", [:]),
                tiles: [
                    (t("cpu", [:]), f.pct(app?.cpu ?? 0, 1)), (t("mem", [:]), f.memory(app?.memory ?? 0)),
                    (t("gpu", [:]), f.pct(app?.gpu ?? 0)), (t("tipProc", [:]), f.n(Double(app?.processCount ?? 0))),
                    (t("tThreads", [:]), f.n(Double(app?.threads ?? 0))), ("PID", app.map { String($0.pid) } ?? dash),
                ],
                app: app)
        }
    }
}

/// Tier 3: hero, 10-minute chart with scrubbing, six tiles.
@MainActor
struct DetailView: View {
    @ObservedObject var store: PulseStore
    let kind: DetailKind
    @Environment(\.palette) private var pal
    @Environment(\.typo) private var typo

    var body: some View {
        let d = DetailBuilder.build(store, kind)
        VStack(alignment: .leading, spacing: 10) {
            HStack(spacing: 8) {
                BackButton(label: store.t("back")) { store.back() }
                HStack(spacing: 6) {
                    Circle().fill(pal.color(d.accent)).frame(width: 8, height: 8)
                        .padding(4).background(Circle().fill(pal.tint(d.accent, 20)))
                    Text(d.title).font(typo.text(13.5, .semibold)).foregroundColor(pal.ink(d.accent)).lineLimit(1).truncationMode(.tail)
                }
                .padding(.horizontal, 8).padding(.vertical, 4)
                .background(Capsule().fill(pal.tint(d.accent)))
                Spacer(minLength: 4)
                if let app = d.app, app.isApp {
                    Button { store.requestEnd(app) } label: {
                        Text(store.t("endApp")).font(typo.text(12, .semibold)).foregroundColor(pal.ink(.crit))
                            .padding(.horizontal, 10).padding(.vertical, 5)
                            .background(RoundedRectangle(cornerRadius: Radius.btn).strokeBorder(pal.line(.crit)))
                    }
                    .buttonStyle(.plain)
                }
            }
            HStack(alignment: .firstTextBaseline, spacing: 8) {
                HeroText(text: d.hero, size: 34)
                Label1(d.sub, size: 12.5)
            }
            CardBox(padding: 12) {
                VStack(alignment: .leading, spacing: 8) {
                    HStack(spacing: 8) {
                        Text(store.t("last10")).font(typo.text(12.5, .semibold)).foregroundColor(pal.ink2).lineLimit(1)
                        if d.b != nil {
                            LegendDot(color: pal.color(d.accent), label: d.aLabel)
                            LegendDot(color: pal.color(d.accent2), label: d.bLabel)
                        }
                        Spacer(minLength: 4)
                        ValueText(stat(d), size: 11, weight: .regular, color: pal.ink3)
                    }
                    HistoryChart(store: store, model: d)
                    HStack {
                        Label1(store.t("ago10"), size: 10.5, color: pal.ink3)
                        Spacer()
                        Label1(store.t("ago5"), size: 10.5, color: pal.ink3)
                        Spacer()
                        Label1(store.t("now"), size: 10.5, color: pal.ink3)
                    }
                    .environment(\.layoutDirection, .leftToRight)
                }
            }
            LazyVGrid(columns: [GridItem(.flexible(), spacing: 8), GridItem(.flexible(), spacing: 8)], spacing: 8) {
                ForEach(Array(d.tiles.enumerated()), id: \.offset) { _, tile in
                    CardBox(padding: 11) {
                        VStack(alignment: .leading, spacing: 3) {
                            Label1(tile.0, size: 11.5)
                            ValueText(tile.1, size: 15, weight: .bold)
                        }
                    }
                    .accessibilityElement(children: .combine)
                }
            }
        }
    }

    private func stat(_ d: DetailModel) -> String {
        if let b = d.b {
            return store.t("peakSplit", ["a": .text(d.fa(d.a.max() ?? 0)), "b": .text(d.fb(b.max() ?? 0))])
        }
        guard !d.a.isEmpty else { return "" }
        let avg = d.a.reduce(0, +) / Double(d.a.count)
        return store.t("peakAvg", ["peak": .text(d.fa(d.a.max() ?? 0)), "avg": .text(d.fa(avg))])
    }
}

@MainActor
struct LegendDot: View {
    let color: Color
    let label: String
    var body: some View {
        HStack(spacing: 4) {
            Circle().fill(color).frame(width: 6, height: 6)
            Label1(label, size: 11)
        }
    }
}

@MainActor
struct BackButton: View {
    let label: String
    let action: () -> Void
    @Environment(\.palette) private var pal
    @Environment(\.typo) private var typo

    var body: some View {
        Button(action: action) {
            HStack(spacing: 4) {
                Image(systemName: "chevron.backward").font(.system(size: 11, weight: .semibold))
                Text(label).font(typo.text(12.5, .medium))
            }
            .foregroundColor(pal.ink)
            .padding(.horizontal, 10).padding(.vertical, 6)
            .background(RoundedRectangle(cornerRadius: Radius.btn, style: .continuous).fill(pal.card))
            .overlay(RoundedRectangle(cornerRadius: Radius.btn, style: .continuous).strokeBorder(pal.cardBorder))
        }
        .buttonStyle(.plain)
        .keyboardShortcut("[", modifiers: .command)
    }
}

/// The 10-minute chart: 121 slots at 5 s, newest on the right in every locale.
@MainActor
struct HistoryChart: View {
    @ObservedObject var store: PulseStore
    let model: DetailModel
    @State private var hover: Int? = nil
    @Environment(\.palette) private var pal
    @Environment(\.typo) private var typo
    static let slots = 121
    static let height: CGFloat = 140

    var body: some View {
        GeometryReader { g in
            layers(width: g.size.width)
        }
        .frame(height: HistoryChart.height)
        .environment(\.layoutDirection, .leftToRight)
        .accessibilityElement()
        .accessibilityLabel(store.t("last10"))
    }

    private func points(width w: CGFloat) -> ([ChartMath.Point], [ChartMath.Point]) {
        let h = Double(HistoryChart.height)
        if let b = model.b {
            return ChartMath.splitPoints(model.a, b, slots: HistoryChart.slots, width: Double(w), height: h)
        }
        return (ChartMath.chartPoints(model.a, slots: HistoryChart.slots, width: Double(w), height: h, fromZero: model.fromZero), [])
    }

    private func layers(width w: CGFloat) -> some View {
        let pts = points(width: w)
        return ZStack(alignment: .topLeading) {
            seriesLayer(pts.0, pts.1, width: w)
            scrubLayer(pts.0, pts.1, width: w)
        }
        .contentShape(Rectangle())
        .onContinuousHover { phase in
            switch phase {
            case .active(let loc):
                let fr = Double(max(0, min(1, loc.x / max(1, w))))
                hover = Int((fr * Double(HistoryChart.slots - 1)).rounded())
            case .ended:
                hover = nil
            }
        }
    }

    private func line(_ y: CGFloat, width w: CGFloat) -> Path {
        var p = Path()
        p.move(to: CGPoint(x: 0, y: y))
        p.addLine(to: CGPoint(x: w, y: y))
        return p
    }

    @ViewBuilder
    private func seriesLayer(_ pa: [ChartMath.Point], _ pb: [ChartMath.Point], width w: CGFloat) -> some View {
        let h = HistoryChart.height
        let c1 = pal.color(model.accent), c2 = pal.color(model.accent2)
        if model.b == nil {
            ForEach(1..<4) { k in
                line(h * CGFloat(k) / 4, width: w).stroke(pal.hair, style: StrokeStyle(lineWidth: 1, dash: [2, 4]))
            }
            SmoothPath(points: pa, closeTo: h)
                .fill(LinearGradient(colors: [c1.opacity(0.28), c1.opacity(0)], startPoint: .top, endPoint: .bottom))
            SmoothPath(points: pa).stroke(c1, style: StrokeStyle(lineWidth: 2, lineCap: .round, lineJoin: .round))
        } else {
            line(h / 2, width: w).stroke(pal.hair, lineWidth: 1)
            SmoothPath(points: pa, closeTo: h / 2).fill(c1.opacity(0.22))
            SmoothPath(points: pa).stroke(c1, lineWidth: 1.8)
            SmoothPath(points: pb, closeTo: h / 2).fill(c2.opacity(0.22))
            SmoothPath(points: pb).stroke(c2, lineWidth: 1.8)
        }
    }

    @ViewBuilder
    private func scrubLayer(_ pa: [ChartMath.Point], _ pb: [ChartMath.Point], width w: CGFloat) -> some View {
        if let i = hover, let pt = point(pa, slot: i) {
            let x = CGFloat(pt.x)
            Path { p in
                p.move(to: CGPoint(x: x, y: 0))
                p.addLine(to: CGPoint(x: x, y: HistoryChart.height))
            }
            .stroke(pal.ink3, style: StrokeStyle(lineWidth: 1, dash: [3, 3]))
            dot(pt, model.accent)
            if let pt2 = point(pb, slot: i) { dot(pt2, model.accent2) }
            Text(scrubLabel(slot: i))
                .font(typo.text(11.5, .semibold))
                .foregroundColor(pal.ink)
                .lineLimit(1)
                .padding(.horizontal, 9)
                .padding(.vertical, 4)
                .background(Capsule().fill(pal.tipBg).shadow(color: .black.opacity(0.14), radius: 8, y: 3))
                .fixedSize()
                .position(x: min(max(x, w * 0.16 + 40), w * 0.84 - 40), y: 14)
        }
    }

    private func dot(_ p: ChartMath.Point, _ a: Accent) -> some View {
        Circle().fill(pal.color(a)).frame(width: 9, height: 9)
            .overlay(Circle().stroke(pal.tipBg, lineWidth: 2))
            .position(x: CGFloat(p.x), y: CGFloat(p.y))
    }

    /// Maps a chart slot to the point drawn there (series fill from the right).
    private func point(_ pts: [ChartMath.Point], slot: Int) -> ChartMath.Point? {
        let offset = HistoryChart.slots - pts.count
        let i = slot - offset
        return i >= 0 && i < pts.count ? pts[i] : nil
    }

    private func value(_ series: [Double], slot: Int) -> Double? {
        let i = slot - (HistoryChart.slots - series.count)
        return i >= 0 && i < series.count ? series[i] : nil
    }

    private func scrubLabel(slot: Int) -> String {
        let f = store.fmt
        let ago = f.ago(seconds: (HistoryChart.slots - 1 - slot) * 5)
        if let b = model.b {
            return store.lc.join([ago, value(model.a, slot: slot).map(model.fa), value(b, slot: slot).map(model.fb)])
        }
        let v = value(model.a, slot: slot).map { model.fa($0) + (model.unit.isEmpty ? "" : " " + model.unit) }
        return store.lc.join([ago, v])
    }
}

@MainActor
struct SettingsView: View {
    @ObservedObject var store: PulseStore
    @Environment(\.palette) private var pal
    @Environment(\.typo) private var typo

    var body: some View {
        let f = store.fmt
        VStack(alignment: .leading, spacing: 10) {
            HStack(spacing: 10) {
                BackButton(label: store.t("back")) { store.back() }
                Text(store.t("aSettings")).font(typo.text(15, .semibold)).foregroundColor(pal.ink)
            }
            CardBox(padding: 0) {
                VStack(spacing: 0) {
                    row(title: store.t("simHog"), sub: store.t("simHogSub", ["pct": .text(f.pct(50))])) {
                        PulseSwitch(on: $store.simHog, label: store.t("simHog"))
                    }
                    Divider().overlay(pal.hair)
                    row(title: store.t("simCharging"), sub: store.t("simChargingSub", ["adapter": .text(DemoOverlay.adapter)])) {
                        PulseSwitch(on: $store.simCharging, label: store.t("simCharging"))
                    }
                    Divider().overlay(pal.hair)
                    row(title: store.t("simLive"),
                        sub: store.t("simLiveSub", ["secs": .text(store.t("seconds", ["n": .text(f.n(PulseStore.interval, 1))]))])) {
                        PulseSwitch(on: $store.live, label: store.t("simLive"))
                    }
                    Divider().overlay(pal.hair)
                    row(title: store.t("tempUnit"), sub: nil) {
                        Segmented(items: [Segmented.Item(id: "c", label: "°C", ltr: true), Segmented.Item(id: "f", label: "°F", ltr: true)],
                                  selected: store.fahrenheit ? "f" : "c", onSelect: { store.fahrenheit = $0 == "f" },
                                  accessibilityLabel: store.t("tempUnit"))
                    }
                    Divider().overlay(pal.hair)
                    row(title: store.t("language"), sub: store.t("languageSub")) {
                        LanguagePicker(store: store)
                    }
                    Divider().overlay(pal.hair)
                    row(title: store.t("restore"), sub: store.lc.plural("ended", store.endedCount)) {
                        Button { store.restoreApps() } label: {
                            Text(store.t("restoreBtn")).font(typo.text(12.5, .semibold))
                                .foregroundColor(store.endedCount > 0 ? pal.ink : pal.ink3)
                                .padding(.horizontal, 12).padding(.vertical, 5)
                                .background(Capsule().fill(pal.trackStrong))
                        }
                        .buttonStyle(.plain)
                        .disabled(store.endedCount == 0)
                        .accessibilityLabel(store.t("restore"))
                    }
                }
            }
            Label1(store.t("simNote"), size: 11.5, color: pal.ink3)
                .padding(.horizontal, 4)
        }
    }

    private func row<Trailing: View>(title: String, sub: String?, @ViewBuilder trailing: () -> Trailing) -> some View {
        HStack(spacing: 10) {
            VStack(alignment: .leading, spacing: 2) {
                Text(title).font(typo.text(13.5, .semibold)).foregroundColor(pal.ink).lineLimit(1).truncationMode(.tail)
                if let s = sub { Label1(s, size: 11.5) }
            }
            Spacer(minLength: 8)
            trailing()
        }
        .padding(.horizontal, 14)
        .padding(.vertical, 11)
    }
}
/// Native popup (NSPopUpButton) with "Match system" and every locale by
/// its own name. It lists whatever the catalog holds, so a new locale file
/// shows up here with no code change.
@MainActor
struct LanguagePicker: View {
    @ObservedObject var store: PulseStore

    var body: some View {
        Picker(store.t("language"), selection: Binding(
            get: { store.languageChoice ?? "" },
            set: { store.setLanguage($0.isEmpty ? nil : $0) }
        )) {
            Text(store.t("langSystem")).tag("")
            Divider()
            ForEach(store.locales, id: \.code) { l in
                Text(l.name).tag(l.code)
            }
        }
        .pickerStyle(.menu)
        .labelsHidden()
        .fixedSize()
        .accessibilityLabel(store.t("language"))
    }
}
#endif

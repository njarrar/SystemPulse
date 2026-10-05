#if canImport(AppKit)
import AppKit
import PulseCore
import SwiftUI

/// Tier 2: the card grid and Top Active Apps.
@MainActor
struct OverviewView: View {
    @ObservedObject var store: PulseStore

    var body: some View {
        VStack(spacing: 8) {
            HStack(alignment: .top, spacing: 8) {
                CPUCard(store: store).frame(maxHeight: .infinity)
                MemoryCard(store: store).frame(maxHeight: .infinity)
            }
            .fixedSize(horizontal: false, vertical: true)
            HStack(alignment: .top, spacing: 8) {
                EnergyCard(store: store).frame(maxHeight: .infinity)
                ThermalCard(store: store).frame(maxHeight: .infinity)
            }
            .fixedSize(horizontal: false, vertical: true)
            GPUStrip(store: store)
            HStack(alignment: .top, spacing: 8) {
                StorageCard(store: store).frame(maxHeight: .infinity)
                NetworkCard(store: store).frame(maxHeight: .infinity)
            }
            .fixedSize(horizontal: false, vertical: true)
            TopApps(store: store)
        }
    }
}

/// A card that opens its detail view on click or Return.
@MainActor
struct MetricCard<Content: View>: View {
    @ObservedObject var store: PulseStore
    let kind: DetailKind
    let symbol: String
    let accent: Accent
    let title: String
    var badge: (String, Accent)? = nil
    var border: Color? = nil
    var a11y: String
    @ViewBuilder var content: () -> Content
    @Environment(\.palette) private var pal

    var body: some View {
        Button { store.open(.detail(kind)) } label: {
            CardBox(border: border) {
                VStack(alignment: .leading, spacing: 6) {
                    HStack(spacing: 8) {
                        IconTile(symbol: symbol, accent: accent)
                        Label1(title, size: 13, weight: .semibold, color: pal.ink2)
                        Spacer(minLength: 2)
                        if let b = badge { Badge(text: b.0, accent: b.1) }
                        Image(systemName: "chevron.forward").font(.system(size: 10, weight: .semibold)).foregroundColor(pal.ink3)
                    }
                    content()
                    Spacer(minLength: 0)
                }
                .frame(maxHeight: .infinity, alignment: .top)
            }
            .contentShape(RoundedRectangle(cornerRadius: Radius.card))
        }
        .buttonStyle(.plain)
        .accessibilityLabel(a11y)
        .accessibilityHint(store.t("tipClick"))
    }
}

@MainActor
struct BarRow: View {
    let label: String
    let fraction: Double
    let value: String
    let accent: Accent
    var soft = false
    @Environment(\.palette) private var pal

    var body: some View {
        HStack(spacing: 6) {
            Label1(label, size: 11.5).frame(width: 64, alignment: .leading)
            Meter(fraction: fraction, fill: AnyShapeStyle(pal.color(accent).opacity(soft ? 0.6 : 1)), height: 4)
            ValueText(value, size: 11.5, weight: .semibold).frame(width: 34, alignment: .trailing)
        }
    }
}

@MainActor
struct CPUCard: View {
    @ObservedObject var store: PulseStore
    @Environment(\.palette) private var pal

    var body: some View {
        let s = store.snapshot.cpu, f = store.fmt
        let high = s.total >= 50
        let pLabel = s.pClusterLoad != nil ? store.hw("coreP") : store.hw("user")
        let eLabel = s.eClusterLoad != nil ? store.hw("coreE") : store.hw("kernel")
        let pVal = s.pClusterLoad ?? s.user, eVal = s.eClusterLoad ?? s.system
        MetricCard(store: store, kind: .cpu, symbol: "cpu", accent: .cpu, title: store.t("cpu"),
                   badge: high ? (store.t("high"), Accent.warn) : nil, border: high ? pal.line(.warn) : nil,
                   a11y: store.t("cpu") + " " + f.pct(s.total)) {
            HeroText(text: f.pct(s.total), color: high ? pal.ink(.warn) : pal.ink)
            Label1(store.t("userSys", ["user": .text(f.pct(s.user)), "sys": .text(f.pct(s.system))]), size: 11.5)
            VStack(spacing: 4) {
                BarRow(label: pLabel, fraction: pVal / 100, value: f.pct(pVal), accent: .cpu)
                BarRow(label: eLabel, fraction: eVal / 100, value: f.pct(eVal), accent: .cpu, soft: true)
                BarRow(label: store.t("gpu"), fraction: store.snapshot.gpu.utilization / 100, value: f.pct(store.snapshot.gpu.utilization), accent: .gpu)
            }
            .padding(.top, 2)
            Sparkline(values: store.values("cpu"), accent: .cpu, label: store.t("min1"))
        }
    }
}

@MainActor
struct MemoryCard: View {
    @ObservedObject var store: PulseStore
    @Environment(\.palette) private var pal

    var body: some View {
        let m = store.snapshot.memory, f = store.fmt
        let gb = { (b: Double) in f.gib(b, 1) + " GB" }
        let pressureAccent: Accent = m.pressure == .normal ? .cpu : m.pressure == .critical ? .crit : .warn
        let pressureText: String = {
            switch m.pressure {
            case .normal: return store.t("pressure")
            case .critical: return store.t("pressureCriticalShort")
            default: return store.t("pressureHighShort")
            }
        }()
        let pressure = (pressureText, pressureAccent)
        MetricCard(store: store, kind: .mem, symbol: "memorychip", accent: .mem, title: store.t("mem"),
                   a11y: store.t("mem") + " " + f.pct(m.usedPercent)) {
            HStack(spacing: 6) {
                HeroText(text: f.pct(m.usedPercent))
                Badge(text: pressure.0, accent: pressure.1, dot: true)
            }
            Label1(store.t("memOf", ["used": .text(gb(m.used)), "total": .text(f.gib(m.total, 0) + " GB")]), size: 11.5)
            StackedMemoryBar(memory: m)
            Grid(alignment: .leading, horizontalSpacing: 8, verticalSpacing: 2) {
                GridRow {
                    LegendItem(color: pal.color(.mem), label: store.t("segApp"), value: gb(m.app))
                    LegendItem(color: pal.color(.mem).opacity(0.55), label: store.hw("wired"), value: gb(m.wired))
                }
                GridRow {
                    LegendItem(color: pal.color(.mem).opacity(0.3), label: store.hw("compressed"), value: gb(m.compressed), hatch: true)
                    LegendItem(color: pal.trackStrong, label: store.t("segFree"), value: gb(m.free))
                }
            }
            Sparkline(values: store.values("mem"), accent: .mem, label: store.t("min1"))
        }
    }
}

@MainActor
struct StackedMemoryBar: View {
    let memory: MemoryReading
    @Environment(\.palette) private var pal

    var body: some View {
        GeometryReader { g in
            let t = max(1, memory.total), w = g.size.width
            HStack(spacing: 1.5) {
                Rectangle().fill(pal.color(.mem)).frame(width: w * memory.app / t)
                Rectangle().fill(pal.color(.mem).opacity(0.55)).frame(width: w * memory.wired / t)
                Rectangle().fill(pal.color(.mem).opacity(0.3)).frame(width: w * memory.compressed / t)
                Spacer(minLength: 0)
            }
            .background(pal.track)
            .clipShape(Capsule())
        }
        .frame(height: 7)
        .animation(.easeOut(duration: 0.4), value: memory.used)
    }
}

@MainActor
struct LegendItem: View {
    let color: Color
    let label: String
    let value: String
    var hatch = false

    var body: some View {
        HStack(spacing: 4) {
            RoundedRectangle(cornerRadius: 2).fill(color).frame(width: 7, height: 7)
                .overlay(hatch ? AnyView(Image(systemName: "line.diagonal").font(.system(size: 6)).foregroundColor(.white)) : AnyView(EmptyView()))
            Label1(label, size: 11)
            ValueText(value, size: 11, weight: .regular)
        }
    }
}

@MainActor
struct EnergyCard: View {
    @ObservedObject var store: PulseStore
    @Environment(\.palette) private var pal
    @Environment(\.typo) private var typo

    var body: some View {
        let p = store.snapshot.power, f = store.fmt
        let hero = p.percent.map { f.pct($0) } ?? p.drawWatts.map { f.watts($0) } ?? "—"
        MetricCard(store: store, kind: .nrg, symbol: "bolt", accent: .nrg, title: store.t("nrg"),
                   a11y: store.t("nrg") + " " + hero) {
            HeroText(text: hero)
            if let sub = EnergyText.subtitle(store) { Label1(sub, size: 11.5) }
            if let w = p.batteryWatts ?? p.drawWatts, p.hasBattery {
                HStack(spacing: 8) {
                    HStack(spacing: 5) {
                        Circle().fill(pal.color(.nrg)).frame(width: 6, height: 6)
                        ValueText(EnergyText.flow(store, watts: w), size: 12.5, weight: .bold, color: pal.ink(.nrg))
                    }
                    .padding(.horizontal, 8).padding(.vertical, 3)
                    .background(Capsule().fill(pal.tint(.nrg)))
                    .overlay(Capsule().strokeBorder(pal.line(.nrg)))
                    Label1(EnergyText.flowLabel(store), size: 11.5)
                }
            }
            if let h = p.healthPercent, let c = p.cycles {
                Label1(store.t("health", ["pct": .text(f.pct(h)), "cycles": .text(store.lc.plural("cycle", c))]), size: 11.5)
            }
            Sparkline(values: store.values("nrg"), accent: .nrg, label: store.t("min1"))
        }
    }
}

@MainActor
enum EnergyText {
    static func subtitle(_ store: PulseStore) -> String? {
        let p = store.snapshot.power, f = store.fmt
        // A desktop Mac has no battery and always runs on AC power.
        if !p.hasBattery { return store.t("acPower") }
        if p.charging, let m = p.minutesToFull { return store.t("fullIn", ["time": .text(f.duration(minutes: m))]) }
        if !p.externalConnected, let m = p.minutesToEmpty { return store.t("timeLeft", ["time": .text(f.duration(minutes: m))]) }
        if p.externalConnected { return store.hw("connected") }
        return nil
    }

    static func flow(_ store: PulseStore, watts: Double) -> String {
        let f = store.fmt, p = store.snapshot.power
        if p.charging { return "+" + f.watts(abs(watts)) }
        if p.externalConnected { return f.watts(abs(watts)) }
        return "−" + f.watts(abs(watts))
    }

    static func flowLabel(_ store: PulseStore) -> String {
        let p = store.snapshot.power
        if p.charging { return store.t("charging") }
        if p.externalConnected { return store.hw("connected") }
        return store.t("onBatt")
    }
}

extension ThermalStage {
    var accent: Accent { [.mem, .nrg, .thm, .crit][rawValue] }
}

@MainActor
struct ThermalCard: View {
    @ObservedObject var store: PulseStore
    @Environment(\.palette) private var pal
    @Environment(\.typo) private var typo

    var body: some View {
        let th = store.snapshot.thermal, f = store.fmt
        let stage = store.thermalStage
        let names = ThermalStage.allCases.map { store.t($0.key) }
        MetricCard(store: store, kind: .thm, symbol: "thermometer.medium", accent: .thm, title: store.t("thm"),
                   a11y: store.t("thm") + " " + names[stage.rawValue]) {
            HeroText(text: names[stage.rawValue])
            Label1(store.t("tempLine", ["cpu": .text(th.cpu.map { f.temp($0) } ?? "—"), "gpu": .text(th.gpu.map { f.temp($0) } ?? "—")]), size: 11.5)
            HStack(spacing: 3) {
                ForEach(ThermalStage.allCases, id: \.rawValue) { s in
                    Capsule().fill(s == stage ? pal.color(stage.accent) : pal.track).frame(height: 5)
                }
            }
            HStack(spacing: 3) {
                ForEach(ThermalStage.allCases, id: \.rawValue) { s in
                    Text(names[s.rawValue])
                        .font(typo.text(10.5, s == stage ? .bold : .medium))
                        .foregroundColor(s == stage ? pal.ink(stage.accent) : pal.ink3)
                        .lineLimit(1).truncationMode(.tail)
                        .frame(maxWidth: .infinity, alignment: .leading)
                }
            }
            Label1(stage == .throttled ? store.t("throttling") : store.t("zero"), size: 11.5,
                   color: stage == .throttled ? pal.ink(.crit) : pal.ink3)
            Sparkline(values: store.values("thm"), accent: .nrg, label: store.t("min1"))
        }
    }
}

@MainActor
struct GPUStrip: View {
    @ObservedObject var store: PulseStore
    @Environment(\.palette) private var pal

    var body: some View {
        let g = store.snapshot.gpu, f = store.fmt, t = store.snapshot.thermal.gpu
        let tAccent: Accent = (t ?? 0) < 60 ? .mem : (t ?? 0) < 80 ? .nrg : .thm
        Button { store.open(.detail(.gpu)) } label: {
            CardBox(padding: 9) {
                HStack(spacing: 8) {
                    IconTile(symbol: "rectangle.3.group", accent: .gpu, size: 24)
                    Label1(store.t("gpu"), size: 13, weight: .semibold, color: pal.ink2).fixedSize()
                    Text(GPUText.name(g, store.lc)).font(.system(size: 11.5)).foregroundColor(pal.ink3).lineLimit(1).truncationMode(.tail)
                        .environment(\.layoutDirection, .leftToRight)
                    Spacer(minLength: 4)
                    Meter(fraction: g.utilization / 100, fill: AnyShapeStyle(pal.color(.gpu)), height: 5).frame(width: 84)
                    ValueText(f.pct(g.utilization), size: 13, weight: .bold).frame(minWidth: 34, alignment: .trailing)
                    if let t = t { Badge(text: f.temp(t), accent: tAccent).environment(\.layoutDirection, .leftToRight) }
                }
            }
        }
        .buttonStyle(.plain)
        .accessibilityLabel(store.t("gpu") + " " + f.pct(g.utilization))
    }
}

enum GPUText {
    /// "Apple M3 Pro · 18 cores"
    static func name(_ g: GPUReading, _ lc: PulseLocale) -> String {
        guard let c = g.cores else { return g.name }
        return lc.join([g.name, lc.plural("core", c)])
    }
}

@MainActor
struct StorageCard: View {
    @ObservedObject var store: PulseStore
    @Environment(\.palette) private var pal

    var body: some View {
        let d = store.snapshot.disk, f = store.fmt
        Button { store.open(.detail(.ssd)) } label: {
            CardBox {
                VStack(alignment: .leading, spacing: 7) {
                    HStack(spacing: 6) {
                        Image(systemName: "internaldrive").font(.system(size: 12)).foregroundColor(pal.ink2)
                        Text(d.volumeName).font(.system(size: 12.5, weight: .semibold)).foregroundColor(pal.ink2).lineLimit(1)
                    }
                    HStack {
                        Label1(store.t("used", ["pct": .text(f.pct(d.usedPercent))]), size: 12.5, weight: .bold, color: pal.ink)
                        Spacer(minLength: 4)
                        Label1(store.t("free", ["value": .text(f.storage(d.free))]), size: 11.5, color: pal.ink3)
                    }
                    Meter(fraction: d.usedPercent / 100,
                          fill: AnyShapeStyle(LinearGradient(colors: [pal.color(.cpu), pal.color(.nrg)], startPoint: .leading, endPoint: .trailing)),
                          height: 5)
                }
            }
        }
        .buttonStyle(.plain)
        .accessibilityLabel(store.t("storage") + " " + store.t("used", ["pct": .text(f.pct(d.usedPercent))]))
    }
}

@MainActor
struct NetworkCard: View {
    @ObservedObject var store: PulseStore
    @Environment(\.palette) private var pal

    var body: some View {
        let n = store.snapshot.network, f = store.fmt
        Button { store.open(.detail(.net)) } label: {
            CardBox {
                VStack(alignment: .leading, spacing: 7) {
                    HStack(spacing: 6) {
                        Image(systemName: n.kind == .wifi ? "wifi" : "network").font(.system(size: 12)).foregroundColor(pal.ink(.mem))
                        Text(NetText.name(store)).font(.system(size: 12.5, weight: .semibold)).foregroundColor(pal.ink2).lineLimit(1)
                    }
                    HStack(spacing: 10) {
                        ValueText("↓ " + f.rate(n.downRate), size: 13, weight: .bold, color: pal.ink(.mem))
                        ValueText("↑ " + f.rate(n.upRate), size: 12, weight: .semibold, color: pal.ink(.cpu))
                    }
                    Spacer(minLength: 0)
                }
            }
        }
        .buttonStyle(.plain)
        .accessibilityLabel(store.t("network") + " " + store.t("down") + " " + f.rate(n.downRate))
    }
}

@MainActor
enum NetText {
    static func name(_ store: PulseStore) -> String {
        let n = store.snapshot.network
        switch n.kind {
        case .wifi: return "Wi-Fi"
        case .ethernet: return "Ethernet"
        default: return store.t("network")
        }
    }
}

// MARK: - Top Active Apps

@MainActor
struct TopApps: View {
    @ObservedObject var store: PulseStore
    @Environment(\.palette) private var pal
    @Environment(\.typo) private var typo
    static let rowHeight: CGFloat = 34

    var body: some View {
        let rows = store.topApps
        CardBox(padding: 12) {
            VStack(alignment: .leading, spacing: 6) {
                HStack {
                    Text(store.t("top")).font(typo.text(13.5, .bold)).foregroundColor(pal.ink).lineLimit(1)
                    Spacer(minLength: 6)
                    Segmented(items: SortKey.allCases.map { Segmented.Item(id: $0.rawValue, label: store.t($0.rawValue)) },
                              selected: store.sort.rawValue, onSelect: { store.sort = SortKey(rawValue: $0) ?? .cpu; store.hoveredApp = nil })
                }
                VStack(spacing: 0) {
                    ForEach(rows) { app in AppRow(store: store, app: app, top: rows.first) }
                }
                .overlay(alignment: .topLeading) { tooltip(rows) }
            }
        }
    }

    @ViewBuilder private func tooltip(_ rows: [AppUsage]) -> some View {
        if let id = store.hoveredApp, let idx = rows.firstIndex(where: { $0.id == id }) {
            let app = rows[idx], f = store.fmt
            let up = idx >= 3
            VStack(alignment: .leading, spacing: 4) {
                Text(store.lc.app(app.name)).font(typo.text(12.5, .bold)).foregroundColor(pal.ink)
                TipRow(k: store.t("tipRam"), v: f.pct(store.snapshot.memory.total > 0 ? app.memory / store.snapshot.memory.total * 100 : 0, 1))
                TipRow(k: store.t("tipGpu"), v: f.pct(app.gpu))
                TipRow(k: store.t("tipProc"), v: f.n(Double(app.processCount)))
                TipRow(k: store.t("tThreads"), v: f.n(Double(app.threads)))
                Text(store.t("tipClick")).font(typo.text(10.5)).foregroundColor(pal.ink3).padding(.top, 2)
            }
            .padding(10)
            .frame(width: 200, alignment: .leading)
            .background(RoundedRectangle(cornerRadius: Radius.tip, style: .continuous).fill(pal.tipBg).shadow(color: .black.opacity(0.16), radius: 18, y: 7))
            .overlay(RoundedRectangle(cornerRadius: Radius.tip, style: .continuous).strokeBorder(pal.cardBorder))
            .offset(x: 30, y: up ? CGFloat(idx) * TopApps.rowHeight - 148 : CGFloat(idx + 1) * TopApps.rowHeight + 2)
            .allowsHitTesting(false)
            .transition(.opacity)
        }
    }
}

@MainActor
struct TipRow: View {
    let k: String
    let v: String
    var body: some View {
        HStack {
            Label1(k, size: 11)
            Spacer(minLength: 8)
            ValueText(v, size: 11)
        }
    }
}

@MainActor
struct AppRow: View {
    @ObservedObject var store: PulseStore
    let app: AppUsage
    let top: AppUsage?
    @Environment(\.palette) private var pal
    @Environment(\.typo) private var typo

    var body: some View {
        let f = store.fmt, key = store.sort
        let hover = store.hoveredApp == app.id
        let hogRow = key == .cpu && store.hogApp?.id == app.id
        let value: Double = key == .cpu ? app.cpu : key == .mem ? app.memory : app.gpu
        let maxV: Double = max(0.0001, top.map { key == .cpu ? $0.cpu : key == .mem ? $0.memory : $0.gpu } ?? 1)
        let metric = key == .cpu ? f.pct(app.cpu, 1) : key == .mem ? f.memory(app.memory) : f.pct(app.gpu)
        let fill: AnyShapeStyle = hogRow
            ? AnyShapeStyle(LinearGradient(colors: [pal.color(.warn), pal.color(.crit)], startPoint: .leading, endPoint: .trailing))
            : AnyShapeStyle(pal.color(key == .cpu ? .cpu : key == .mem ? .mem : .gpu))
        HStack(spacing: 8) {
            AppGlyph(app: app)
            Text(store.lc.app(app.name)).font(typo.text(13, .medium)).foregroundColor(pal.ink).lineLimit(1).truncationMode(.tail)
            if app.processCount > 1 {
                ValueText("×" + f.n(Double(app.processCount)), size: 10.5, weight: .medium, color: pal.ink2)
                    .padding(.horizontal, 5).padding(.vertical, 1)
                    .background(RoundedRectangle(cornerRadius: 5).fill(pal.track))
            }
            Spacer(minLength: 6)
            Meter(fraction: value / maxV, fill: fill, height: 4).frame(width: 56)
            ZStack(alignment: .trailing) {
                ValueText(metric, size: 12.5, weight: .semibold, color: hogRow ? pal.ink(.warn) : pal.ink).opacity(hover && app.isApp ? 0 : 1)
                if hover && app.isApp {
                    Button { store.requestEnd(app) } label: {
                        Text(store.t("end")).font(typo.text(11.5, .semibold)).foregroundColor(pal.ink(.crit))
                            .padding(.horizontal, 8).padding(.vertical, 3)
                            .background(RoundedRectangle(cornerRadius: Radius.btn).fill(pal.tint(.crit, 12)))
                    }
                    .buttonStyle(.plain)
                    .accessibilityLabel(store.t("endNamed", ["app": .text(store.lc.app(app.name))]))
                }
            }
            .frame(width: 58, alignment: .trailing)
        }
        .padding(.horizontal, 6)
        .frame(height: TopApps.rowHeight)
        .background(RoundedRectangle(cornerRadius: Radius.row, style: .continuous).fill(hover ? pal.hover : .clear))
        .contentShape(Rectangle())
        .onHover { h in
            if h { store.hoveredApp = app.id } else if store.hoveredApp == app.id { store.hoveredApp = nil }
        }
        .onTapGesture { store.open(.detail(.app(app.id))) }
        .accessibilityElement(children: .combine)
        .accessibilityAddTraits(.isButton)
        .accessibilityAction { store.open(.detail(.app(app.id))) }
    }
}
#endif

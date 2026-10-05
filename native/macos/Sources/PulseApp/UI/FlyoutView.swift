#if canImport(AppKit)
import AppKit
import PulseCore
import SwiftUI

struct ContentHeightKey: PreferenceKey {
    static var defaultValue: CGFloat = 0
    static func reduce(value: inout CGFloat, nextValue: () -> CGFloat) { value = max(value, nextValue()) }
}

@MainActor
enum TypographyCache {
    private static var cache: [String: Typography] = [:]
    static func get(_ lc: PulseLocale) -> Typography {
        if let t = cache[lc.code] { return t }
        let t = Typography(locale: lc)
        cache[lc.code] = t
        return t
    }
}

/// The 420 pt flyout: header, hog banner, then the overview (Tier 2), a
/// detail view (Tier 3) or settings, then the footer.
@MainActor
struct FlyoutRoot: View {
    @ObservedObject var store: PulseStore
    /// Off for offscreen rendering: no scroll view, solid background.
    var live = true
    @Environment(\.colorScheme) private var scheme

    var body: some View {
        let pal = Palette(dark: scheme == .dark)
        let lc = store.lc
        ZStack {
            if live { pal.flyTint } else { pal.flySolid }
            if live {
                ScrollView(.vertical, showsIndicators: false) { content(lc) }
            } else {
                content(lc)
            }
            if let app = store.confirm {
                ConfirmOverlay(store: store, app: app)
            }
            if let toast = store.toast, store.confirm == nil {
                VStack { Spacer(); ToastView(text: toast).padding(.bottom, 64) }
                    .transition(.opacity.combined(with: .move(edge: .bottom)))
                    .allowsHitTesting(false)
            }
        }
        .frame(width: 420)
        .animation(.easeOut(duration: 0.2), value: store.route)
        .animation(.easeOut(duration: 0.2), value: store.toast)
        .environment(\.palette, pal)
        .environment(\.typo, TypographyCache.get(lc))
        .environment(\.layoutDirection, lc.isRTL ? .rightToLeft : .leftToRight)
        .environment(\.locale, Locale(identifier: lc.code))
        .onPreferenceChange(ContentHeightKey.self) { store.contentHeight = $0 }
        .onExitCommand {
            if store.confirm != nil { store.confirm = nil } else if store.route != .overview { store.back() } else { FlyoutController.shared?.close() }
        }
    }

    private func content(_ lc: PulseLocale) -> some View {
        VStack(spacing: 8) {
            HeaderBar(store: store)
            if store.route == .overview, let hog = store.hogApp, !store.hogDismissed, store.confirm == nil {
                HogBanner(store: store, app: hog)
                    .transition(.opacity.combined(with: .move(edge: .top)))
            }
            Group {
                switch store.route {
                case .overview: OverviewView(store: store)
                case .detail(let kind): DetailView(store: store, kind: kind)
                case .settings: SettingsView(store: store)
                }
            }
            .transition(.asymmetric(insertion: .opacity.combined(with: .offset(x: lc.isRTL ? -16 : 16)), removal: .opacity))
            FooterBar(store: store)
        }
        .padding(12)
        .background(GeometryReader { g in Color.clear.preference(key: ContentHeightKey.self, value: g.size.height) })
    }
}

/// Two rings, the Pulse mark.
@MainActor
struct LogoMark: View {
    @Environment(\.palette) private var pal
    var body: some View {
        ZStack {
            Circle().stroke(pal.color(.cpu), lineWidth: 2.6).frame(width: 15, height: 15).offset(x: -3.5)
            Circle().stroke(pal.color(.mem), lineWidth: 2.6).frame(width: 15, height: 15).offset(x: 3.5)
        }
        .frame(width: 30, height: 30)
        .background(RoundedRectangle(cornerRadius: Radius.logo, style: .continuous)
            .fill(LinearGradient(colors: [pal.card, pal.tint(.cpu, 18)], startPoint: .topLeading, endPoint: .bottomTrailing)))
        .overlay(RoundedRectangle(cornerRadius: Radius.logo, style: .continuous).strokeBorder(pal.cardBorder))
        .environment(\.layoutDirection, .leftToRight)
        .accessibilityHidden(true)
    }
}

@MainActor
struct HeaderBar: View {
    @ObservedObject var store: PulseStore
    @Environment(\.palette) private var pal
    @Environment(\.typo) private var typo
    @Environment(\.colorScheme) private var scheme

    var body: some View {
        HStack(spacing: 8) {
            LogoMark()
            Text(verbatim: "Pulse").font(typo.heroFont(17)).foregroundColor(pal.ink).environment(\.layoutDirection, .leftToRight)
            statusPill
            Spacer(minLength: 4)
            SquareButton(symbol: scheme == .dark ? "sun.max" : "moon", label: scheme == .dark ? store.t("aLight") : store.t("aDark")) {
                store.themeOverride = scheme == .dark ? "light" : "dark"
            }
            LocaleSwitcher(store: store)
            SquareButton(symbol: "slider.horizontal.3", label: store.t("aSettings"), active: store.route == .settings) {
                store.open(store.route == .settings ? .overview : .settings)
            }
        }
    }

    @ViewBuilder private var statusPill: some View {
        let f = store.fmt
        if let hog = store.hogApp, !store.hogDismissed {
            Badge(text: store.t("pillValue", ["label": .text(store.t("hogPill")), "value": .text(f.pct(hog.cpu))]), accent: .warn, dot: true)
        } else {
            let value = store.snapshot.power.drawWatts.map { f.wattsShort($0) } ?? f.pct(store.snapshot.cpu.total)
            Badge(text: store.t("pillValue", ["label": .text(store.t("calm")), "value": .text(value)]), accent: .cpu, dot: true)
        }
    }
}

/// Two locales: the EN | ع pill. Three or more: a compact menu.
@MainActor
struct LocaleSwitcher: View {
    @ObservedObject var store: PulseStore
    @Environment(\.palette) private var pal
    @Environment(\.typo) private var typo

    var body: some View {
        let list = store.locales
        let label = store.t("aLang") + ": " + store.lc.name
        if list.count <= 2 {
            Segmented(items: list.map { l in
                let font = (try? store.registry.get(l.code)).map { TypographyCache.get($0).text(12.5, .semibold) }
                return Segmented.Item(id: l.code, label: l.label, ltr: !l.isRTL, font: font)
            }, selected: store.lc.code, onSelect: { store.setLanguage($0) }, accessibilityLabel: label)
            .help(label)
        } else {
            Menu {
                ForEach(list, id: \.code) { l in
                    Button { store.setLanguage(l.code) } label: {
                        if l.code == store.lc.code { Label(l.name, systemImage: "checkmark") } else { Text(l.name) }
                    }
                }
            } label: {
                Text(store.lc.label).font(typo.text(12, .semibold))
            }
            .menuStyle(.borderlessButton)
            .fixedSize()
            .padding(.horizontal, 8)
            .padding(.vertical, 4)
            .background(Capsule().fill(pal.track))
            .accessibilityLabel(label)
            .help(label)
        }
    }
}

@MainActor
struct HogBanner: View {
    @ObservedObject var store: PulseStore
    let app: AppUsage
    @Environment(\.palette) private var pal
    @Environment(\.typo) private var typo

    var body: some View {
        let f = store.fmt
        let title = store.t("hogTitle", ["app": .text(store.lc.app(app.name)), "pct": .text(f.pct(app.cpu))])
        HStack(spacing: 10) {
            IconTile(symbol: "exclamationmark.triangle", accent: .warn, size: 30)
            VStack(alignment: .leading, spacing: 2) {
                Text(title).font(typo.text(13, .semibold)).foregroundColor(pal.ink).lineLimit(1).truncationMode(.tail).help(title)
                Label1(store.t("hogSub", ["pct": .text(f.pct(50))]), size: 11.5)
            }
            Spacer(minLength: 4)
            if app.isApp {
                Button { store.requestEnd(app) } label: {
                    Text(store.t("endApp")).font(typo.text(12, .semibold)).foregroundColor(pal.ink(.warn))
                        .padding(.horizontal, 10).padding(.vertical, 5)
                        .background(RoundedRectangle(cornerRadius: Radius.btn).strokeBorder(pal.line(.warn)))
                }
                .buttonStyle(.plain)
            }
            Button { store.hogDismissed = true } label: {
                Image(systemName: "xmark").font(.system(size: 11, weight: .semibold)).foregroundColor(pal.ink2).frame(width: 22, height: 22)
            }
            .buttonStyle(.plain)
            .accessibilityLabel(store.t("aDismiss"))
        }
        .padding(10)
        .background(RoundedRectangle(cornerRadius: Radius.card, style: .continuous)
            .fill(LinearGradient(colors: [pal.tint(.warn, pal.dark ? 18 : 13), pal.tint(.crit, pal.dark ? 12 : 8)], startPoint: .leading, endPoint: .trailing)))
        .overlay(RoundedRectangle(cornerRadius: Radius.card, style: .continuous).strokeBorder(pal.line(.warn)))
        .accessibilityElement(children: .contain)
    }
}

@MainActor
struct FooterBar: View {
    @ObservedObject var store: PulseStore
    @Environment(\.palette) private var pal
    @Environment(\.typo) private var typo

    var body: some View {
        HStack(spacing: 8) {
            Button { store.openActivityMonitor() } label: {
                HStack(spacing: 6) {
                    Text(store.t("openMonitor", ["monitor": .text(store.hw("activityMonitor"))]))
                        .font(typo.text(12.5, .semibold)).foregroundColor(pal.ink).lineLimit(1).truncationMode(.tail)
                    Image(systemName: "arrow.up.forward.square").font(.system(size: 11)).foregroundColor(pal.ink2)
                }
                .padding(.horizontal, 12).padding(.vertical, 7)
                .background(RoundedRectangle(cornerRadius: Radius.btn, style: .continuous).fill(pal.card))
                .overlay(RoundedRectangle(cornerRadius: Radius.btn, style: .continuous).strokeBorder(pal.cardBorder))
            }
            .buttonStyle(.plain)
            .layoutPriority(1)
            Spacer(minLength: 4)
            HStack(spacing: 4) {
                Image(systemName: "lock").font(.system(size: 10))
                Text(store.t("privacy")).font(typo.text(11.5)).lineLimit(1).truncationMode(.tail)
            }
            .foregroundColor(pal.ink3)
            Spacer(minLength: 4)
            Button { NSApp.terminate(nil) } label: {
                Text(store.t("quit")).font(typo.text(12.5, .semibold)).foregroundColor(pal.ink2).padding(.horizontal, 6).padding(.vertical, 7)
            }
            .buttonStyle(.plain)
        }
    }
}

@MainActor
struct ToastView: View {
    let text: String
    @Environment(\.palette) private var pal
    @Environment(\.typo) private var typo
    var body: some View {
        Text(text)
            .font(typo.text(12.5, .medium))
            .foregroundColor(pal.ink)
            .lineLimit(1).truncationMode(.tail)
            .padding(.horizontal, 14).padding(.vertical, 8)
            .background(Capsule().fill(pal.tipBg).shadow(color: .black.opacity(0.16), radius: 14, y: 6))
            .padding(.horizontal, 24)
            .accessibilityAddTraits(.updatesFrequently)
    }
}

@MainActor
struct ConfirmOverlay: View {
    @ObservedObject var store: PulseStore
    let app: AppUsage
    @Environment(\.palette) private var pal
    @Environment(\.typo) private var typo

    var body: some View {
        ZStack {
            Color.black.opacity(pal.dark ? 0.35 : 0.12).onTapGesture { store.confirm = nil }
            VStack(alignment: .leading, spacing: 10) {
                HStack(spacing: 10) {
                    AppGlyph(app: app, size: 28)
                    Text(store.t("confirmTitle", ["app": .text(store.lc.app(app.name))]))
                        .font(typo.text(14, .semibold)).foregroundColor(pal.ink).lineLimit(1).truncationMode(.tail)
                }
                Label1(store.t("confirmSub"), size: 12)
                HStack(spacing: 8) {
                    Spacer()
                    Button(store.t("cancel")) { store.confirm = nil }
                        .keyboardShortcut(.cancelAction)
                    Button(store.t("confirmBtn")) { store.confirmEnd() }
                        .keyboardShortcut(.defaultAction)
                        .tint(pal.danger)
                }
                .controlSize(.regular)
            }
            .padding(16)
            .frame(width: 320)
            .background(RoundedRectangle(cornerRadius: Radius.card, style: .continuous).fill(pal.tipBg).shadow(color: .black.opacity(0.2), radius: 20, y: 8))
            .overlay(RoundedRectangle(cornerRadius: Radius.card, style: .continuous).strokeBorder(pal.line(.crit)))
        }
        .transition(.opacity)
    }
}
#endif

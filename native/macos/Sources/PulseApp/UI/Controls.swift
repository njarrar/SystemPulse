#if canImport(AppKit)
import AppKit
import PulseCore
import SwiftUI

/// Numbers, units and signs: always laid out left to right, tabular digits.
@MainActor
struct ValueText: View {
    let text: String
    var size: CGFloat = 13
    var weight: Font.Weight = .semibold
    var color: Color? = nil
    @Environment(\.palette) private var pal
    @Environment(\.typo) private var typo

    init(_ text: String, size: CGFloat = 13, weight: Font.Weight = .semibold, color: Color? = nil) {
        self.text = text; self.size = size; self.weight = weight; self.color = color
    }

    var body: some View {
        Text(text)
            .font(.system(size: size, weight: weight).monospacedDigit())
            .foregroundColor(color ?? pal.ink)
            .lineLimit(1)
            .environment(\.layoutDirection, .leftToRight)
    }
}

/// Body copy in the locale's font, one line, ellipsis when it runs long.
@MainActor
struct Label1: View {
    let text: String
    var size: CGFloat = 12
    var weight: Font.Weight = .regular
    var color: Color? = nil
    @Environment(\.palette) private var pal
    @Environment(\.typo) private var typo

    init(_ text: String, size: CGFloat = 12, weight: Font.Weight = .regular, color: Color? = nil) {
        self.text = text; self.size = size; self.weight = weight; self.color = color
    }

    var body: some View {
        Text(text)
            .font(typo.text(size, weight))
            .foregroundColor(color ?? pal.ink2)
            .lineLimit(1)
            .truncationMode(.tail)
            .help(text)
    }
}

@MainActor
struct HeroText: View {
    let text: String
    var size: CGFloat = 30
    var color: Color? = nil
    @Environment(\.palette) private var pal
    @Environment(\.typo) private var typo

    var body: some View {
        Text(text)
            .font(typo.heroFont(size).monospacedDigit())
            .foregroundColor(color ?? pal.ink)
            .lineLimit(1)
            .minimumScaleFactor(0.6)
    }
}

/// Rounded card with the Part 1 card tokens and a hover lift.
@MainActor
struct CardBox<Content: View>: View {
    var border: Color? = nil
    var padding: CGFloat = 12
    @ViewBuilder var content: () -> Content
    @Environment(\.palette) private var pal
    @State private var hover = false

    var body: some View {
        content()
            .padding(padding)
            .frame(maxWidth: .infinity, alignment: .leading)
            .background(
                RoundedRectangle(cornerRadius: Radius.card, style: .continuous)
                    .fill(hover ? pal.cardHover : pal.card)
                    .shadow(color: .black.opacity(hover ? (pal.dark ? 0.35 : 0.09) : (pal.dark ? 0 : 0.04)), radius: hover ? 11 : 5, y: hover ? 4 : 1)
            )
            .overlay(
                RoundedRectangle(cornerRadius: Radius.card, style: .continuous)
                    .strokeBorder(border ?? pal.cardBorder, lineWidth: 1)
            )
            .onHover { h in withAnimation(.easeOut(duration: 0.15)) { hover = h } }
    }
}

/// Square icon tile used in card headers.
@MainActor
struct IconTile: View {
    let symbol: String
    let accent: Accent
    var size: CGFloat = 26
    @Environment(\.palette) private var pal

    var body: some View {
        Image(systemName: symbol)
            .font(.system(size: size * 0.5, weight: .semibold))
            .foregroundColor(pal.ink(accent))
            .frame(width: size, height: size)
            .background(RoundedRectangle(cornerRadius: Radius.btn, style: .continuous).fill(pal.tint(accent)))
            .accessibilityHidden(true)
    }
}

@MainActor
struct Badge: View {
    let text: String
    let accent: Accent
    var dot = false
    @Environment(\.palette) private var pal
    @Environment(\.typo) private var typo

    var body: some View {
        HStack(spacing: 5) {
            if dot { Circle().fill(pal.color(accent)).frame(width: 6, height: 6) }
            Text(text).font(typo.text(11.5, .semibold)).lineLimit(1).truncationMode(.tail)
        }
        .foregroundColor(pal.ink(accent))
        .padding(.horizontal, 8)
        .padding(.vertical, 3)
        .background(Capsule().fill(pal.tint(accent)))
        .help(text)
    }
}

/// Horizontal meter. Fills from the leading edge, so it mirrors in RTL.
@MainActor
struct Meter: View {
    let fraction: Double
    var fill: AnyShapeStyle
    var height: CGFloat = 5
    @Environment(\.palette) private var pal

    var body: some View {
        GeometryReader { g in
            ZStack(alignment: .leading) {
                Capsule().fill(pal.track)
                Capsule().fill(fill).frame(width: max(height, g.size.width * CGFloat(min(1, max(0.02, fraction)))))
            }
        }
        .frame(height: height)
        .animation(.easeOut(duration: 0.4), value: fraction)
    }
}

/// Smoothed line and area, drawn from ChartMath points.
struct SmoothPath: Shape {
    let points: [ChartMath.Point]
    var closeTo: CGFloat? = nil

    func path(in rect: CGRect) -> Path {
        var p = Path()
        guard let first = points.first else { return p }
        p.move(to: CGPoint(x: first.x, y: first.y))
        for (c1, c2, end) in ChartMath.smooth(points) {
            p.addCurve(to: CGPoint(x: end.x, y: end.y), control1: CGPoint(x: c1.x, y: c1.y), control2: CGPoint(x: c2.x, y: c2.y))
        }
        if let base = closeTo, let last = points.last {
            p.addLine(to: CGPoint(x: last.x, y: base))
            p.addLine(to: CGPoint(x: first.x, y: base))
            p.closeSubpath()
        }
        return p
    }
}

/// Tier 2 sparkline with the "1 min" tag. Time runs left to right in every locale.
@MainActor
struct Sparkline: View {
    let values: [Double]
    let accent: Accent
    let label: String
    var height: CGFloat = 28
    @Environment(\.palette) private var pal
    @Environment(\.typo) private var typo

    var body: some View {
        GeometryReader { g in
            let pts = ChartMath.sparkPoints(values.isEmpty ? [0] : values, width: Double(g.size.width), height: Double(height))
            ZStack(alignment: .topLeading) {
                SmoothPath(points: pts, closeTo: height)
                    .fill(LinearGradient(colors: [pal.color(accent).opacity(0.28), pal.color(accent).opacity(0)], startPoint: .top, endPoint: .bottom))
                SmoothPath(points: pts).stroke(pal.color(accent), style: StrokeStyle(lineWidth: 1.6, lineCap: .round, lineJoin: .round))
                Text(label)
                    .font(typo.text(9.5, .medium))
                    .foregroundColor(pal.ink3)
                    .padding(.horizontal, 3)
                    .background(RoundedRectangle(cornerRadius: 3).fill(pal.card))
                    .offset(y: -2)
            }
        }
        .frame(height: height)
        .environment(\.layoutDirection, .leftToRight)
        .accessibilityHidden(true)
    }
}

/// Segmented control with a sliding pill (sort tabs, °C/°F, EN | ع).
@MainActor
struct Segmented: View {
    struct Item: Identifiable { let id: String; let label: String; var ltr = false; var font: Font? = nil }
    let items: [Item]
    let selected: String
    let onSelect: (String) -> Void
    var accessibilityLabel: String = ""
    @Environment(\.palette) private var pal
    @Environment(\.typo) private var typo
    @Environment(\.layoutDirection) private var dir
    @Namespace private var ns

    var body: some View {
        HStack(spacing: 0) {
            ForEach(items) { item in
                let on = item.id == selected
                Button { onSelect(item.id) } label: {
                    Text(item.label)
                        .font(item.font ?? typo.text(12, on ? .semibold : .medium))
                        .foregroundColor(on ? pal.ink : pal.ink2)
                        .lineLimit(1)
                        .padding(.horizontal, 10)
                        .padding(.vertical, 4)
                        .frame(minWidth: 30)
                        .background {
                            if on {
                                Capsule().fill(pal.segPill)
                                    .shadow(color: .black.opacity(pal.dark ? 0 : 0.14), radius: 1.5, y: 1)
                                    .matchedGeometryEffect(id: "pill", in: ns)
                            }
                        }
                        .environment(\.layoutDirection, item.ltr ? .leftToRight : dir)
                        .contentShape(Capsule())
                }
                .buttonStyle(.plain)
                .accessibilityAddTraits(on ? .isSelected : [])
            }
        }
        .padding(2)
        .background(Capsule().fill(pal.track))
        .animation(.spring(response: 0.3, dampingFraction: 0.85), value: selected)
        .accessibilityElement(children: .contain)
        .accessibilityLabel(accessibilityLabel)
    }
}

/// Square header button (theme, settings).
@MainActor
struct SquareButton: View {
    let symbol: String
    let label: String
    var active = false
    let action: () -> Void
    @Environment(\.palette) private var pal

    var body: some View {
        Button(action: action) {
            Image(systemName: symbol)
                .font(.system(size: 13, weight: .medium))
                .foregroundColor(active ? pal.ink(.mem) : pal.ink2)
                .frame(width: 28, height: 28)
                .background(RoundedRectangle(cornerRadius: Radius.btn, style: .continuous).fill(active ? pal.cardHover : pal.card))
                .overlay(RoundedRectangle(cornerRadius: Radius.btn, style: .continuous).strokeBorder(active ? pal.line(.mem) : pal.cardBorder))
        }
        .buttonStyle(.plain)
        .help(label)
        .accessibilityLabel(label)
    }
}

/// A native switch (AXSwitch, role=switch) tinted with the CPU accent.
@MainActor
struct PulseSwitch: View {
    @Binding var on: Bool
    let label: String
    @Environment(\.palette) private var pal

    var body: some View {
        Toggle(label, isOn: $on)
            .toggleStyle(.switch)
            .labelsHidden()
            .controlSize(.small)
            .tint(pal.color(.cpu))
            .accessibilityLabel(label)
    }
}

/// App icon from NSRunningApplication, or a monogram tile for background processes.
@MainActor
struct AppGlyph: View {
    let app: AppUsage
    var size: CGFloat = 22
    @Environment(\.palette) private var pal

    var body: some View {
        if let icon = NSRunningApplication(processIdentifier: app.pid)?.icon, app.isApp {
            Image(nsImage: icon).resizable().interpolation(.high).frame(width: size, height: size)
        } else {
            // A stable hue per name (FNV-1a), so a tile keeps its colour across launches.
            let hue = Double(app.name.unicodeScalars.reduce(UInt32(2_166_136_261)) { ($0 ^ $1.value) &* 16_777_619 } % 360) / 360
            Text(String(app.name.prefix(1)).uppercased())
                .font(.system(size: size * 0.5, weight: .bold, design: .rounded))
                .foregroundColor(Color(hue: hue, saturation: 0.6, brightness: pal.dark ? 0.9 : 0.45))
                .frame(width: size, height: size)
                .background(RoundedRectangle(cornerRadius: Radius.mono, style: .continuous)
                    .fill(Color(hue: hue, saturation: pal.dark ? 0.4 : 0.18, brightness: pal.dark ? 0.35 : 0.97)))
                .environment(\.layoutDirection, .leftToRight)
        }
    }
}
#endif

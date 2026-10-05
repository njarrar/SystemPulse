#if canImport(AppKit)
import AppKit
import PulseCore
import SwiftUI

extension Color {
    init(hex: UInt32, alpha: Double = 1) {
        self.init(.sRGB, red: Double((hex >> 16) & 0xFF) / 255, green: Double((hex >> 8) & 0xFF) / 255,
                  blue: Double(hex & 0xFF) / 255, opacity: alpha)
    }
    init(r: Double, g: Double, b: Double, a: Double) {
        self.init(.sRGB, red: r / 255, green: g / 255, blue: b / 255, opacity: a)
    }
}

/// Accent families from Part 1 tokens.
enum Accent { case cpu, mem, nrg, thm, gpu, warn, crit }

/// Design tokens (Part 1, macOS): the prototype's LIGHT and DARK sets.
struct Palette {
    let dark: Bool

    var ink: Color { dark ? Color(hex: 0xECFDF5) : Color(hex: 0x0E1E19) }
    var ink2: Color { dark ? Color(r: 236, g: 253, b: 245, a: 0.68) : Color(hex: 0x4B635B) }
    var ink3: Color { dark ? Color(r: 236, g: 253, b: 245, a: 0.52) : Color(hex: 0x5E746C) }
    var track: Color { dark ? Color(r: 255, g: 255, b: 255, a: 0.08) : Color(r: 14, g: 30, b: 25, a: 0.07) }
    var trackStrong: Color { dark ? Color(r: 255, g: 255, b: 255, a: 0.22) : Color(r: 14, g: 30, b: 25, a: 0.2) }
    var hover: Color { dark ? Color(r: 255, g: 255, b: 255, a: 0.05) : Color(r: 14, g: 30, b: 25, a: 0.045) }
    var hair: Color { dark ? Color(r: 255, g: 255, b: 255, a: 0.1) : Color(r: 14, g: 30, b: 25, a: 0.1) }
    var card: Color { dark ? Color(r: 255, g: 255, b: 255, a: 0.055) : Color(r: 255, g: 255, b: 255, a: 0.78) }
    var cardHover: Color { dark ? Color(r: 255, g: 255, b: 255, a: 0.09) : Color(r: 255, g: 255, b: 255, a: 0.96) }
    var cardBorder: Color { dark ? Color(r: 255, g: 255, b: 255, a: 0.08) : Color(r: 14, g: 30, b: 25, a: 0.06) }
    var flyTint: Color { dark ? Color(r: 12, g: 21, b: 18, a: 0.62) : Color(r: 245, g: 248, b: 246, a: 0.6) }
    var flySolid: Color { dark ? Color(hex: 0x0F1A16) : Color(hex: 0xF5F8F6) }
    var flyBorder: Color { dark ? Color(r: 167, g: 243, b: 208, a: 0.12) : Color(r: 14, g: 30, b: 25, a: 0.08) }
    var tipBg: Color { dark ? Color(r: 20, g: 32, b: 28, a: 0.96) : Color(r: 255, g: 255, b: 255, a: 0.97) }
    var segPill: Color { dark ? Color(r: 255, g: 255, b: 255, a: 0.16) : .white }
    var danger: Color { dark ? Color(hex: 0xF87171) : Color(hex: 0xDC2626) }
    var dangerOn: Color { dark ? Color(hex: 0x2A0E00) : .white }

    func color(_ a: Accent) -> Color {
        switch a {
        case .cpu: return dark ? Color(hex: 0x34D399) : Color(hex: 0x10B981)
        case .mem: return dark ? Color(hex: 0x38BDF8) : Color(hex: 0x0EA5E9)
        case .nrg: return dark ? Color(hex: 0xFBBF24) : Color(hex: 0xF59E0B)
        case .thm: return dark ? Color(hex: 0xFB7185) : Color(hex: 0xF43F5E)
        case .gpu: return dark ? Color(hex: 0x2DD4BF) : Color(hex: 0x14B8A6)
        case .warn: return dark ? Color(hex: 0xFB923C) : Color(hex: 0xF97316)
        case .crit: return dark ? Color(hex: 0xF87171) : Color(hex: 0xEF4444)
        }
    }

    func ink(_ a: Accent) -> Color {
        switch a {
        case .cpu: return dark ? Color(hex: 0x34D399) : Color(hex: 0x047857)
        case .mem: return dark ? Color(hex: 0x38BDF8) : Color(hex: 0x0369A1)
        case .nrg: return dark ? Color(hex: 0xFBBF24) : Color(hex: 0xB45309)
        case .thm: return dark ? Color(hex: 0xFB7185) : Color(hex: 0xBE123C)
        case .gpu: return dark ? Color(hex: 0x2DD4BF) : Color(hex: 0x0F766E)
        case .warn: return dark ? Color(hex: 0xFDBA74) : Color(hex: 0xC2410C)
        case .crit: return dark ? Color(hex: 0xFCA5A5) : Color(hex: 0xB91C1C)
        }
    }

    func tint(_ a: Accent, _ pct: Double = 13) -> Color { color(a).opacity(pct / 100) }
    func line(_ a: Accent) -> Color { color(a).opacity(0.35) }
}

/// Corner radii for macOS (Part 1, PLAT.mac).
enum Radius {
    static let fly: CGFloat = 22
    static let card: CGFloat = 14
    static let btn: CGFloat = 8
    static let badge: CGFloat = 8
    static let row: CGFloat = 9
    static let mono: CGFloat = 7
    static let tip: CGFloat = 12
    static let logo: CGFloat = 10
}

/// Fonts: SF Pro (and SF Pro Rounded for hero numbers), or the stack a
/// locale file asks for when one of its families is installed.
struct Typography {
    let ui: String?
    let hero: String?

    @MainActor init(locale: PulseLocale) {
        let stack = locale.fonts(platform: "mac")
        ui = Typography.firstInstalled(stack.ui)
        hero = Typography.firstInstalled(stack.hero)
    }

    @MainActor static func firstInstalled(_ stack: String?) -> String? {
        guard let stack = stack else { return nil }
        let installed = Set(NSFontManager.shared.availableFontFamilies)
        for raw in stack.split(separator: ",") {
            let name = raw.trimmingCharacters(in: .whitespaces).trimmingCharacters(in: CharacterSet(charactersIn: "\"'"))
            if installed.contains(name) { return name }
        }
        return nil
    }

    func text(_ size: CGFloat, _ weight: Font.Weight = .regular) -> Font {
        if let f = ui { return .custom(f, size: size).weight(weight) }
        return .system(size: size, weight: weight)
    }

    func heroFont(_ size: CGFloat) -> Font {
        if let f = hero { return .custom(f, size: size).weight(.bold) }
        return .system(size: size, weight: .bold, design: .rounded)
    }
}

struct PaletteKey: EnvironmentKey { static let defaultValue = Palette(dark: false) }
struct TypographyKey: EnvironmentKey { static let defaultValue = Typography(ui: nil, hero: nil) }

extension Typography {
    init(ui: String?, hero: String?) { self.ui = ui; self.hero = hero }
}

extension EnvironmentValues {
    var palette: Palette {
        get { self[PaletteKey.self] }
        set { self[PaletteKey.self] = newValue }
    }
    var typo: Typography {
        get { self[TypographyKey.self] }
        set { self[TypographyKey.self] = newValue }
    }
}
#endif

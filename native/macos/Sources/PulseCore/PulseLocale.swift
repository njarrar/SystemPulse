import Foundation

/// Font stacks a locale asks for (`fonts` in the locale file).
public struct LocaleFonts: Codable, Equatable, Sendable {
    public struct Stack: Codable, Equatable, Sendable {
        public var ui: String?
        public var hero: String?
        public init(ui: String? = nil, hero: String? = nil) { self.ui = ui; self.hero = hero }
    }
    public var ui: String?
    public var hero: String?
    public var platforms: [String: Stack]?

    public init(ui: String? = nil, hero: String? = nil, platforms: [String: Stack]? = nil) {
        self.ui = ui; self.hero = hero; self.platforms = platforms
    }
}

/// One locale file (locales/<code>.json), as written on disk.
public struct LocaleData: Codable, Equatable, Sendable {
    public var code: String
    public var label: String?
    public var name: String?
    public var dir: String?
    public var order: Double?
    public var fallback: String?
    public var numberingSystem: String?
    public var fonts: LocaleFonts?
    public var pluralRules: [String: String]?
    public var plurals: [String: [String: String]]?
    public var strings: [String: String]?
    public var hardware: [String: String]?
    public var apps: [String: String]?

    public init(code: String, label: String? = nil, name: String? = nil, dir: String? = nil, order: Double? = nil,
                fallback: String? = nil, numberingSystem: String? = nil, fonts: LocaleFonts? = nil,
                pluralRules: [String: String]? = nil, plurals: [String: [String: String]]? = nil,
                strings: [String: String]? = nil, hardware: [String: String]? = nil, apps: [String: String]? = nil) {
        self.code = code; self.label = label; self.name = name; self.dir = dir; self.order = order
        self.fallback = fallback; self.numberingSystem = numberingSystem; self.fonts = fonts
        self.pluralRules = pluralRules; self.plurals = plurals; self.strings = strings
        self.hardware = hardware; self.apps = apps
    }

    public static func decode(_ data: Data) throws -> LocaleData {
        try JSONDecoder().decode(LocaleData.self, from: data)
    }
}

/// Values passed to `{placeholder}` templates.
public enum Arg: Sendable, Equatable, ExpressibleByStringLiteral, ExpressibleByIntegerLiteral, ExpressibleByFloatLiteral {
    case text(String)
    case number(Double)

    public init(stringLiteral value: String) { self = .text(value) }
    public init(integerLiteral value: Int) { self = .number(Double(value)) }
    public init(floatLiteral value: Double) { self = .number(value) }
}

public enum Bidi {
    public static let fsi = "\u{2068}"
    public static let pdi = "\u{2069}"
    public static func isolate(_ s: String) -> String { fsi + s + pdi }

    /// True when the text holds Hebrew, Arabic, Syriac, Thaana, NKo or
    /// Arabic presentation forms (the engine's RTL_CHARS class).
    public static func hasRTL(_ s: String) -> Bool {
        s.unicodeScalars.contains { u in
            let v = u.value
            return (0x0590...0x08FF).contains(v) || (0xFB1D...0xFDFF).contains(v) || (0xFE70...0xFEFF).contains(v)
        }
    }

    /// Removes isolate and embedding controls (for comparisons in tests).
    public static func strip(_ s: String) -> String {
        String(String.UnicodeScalarView(s.unicodeScalars.filter { !(0x2066...0x2069).contains($0.value) }))
    }
}

/// A loaded locale with its fallback chain. Port of `Locale` in i18n/pulse-i18n.js.
public final class PulseLocale {
    public let data: LocaleData
    public let fallback: PulseLocale?
    public let code: String
    public let label: String
    public let name: String
    public let isRTL: Bool
    public let numberingSystem: String
    public let selector: PluralSelector

    /// Called once per missing key. Defaults to a line on stderr.
    public var onMissing: (String) -> Void = { msg in
        FileHandle.standardError.write(Data((msg + "\n").utf8))
    }
    private var warned = Set<String>()

    public init(_ data: LocaleData, fallback: PulseLocale? = nil) throws {
        self.data = data
        self.fallback = fallback
        self.code = data.code
        self.label = data.label ?? data.code.uppercased()
        self.name = data.name ?? data.code
        self.isRTL = data.dir == "rtl"
        self.numberingSystem = data.numberingSystem ?? "latn"
        self.selector = try PluralSelector(rules: data.pluralRules)
    }

    public var dir: String { isRTL ? "rtl" : "ltr" }

    // MARK: lookup

    func lookup(_ section: KeyPath<LocaleData, [String: String]?>, _ key: String) -> String? {
        if let s = data[keyPath: section]?[key] { return s }
        return fallback?.lookup(section, key)
    }

    private func miss(_ section: String, _ key: String) -> String {
        let id = section + "." + key
        if !warned.contains(id) {
            warned.insert(id)
            onMissing("[i18n] missing \(id) in \(code)")
        }
        return key
    }

    public func has(_ key: String) -> Bool { lookup(\.strings, key) != nil }

    // MARK: numbers

    /// Formats a number with fixed decimals in this locale's numbering system.
    public func num(_ x: Double, _ decimals: Int = 0) -> String {
        NumberText.localize(NumberText.fixed(x, decimals), system: numberingSystem)
    }

    public func num(_ x: Int) -> String { num(Double(x), 0) }

    /// Number with thousands grouping (for counts like "1,180 RPM").
    public func grouped(_ x: Double) -> String {
        let s = NumberText.fixed(x, 0)
        var out: [Character] = []
        for (k, ch) in s.reversed().enumerated() {
            if k > 0 && k % 3 == 0 && ch != "-" { out.append(",") }
            out.append(ch)
        }
        return NumberText.localize(String(out.reversed()), system: numberingSystem)
    }

    // MARK: templates

    /// Replaces {name} with params[name]. Inserted values are isolated in
    /// RTL locales, and in LTR locales when the value holds RTL text.
    public func format(_ template: String, _ params: [String: Arg] = [:]) -> String {
        var out = ""
        var rest = Substring(template)
        while let open = rest.firstIndex(of: "{") {
            out += rest[..<open]
            let after = rest.index(after: open)
            if let close = rest[after...].firstIndex(of: "}") {
                let name = rest[after..<close]
                if !name.isEmpty, name.allSatisfy(Template.isWordChar) {
                    if let v = params[String(name)] {
                        let s: String
                        switch v {
                        case .text(let t): s = t
                        case .number(let d): s = num(d)
                        }
                        out += (isRTL || Bidi.hasRTL(s)) ? Bidi.isolate(s) : s
                    } else {
                        out += rest[open...close]
                    }
                    rest = rest[rest.index(after: close)...]
                    continue
                }
            }
            out += "{"
            rest = rest[after...]
        }
        out += rest
        return out
    }

    /// A UI string from `strings`.
    public func t(_ key: String, _ params: [String: Arg] = [:]) -> String {
        format(lookup(\.strings, key) ?? miss("strings", key), params)
    }

    /// A hardware label from `hardware`.
    public func hw(_ key: String, _ params: [String: Arg] = [:]) -> String {
        format(lookup(\.hardware, key) ?? miss("hardware", key), params)
    }

    /// The plural form for `n`. Exact forms ("=0") win over CLDR categories.
    public func plural(_ key: String, _ n: Double, _ params: [String: Arg] = [:], decimals: Int? = nil) -> String {
        guard let forms = data.plurals?[key] else {
            if let fb = fallback { return fb.plural(key, n, params, decimals: decimals) }
            return miss("plurals", key)
        }
        let cat = selector.select(n, decimals: decimals)
        let exact = "=" + NumberText.shortest(n)
        let tpl = forms[exact] ?? forms[cat.rawValue] ?? forms["other"] ?? ""
        var p = params
        p["n"] = .text(decimals == nil ? num(n) : num(n, decimals!))
        return format(tpl, p)
    }

    public func plural(_ key: String, _ n: Int, _ params: [String: Arg] = [:]) -> String {
        plural(key, Double(n), params)
    }

    /// Joins items with `listSep`, isolating each item in RTL locales.
    public func join(_ items: [String?]) -> String {
        let sep = lookup(\.strings, "listSep") ?? " · "
        return items.compactMap { $0 }.filter { !$0.isEmpty }.map { isRTL ? Bidi.isolate($0) : $0 }.joined(separator: sep)
    }

    /// App display name, translated under `apps` when the file has one.
    public func app(_ name: String) -> String {
        data.apps?[name] ?? name
    }

    /// Font stacks for a platform key ("mac", "win", ...).
    public func fonts(platform: String) -> LocaleFonts.Stack {
        let f = data.fonts
        let o = f?.platforms?[platform]
        return LocaleFonts.Stack(ui: o?.ui ?? f?.ui, hero: o?.hero ?? f?.hero)
    }
}

public struct LocaleSummary: Equatable, Sendable {
    public let code: String
    public let label: String
    public let name: String
    public let isRTL: Bool
}

/// Registered locales with lazy fallback chains. Port of the engine registry.
public final class LocaleRegistry {
    public let baseCode: String
    private var entries: [String: LocaleData] = [:]
    private var cache: [String: PulseLocale] = [:]

    public init(baseCode: String = "en") { self.baseCode = baseCode }

    public convenience init(_ locales: [LocaleData], baseCode: String = "en") {
        self.init(baseCode: baseCode)
        locales.forEach { register($0) }
    }

    @discardableResult
    public func register(_ data: LocaleData) -> String {
        entries[data.code] = data
        cache.removeAll()
        return data.code
    }

    public var codes: [String] { list().map { $0.code } }

    /// "ar-EG" -> "ar" when only "ar" exists; unknown -> base locale.
    public func resolve(_ code: String?) -> String {
        if let c = code, entries[c] != nil { return c }
        var parts = (code ?? "").split(whereSeparator: { $0 == "-" || $0 == "_" }).map(String.init)
        while parts.count > 1 {
            parts.removeLast()
            let c = parts.joined(separator: "-")
            if entries[c] != nil { return c }
        }
        if entries[baseCode] != nil { return baseCode }
        return entries.keys.sorted().first ?? baseCode
    }

    public func get(_ code: String?) throws -> PulseLocale {
        let c = resolve(code)
        if let l = cache[c] { return l }
        guard let d = entries[c] else { throw PluralRuleError.syntax("No locale registered for \(code ?? "nil")") }
        let fbCode = d.fallback ?? (d.code == baseCode ? nil : baseCode)
        var fb: PulseLocale? = nil
        if let f = fbCode, f != d.code, entries[f] != nil { fb = try get(f) }
        let l = try PulseLocale(d, fallback: fb)
        cache[c] = l
        return l
    }

    /// Locales in switcher order: `order`, then the base locale, then code.
    public func list() -> [LocaleSummary] {
        let base = baseCode
        return entries.values.sorted { a, b in
            let oa = a.order ?? (a.code == base ? -1 : 0)
            let ob = b.order ?? (b.code == base ? -1 : 0)
            if oa != ob { return oa < ob }
            return a.code < b.code
        }.map { LocaleSummary(code: $0.code, label: $0.label ?? $0.code.uppercased(), name: $0.name ?? $0.code, isRTL: $0.dir == "rtl") }
    }

    public var allData: [LocaleData] { list().compactMap { entries[$0.code] } }
}

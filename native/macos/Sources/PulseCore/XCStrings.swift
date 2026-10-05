import Foundation

/// Converts locale files to an Xcode String Catalog (Localizable.xcstrings)
/// and back.
///
/// Catalog keys carry their section: `strings.cpu`, `hardware.coreP`,
/// `plurals.thread`, `apps.Xcode`. Plurals become `variations.plural`.
/// `{name}` placeholders become positional specifiers (`%1$@`, and `%1$lld`
/// for a plural's `{n}`); each entry's comment lists the argument names so
/// the catalog reads back losslessly. Per-locale engine data (direction,
/// label, CLDR plural rules, numbering system, fonts) lives in `_locale.*`
/// entries marked `shouldTranslate: false`.
public enum XCStrings {
    public struct Output {
        public let json: JSONValue
        public let warnings: [String]
        public var text: String { json.serialized() }
    }

    public enum CatalogError: Error, CustomStringConvertible {
        case invalid(String)
        public var description: String { if case .invalid(let s) = self { return s }; return "" }
    }

    static let sections: [(String, WritableKeyPath<LocaleData, [String: String]?>)] = [
        ("strings", \.strings), ("hardware", \.hardware), ("apps", \.apps),
    ]

    // MARK: write

    public static func build(_ locales: [LocaleData], sourceLanguage: String = "en") throws -> Output {
        var warnings: [String] = []
        var entries: [String: JSONValue] = [:]
        let ordered = locales.sorted { a, b in
            if a.code == sourceLanguage { return b.code != sourceLanguage }
            if b.code == sourceLanguage { return false }
            return a.code < b.code
        }

        func unit(_ value: String) -> JSONValue {
            .object(["stringUnit": .object(["state": .string("translated"), "value": .string(value)])])
        }

        // Plain sections.
        for (sec, path) in sections {
            var keys = Set<String>()
            ordered.forEach { keys.formUnion(($0[keyPath: path] ?? [:]).keys) }
            for key in keys {
                var args: [String] = []
                for l in ordered {
                    if let v = l[keyPath: path]?[key] {
                        for p in Template.placeholders(v) where !args.contains(p) { args.append(p) }
                    }
                }
                var locs: [String: JSONValue] = [:]
                for l in ordered {
                    guard let v = l[keyPath: path]?[key] else { continue }
                    locs[l.code] = unit(toApple(v, args: args, numeric: []))
                }
                var e: [String: JSONValue] = ["localizations": .object(locs), "extractionState": .string("manual")]
                if !args.isEmpty { e["comment"] = .string(argComment(args)) }
                entries[sec + "." + key] = .object(e)
            }
        }

        // Plurals.
        var pluralKeys = Set<String>()
        ordered.forEach { pluralKeys.formUnion(($0.plurals ?? [:]).keys) }
        for key in pluralKeys {
            var args = ["n"]
            for l in ordered {
                for (_, v) in (l.plurals?[key] ?? [:]).sorted(by: { $0.key < $1.key }) {
                    for p in Template.placeholders(v) where !args.contains(p) { args.append(p) }
                }
            }
            var locs: [String: JSONValue] = [:]
            for l in ordered {
                guard let forms = l.plurals?[key] else { continue }
                let cats = Set((l.pluralRules ?? [:]).keys)
                var vars: [String: JSONValue] = [:]
                for (form, v) in forms {
                    let target: String
                    if form.hasPrefix("=") {
                        if form == "=0" && !cats.contains("zero") && forms["zero"] == nil {
                            target = "zero"
                        } else {
                            warnings.append("\(l.code): plurals.\(key).\(form) has no String Catalog form and was left out")
                            continue
                        }
                    } else {
                        target = form
                    }
                    vars[target] = unit(toApple(v, args: args, numeric: ["n"]))
                }
                locs[l.code] = .object(["variations": .object(["plural": .object(vars)])])
            }
            entries["plurals." + key] = .object([
                "localizations": .object(locs),
                "extractionState": .string("manual"),
                "comment": .string(argComment(args)),
            ])
        }

        // Locale metadata.
        var meta: [String: [String: String]] = [:]
        func put(_ k: String, _ code: String, _ v: String?) {
            guard let v = v else { return }
            meta[k, default: [:]][code] = v
        }
        for l in ordered {
            put("code", l.code, l.code)
            put("label", l.code, l.label)
            put("name", l.code, l.name)
            put("dir", l.code, l.dir)
            put("order", l.code, l.order.map { NumberText.shortest($0) })
            put("fallback", l.code, l.fallback)
            put("numberingSystem", l.code, l.numberingSystem)
            for (cat, rule) in l.pluralRules ?? [:] { put("pluralRules." + cat, l.code, rule) }
            if let f = l.fonts {
                put("fonts.ui", l.code, f.ui)
                put("fonts.hero", l.code, f.hero)
                for (p, s) in f.platforms ?? [:] {
                    put("fonts.platforms.\(p).ui", l.code, s.ui)
                    put("fonts.platforms.\(p).hero", l.code, s.hero)
                }
            }
        }
        for (k, byCode) in meta {
            var locs: [String: JSONValue] = [:]
            for (code, v) in byCode { locs[code] = unit(v) }
            entries["_locale." + k] = .object([
                "localizations": .object(locs),
                "extractionState": .string("manual"),
                "shouldTranslate": .bool(false),
                "comment": .string("Pulse locale data, generated from locales/<code>.json. Do not translate."),
            ])
        }

        let root: JSONValue = .object([
            "sourceLanguage": .string(sourceLanguage),
            "strings": .object(entries),
            "version": .string("1.0"),
        ])
        return Output(json: root, warnings: warnings)
    }

    static func argComment(_ args: [String]) -> String {
        "Arguments: " + args.enumerated().map { "\($0.offset + 1)=\($0.element)" }.joined(separator: " ")
    }

    static func parseArgComment(_ c: String?) -> [String] {
        guard let c = c, let r = c.range(of: "Arguments:") else { return [] }
        var out: [(Int, String)] = []
        for part in c[r.upperBound...].split(separator: " ") {
            let kv = part.split(separator: "=", maxSplits: 1)
            if kv.count == 2, let i = Int(kv[0]) { out.append((i, String(kv[1]))) }
        }
        return out.sorted { $0.0 < $1.0 }.map { $0.1 }
    }

    /// "{app} uses {pct}%" -> "%1$@ uses %2$@%%"
    static func toApple(_ s: String, args: [String], numeric: Set<String>) -> String {
        var out = ""
        var rest = Substring(s)
        while let idx = rest.firstIndex(where: { $0 == "{" || $0 == "%" }) {
            out += rest[..<idx]
            if rest[idx] == "%" {
                out += "%%"
                rest = rest[rest.index(after: idx)...]
                continue
            }
            let after = rest.index(after: idx)
            if let close = rest[after...].firstIndex(of: "}") {
                let name = String(rest[after..<close])
                if let pos = args.firstIndex(of: name) {
                    out += "%\(pos + 1)$" + (numeric.contains(name) ? "lld" : "@")
                    rest = rest[rest.index(after: close)...]
                    continue
                }
            }
            out += "{"
            rest = rest[after...]
        }
        return out + rest
    }

    /// "%1$@ uses %2$@%%" -> "{app} uses {pct}%"
    static func fromApple(_ s: String, args: [String]) -> String {
        var out = ""
        let chars = Array(s)
        var k = 0
        var seq = 0
        while k < chars.count {
            let ch = chars[k]
            guard ch == "%" else { out.append(ch); k += 1; continue }
            if k + 1 < chars.count && chars[k + 1] == "%" { out += "%"; k += 2; continue }
            var j = k + 1
            var digits = ""
            while j < chars.count, chars[j].isASCII, chars[j].isNumber { digits.append(chars[j]); j += 1 }
            var position: Int? = nil
            if !digits.isEmpty, j < chars.count, chars[j] == "$" { position = Int(digits); j += 1 }
            else { j = k + 1 }
            var spec = ""
            for candidate in ["lld", "ld", "d", "@"] where String(chars[j..<min(chars.count, j + candidate.count)]) == candidate {
                spec = candidate; break
            }
            if spec.isEmpty { out.append(ch); k += 1; continue }
            let idx = (position ?? (seq + 1)) - 1
            seq += 1
            out += idx >= 0 && idx < args.count ? "{\(args[idx])}" : "{arg\(idx + 1)}"
            k = j + spec.count
        }
        return out
    }

    // MARK: read

    public static func read(_ data: Data) throws -> [LocaleData] {
        let root = try JSONValue.parse(data)
        guard let strings = root["strings"]?.object else { throw CatalogError.invalid("catalog has no \"strings\"") }
        var out: [String: LocaleData] = [:]
        func locale(_ code: String) -> LocaleData { out[code] ?? LocaleData(code: code) }

        // Metadata first, so plural rules are known when plurals are read.
        for (key, entry) in strings where key.hasPrefix("_locale.") {
            let field = String(key.dropFirst("_locale.".count))
            for (code, loc) in entry["localizations"]?.object ?? [:] {
                guard let v = loc["stringUnit"]?["value"]?.string else { continue }
                var l = locale(code)
                switch field {
                case "code": break
                case "label": l.label = v
                case "name": l.name = v
                case "dir": l.dir = v
                case "order": l.order = Double(v)
                case "fallback": l.fallback = v
                case "numberingSystem": l.numberingSystem = v
                case "fonts.ui": l.fonts = (l.fonts ?? LocaleFonts()); l.fonts!.ui = v
                case "fonts.hero": l.fonts = (l.fonts ?? LocaleFonts()); l.fonts!.hero = v
                default:
                    if field.hasPrefix("pluralRules.") {
                        l.pluralRules = l.pluralRules ?? [:]
                        l.pluralRules![String(field.dropFirst("pluralRules.".count))] = v
                    } else if field.hasPrefix("fonts.platforms.") {
                        let parts = field.dropFirst("fonts.platforms.".count).split(separator: ".")
                        if parts.count == 2 {
                            l.fonts = l.fonts ?? LocaleFonts()
                            var plats = l.fonts!.platforms ?? [:]
                            var s = plats[String(parts[0])] ?? LocaleFonts.Stack()
                            if parts[1] == "ui" { s.ui = v } else { s.hero = v }
                            plats[String(parts[0])] = s
                            l.fonts!.platforms = plats
                        }
                    }
                }
                out[code] = l
            }
        }

        for (key, entry) in strings where !key.hasPrefix("_locale.") {
            guard let dot = key.firstIndex(of: ".") else { continue }
            let sec = String(key[..<dot]), name = String(key[key.index(after: dot)...])
            let args = parseArgComment(entry["comment"]?.string)
            for (code, loc) in entry["localizations"]?.object ?? [:] {
                var l = locale(code)
                if sec == "plurals" {
                    guard let vars = loc["variations"]?["plural"]?.object else { continue }
                    let cats = Set((l.pluralRules ?? [:]).keys)
                    var forms: [String: String] = [:]
                    for (form, u) in vars {
                        guard let v = u["stringUnit"]?["value"]?.string else { continue }
                        let target = (form == "zero" && !cats.contains("zero")) ? "=0" : form
                        forms[target] = fromApple(v, args: args)
                    }
                    l.plurals = l.plurals ?? [:]
                    l.plurals![name] = forms
                } else if let path = sections.first(where: { $0.0 == sec })?.1 {
                    guard let v = loc["stringUnit"]?["value"]?.string else { continue }
                    var d = l[keyPath: path] ?? [:]
                    d[name] = fromApple(v, args: args)
                    l[keyPath: path] = d
                }
                out[code] = l
            }
        }
        return out.values.sorted { $0.code < $1.code }
    }
}

extension LocaleData {
    /// Empty sections become nil, so a file with `"apps": {}` equals the same
    /// file read back from a catalog.
    public func normalized() -> LocaleData {
        var c = self
        func nz<T>(_ d: [String: T]?) -> [String: T]? { (d?.isEmpty ?? true) ? nil : d }
        c.pluralRules = nz(c.pluralRules)
        c.plurals = nz(c.plurals)
        c.strings = nz(c.strings)
        c.hardware = nz(c.hardware)
        c.apps = nz(c.apps)
        if let f = c.fonts, f.ui == nil, f.hero == nil, (f.platforms ?? [:]).isEmpty { c.fonts = nil }
        return c
    }
}

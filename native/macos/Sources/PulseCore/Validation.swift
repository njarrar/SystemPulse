import Foundation

public enum Template {
    static func isWordChar(_ c: Character) -> Bool {
        c == "_" || (c.isASCII && (c.isLetter || c.isNumber))
    }

    /// Placeholder names in order of first appearance.
    public static func placeholders(_ s: String) -> [String] {
        var out: [String] = []
        var rest = Substring(s)
        while let open = rest.firstIndex(of: "{") {
            let after = rest.index(after: open)
            guard let close = rest[after...].firstIndex(of: "}") else { break }
            let name = rest[after..<close]
            if !name.isEmpty, name.allSatisfy(isWordChar) {
                if !out.contains(String(name)) { out.append(String(name)) }
                rest = rest[rest.index(after: close)...]
            } else {
                rest = rest[after...]
            }
        }
        return out
    }
}

/// Checks a locale file against the base locale. Port of `validate()`.
public enum LocaleValidator {
    public struct Report: Equatable {
        public var errors: [String] = []
        public var warnings: [String] = []
    }

    public static func validate(_ data: LocaleData, base: LocaleData?) -> Report {
        var r = Report()
        let categories = PluralCategory.allCases.map { $0.rawValue }
        if data.code.isEmpty { r.errors.append("missing \"code\"") }
        if let d = data.dir, d != "ltr", d != "rtl" { r.errors.append("\"dir\" must be \"ltr\" or \"rtl\"") }
        if let rules = data.pluralRules {
            for c in rules.keys.sorted() {
                if !categories.contains(c) || c == "other" { r.errors.append("pluralRules: bad category \"\(c)\"") }
                else {
                    do { _ = try PluralRule(rules[c]!) } catch { r.errors.append("pluralRules.\(c): \(error)") }
                }
            }
        }
        let cats = ["other"] + (data.pluralRules ?? [:]).keys.sorted()
        for key in (data.plurals ?? [:]).keys.sorted() {
            let forms = data.plurals![key]!
            if forms["other"] == nil { r.errors.append("plurals.\(key): needs an \"other\" form") }
            for f in forms.keys.sorted() {
                let exact = f.hasPrefix("=") && f.count > 1 && f.dropFirst().allSatisfy { $0.isASCII && $0.isNumber }
                if !exact && !categories.contains(f) { r.errors.append("plurals.\(key): unknown form \"\(f)\"") }
                else if data.pluralRules != nil && categories.contains(f) && !cats.contains(f) {
                    r.warnings.append("plurals.\(key).\(f): this language never selects \"\(f)\"")
                }
            }
            if data.pluralRules != nil {
                for c in cats where c != "other" && forms[c] == nil {
                    r.warnings.append("plurals.\(key): no \"\(c)\" form, \"other\" is used")
                }
            }
        }
        if let base = base {
            let sections: [(String, [String: String]?, [String: String]?)] = [
                ("strings", base.strings, data.strings),
                ("hardware", base.hardware, data.hardware),
            ]
            for (sec, b0, d0) in sections {
                let b = b0 ?? [:], d = d0 ?? [:]
                for k in b.keys.sorted() {
                    guard let dv = d[k] else { r.warnings.append("\(sec).\(k): missing, falls back to \(base.code)"); continue }
                    let pb = Set(Template.placeholders(b[k]!)), pd = Set(Template.placeholders(dv))
                    for p in pb.sorted() where !pd.contains(p) { r.warnings.append("\(sec).\(k): drops {\(p)}") }
                    for p in pd.sorted() where !pb.contains(p) { r.errors.append("\(sec).\(k): unknown placeholder {\(p)}") }
                }
                for k in d.keys.sorted() where b[k] == nil { r.warnings.append("\(sec).\(k): not in \(base.code), never used") }
            }
            let bp = base.plurals ?? [:], dp = data.plurals ?? [:]
            for k in bp.keys.sorted() where dp[k] == nil { r.warnings.append("plurals.\(k): missing, falls back to \(base.code)") }
            for k in dp.keys.sorted() where bp[k] == nil { r.warnings.append("plurals.\(k): not in \(base.code), never used") }
        }
        return r
    }
}

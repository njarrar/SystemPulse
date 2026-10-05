import Foundation

/// CLDR plural categories, in the order rules are tried.
public enum PluralCategory: String, CaseIterable, Sendable {
    case zero, one, two, few, many, other
}

/// CLDR plural operands (UTS #35, "Plural Operand Meanings").
public struct PluralOperands: Equatable, Sendable {
    public var n: Double
    public var i: Double
    public var v: Double
    public var w: Double
    public var f: Double
    public var t: Double
    public var c: Double
    public var e: Double

    public init(n: Double, i: Double, v: Double, w: Double, f: Double, t: Double, c: Double, e: Double) {
        self.n = n; self.i = i; self.v = v; self.w = w; self.f = f; self.t = t; self.c = c; self.e = e
    }

    /// Operands for a number. `decimals` fixes the visible fraction digits,
    /// so 1 with one decimal ("1.0") differs from 1 in most languages.
    public init(_ value: Double, decimals: Int? = nil) {
        let s: String
        if let d = decimals {
            s = NumberText.fixed(abs(value), d)
        } else {
            s = NumberText.shortest(abs(value))
        }
        self.init(s)
    }

    /// Operands for a decimal string such as "1.50" or "1.2c3".
    public init(_ text: String) {
        var s = text.trimmingCharacters(in: .whitespaces)
        if s.hasPrefix("-") || s.hasPrefix("+") { s.removeFirst() }
        var exp = 0
        if let idx = s.lastIndex(where: { $0 == "c" || $0 == "e" || $0 == "C" || $0 == "E" }) {
            let tail = s[s.index(after: idx)...]
            if !tail.isEmpty, tail.allSatisfy({ $0.isASCII && $0.isNumber }), let ev = Int(tail) {
                exp = ev
                s = PluralOperands.shiftDecimal(String(s[..<idx]), ev)
            }
        }
        let parts = s.split(separator: ".", maxSplits: 1, omittingEmptySubsequences: false)
        let intPart = parts.first.map(String.init) ?? ""
        let frac = parts.count > 1 ? String(parts[1]) : ""
        var trimmed = frac
        while trimmed.hasSuffix("0") { trimmed.removeLast() }
        self.n = abs(Double(s) ?? 0)
        self.i = Double(intPart.isEmpty ? "0" : intPart) ?? 0
        self.v = Double(frac.count)
        self.w = Double(trimmed.count)
        self.f = frac.isEmpty ? 0 : (Double(frac) ?? 0)
        self.t = trimmed.isEmpty ? 0 : (Double(trimmed) ?? 0)
        self.c = Double(exp)
        self.e = Double(exp)
    }

    static func shiftDecimal(_ s: String, _ e: Int) -> String {
        let dot = s.firstIndex(of: ".")
        var digits = s.replacingOccurrences(of: ".", with: "")
        let pos = (dot.map { s.distance(from: s.startIndex, to: $0) } ?? s.count) + e
        while digits.count < pos { digits += "0" }
        if pos >= digits.count { return digits }
        let at = digits.index(digits.startIndex, offsetBy: pos)
        return String(digits[..<at]) + "." + String(digits[at...])
    }

    func value(_ name: String) -> Double? {
        switch name {
        case "n": return n
        case "i": return i
        case "v": return v
        case "w": return w
        case "f": return f
        case "t": return t
        case "c": return c
        case "e": return e
        default: return nil
        }
    }
}

public enum PluralRuleError: Error, CustomStringConvertible, Equatable {
    case syntax(String)
    public var description: String {
        switch self { case .syntax(let m): return m }
    }
}

/// A compiled CLDR plural condition.
///
///     condition     = and_condition ('or' and_condition)*
///     and_condition = relation ('and' relation)*
///     relation      = expr ('=' | '!=') range_list
///                   | expr 'is' 'not'? value
///                   | expr 'not'? ('in' | 'within') range_list
///     expr          = operand ('%' | 'mod' value)?
///     range_list    = (value | value '..' value) (',' range_list)*
///
/// Sample lists ("@integer ...", "@decimal ...") are ignored.
public struct PluralRule: Sendable {
    public let source: String
    private let test: @Sendable (PluralOperands) -> Bool

    public init(_ source: String) throws {
        self.source = source
        let tokens = try PluralRule.tokenize(source)
        var parser = Parser(tokens: tokens, source: source)
        self.test = try parser.parse()
    }

    public func matches(_ o: PluralOperands) -> Bool { test(o) }

    static func tokenize(_ src: String) throws -> [String] {
        var s = src
        if let at = s.firstIndex(of: "@") { s = String(s[..<at]) }
        let chars = Array(s.trimmingCharacters(in: .whitespacesAndNewlines))
        var out: [String] = []
        var k = 0
        while k < chars.count {
            let ch = chars[k]
            if ch == " " || ch == "\t" || ch == "\n" { k += 1; continue }
            if ch == "." && k + 1 < chars.count && chars[k + 1] == "." { out.append(".."); k += 2; continue }
            if ch == "!" && k + 1 < chars.count && chars[k + 1] == "=" { out.append("!="); k += 2; continue }
            if ch == "=" || ch == "%" || ch == "," { out.append(String(ch)); k += 1; continue }
            if ch.isASCII && ch.isLetter && ch.isLowercase {
                var j = k
                while j < chars.count, chars[j].isASCII, chars[j].isLetter, chars[j].isLowercase { j += 1 }
                out.append(String(chars[k..<j])); k = j; continue
            }
            if ch.isASCII && ch.isNumber {
                var j = k
                while j < chars.count, chars[j].isASCII, chars[j].isNumber { j += 1 }
                // A decimal part, but not the start of a ".." range.
                if j + 1 < chars.count, chars[j] == ".", chars[j + 1] != ".", chars[j + 1].isASCII, chars[j + 1].isNumber {
                    j += 1
                    while j < chars.count, chars[j].isASCII, chars[j].isNumber { j += 1 }
                }
                out.append(String(chars[k..<j])); k = j; continue
            }
            throw PluralRuleError.syntax("Bad plural rule near: " + String(chars[k...]))
        }
        return out
    }

    private struct Parser {
        let tokens: [String]
        let source: String
        var p = 0

        init(tokens: [String], source: String) { self.tokens = tokens; self.source = source }

        func peek() -> String? { p < tokens.count ? tokens[p] : nil }
        mutating func next() -> String? { defer { p += 1 }; return peek() }

        mutating func num() throws -> Double {
            guard let x = next(), let first = x.first, first.isNumber, let v = Double(x) else {
                throw PluralRuleError.syntax("Expected number in plural rule: \(source)")
            }
            return v
        }

        mutating func expr() throws -> @Sendable (PluralOperands) -> Double {
            guard let op = next(), op.count == 1, "nivwftce".contains(op) else {
                throw PluralRuleError.syntax("Unknown operand \"\(tokens[min(p - 1, tokens.count - 1)])\" in plural rule: \(source)")
            }
            var mod: Double? = nil
            if peek() == "%" || peek() == "mod" { _ = next(); mod = try num() }
            let m = mod
            return { o in
                let x = o.value(op) ?? 0
                guard let m = m else { return x }
                return x.truncatingRemainder(dividingBy: m)
            }
        }

        mutating func rangeList() throws -> [(Double, Double)] {
            var ranges: [(Double, Double)] = []
            repeat {
                let lo = try num()
                var hi = lo
                if peek() == ".." { _ = next(); hi = try num() }
                ranges.append((lo, hi))
                if peek() == "," { _ = next() } else { break }
            } while true
            return ranges
        }

        mutating func relation() throws -> @Sendable (PluralOperands) -> Bool {
            let e = try expr()
            var neg = false
            var integerOnly = true
            var ranges: [(Double, Double)]
            guard var op = next() else { throw PluralRuleError.syntax("Unexpected end of plural rule: \(source)") }
            if op == "is" {
                if peek() == "not" { _ = next(); neg = true }
                let v = try num()
                ranges = [(v, v)]
            } else if op == "=" || op == "!=" {
                neg = op == "!="
                ranges = try rangeList()
            } else {
                if op == "not" {
                    neg = true
                    guard let o2 = next() else { throw PluralRuleError.syntax("Unexpected end of plural rule: \(source)") }
                    op = o2
                }
                if op == "within" { integerOnly = false }
                else if op != "in" { throw PluralRuleError.syntax("Unknown relation \"\(op)\" in plural rule: \(source)") }
                ranges = try rangeList()
            }
            let rs = ranges, n = neg, io = integerOnly
            return { o in
                let x = e(o)
                var hit = false
                for r in rs where !hit {
                    hit = x >= r.0 && x <= r.1 && (!io || x == x.rounded(.down))
                }
                return n ? !hit : hit
            }
        }

        mutating func andCond() throws -> @Sendable (PluralOperands) -> Bool {
            var rs = [try relation()]
            while peek() == "and" { _ = next(); rs.append(try relation()) }
            let all = rs
            return { o in all.allSatisfy { $0(o) } }
        }

        mutating func parse() throws -> @Sendable (PluralOperands) -> Bool {
            if tokens.isEmpty { return { _ in true } }
            var ors = [try andCond()]
            while peek() == "or" { _ = next(); ors.append(try andCond()) }
            if p != tokens.count { throw PluralRuleError.syntax("Unexpected \"\(tokens[p])\" in plural rule: \(source)") }
            let all = ors
            return { o in all.contains { $0(o) } }
        }
    }
}

/// Picks a CLDR category from a locale file's `pluralRules`.
public struct PluralSelector: Sendable {
    private let compiled: [(PluralCategory, PluralRule)]
    public let hasRules: Bool

    public init(rules: [String: String]?) throws {
        var list: [(PluralCategory, PluralRule)] = []
        for cat in PluralCategory.allCases where cat != .other {
            if let src = rules?[cat.rawValue] { list.append((cat, try PluralRule(src))) }
        }
        compiled = list
        hasRules = rules != nil
    }

    /// The categories this language can select, always ending in `other`.
    public var categories: [PluralCategory] { compiled.map { $0.0 } + [.other] }

    public func select(_ o: PluralOperands) -> PluralCategory {
        if !hasRules {
            // No "pluralRules" in the file: one for exactly 1, else other.
            // An empty object means the language only uses "other" (ja, zh).
            return (o.i == 1 && o.v == 0) ? .one : .other
        }
        for (cat, rule) in compiled where rule.matches(o) { return cat }
        return .other
    }

    public func select(_ value: Double, decimals: Int? = nil) -> PluralCategory {
        select(PluralOperands(value, decimals: decimals))
    }

    public func select(_ text: String) -> PluralCategory {
        select(PluralOperands(text))
    }
}

import Foundation

/// Number to text without locale data, so results match on macOS and Linux.
public enum NumberText {
    /// Fixed decimals, rounding half away from zero on the exact binary value
    /// (the same result as JavaScript `toFixed` and `Intl.NumberFormat`).
    public static func fixed(_ x: Double, _ decimals: Int) -> String {
        guard x.isFinite else { return x.isNaN ? "NaN" : (x < 0 ? "-∞" : "∞") }
        let d = max(0, min(20, decimals))
        let neg = x < 0
        // %f with a long precision prints the exact decimal expansion.
        let exact = String(format: "%.60f", abs(x))
        let parts = exact.split(separator: ".", maxSplits: 1)
        var intDigits = Array(parts[0])
        let fracAll = parts.count > 1 ? Array(parts[1]) : []
        var frac = Array(fracAll.prefix(d))
        while frac.count < d { frac.append("0") }
        let rest = fracAll.count > d ? fracAll[d...] : []
        let roundUp = rest.first.map { $0 >= "5" } ?? false
        if roundUp {
            var digits = intDigits + frac
            var k = digits.count - 1
            var carry = true
            while carry && k >= 0 {
                if digits[k] == "9" { digits[k] = "0"; k -= 1 } else {
                    digits[k] = Character(String(digits[k].wholeNumberValue! + 1)); carry = false
                }
            }
            if carry { digits.insert("1", at: 0) }
            intDigits = Array(digits.prefix(digits.count - d))
            frac = Array(digits.suffix(d))
        }
        var s = String(intDigits)
        if d > 0 { s += "." + String(frac) }
        let isZero = s.allSatisfy { $0 == "0" || $0 == "." }
        return (neg && !isZero ? "-" : "") + s
    }

    /// Shortest text that reads back as the same number, with no ".0" on
    /// whole numbers (JavaScript `String(x)` for everyday values).
    public static func shortest(_ x: Double) -> String {
        if x.isFinite, x == x.rounded(), abs(x) < 1e15 {
            return String(Int64(x))
        }
        return "\(x)"
    }

    /// Digits and decimal separator for a CLDR numbering system.
    public struct System: Sendable {
        public let zero: UInt32
        public let decimal: String
    }

    public static let systems: [String: System] = [
        "latn": System(zero: 0x30, decimal: "."),
        "arab": System(zero: 0x660, decimal: "\u{066B}"),
        "arabext": System(zero: 0x6F0, decimal: "\u{066B}"),
        "deva": System(zero: 0x966, decimal: "."),
        "beng": System(zero: 0x9E6, decimal: "."),
        "guru": System(zero: 0xA66, decimal: "."),
        "gujr": System(zero: 0xAE6, decimal: "."),
        "tamldec": System(zero: 0xBE6, decimal: "."),
        "telu": System(zero: 0xC66, decimal: "."),
        "knda": System(zero: 0xCE6, decimal: "."),
        "mlym": System(zero: 0xD66, decimal: "."),
        "thai": System(zero: 0xE50, decimal: "."),
        "laoo": System(zero: 0xED0, decimal: "."),
        "tibt": System(zero: 0xF20, decimal: "."),
        "mymr": System(zero: 0x1040, decimal: "."),
        "khmr": System(zero: 0x17E0, decimal: "."),
        "fullwide": System(zero: 0xFF10, decimal: "."),
    ]

    /// Rewrites ASCII digits and the "." separator into a numbering system.
    public static func localize(_ ascii: String, system: String) -> String {
        guard let sys = systems[system], system != "latn" else { return ascii }
        var out = ""
        for ch in ascii.unicodeScalars {
            if ch.value >= 0x30 && ch.value <= 0x39, let u = Unicode.Scalar(sys.zero + (ch.value - 0x30)) {
                out.unicodeScalars.append(u)
            } else if ch == "." {
                out += sys.decimal
            } else {
                out.unicodeScalars.append(ch)
            }
        }
        return out
    }
}

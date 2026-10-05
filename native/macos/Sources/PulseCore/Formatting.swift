import Foundation

/// Unit formatting for the flyout. Units and signs stay Latin and LTR in
/// every locale; digits follow the locale's numbering system.
public struct PulseFormat {
    public let lc: PulseLocale
    public var fahrenheit: Bool

    public init(_ lc: PulseLocale, fahrenheit: Bool = false) {
        self.lc = lc
        self.fahrenheit = fahrenheit
    }

    public func n(_ x: Double, _ d: Int = 0) -> String { lc.num(x, d) }

    /// "64%". With no decimals the value is rounded first, like the prototype.
    public func pct(_ x: Double, _ d: Int = 0) -> String {
        lc.num(d == 0 ? x.rounded(.toNearestOrAwayFromZero) : x, d) + "%"
    }

    public func temp(_ c: Double, _ d: Int = 0) -> String {
        fahrenheit ? lc.num(c * 9 / 5 + 32, d) + "°F" : lc.num(c, d) + "°C"
    }

    public func watts(_ w: Double) -> String { lc.num(w, 1) + " W" }

    /// "15.1W" for the status pill and the menu bar.
    public func wattsShort(_ w: Double) -> String { lc.num(w, 1) + "W" }

    /// Memory in binary units, the way Activity Monitor shows it ("6.1 GB").
    public func memory(_ bytes: Double) -> String {
        let gb = bytes / 1_073_741_824
        if gb < 0.25 {
            let mb = bytes / 1_048_576
            return lc.num(mb, mb < 10 ? 1 : 0) + " MB"
        }
        return lc.num(gb, 2) + " GB"
    }

    /// GB value with fixed decimals, no unit (for "9.3 GB of 16 GB").
    public func gib(_ bytes: Double, _ d: Int) -> String { lc.num(bytes / 1_073_741_824, d) }

    /// Storage sizes in decimal units, the way Finder shows them.
    public func storage(_ bytes: Double) -> String {
        let units = ["B", "KB", "MB", "GB", "TB", "PB"]
        var v = bytes, k = 0
        while v >= 1000 && k < units.count - 1 { v /= 1000; k += 1 }
        let d = k == 0 ? 0 : (v < 10 ? 1 : 0)
        return lc.num(v, d) + " " + units[k]
    }

    /// Transfer rates in decimal units: "1.7 MB/s", "149 KB/s".
    public func rate(_ bytesPerSec: Double) -> String {
        let b = max(0, bytesPerSec)
        if b >= 1_000_000_000 { return lc.num(b / 1_000_000_000, 1) + " GB/s" }
        if b >= 1_000_000 { return lc.num(b / 1_000_000, 1) + " MB/s" }
        return lc.num((b / 1000).rounded(.toNearestOrAwayFromZero), 0) + " KB/s"
    }

    /// "2h 40m" or "52m".
    public func duration(minutes: Int) -> String {
        let h = minutes / 60, m = minutes % 60
        return h > 0 ? lc.t("durHM", ["h": .number(Double(h)), "m": .number(Double(m))]) : lc.t("durM", ["m": .number(Double(m))])
    }

    public func rpm(_ x: Double) -> String { lc.grouped(x) + " RPM" }

    public func ghz(_ mhz: Double) -> String {
        mhz >= 1000 ? lc.num(mhz / 1000, 2) + " GHz" : lc.num(mhz, 0) + " MHz"
    }

    /// Chart scrub label: "Now", "45s ago", "4m ago", "4m 30s ago".
    public func ago(seconds: Int) -> String {
        if seconds <= 0 { return lc.t("now") }
        let m = seconds / 60, s = seconds % 60
        if m == 0 { return lc.t("agoS", ["s": .number(Double(s))]) }
        if s == 0 { return lc.t("agoM", ["m": .number(Double(m))]) }
        return lc.t("agoMS", ["m": .number(Double(m)), "s": .number(Double(s))])
    }
}

/// Thermal stage from a CPU temperature (°C): Cool, Warm, Hot, Throttled.
public enum ThermalStage: Int, CaseIterable, Sendable {
    case cool = 0, warm, hot, throttled

    public init(celsius c: Double, osThermalState: Int = 0) {
        // ProcessInfo.ThermalState: 0 nominal, 1 fair, 2 serious, 3 critical.
        if osThermalState >= 2 { self = .throttled; return }
        if c < 48 { self = .cool } else if c < 70 { self = .warm } else if c < 90 { self = .hot } else { self = .throttled }
    }

    public var key: String { ["cool", "warm", "hot", "throttled"][rawValue] }
}

import Foundation

/// Fixed-capacity ring buffer. Appending past capacity drops the oldest value.
public struct RingBuffer<Element>: Sequence {
    private var storage: [Element?]
    private var head = 0
    public private(set) var count = 0
    public let capacity: Int

    public init(capacity: Int) {
        precondition(capacity > 0, "capacity must be positive")
        self.capacity = capacity
        storage = Array(repeating: nil, count: capacity)
    }

    public mutating func append(_ x: Element) {
        storage[(head + count) % capacity] = x
        if count < capacity { count += 1 } else { head = (head + 1) % capacity }
    }

    public mutating func removeAll() {
        storage = Array(repeating: nil, count: capacity)
        head = 0
        count = 0
    }

    public var isEmpty: Bool { count == 0 }
    public var isFull: Bool { count == capacity }

    /// Oldest first.
    public subscript(i: Int) -> Element {
        precondition(i >= 0 && i < count, "index out of range")
        return storage[(head + i) % capacity]!
    }

    public var last: Element? { count == 0 ? nil : self[count - 1] }
    public var first: Element? { count == 0 ? nil : self[0] }

    public var elements: [Element] { (0..<count).map { self[$0] } }

    public func makeIterator() -> IndexingIterator<[Element]> { elements.makeIterator() }
}

/// A timestamped sample.
public struct Sample: Equatable, Sendable {
    public var time: TimeInterval
    public var value: Double
    public init(_ time: TimeInterval, _ value: Double) { self.time = time; self.value = value }
}

/// Two views of one metric: a short sparkline (Tier 2, "1 min") and the
/// 10-minute history for Tier 3 charts, sampled at a slower fixed step.
public struct MetricHistory {
    public private(set) var spark: RingBuffer<Double>
    public private(set) var long: RingBuffer<Sample>
    public let longStep: TimeInterval
    private var bucket: (sum: Double, n: Int, start: TimeInterval)? = nil

    /// Defaults: 41 sparkline points (60 s at 1.5 s), 121 long points (10 min at 5 s).
    public init(sparkCount: Int = 41, longCount: Int = 121, longStep: TimeInterval = 5) {
        spark = RingBuffer(capacity: sparkCount)
        long = RingBuffer(capacity: longCount)
        self.longStep = longStep
    }

    public mutating func add(_ value: Double, at time: TimeInterval) {
        spark.append(value)
        if var b = bucket {
            b.sum += value; b.n += 1
            if time - b.start >= longStep {
                long.append(Sample(time, b.sum / Double(b.n)))
                bucket = (0, 0, time)
            } else {
                bucket = b
            }
        } else {
            long.append(Sample(time, value))
            bucket = (0, 0, time)
        }
    }

    /// The long series with the live value as its last point, like the prototype.
    public func chartValues(live: Double?) -> [Double] {
        var v = long.elements.map { $0.value }
        if let l = live { if v.isEmpty { v = [l] } else { v[v.count - 1] = l } }
        return v
    }

    public var sparkValues: [Double] { spark.elements }

    public mutating func reset() {
        spark.removeAll(); long.removeAll(); bucket = nil
    }
}

/// Raises a hog alert when one app holds more than `threshold` percent of
/// total CPU for at least `window` seconds without a break.
public struct HogDetector {
    public var threshold: Double
    public var window: TimeInterval
    private var above: [String: TimeInterval] = [:]

    public init(threshold: Double = 50, window: TimeInterval = 120) {
        self.threshold = threshold
        self.window = window
    }

    /// Feeds one round of per-app CPU (percent of total). Returns the id of
    /// the app over the line longest, if any has stayed there for `window`.
    public mutating func update(_ usage: [String: Double], at time: TimeInterval) -> String? {
        var next: [String: TimeInterval] = [:]
        for (id, cpu) in usage where cpu > threshold {
            next[id] = above[id] ?? time
        }
        above = next
        return above.filter { time - $0.value >= window }.min { $0.value < $1.value }?.key
    }

    /// Seconds an app has been above the threshold, or nil.
    public func since(_ id: String, now: TimeInterval) -> TimeInterval? {
        above[id].map { now - $0 }
    }
}

/// Chart geometry shared by sparklines and detail charts.
public enum ChartMath {
    public struct Point: Equatable { public var x: Double; public var y: Double }

    /// Catmull-Rom to cubic Bezier: returns (control1, control2, end) per segment.
    public static func smooth(_ p: [Point]) -> [(Point, Point, Point)] {
        guard p.count > 1 else { return [] }
        var out: [(Point, Point, Point)] = []
        for i in 0..<(p.count - 1) {
            let p0 = i > 0 ? p[i - 1] : p[i], p1 = p[i], p2 = p[i + 1]
            let p3 = i + 2 < p.count ? p[i + 2] : p2
            let c1 = Point(x: p1.x + (p2.x - p0.x) / 6, y: p1.y + (p2.y - p0.y) / 6)
            let c2 = Point(x: p2.x - (p3.x - p1.x) / 6, y: p2.y - (p3.y - p1.y) / 6)
            out.append((c1, c2, p2))
        }
        return out
    }

    /// Sparkline points in a W x H box (prototype `spark()`).
    public static func sparkPoints(_ vals: [Double], width w: Double, height h: Double) -> [Point] {
        guard !vals.isEmpty else { return [] }
        let v = vals.count == 1 ? [vals[0], vals[0]] : vals
        let mn = v.min()!, mx = v.max()!, rg = max(mx - mn, 0.5)
        let lo = mn - rg * 0.25, hi = mx + rg * 0.35
        let n = Double(v.count - 1)
        return v.enumerated().map { i, x in
            Point(x: Double(i) / n * w, y: h - 2 - (x - lo) / (hi - lo) * (h - 4))
        }
    }

    /// Detail chart points (prototype `buildDetail`). `fromZero` pins the floor at 0.
    /// The series fills the right side when it is shorter than `slots`.
    public static func chartPoints(_ vals: [Double], slots: Int, width w: Double, height h: Double, fromZero: Bool) -> [Point] {
        guard !vals.isEmpty else { return [] }
        let mx = vals.max()!, mn = vals.min()!
        let lo = fromZero ? 0 : max(0, mn - (mx - mn) * 0.8)
        var hi = mx + (mx - lo) * 0.2
        if hi - lo < 1e-9 { hi = lo + 1 }
        let n = Double(max(slots, vals.count) - 1)
        let offset = max(slots, vals.count) - vals.count
        return vals.enumerated().map { i, x in
            Point(x: Double(i + offset) / max(n, 1) * w, y: h - 6 - (x - lo) / (hi - lo) * (h - 34))
        }
    }

    /// Split (mirrored) chart: series A above the centre line, B below.
    public static func splitPoints(_ a: [Double], _ b: [Double], slots: Int, width w: Double, height h: Double) -> ([Point], [Point]) {
        let c = h / 2
        let ma = max((a.max() ?? 0) * 1.12, 1e-9), mb = max((b.max() ?? 0) * 1.12, 1e-9)
        let n = Double(max(slots, a.count, b.count) - 1)
        func place(_ v: [Double], up: Bool, m: Double) -> [Point] {
            let offset = max(slots, v.count) - v.count
            return v.enumerated().map { i, x in
                let y = up ? c - 1 - (x / m) * (c - 22) : c + 1 + (x / m) * (c - 22)
                return Point(x: Double(i + offset) / max(n, 1) * w, y: y)
            }
        }
        return (place(a, up: true, m: ma), place(b, up: false, m: mb))
    }
}

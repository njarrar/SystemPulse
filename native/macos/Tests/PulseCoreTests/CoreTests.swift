import Foundation
import XCTest
@testable import PulseCore

final class CoreTests: XCTestCase {
    func testFixedRoundsHalfAwayFromZeroOnExactValue() {
        XCTAssertEqual(NumberText.fixed(2.5, 0), "3")
        XCTAssertEqual(NumberText.fixed(0.125, 2), "0.13")
        XCTAssertEqual(NumberText.fixed(1.005, 2), "1.00")   // 1.00499999... in binary
        XCTAssertEqual(NumberText.fixed(9.96, 1), "10.0")
        XCTAssertEqual(NumberText.fixed(-0.04, 1), "0.0")
        XCTAssertEqual(NumberText.fixed(-1.25, 1), "-1.3")
        XCTAssertEqual(NumberText.shortest(84), "84")
        XCTAssertEqual(NumberText.shortest(1.5), "1.5")
    }

    func testFormatting() throws {
        let lc = try PulseLocale(try Paths.locale("en"))
        var f = PulseFormat(lc)
        XCTAssertEqual(f.pct(63.6), "64%")
        XCTAssertEqual(f.pct(52.04, 1), "52.0%")
        XCTAssertEqual(f.rate(1_740_000), "1.7 MB/s")
        XCTAssertEqual(f.rate(149_400), "149 KB/s")
        XCTAssertEqual(f.memory(3.12 * 1_073_741_824), "3.12 GB")
        XCTAssertEqual(f.memory(40 * 1_048_576), "40 MB")
        XCTAssertEqual(f.storage(248e9), "248 GB")
        XCTAssertEqual(f.duration(minutes: 160), "2h 40m")
        XCTAssertEqual(f.duration(minutes: 52), "52m")
        XCTAssertEqual(f.temp(52), "52°C")
        f.fahrenheit = true
        XCTAssertEqual(f.temp(52), "126°F")
        XCTAssertEqual(f.ago(seconds: 0), "Now")
        XCTAssertEqual(f.ago(seconds: 45), "45s ago")
        XCTAssertEqual(f.ago(seconds: 240), "4m ago")
        XCTAssertEqual(f.ago(seconds: 270), "4m 30s ago")
        XCTAssertEqual(f.rpm(1180), "1,180 RPM")
    }

    func testThermalStages() {
        XCTAssertEqual(ThermalStage(celsius: 33), .cool)
        XCTAssertEqual(ThermalStage(celsius: 52), .warm)
        XCTAssertEqual(ThermalStage(celsius: 75), .hot)
        XCTAssertEqual(ThermalStage(celsius: 95), .throttled)
        XCTAssertEqual(ThermalStage(celsius: 40, osThermalState: 2), .throttled)
    }

    func testRingBuffer() {
        var r = RingBuffer<Int>(capacity: 3)
        XCTAssertTrue(r.isEmpty)
        for i in 1...5 { r.append(i) }
        XCTAssertEqual(r.elements, [3, 4, 5])
        XCTAssertEqual(r.first, 3)
        XCTAssertEqual(r.last, 5)
        XCTAssertTrue(r.isFull)
        r.removeAll()
        XCTAssertEqual(r.count, 0)
    }

    func testMetricHistoryBucketsTheLongSeries() {
        var h = MetricHistory(sparkCount: 4, longCount: 3, longStep: 5)
        var t = 0.0
        for v in stride(from: 1.0, through: 20.0, by: 1.0) { h.add(v, at: t); t += 1.5 }
        XCTAssertEqual(h.sparkValues, [17, 18, 19, 20])
        XCTAssertEqual(h.long.count, 3)
        XCTAssertEqual(h.chartValues(live: 99).last, 99)
        // Each long point averages the samples of one 5 s step.
        let vals = h.long.elements.map { $0.value }
        XCTAssertTrue(zip(vals, vals.dropFirst()).allSatisfy { $0 < $1 })
    }

    func testHogNeedsTwoMinutesAboveFiftyPercent() {
        var d = HogDetector()
        XCTAssertNil(d.update(["xcode": 60, "safari": 4], at: 0))
        XCTAssertNil(d.update(["xcode": 61], at: 119))
        XCTAssertEqual(d.update(["xcode": 58], at: 120), "xcode")
        // One dip below the line restarts the clock.
        XCTAssertNil(d.update(["xcode": 40], at: 121))
        XCTAssertNil(d.update(["xcode": 70], at: 122))
        XCTAssertEqual(d.since("xcode", now: 130), 8)
    }

    func testProcessGroupingUsesResponsiblePidThenParents() {
        var g = ProcessGrouper()
        let apps = [AppIdentity(pid: 100, name: "Safari", bundleID: "com.apple.Safari"),
                    AppIdentity(pid: 200, name: "Xcode", bundleID: "com.apple.dt.Xcode")]
        func procs(_ scale: UInt64) -> [ProcessSample] {
            [
                ProcessSample(pid: 100, ppid: 1, name: "Safari", cpuTimeNs: 1_000_000_000 * scale, residentBytes: 500, threads: 10),
                // XPC helper: parent is launchd, responsible pid is Safari.
                ProcessSample(pid: 101, ppid: 1, responsiblePid: 100, name: "com.apple.WebKit.WebContent", cpuTimeNs: 500_000_000 * scale, residentBytes: 300, threads: 5),
                ProcessSample(pid: 200, ppid: 1, name: "Xcode", cpuTimeNs: 4_000_000_000 * scale, residentBytes: 900, threads: 80),
                // Child by parent chain.
                ProcessSample(pid: 201, ppid: 200, name: "SourceKitService", cpuTimeNs: 0, residentBytes: 100, threads: 4),
                ProcessSample(pid: 300, ppid: 1, name: "mds_stores", cpuTimeNs: 100_000_000 * scale, residentBytes: 50, threads: 3),
                ProcessSample(pid: 301, ppid: 1, name: "mds_stores", cpuTimeNs: 100_000_000 * scale, residentBytes: 50, threads: 3),
            ]
        }
        XCTAssertEqual(g.update(processes: procs(1), apps: apps, cpuCount: 4, at: 10).map { $0.cpu }.max(), 0)
        let rows = g.update(processes: procs(2), apps: apps, cpuCount: 4, at: 12)
        let byName = Dictionary(uniqueKeysWithValues: rows.map { ($0.name, $0) })
        XCTAssertEqual(rows.count, 3)
        XCTAssertEqual(byName["Safari"]?.processCount, 2)
        XCTAssertEqual(byName["Safari"]?.threads, 15)
        XCTAssertEqual(byName["Safari"]?.memory, 800)
        XCTAssertEqual(byName["Safari"]?.id, "com.apple.Safari")
        XCTAssertEqual(byName["Xcode"]?.processCount, 2)
        // 4 s of CPU time over 2 s on 4 cores = 50% of total CPU.
        XCTAssertEqual(byName["Xcode"]?.cpu ?? 0, 50, accuracy: 0.001)
        XCTAssertEqual(byName["Safari"]?.cpu ?? 0, 18.75, accuracy: 0.001)
        XCTAssertEqual(byName["mds_stores"]?.isApp, false)
        XCTAssertEqual(byName["mds_stores"]?.processCount, 2)
        XCTAssertEqual(byName["mds_stores"]?.pid, 300)
        XCTAssertEqual(rows.first?.name, "Xcode")
    }

    func testCPUTicks() {
        let a = CPUTicks(user: 100, system: 50, idle: 850, nice: 0)
        let b = CPUTicks(user: 160, system: 70, idle: 870, nice: 0)
        let l = CPUTicks.load(from: a, to: b)
        XCTAssertEqual(l.user, 60, accuracy: 0.01)
        XCTAssertEqual(l.system, 20, accuracy: 0.01)
        XCTAssertEqual(l.total, 80, accuracy: 0.01)
    }

    func testChartMath() {
        let pts = ChartMath.chartPoints([0, 50, 100], slots: 121, width: 372, height: 140, fromZero: true)
        XCTAssertEqual(pts.count, 3)
        XCTAssertEqual(pts.last?.x, 372)
        XCTAssertEqual(pts.first?.x ?? 0, 372 * 118.0 / 120.0, accuracy: 0.001)
        XCTAssertEqual(ChartMath.smooth(pts).count, 2)
        let (a, b) = ChartMath.splitPoints([1, 2], [3, 4], slots: 2, width: 100, height: 100)
        XCTAssertTrue(a.allSatisfy { $0.y < 50 })
        XCTAssertTrue(b.allSatisfy { $0.y > 50 })
        XCTAssertEqual(ChartMath.sparkPoints([5], width: 10, height: 10).count, 2)
    }

    func testJSONWriterIsStable() throws {
        let v: JSONValue = .object(["b": .array([.string("x\"y"), .number(1.5)]), "a": .bool(false), "c": .object([:])])
        XCTAssertEqual(v.serialized(), "{\n  \"a\" : false,\n  \"b\" : [\n    \"x\\\"y\",\n    1.5\n  ],\n  \"c\" : {\n\n  }\n}\n")
        XCTAssertEqual(try JSONValue.parse(Data(v.serialized().utf8))["b"], v["b"])
    }
}

import XCTest
@testable import PulseCore

final class DemoOverlayTests: XCTestCase {
    private func sample() -> Snapshot {
        var s = Snapshot()
        s.cpu.total = 20; s.cpu.user = 12; s.cpu.system = 8
        s.power.systemWatts = 9; s.power.hasBattery = true; s.power.percent = 70; s.power.batteryWatts = -9
        s.thermal.cpu = 40
        s.apps = [
            AppUsage(id: "a", name: "Safari", pid: 10, pids: [10, 11], processCount: 2, threads: 30, cpu: 8, memory: 1e9, gpu: 0, isApp: true),
            AppUsage(id: "b", name: "Music", pid: 20, pids: [20], processCount: 1, threads: 12, cpu: 4, memory: 2e8, gpu: 0, isApp: true),
            AppUsage(id: "k", name: "kernel_task", pid: 0, pids: [0], processCount: 1, threads: 200, cpu: 9, memory: 1e8, gpu: 0, isApp: false),
        ]
        return s
    }

    func testOffLeavesRealDataAlone() {
        let r = DemoOverlay().apply(sample())
        XCTAssertEqual(r.snapshot.cpu.total, 20)
        XCTAssertNil(r.hogID)
    }

    func testHogRampsTopAppAndTotals() {
        let r = DemoOverlay(hog: true).apply(sample())
        XCTAssertEqual(r.hogID, "a")
        let hog = r.snapshot.apps.first!
        XCTAssertEqual(hog.id, "a")
        XCTAssertGreaterThan(hog.cpu, 50)
        XCTAssertEqual(r.snapshot.cpu.total, 20 + hog.cpu - 8, accuracy: 1e-9)
        XCTAssertEqual(r.snapshot.power.systemWatts!, 9 + DemoOverlay.hogWatts, accuracy: 1e-9)
        XCTAssertEqual(r.snapshot.thermal.cpu!, 40 + DemoOverlay.hogTempRise, accuracy: 1e-9)
    }

    func testEndedAppsLeaveAndFreeCPU() {
        let r = DemoOverlay(hog: true).apply(sample(), ended: [11])
        XCTAssertFalse(r.snapshot.apps.contains { $0.id == "a" })
        XCTAssertEqual(r.hogID, "b")
        let plain = DemoOverlay().apply(sample(), ended: [11])
        XCTAssertEqual(plain.snapshot.cpu.total, 12, accuracy: 1e-9)
    }

    func testChargingShowsFullIn() {
        let r = DemoOverlay(charging: true).apply(sample())
        XCTAssertTrue(r.snapshot.power.charging)
        XCTAssertEqual(r.snapshot.power.minutesToFull, DemoOverlay.chargeMinutes)
        XCTAssertEqual(r.snapshot.power.adapterName, "USB-C 96 W")
        XCTAssertEqual(r.snapshot.power.batteryWatts, DemoOverlay.chargeWatts)
    }
}

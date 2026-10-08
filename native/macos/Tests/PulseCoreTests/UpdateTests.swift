import Foundation
import XCTest
@testable import PulseCore

final class UpdateTests: XCTestCase {
    func testVersionComparison() {
        XCTAssertTrue(UpdateVersion.isNewer("1.3.0", than: "1.2.2"))
        XCTAssertTrue(UpdateVersion.isNewer("1.10", than: "1.9.9"))
        XCTAssertTrue(UpdateVersion.isNewer("2", than: "1.99.99"))
        XCTAssertTrue(UpdateVersion.isNewer("1.2.2.1", than: "1.2.2"))
        XCTAssertFalse(UpdateVersion.isNewer("1.2.2", than: "1.2.2"))
        XCTAssertFalse(UpdateVersion.isNewer("1.2", than: "1.2.0"))
        XCTAssertFalse(UpdateVersion.isNewer("1.2.1", than: "1.2.10"))
        XCTAssertFalse(UpdateVersion.isNewer("0.9", than: "1.0"))
        XCTAssertEqual(UpdateVersion.parts("1.3.0-beta"), [1, 3, 0])
    }

    func testManifestDecodes() throws {
        let json = #"{"version":"1.3.0","files":{"universal":{"path":"build/macos/Pulse-macos-universal.zip","sha256":"ab12","size":123}}}"#
        let m = try UpdateManifest.decode(Data(json.utf8))
        XCTAssertEqual(m.version, "1.3.0")
        XCTAssertEqual(m.files["universal"]?.path, "build/macos/Pulse-macos-universal.zip")
        XCTAssertEqual(m.files["universal"]?.size, 123)
    }
}

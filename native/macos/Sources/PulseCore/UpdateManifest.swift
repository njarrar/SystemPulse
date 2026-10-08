import Foundation

/// `build/<platform>/latest.json`, written by tools/update_manifest.py:
/// `{"version":"1.3.0","files":{"universal":{"path":"...","sha256":"...","size":123}}}`
public struct UpdateManifest: Codable, Equatable, Sendable {
    public struct File: Codable, Equatable, Sendable {
        public var path: String
        public var sha256: String
        public var size: Int?
    }

    public var version: String
    public var files: [String: File]

    public static func decode(_ data: Data) throws -> UpdateManifest {
        try JSONDecoder().decode(UpdateManifest.self, from: data)
    }
}

public enum UpdateVersion {
    /// Numeric dotted parts: "1.10" -> [1, 10]. Text after the digits of a
    /// part is ignored ("2-beta" -> 2), missing parts count as 0.
    public static func parts(_ v: String) -> [Int] {
        v.trimmingCharacters(in: .whitespaces)
            .split(separator: ".", omittingEmptySubsequences: false)
            .map { Int($0.prefix(while: { $0.isASCII && $0.isNumber })) ?? 0 }
    }

    /// True when `a` is a later version than `b`.
    public static func isNewer(_ a: String, than b: String) -> Bool {
        let x = parts(a), y = parts(b)
        for i in 0..<max(x.count, y.count) {
            let p = i < x.count ? x[i] : 0, q = i < y.count ? y[i] : 0
            if p != q { return p > q }
        }
        return false
    }
}

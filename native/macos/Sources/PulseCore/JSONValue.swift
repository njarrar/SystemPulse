import Foundation

/// A small JSON tree with a deterministic writer, so generated catalogs are
/// byte-identical on macOS and Linux.
public indirect enum JSONValue: Equatable {
    case string(String)
    case number(Double)
    case bool(Bool)
    case object([String: JSONValue])
    case array([JSONValue])
    case null

    public static func parse(_ data: Data) throws -> JSONValue {
        try from(JSONSerialization.jsonObject(with: data, options: [.fragmentsAllowed]))
    }

    static func from(_ any: Any) -> JSONValue {
        switch any {
        case let s as String: return .string(s)
        case let d as [String: Any]: return .object(d.mapValues(from))
        case let a as [Any]: return .array(a.map(from))
        case let n as NSNumber:
            #if canImport(Darwin)
            if CFGetTypeID(n) == CFBooleanGetTypeID() { return .bool(n.boolValue) }
            #else
            if String(cString: n.objCType) == "c" { return .bool(n.boolValue) }
            #endif
            return .number(n.doubleValue)
        case let b as Bool: return .bool(b)
        default: return .null
        }
    }

    public subscript(key: String) -> JSONValue? {
        if case .object(let o) = self { return o[key] }
        return nil
    }

    public var string: String? { if case .string(let s) = self { return s }; return nil }
    public var object: [String: JSONValue]? { if case .object(let o) = self { return o }; return nil }

    /// Pretty JSON in the layout Xcode uses for .xcstrings: two-space indent,
    /// `"key" : value`, keys sorted.
    public func serialized() -> String {
        var out = ""
        write(&out, indent: 0)
        return out + "\n"
    }

    private func write(_ out: inout String, indent: Int) {
        let pad = String(repeating: "  ", count: indent)
        let pad2 = String(repeating: "  ", count: indent + 1)
        switch self {
        case .string(let s): out += JSONValue.quote(s)
        case .number(let d): out += NumberText.shortest(d)
        case .bool(let b): out += b ? "true" : "false"
        case .null: out += "null"
        case .array(let a):
            if a.isEmpty { out += "[]"; return }
            out += "[\n"
            for (k, v) in a.enumerated() {
                out += pad2
                v.write(&out, indent: indent + 1)
                out += k < a.count - 1 ? ",\n" : "\n"
            }
            out += pad + "]"
        case .object(let o):
            if o.isEmpty { out += "{\n\n" + pad + "}"; return }
            out += "{\n"
            let keys = o.keys.sorted(by: JSONValue.xcodeOrder)
            for (k, key) in keys.enumerated() {
                out += pad2 + JSONValue.quote(key) + " : "
                o[key]!.write(&out, indent: indent + 1)
                out += k < keys.count - 1 ? ",\n" : "\n"
            }
            out += pad + "}"
        }
    }

    /// Byte order of UTF-16 code units, close to what Xcode writes.
    static func xcodeOrder(_ a: String, _ b: String) -> Bool {
        Array(a.utf16).lexicographicallyPrecedes(Array(b.utf16))
    }

    static func quote(_ s: String) -> String {
        var out = "\""
        for u in s.unicodeScalars {
            switch u {
            case "\"": out += "\\\""
            case "\\": out += "\\\\"
            case "\n": out += "\\n"
            case "\r": out += "\\r"
            case "\t": out += "\\t"
            default:
                if u.value < 0x20 {
                    out += String(format: "\\u%04x", u.value)
                } else {
                    out.unicodeScalars.append(u)
                }
            }
        }
        return out + "\""
    }
}

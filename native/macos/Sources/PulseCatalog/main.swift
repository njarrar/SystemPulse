// pulse-catalog: writes Localizable.xcstrings from locales/*.json.
//
//   swift run pulse-catalog [--locales DIR] [--extra FILE]... [--out FILE] [--check] [--codes]
//
// Every *.json file in the locales folder with a "code" becomes a locale, so
// adding a language needs no code change. --check exits 1 when the catalog on
// disk is out of date instead of writing it.
import Foundation
import PulseCore

let here = URL(fileURLWithPath: #filePath)
let packageRoot = here.deletingLastPathComponent().deletingLastPathComponent().deletingLastPathComponent()
var localesDir = packageRoot.appendingPathComponent("../../locales").standardizedFileURL
var outFile = packageRoot.appendingPathComponent("Sources/PulseApp/Resources/Localizable.xcstrings")
var extras: [URL] = []
var check = false
var printCodes = false

var args = CommandLine.arguments.dropFirst().makeIterator()
while let a = args.next() {
    switch a {
    case "--locales": localesDir = URL(fileURLWithPath: args.next() ?? ".")
    case "--out": outFile = URL(fileURLWithPath: args.next() ?? "Localizable.xcstrings")
    case "--extra": extras.append(URL(fileURLWithPath: args.next() ?? ""))
    case "--check": check = true
    case "--codes": printCodes = true
    case "-h", "--help":
        print("usage: pulse-catalog [--locales DIR] [--extra FILE]... [--out FILE] [--check] [--codes]")
        exit(0)
    default:
        FileHandle.standardError.write(Data("unknown argument \(a)\n".utf8))
        exit(2)
    }
}

func fail(_ msg: String) -> Never {
    FileHandle.standardError.write(Data("pulse-catalog: \(msg)\n".utf8))
    exit(1)
}

var files: [URL] = []
do {
    files = try FileManager.default.contentsOfDirectory(at: localesDir, includingPropertiesForKeys: nil)
        .filter { $0.pathExtension == "json" && $0.lastPathComponent != "schema.json" }
        .sorted { $0.lastPathComponent < $1.lastPathComponent }
} catch {
    fail("cannot read \(localesDir.path): \(error)")
}
files += extras

var locales: [LocaleData] = []
for f in files {
    guard let data = try? Data(contentsOf: f) else { fail("cannot read \(f.path)") }
    guard let l = try? LocaleData.decode(data), !l.code.isEmpty else { continue }
    locales.append(l)
}
guard let en = locales.first(where: { $0.code == "en" }) else { fail("no en.json in \(localesDir.path)") }

if printCodes {
    // Locale codes for CFBundleLocalizations, one per line.
    locales.map { $0.code }.sorted().forEach { print($0) }
    exit(0)
}

var hadErrors = false
for l in locales {
    let r = LocaleValidator.validate(l, base: l.code == "en" ? nil : en)
    for e in r.errors { print("error   \(l.code): \(e)"); hadErrors = true }
    for w in r.warnings { print("warning \(l.code): \(w)") }
}
if hadErrors { fail("locale files have errors") }

let out: XCStrings.Output
do { out = try XCStrings.build(locales) } catch { fail("\(error)") }
for w in out.warnings { print("warning \(w)") }

// Read the catalog back and make sure nothing was lost on the way.
let roundTrip = (try? XCStrings.read(Data(out.text.utf8))) ?? []
let want = locales.map { $0.normalized() }.sorted { $0.code < $1.code }
let got = roundTrip.map { $0.normalized() }.sorted { $0.code < $1.code }
if out.warnings.isEmpty && want != got { fail("catalog does not read back to the same locale data") }

if check {
    let current = (try? String(contentsOf: outFile, encoding: .utf8)) ?? ""
    if current != out.text { fail("\(outFile.path) is out of date; run swift run pulse-catalog") }
    print("catalog up to date: \(locales.map { $0.code }.sorted().joined(separator: ", "))")
} else {
    do {
        try FileManager.default.createDirectory(at: outFile.deletingLastPathComponent(), withIntermediateDirectories: true)
        try out.text.write(to: outFile, atomically: true, encoding: .utf8)
    } catch { fail("cannot write \(outFile.path): \(error)") }
    print("wrote \(outFile.path) (\(locales.map { $0.code }.sorted().joined(separator: ", ")))")
}

import Foundation
import XCTest
@testable import PulseCore

/// Paths into the shared pulse/ folder (two levels above this package).
enum Paths {
    static let package = URL(fileURLWithPath: #filePath).deletingLastPathComponent().deletingLastPathComponent().deletingLastPathComponent()
    static let pulse = package.appendingPathComponent("../..").standardizedFileURL
    static let locales = pulse.appendingPathComponent("locales")
    static let vectors = pulse.appendingPathComponent("i18n/test/fixtures/vectors.json")
    static let pseudo = pulse.appendingPathComponent("i18n/test/fixtures/en-XA.json")
    static let catalog = package.appendingPathComponent("Sources/PulseApp/Resources/Localizable.xcstrings")

    static func locale(_ code: String) throws -> LocaleData {
        try LocaleData.decode(Data(contentsOf: locales.appendingPathComponent(code + ".json")))
    }

    static func fixture(_ name: String) throws -> Data {
        guard let url = Bundle.module.url(forResource: "Fixtures/" + name, withExtension: nil) else {
            throw NSError(domain: "fixture", code: 1, userInfo: [NSLocalizedDescriptionKey: "missing fixture \(name)"])
        }
        return try Data(contentsOf: url)
    }
}

let FSI = "\u{2068}", PDI = "\u{2069}"

final class I18nTests: XCTestCase {
    var registry: LocaleRegistry!

    override func setUpWithError() throws {
        registry = LocaleRegistry([try Paths.locale("en"), try Paths.locale("ar")])
    }

    // MARK: shared vectors (i18n/test/fixtures/vectors.json)

    func checkVectors(_ reg: LocaleRegistry, file: StaticString = #filePath, line: UInt = #line) throws {
        let root = try JSONValue.parse(Data(contentsOf: Paths.vectors))
        guard case .object(let langs) = root else { return XCTFail("bad vectors file") }
        var checked = 0
        for (code, body) in langs {
            let lc = try reg.get(code)
            XCTAssertEqual(lc.code, code)
            guard case .array(let cats)? = body["categories"] else { return XCTFail("no categories for \(code)") }
            for pair in cats {
                guard case .array(let p) = pair, p.count == 2, let v = p[0].string, let want = p[1].string else { continue }
                XCTAssertEqual(lc.selector.select(v).rawValue, want, "\(code) \(v)", file: file, line: line)
                checked += 1
            }
            for (spec, want) in body["samples"]?.object ?? [:] {
                let parts = spec.split(separator: ":", maxSplits: 1).map(String.init)
                let key = parts[0], arg = parts[1]
                let got: String
                if let n = Double(arg) {
                    got = lc.plural(key, n)
                } else {
                    var params: [String: Arg] = [:]
                    for kv in arg.split(separator: ",") {
                        let x = kv.split(separator: "=", maxSplits: 1).map(String.init)
                        params[x[0]] = .text(x[1])
                    }
                    got = lc.t(key, params)
                }
                XCTAssertEqual(got, want.string, "\(code) \(spec)", file: file, line: line)
                checked += 1
            }
        }
        XCTAssertGreaterThan(checked, 400)
    }

    func testVectorsFromLocaleFiles() throws {
        try checkVectors(registry)
    }

    func testVectorsFromGeneratedCatalog() throws {
        let data = try Data(contentsOf: Paths.catalog)
        let reg = LocaleRegistry(try XCStrings.read(data))
        XCTAssertEqual(reg.codes, ["en", "ar"])
        try checkVectors(reg)
    }

    // MARK: plural engine port

    func testPluralRulesMatchIntlForTenLanguages() throws {
        let root = try JSONValue.parse(try Paths.fixture("cldr-intl.json"))
        let letters: [Character: String] = ["z": "zero", "o": "one", "t": "two", "f": "few", "m": "many", "x": "other"]
        var count = 0
        for (code, row) in root["languages"]?.object ?? [:] {
            var rules: [String: String] = [:]
            for (k, v) in row["rules"]?.object ?? [:] { rules[k] = v.string }
            let sel = try PluralSelector(rules: rules)
            for d in 0...2 {
                let expected = Array(row["d\(d)"]?.string ?? "")
                let step = d == 0 ? 1.0 : 0.01
                for (k, letter) in expected.enumerated() {
                    let x = Double(NumberText.fixed(Double(k) * step, d))!
                    let got = sel.select(x, decimals: d).rawValue
                    if got != letters[letter]! {
                        XCTFail("\(code) \(NumberText.fixed(x, d)): got \(got), want \(letters[letter]!)")
                        return
                    }
                    count += 1
                }
            }
        }
        XCTAssertEqual(count, 10 * (1201 + 3001 + 3001))
    }

    func testOperands() {
        XCTAssertEqual(PluralOperands("1.50"), PluralOperands(n: 1.5, i: 1, v: 2, w: 1, f: 50, t: 5, c: 0, e: 0))
        XCTAssertEqual(PluralOperands(7), PluralOperands(n: 7, i: 7, v: 0, w: 0, f: 0, t: 0, c: 0, e: 0))
        XCTAssertEqual(PluralOperands("1.2c3").i, 1200)
        XCTAssertEqual(PluralOperands(1, decimals: 1).v, 1)
    }

    func testBadRulesThrow() {
        XCTAssertThrowsError(try PluralRule("n > 3"))
        XCTAssertThrowsError(try PluralRule("q = 1"))
        XCTAssertNoThrow(try PluralRule("n = 1 @integer 1 @decimal 1.0, 1.00"))
    }

    func testArabicPicksAllSixForms() throws {
        let L = try registry.get("ar")
        let want: [Double: String] = [0: "zero", 1: "one", 2: "two", 3: "few", 10: "few", 11: "many", 99: "many", 100: "other",
                                      101: "other", 102: "other", 103: "few", 111: "many", 1000: "other"]
        for (n, cat) in want { XCTAssertEqual(L.selector.select(n).rawValue, cat, "ar \(n)") }
        XCTAssertEqual(Bidi.strip(L.plural("process", 1)), "عملية واحدة")
        XCTAssertEqual(Bidi.strip(L.plural("process", 2)), "عمليتان")
        XCTAssertEqual(Bidi.strip(L.plural("process", 9)), "9 عمليات")
        XCTAssertEqual(Bidi.strip(L.plural("thread", 84)), "84 خيطاً")
        XCTAssertEqual(Bidi.strip(L.plural("thread", 103)), "103 خيوط")
        XCTAssertEqual(Bidi.strip(L.plural("ended", 0)), "لم يتم إنهاء أي تطبيق بعد")
    }

    func testEnglishExactZeroWins() throws {
        let L = try registry.get("en")
        XCTAssertEqual(L.plural("ended", 0), "No apps ended yet")
        XCTAssertEqual(L.plural("ended", 1), "1 app ended this session")
        XCTAssertEqual(L.plural("ended", 3), "3 apps ended this session")
        XCTAssertEqual(L.plural("thread", 1), "1 thread")
    }

    func testRTLIsolatesEveryValue() throws {
        let L = try registry.get("ar")
        XCTAssertEqual(L.t("hogTitle", ["app": "Xcode", "pct": "52%"]), "\(FSI)Xcode\(PDI) يستهلك \(FSI)52%\(PDI) من المعالج")
        XCTAssertEqual(L.t("tempLine", ["cpu": "52°C", "gpu": "31°C"]), "المعالج \(FSI)52°C\(PDI) · الرسوميات \(FSI)31°C\(PDI)")
    }

    func testLTRIsolatesOnlyRTLValues() throws {
        let L = try registry.get("en")
        XCTAssertEqual(L.t("hogTitle", ["app": "Xcode", "pct": "52%"]), "Xcode is using 52% of total CPU")
        XCTAssertEqual(L.t("endNamed", ["app": "خادم النوافذ"]), "End \(FSI)خادم النوافذ\(PDI)")
    }

    func testFallbackToEnglishThenKey() throws {
        registry.register(LocaleData(code: "xx", dir: "ltr", strings: ["cpu": "XPU"]))
        let L = try registry.get("xx")
        var missing: [String] = []
        L.onMissing = { missing.append($0) }
        XCTAssertEqual(L.t("cpu"), "XPU")
        XCTAssertEqual(L.t("quit"), "Quit")
        XCTAssertEqual(L.plural("process", 3), "3 processes")
        XCTAssertEqual(L.t("noSuchKey"), "noSuchKey")
        XCTAssertEqual(L.t("noSuchKey"), "noSuchKey")
        XCTAssertEqual(missing, ["[i18n] missing strings.noSuchKey in xx"])
    }

    func testCodesResolveByPrefix() {
        XCTAssertEqual(registry.resolve("ar-EG"), "ar")
        XCTAssertEqual(registry.resolve("en_GB"), "en")
        XCTAssertEqual(registry.resolve("zz"), "en")
        XCTAssertEqual(registry.resolve(nil), "en")
    }

    func testNumberingSystems() throws {
        registry.register(LocaleData(code: "fa", dir: "rtl", numberingSystem: "arabext", strings: [:]))
        XCTAssertEqual(try registry.get("fa").num(54.25, 1), "۵۴٫۳")
        XCTAssertEqual(try registry.get("ar").num(54.25, 1), "54.3")
        XCTAssertEqual(try registry.get("en").grouped(1180), "1,180")
    }

    func testJoinIsolatesInRTL() throws {
        XCTAssertEqual(try registry.get("ar").join(["a", "b"]), "\(FSI)a\(PDI) · \(FSI)b\(PDI)")
        XCTAssertEqual(try registry.get("en").join(["a", "", "b"]), "a · b")
    }

    func testValidatorCatchesBadFiles() throws {
        let bad = LocaleData(code: "zz", dir: "up", pluralRules: ["one": "n = 1", "lots": "n > 3"],
                             plurals: ["process": ["one": "x"]], strings: ["calm": "{oops}"])
        let text = LocaleValidator.validate(bad, base: try Paths.locale("en")).errors.joined(separator: "\n")
        XCTAssertTrue(text.contains("\"dir\""))
        XCTAssertTrue(text.contains("bad category \"lots\""))
        XCTAssertTrue(text.contains("needs an \"other\" form"))
        XCTAssertTrue(text.contains("unknown placeholder {oops}"))
    }

    func testShippedLocalesAreClean() throws {
        let en = try Paths.locale("en"), ar = try Paths.locale("ar")
        XCTAssertEqual(LocaleValidator.validate(en, base: nil).errors, [])
        let r = LocaleValidator.validate(ar, base: en)
        XCTAssertEqual(r.errors, [])
        XCTAssertEqual(r.warnings, [])
    }

    func testEveryKeyThePrototypeUsesExists() throws {
        let en = try Paths.locale("en")
        for key in ["calm", "hogPill", "pillValue", "tempLine", "memOf", "health", "openMonitor", "privacy", "quit", "last10", "peakAvg"] {
            XCTAssertNotNil(en.strings?[key], key)
        }
    }

    // MARK: String Catalog

    func testCatalogRoundTripsLosslessly() throws {
        let files = [try Paths.locale("en"), try Paths.locale("ar"), try LocaleData.decode(Data(contentsOf: Paths.pseudo))]
        let out = try XCStrings.build(files)
        XCTAssertEqual(out.warnings, [])
        let back = try XCStrings.read(Data(out.text.utf8))
        XCTAssertEqual(back.map { $0.normalized() }, files.map { $0.normalized() }.sorted { $0.code < $1.code })
    }

    func testCommittedCatalogIsCurrent() throws {
        let out = try XCStrings.build([try Paths.locale("en"), try Paths.locale("ar")])
        let disk = try String(contentsOf: Paths.catalog, encoding: .utf8)
        XCTAssertEqual(disk, out.text, "run: swift run pulse-catalog")
    }

    func testCatalogUsesAppleSpecifiersAndPluralVariations() throws {
        let out = try XCStrings.build([try Paths.locale("en"), try Paths.locale("ar")])
        let hog = out.json["strings"]?["strings.hogTitle"]
        XCTAssertEqual(hog?["localizations"]?["en"]?["stringUnit"]?["value"]?.string, "%1$@ is using %2$@ of total CPU")
        XCTAssertEqual(hog?["comment"]?.string, "Arguments: 1=app 2=pct")
        let ended = out.json["strings"]?["plurals.ended"]?["localizations"]
        XCTAssertEqual(ended?["en"]?["variations"]?["plural"]?["zero"]?["stringUnit"]?["value"]?.string, "No apps ended yet")
        XCTAssertEqual(ended?["en"]?["variations"]?["plural"]?["other"]?["stringUnit"]?["value"]?.string, "%1$lld apps ended this session")
        XCTAssertEqual(ended?["ar"]?["variations"]?["plural"]?.object?.count, 6)
        XCTAssertEqual(XCStrings.toApple("100% of {x}", args: ["x"], numeric: []), "100%% of %1$@")
        XCTAssertEqual(XCStrings.fromApple("100%% of %1$@", args: ["x"]), "100% of {x}")
    }

    func testAddingALocaleFileNeedsNoCode() throws {
        // A third file (the 40% longer pseudo-locale) flows through the catalog,
        // the registry and the switcher list with no code change.
        let files = [try Paths.locale("en"), try Paths.locale("ar"), try LocaleData.decode(Data(contentsOf: Paths.pseudo))]
        let reg = LocaleRegistry(try XCStrings.read(Data(try XCStrings.build(files).text.utf8)))
        XCTAssertEqual(reg.list().map { $0.code }, ["en", "ar", "en-XA"])
        let xa = try reg.get("en-XA")
        XCTAssertTrue(xa.t("calm").hasPrefix("[Sýštém Cálm ẋ"))
        XCTAssertEqual(reg.resolve("en-XA"), "en-XA")
        XCTAssertTrue(xa.plural("ended", 0).hasPrefix("[Nó áppš"))
    }
}

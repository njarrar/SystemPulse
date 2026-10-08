#if canImport(AppKit)
import AppKit
import CryptoKit
import PulseCore

/// Updates Pulse from the repo's `build/macos` folder. Pulse only goes online
/// when someone picks Check for Updates (or runs `Pulse --update-check`).
///
/// Steps: fetch `build/macos/latest.json`, compare its version with ours,
/// download the zip it names, check its SHA-256, unzip it with ditto, then a
/// small shell script waits for Pulse to quit, swaps the bundle and opens it.
@MainActor
final class Updater {
    enum UpdateError: LocalizedError {
        case message(String)
        var errorDescription: String? { if case .message(let s) = self { return s }; return nil }
    }

    /// `PULSE_UPDATE_URL` (for tests and mirrors) or the repo's main branch.
    nonisolated static var baseURL: URL {
        var s = ProcessInfo.processInfo.environment["PULSE_UPDATE_URL"] ?? ""
        if s.isEmpty { s = "https://raw.githubusercontent.com/njarrar/SystemPulse/main/" }
        if !s.hasSuffix("/") { s += "/" }
        return URL(string: s)!
    }

    /// CFBundleShortVersionString; "0" when run outside a bundle (`swift run`).
    nonisolated static var currentVersion: String {
        Bundle.main.object(forInfoDictionaryKey: "CFBundleShortVersionString") as? String ?? "0"
    }

    private let store: PulseStore
    private var busy = false
    /// Called with the "updating" text while a download runs, nil when done.
    var onProgress: ((String?) -> Void)?

    init(store: PulseStore) {
        self.store = store
    }

    nonisolated static func fetchManifest() async throws -> UpdateManifest {
        let url = baseURL.appendingPathComponent("build/macos/latest.json")
        let data = try await fetch(url)
        do { return try UpdateManifest.decode(data) } catch {
            throw UpdateError.message("latest.json: \(error.localizedDescription)")
        }
    }

    nonisolated private static func fetch(_ url: URL) async throws -> Data {
        var req = URLRequest(url: url)
        req.cachePolicy = .reloadIgnoringLocalCacheData
        let (data, response) = try await URLSession.shared.data(for: req)
        if let http = response as? HTTPURLResponse, !(200..<300).contains(http.statusCode) {
            throw UpdateError.message("HTTP \(http.statusCode) for \(url.absoluteString)")
        }
        return data
    }

    // MARK: command line

    /// `Pulse --update-check`: prints "current=<v> latest=<v>" and exits.
    static func commandLineCheck() {
        Task {
            do {
                let m = try await fetchManifest()
                print("current=\(currentVersion) latest=\(m.version)")
                exit(0)
            } catch {
                FileHandle.standardError.write(Data("update check failed: \(error.localizedDescription)\n".utf8))
                exit(1)
            }
        }
    }

    // MARK: menu

    func check() {
        guard !busy else { return }
        busy = true
        Task { @MainActor in
            defer { busy = false; onProgress?(nil) }
            let current = Updater.currentVersion
            do {
                let m = try await Updater.fetchManifest()
                guard UpdateVersion.isNewer(m.version, than: current) else {
                    alert(store.t("updateNone", ["current": .text(current)]))
                    return
                }
                let install = alert(store.t("updateAvailable", ["version": .text(m.version), "current": .text(current)]),
                                    buttons: [store.t("updateInstall"), store.t("updateLater")])
                guard install == .alertFirstButtonReturn else { return }
                if !canReplaceBundle() {
                    alert(store.t("updateMove"))
                    return
                }
                onProgress?(store.t("updating", ["version": .text(m.version)]))
                let app = try await download(m)
                try relaunch(into: app)
            } catch {
                alert(store.t("updateFailed", ["error": .text(error.localizedDescription)]))
            }
        }
    }

    @discardableResult
    private func alert(_ text: String, buttons: [String] = []) -> NSApplication.ModalResponse {
        let a = NSAlert()
        a.messageText = "Pulse"
        a.informativeText = text
        a.icon = NSApp.applicationIconImage
        for b in buttons { a.addButton(withTitle: b) }
        // A menu bar app is never frontmost on its own; bring the alert forward.
        NSApp.activate(ignoringOtherApps: true)
        return a.runModal()
    }

    /// False when macOS runs Pulse from a read-only translocated copy, or the
    /// folder it sits in cannot be written.
    private func canReplaceBundle() -> Bool {
        let path = Bundle.main.bundlePath
        if path.contains("/AppTranslocation/") { return false }
        let parent = (path as NSString).deletingLastPathComponent
        return FileManager.default.isWritableFile(atPath: parent)
    }

    // MARK: install

    /// Downloads and checks the zip, unzips it and returns the new Pulse.app.
    private func download(_ m: UpdateManifest) async throws -> URL {
        guard let file = m.files["universal"] else { throw UpdateError.message("latest.json has no universal build") }
        let data = try await Updater.fetch(Updater.baseURL.appendingPathComponent(file.path))
        let digest = SHA256.hash(data: data).map { String(format: "%02x", $0) }.joined()
        guard digest == file.sha256.lowercased() else { throw UpdateError.message("SHA-256 mismatch") }

        let fm = FileManager.default
        let dir = fm.temporaryDirectory.appendingPathComponent("pulse-update-\(UUID().uuidString)")
        try fm.createDirectory(at: dir, withIntermediateDirectories: true)
        let zip = dir.appendingPathComponent("Pulse.zip")
        try data.write(to: zip)
        let out = dir.appendingPathComponent("app")
        let status = try await Updater.run("/usr/bin/ditto", ["-x", "-k", zip.path, out.path])
        guard status == 0 else { throw UpdateError.message("ditto exited with \(status)") }

        // The zip holds Pulse.app at its top level; allow one folder above it too.
        let tops = (try? fm.contentsOfDirectory(at: out, includingPropertiesForKeys: nil)) ?? []
        for top in [out] + tops {
            let app = top.appendingPathComponent("Pulse.app")
            if fm.fileExists(atPath: app.appendingPathComponent("Contents/MacOS/Pulse").path) { return app }
        }
        throw UpdateError.message("Pulse.app not found in the download")
    }

    nonisolated private static func run(_ tool: String, _ args: [String]) async throws -> Int32 {
        try await withCheckedThrowingContinuation { cont in
            let p = Process()
            p.executableURL = URL(fileURLWithPath: tool)
            p.arguments = args
            p.terminationHandler = { cont.resume(returning: $0.terminationStatus) }
            do { try p.run() } catch { cont.resume(throwing: error) }
        }
    }

    /// Starts a detached script that swaps the bundle once Pulse has quit,
    /// then quits Pulse.
    private func relaunch(into app: URL) throws {
        let script = """
        #!/bin/sh
        # Waits for Pulse to quit, puts the new bundle in place and opens it.
        pid="$1"; old="$2"; new="$3"
        while kill -0 "$pid" 2>/dev/null; do sleep 0.2; done
        aside="$old.old-$$"
        mv "$old" "$aside" || exit 1
        if mv "$new" "$old"; then rm -rf "$aside"; else mv "$aside" "$old"; fi
        xattr -dr com.apple.quarantine "$old" 2>/dev/null || true
        /usr/bin/open "$old"
        """
        let url = app.deletingLastPathComponent().deletingLastPathComponent().appendingPathComponent("install.sh")
        try script.write(to: url, atomically: true, encoding: .utf8)
        let p = Process()
        p.executableURL = URL(fileURLWithPath: "/bin/sh")
        p.arguments = ["-c", "nohup /bin/sh \"$0\" \"$1\" \"$2\" \"$3\" >/dev/null 2>&1 &",
                       url.path, String(ProcessInfo.processInfo.processIdentifier), Bundle.main.bundlePath, app.path]
        try p.run()
        p.waitUntilExit()
        NSApp.terminate(nil)
    }
}
#endif

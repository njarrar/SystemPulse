#if canImport(AppKit)
import AppKit

@MainActor
final class AppDelegate: NSObject, NSApplicationDelegate {
    private var store: PulseStore!
    private var flyout: FlyoutController!
    private var status: StatusItemController!

    func applicationDidFinishLaunching(_ notification: Notification) {
        let args = CommandLine.arguments
        if args.contains("--probe") {
            DevTools.probe()
            return
        }
        // `--update-check` prints "current=<v> latest=<v>" and exits (no UI).
        if args.contains("--update-check") {
            Updater.commandLineCheck()
            return
        }
        if let i = args.firstIndex(of: "--render"), i + 1 < args.count {
            store = PulseStore()
            DevTools.render(store: store, to: URL(fileURLWithPath: args[i + 1]))
            return
        }
        store = PulseStore()
        flyout = FlyoutController(store: store)
        status = StatusItemController(store: store, flyout: flyout)
        // `--open` shows the flyout at launch (used for screenshots and checks).
        if args.contains("--open"), let button = FlyoutController.statusButton {
            Task { @MainActor [weak self] in
                try? await Task.sleep(nanoseconds: 1_600_000_000)
                self?.flyout.open(from: button)
            }
        }
    }

    func applicationShouldTerminateAfterLastWindowClosed(_ sender: NSApplication) -> Bool { false }
}
#endif

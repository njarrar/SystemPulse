#if canImport(AppKit)
import AppKit
#endif

@main
enum PulseMain {
    @MainActor static func main() {
        #if canImport(AppKit)
        let app = NSApplication.shared
        let delegate = AppDelegate()
        app.delegate = delegate
        // A menu bar app: no Dock icon, no main menu (LSUIElement in Info.plist too).
        app.setActivationPolicy(.accessory)
        withExtendedLifetime(delegate) { app.run() }
        #else
        print("Pulse is a macOS menu bar app. On this platform only the PulseCore library and its tests build.")
        #endif
    }
}

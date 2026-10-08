#if canImport(AppKit)
import AppKit
import Combine

/// Tier 1: the menu bar readouts. Clicking them opens the flyout; a right
/// click or control-click shows a small menu (Open, Check for Updates, Quit).
@MainActor
final class StatusItemController: NSObject {
    private let item = NSStatusBar.system.statusItem(withLength: NSStatusItem.variableLength)
    private let store: PulseStore
    private let flyout: FlyoutController
    private let updater: Updater
    private var bag = Set<AnyCancellable>()

    init(store: PulseStore, flyout: FlyoutController) {
        self.store = store
        self.flyout = flyout
        self.updater = Updater(store: store)
        super.init()
        if let button = item.button {
            button.target = self
            button.action = #selector(clicked(_:))
            button.sendAction(on: [.leftMouseUp, .rightMouseUp])
            button.imagePosition = .imageLeading
            let image = NSImage(systemSymbolName: "waveform.path.ecg", accessibilityDescription: "Pulse")
            image?.isTemplate = true
            button.image = image
            FlyoutController.statusButton = button
        }
        updater.onProgress = { [weak self] text in self?.item.button?.toolTip = text }
        store.$snapshot.sink { [weak self] _ in Task { @MainActor [weak self] in self?.render() } }.store(in: &bag)
        store.$localeCode.sink { [weak self] _ in Task { @MainActor [weak self] in self?.render() } }.store(in: &bag)
        render()
    }

    @objc private func clicked(_ sender: NSStatusBarButton) {
        if let e = NSApp.currentEvent,
           e.type == .rightMouseUp || e.type == .rightMouseDown || e.modifierFlags.contains(.control) {
            showMenu(sender)
            return
        }
        flyout.toggle(from: sender)
    }

    private func showMenu(_ button: NSStatusBarButton) {
        if flyout.isOpen { flyout.close() }
        let menu = NSMenu()
        menu.userInterfaceLayoutDirection = store.lc.isRTL ? .rightToLeft : .leftToRight
        func add(_ key: String, _ action: Selector) {
            let entry = NSMenuItem(title: store.t(key), action: action, keyEquivalent: "")
            entry.target = self
            menu.addItem(entry)
        }
        add("openPulse", #selector(openFlyout))
        menu.addItem(.separator())
        add("checkUpdates", #selector(checkForUpdates))
        menu.addItem(.separator())
        add("quit", #selector(quit))
        // Attach the menu only for this click so a left click still opens the flyout.
        item.menu = menu
        button.performClick(nil)
        item.menu = nil
    }

    @objc private func openFlyout() {
        guard let button = item.button else { return }
        flyout.open(from: button)
    }

    @objc private func checkForUpdates() { updater.check() }

    @objc private func quit() { NSApp.terminate(nil) }

    private func render() {
        guard let button = item.button else { return }
        let font = NSFont.monospacedDigitSystemFont(ofSize: 12, weight: .medium)
        let para = NSMutableParagraphStyle()
        para.baseWritingDirection = store.lc.isRTL ? .rightToLeft : .leftToRight
        let out = NSMutableAttributedString()
        let alertColor = NSColor.systemOrange
        for (k, part) in store.statusParts().enumerated() {
            if k > 0 { out.append(NSAttributedString(string: "  ", attributes: [.font: font])) }
            var attrs: [NSAttributedString.Key: Any] = [.font: font, .paragraphStyle: para]
            if part.alert { attrs[.foregroundColor] = alertColor }
            // Each readout is an isolated left-to-right run, so "CPU 64%" never reorders.
            out.append(NSAttributedString(string: "\u{2066}" + part.text + "\u{2069}", attributes: attrs))
        }
        out.addAttribute(.baselineOffset, value: 0.5, range: NSRange(location: 0, length: out.length))
        button.attributedTitle = out
        button.setAccessibilityLabel(store.statusParts().map { $0.text }.joined(separator: ", "))
    }
}
#endif

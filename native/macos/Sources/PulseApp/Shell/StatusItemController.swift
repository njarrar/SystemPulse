#if canImport(AppKit)
import AppKit
import Combine

/// Tier 1: the menu bar readouts. Clicking them opens the flyout.
@MainActor
final class StatusItemController: NSObject {
    private let item = NSStatusBar.system.statusItem(withLength: NSStatusItem.variableLength)
    private let store: PulseStore
    private let flyout: FlyoutController
    private var bag = Set<AnyCancellable>()

    init(store: PulseStore, flyout: FlyoutController) {
        self.store = store
        self.flyout = flyout
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
        store.$snapshot.sink { [weak self] _ in Task { @MainActor [weak self] in self?.render() } }.store(in: &bag)
        store.$localeCode.sink { [weak self] _ in Task { @MainActor [weak self] in self?.render() } }.store(in: &bag)
        render()
    }

    @objc private func clicked(_ sender: NSStatusBarButton) {
        flyout.toggle(from: sender)
    }

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

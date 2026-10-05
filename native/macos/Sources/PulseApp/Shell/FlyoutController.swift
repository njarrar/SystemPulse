#if canImport(AppKit)
import AppKit
import Combine
import SwiftUI

/// Borderless, non-activating panel: it takes keys (Escape, Tab, Return)
/// without pulling the user out of the app they are in.
final class FlyoutPanel: NSPanel {
    var onCancel: (@MainActor () -> Void)?

    init() {
        super.init(contentRect: NSRect(x: 0, y: 0, width: 420, height: 600),
                   styleMask: [.borderless, .nonactivatingPanel],
                   backing: .buffered, defer: true)
        isFloatingPanel = true
        level = .statusBar
        hidesOnDeactivate = false
        becomesKeyOnlyIfNeeded = false
        isOpaque = false
        backgroundColor = .clear
        hasShadow = true
        isMovable = false
        collectionBehavior = [.canJoinAllSpaces, .fullScreenAuxiliary, .transient]
        animationBehavior = .utilityWindow
    }

    override var canBecomeKey: Bool { true }
    override var canBecomeMain: Bool { false }

    override func cancelOperation(_ sender: Any?) { onCancel?() }
}

/// Owns the panel, its vibrancy, placement under the status item and the
/// close-on-outside-click behaviour.
@MainActor
final class FlyoutController {
    static weak var shared: FlyoutController?

    private let store: PulseStore
    private let panel = FlyoutPanel()
    private let effect = NSVisualEffectView()
    private var outsideMonitor: Any?
    private var bag = Set<AnyCancellable>()
    private weak var anchor: NSStatusBarButton?

    var isOpen: Bool { panel.isVisible }

    init(store: PulseStore) {
        self.store = store
        effect.material = .popover
        effect.blendingMode = .behindWindow
        effect.state = .active
        effect.maskImage = FlyoutController.roundedMask(radius: 22)
        effect.autoresizingMask = [.width, .height]

        let host = NSHostingView(rootView: FlyoutRoot(store: store))
        host.autoresizingMask = [.width, .height]
        host.wantsLayer = true
        host.layer?.cornerRadius = 22
        host.layer?.masksToBounds = true

        let container = NSView(frame: panel.contentRect(forFrameRect: panel.frame))
        effect.frame = container.bounds
        host.frame = container.bounds
        container.addSubview(effect)
        container.addSubview(host)
        panel.contentView = container
        panel.onCancel = { [weak self] in self?.handleCancel() }

        store.$contentHeight.removeDuplicates().sink { [weak self] _ in
            Task { @MainActor [weak self] in self?.reposition() }
        }.store(in: &bag)
        store.$themeOverride.sink { [weak self] t in Task { @MainActor [weak self] in self?.applyTheme(t) } }.store(in: &bag)
        store.$localeCode.sink { [weak self] _ in Task { @MainActor [weak self] in self?.reposition() } }.store(in: &bag)
        FlyoutController.shared = self
    }

    static func roundedMask(radius: CGFloat) -> NSImage {
        let edge = 2 * radius + 1
        let image = NSImage(size: NSSize(width: edge, height: edge), flipped: false) { rect in
            NSColor.black.setFill()
            NSBezierPath(roundedRect: rect, xRadius: radius, yRadius: radius).fill()
            return true
        }
        image.capInsets = NSEdgeInsets(top: radius, left: radius, bottom: radius, right: radius)
        image.resizingMode = .stretch
        return image
    }

    private func applyTheme(_ t: String?) {
        let appearance: NSAppearance? = t == "dark" ? NSAppearance(named: .darkAqua) : t == "light" ? NSAppearance(named: .aqua) : nil
        panel.appearance = appearance
    }

    private func handleCancel() {
        if store.confirm != nil { store.confirm = nil }
        else if store.route != .overview { store.back() }
        else { close() }
    }

    func toggle(from button: NSStatusBarButton) {
        isOpen ? close() : open(from: button)
    }

    func open(from button: NSStatusBarButton) {
        anchor = button
        reposition()
        panel.alphaValue = 0
        panel.makeKeyAndOrderFront(nil)
        NSAnimationContext.runAnimationGroup { ctx in
            ctx.duration = 0.16
            panel.animator().alphaValue = 1
        }
        button.highlight(true)
        outsideMonitor = NSEvent.addGlobalMonitorForEvents(matching: [.leftMouseDown, .rightMouseDown]) { [weak self] _ in
            Task { @MainActor [weak self] in self?.close() }
        }
    }

    func close() {
        guard isOpen else { return }
        if let m = outsideMonitor { NSEvent.removeMonitor(m) }
        outsideMonitor = nil
        anchor?.highlight(false)
        store.hoveredApp = nil
        store.confirm = nil
        NSAnimationContext.runAnimationGroup({ ctx in
            ctx.duration = 0.12
            panel.animator().alphaValue = 0
        }, completionHandler: { [weak self] in
            Task { @MainActor [weak self] in
                self?.panel.orderOut(nil)
                self?.panel.alphaValue = 1
                self?.store.route = .overview
            }
        })
    }

    /// Places the panel under the status item: right-aligned to it in LTR,
    /// left-aligned in RTL, kept on screen, as tall as its content allows.
    func reposition() {
        guard let button = anchor ?? FlyoutController.statusButton, let window = button.window else { return }
        let b = window.convertToScreen(button.convert(button.bounds, to: nil))
        let screen = window.screen ?? NSScreen.main
        let visible = screen?.visibleFrame ?? NSRect(x: 0, y: 0, width: 1440, height: 900)
        let width: CGFloat = 420
        let gap: CGFloat = 6
        let maxHeight = visible.height - gap * 2
        let height = min(max(200, store.contentHeight), maxHeight)
        var x = store.lc.isRTL ? b.minX : b.maxX - width
        x = min(max(visible.minX + 8, x), visible.maxX - width - 8)
        let top = min(b.minY - gap, visible.maxY)
        let frame = NSRect(x: x, y: top - height, width: width, height: height)
        panel.setFrame(frame, display: true, animate: false)
    }

    static weak var statusButton: NSStatusBarButton?
}
#endif

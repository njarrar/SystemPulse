// swift-tools-version:5.10
import PackageDescription

let package = Package(
    name: "Pulse",
    defaultLocalization: "en",
    platforms: [.macOS(.v13)],
    products: [
        .library(name: "PulseCore", targets: ["PulseCore"]),
        .executable(name: "Pulse", targets: ["PulseApp"]),
        .executable(name: "pulse-catalog", targets: ["PulseCatalog"]),
    ],
    targets: [
        // Platform-neutral core: i18n engine, CLDR plurals, formatting,
        // telemetry models, process grouping, history ring buffers.
        // Builds and tests on Linux and macOS.
        .target(name: "PulseCore"),

        // C shim for the SMC and the IOHID temperature sensors. Only built on macOS.
        .target(
            name: "CPulseSensors",
            linkerSettings: [
                .linkedFramework("IOKit", .when(platforms: [.macOS])),
                .linkedFramework("CoreFoundation", .when(platforms: [.macOS])),
            ]
        ),

        // The menu bar app. AppKit, SwiftUI and IOKit code sits behind
        // `#if canImport(AppKit)` so the target still builds as a stub on Linux.
        .executableTarget(
            name: "PulseApp",
            dependencies: [
                "PulseCore",
                .target(name: "CPulseSensors", condition: .when(platforms: [.macOS])),
            ],
            resources: [.copy("Resources/Localizable.xcstrings")],
            linkerSettings: [
                .linkedFramework("IOKit", .when(platforms: [.macOS])),
                .linkedFramework("CoreWLAN", .when(platforms: [.macOS])),
            ]
        ),

        // Build-time generator: locales/*.json -> Localizable.xcstrings.
        .executableTarget(name: "PulseCatalog", dependencies: ["PulseCore"]),

        .testTarget(
            name: "PulseCoreTests",
            dependencies: ["PulseCore"],
            resources: [.copy("Fixtures")]
        ),
    ]
)

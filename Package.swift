// swift-tools-version:5.9
//
// SwiftPM package for embedding the exporter in an iOS (or macOS) app. It builds
// the C++ core + SQLite reader as one target and exposes the pure-C bridge
// (include/imsg/imsg_bridge.h) as the module `ImsgExporter`, importable from
// Swift. See docs/IOS.md. Building for iOS requires a Mac with Xcode.
import PackageDescription

let package = Package(
    name: "ImsgExporter",
    platforms: [
        .iOS(.v15),
        .macOS(.v12),
    ],
    products: [
        .library(name: "ImsgExporter", targets: ["ImsgExporter"]),
    ],
    targets: [
        .target(
            name: "ImsgExporter",
            path: ".",
            sources: [
                "src/models.cpp",
                "src/time_util.cpp",
                "src/attributed_body.cpp",
                "src/exporters.cpp",
                "src/database.cpp",
                "src/export_job.cpp",
                "src/imsg_bridge.cpp",
            ],
            publicHeadersPath: "include",
            cxxSettings: [
                .headerSearchPath("include"),
            ],
            linkerSettings: [
                // iOS and macOS ship the SQLite client library.
                .linkedLibrary("sqlite3"),
            ]
        ),
    ],
    cxxLanguageStandard: .cxx17
)

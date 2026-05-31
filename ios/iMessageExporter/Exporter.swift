// Swift wrapper around the pure-C export bridge (include/imsg/imsg_bridge.h),
// exposed by the local SwiftPM package as the module `ImsgExporter`.
import Foundation
import ImsgExporter

enum ExportFormat: String, CaseIterable, Identifiable {
    case txt, json, html
    var id: String { rawValue }
    var label: String { rawValue.uppercased() }
}

enum LogLevel: Int32, CaseIterable, Identifiable {
    case error = 0, warn = 1, info = 2, debug = 3
    var id: Int32 { rawValue }
    var label: String { ["error", "warn", "info", "debug"][Int(rawValue)] }
}

struct ExportError: LocalizedError {
    let message: String
    var errorDescription: String? { message }
}

enum Exporter {
    /// Library version, e.g. "0.1.0".
    static var version: String { String(cString: imsg_version()) }

    static func setLogLevel(_ level: LogLevel) { imsg_set_log_level(level.rawValue) }

    /// Runs the export on `dbPath`, writing one file per conversation into
    /// `outDir`. Returns the number of conversations written, or throws with the
    /// engine's error message. Synchronous — call off the main actor.
    static func export(dbPath: URL, outDir: URL, format: ExportFormat,
                       meLabel: String) throws -> Int {
        var errBuf = [CChar](repeating: 0, count: 1024)
        let count = dbPath.path.withCString { db in
            outDir.path.withCString { out in
                format.rawValue.withCString { fmt in
                    meLabel.withCString { me in
                        imsg_export(db, out, fmt, me, &errBuf, Int32(errBuf.count))
                    }
                }
            }
        }
        if count < 0 { throw ExportError(message: String(cString: errBuf)) }
        return Int(count)
    }
}

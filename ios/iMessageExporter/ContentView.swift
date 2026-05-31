import SwiftUI
import UniformTypeIdentifiers

struct ContentView: View {
    @State private var pickedDB: URL?
    @State private var format: ExportFormat = .html
    @State private var meLabel: String = "Me"
    @State private var logLevel: LogLevel = .info
    @State private var importing = false
    @State private var exporting = false
    @State private var status = "Pick a Messages database (chat.db) to begin."
    @State private var shareURL: URL?
    @State private var presentingShare = false

    var body: some View {
        NavigationView {
            Form {
                Section("Source") {
                    Button {
                        importing = true
                    } label: {
                        Label(pickedDB?.lastPathComponent ?? "Choose database file…",
                              systemImage: "doc.badge.plus")
                    }
                    Text("iOS apps can't read the live Messages database. Copy a "
                         + "chat.db from a Mac (or extract one from a backup) and "
                         + "import it here.")
                        .font(.footnote)
                        .foregroundStyle(.secondary)
                }

                Section("Options") {
                    Picker("Format", selection: $format) {
                        ForEach(ExportFormat.allCases) { Text($0.label).tag($0) }
                    }
                    TextField("Your name", text: $meLabel)
                    Picker("Log level", selection: $logLevel) {
                        ForEach(LogLevel.allCases) { Text($0.label).tag($0) }
                    }
                }

                Section {
                    Button(action: runExport) {
                        if exporting {
                            ProgressView()
                        } else {
                            Text("Export")
                        }
                    }
                    .disabled(pickedDB == nil || exporting)

                    if let shareURL {
                        Button {
                            presentingShare = true
                        } label: {
                            Label("Share results", systemImage: "square.and.arrow.up")
                        }
                        .accessibilityIdentifier(shareURL.lastPathComponent)
                    }
                } footer: {
                    Text(status)
                }
            }
            .navigationTitle("iMessage Exporter")
            .fileImporter(isPresented: $importing, allowedContentTypes: [.item]) { result in
                if case .success(let url) = result { pickedDB = url }
            }
            .sheet(isPresented: $presentingShare) {
                if let shareURL { ActivityView(items: [shareURL]) }
            }
        }
        .navigationViewStyle(.stack)
    }

    private func runExport() {
        guard let src = pickedDB else { return }
        exporting = true
        status = "Exporting…"
        shareURL = nil
        let fmt = format
        let me = meLabel.isEmpty ? "Me" : meLabel
        let level = logLevel

        Task.detached {
            do {
                let zip = try export(src: src, format: fmt, meLabel: me, level: level)
                await MainActor.run {
                    shareURL = zip.url
                    status = "Exported \(zip.count) conversation(s). Tap Share to save them."
                    exporting = false
                }
            } catch {
                await MainActor.run {
                    status = "Failed: \(error.localizedDescription)"
                    exporting = false
                }
            }
        }
    }

    /// Copies the picked DB into the sandbox, runs the export, and zips the
    /// output for sharing. Returns the zip URL and conversation count.
    private func export(src: URL, format: ExportFormat, meLabel: String,
                        level: LogLevel) throws -> (url: URL, count: Int) {
        let fm = FileManager.default
        let work = fm.temporaryDirectory.appendingPathComponent("imsg-\(UUID().uuidString)")
        let inDir = work.appendingPathComponent("in")
        let outDir = work.appendingPathComponent("out")
        try fm.createDirectory(at: inDir, withIntermediateDirectories: true)
        try fm.createDirectory(at: outDir, withIntermediateDirectories: true)

        let scoped = src.startAccessingSecurityScopedResource()
        defer { if scoped { src.stopAccessingSecurityScopedResource() } }
        let dbCopy = inDir.appendingPathComponent("chat.db")
        try fm.copyItem(at: src, to: dbCopy)

        Exporter.setLogLevel(level)
        let count = try Exporter.export(dbPath: dbCopy, outDir: outDir,
                                        format: format, meLabel: meLabel)

        return (try zipDirectory(outDir, named: "iMessage Export"), count)
    }

    /// Zips a directory into a single file using the system file coordinator
    /// (the .forUploading option packages a folder as a .zip).
    private func zipDirectory(_ dir: URL, named name: String) throws -> URL {
        let coordinator = NSFileCoordinator()
        var coordErr: NSError?
        var output: URL?
        var moveErr: Error?
        coordinator.coordinate(readingItemAt: dir, options: [.forUploading],
                               error: &coordErr) { zipURL in
            let dest = FileManager.default.temporaryDirectory
                .appendingPathComponent("\(name).zip")
            try? FileManager.default.removeItem(at: dest)
            do {
                try FileManager.default.moveItem(at: zipURL, to: dest)
                output = dest
            } catch {
                moveErr = error
            }
        }
        if let coordErr { throw coordErr }
        if let moveErr { throw moveErr }
        guard let output else { throw ExportError(message: "could not package the export") }
        return output
    }
}

/// Bridges UIActivityViewController (share sheet) into SwiftUI for iOS 15+.
struct ActivityView: UIViewControllerRepresentable {
    let items: [Any]
    func makeUIViewController(context: Context) -> UIActivityViewController {
        UIActivityViewController(activityItems: items, applicationActivities: nil)
    }
    func updateUIViewController(_ controller: UIActivityViewController, context: Context) {}
}

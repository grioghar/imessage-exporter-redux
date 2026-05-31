# Running on iPhone (iOS)

This document explains how to embed the exporter in an iOS app. The C++ core is
portable and iOS ships `libsqlite3`, so the engine itself compiles for iOS
essentially unchanged — what's new is **where the data comes from** and the thin
**C bridge + Swift UI** around it.

> Building and shipping an iOS app requires a **Mac with Xcode**. That's an Apple
> requirement, not a choice of this project.

## The hard constraint: no access to the live Messages database

iOS sandboxes every app into its own container. A normal App Store app **cannot
read another app's data**, and the Messages database lives in Apple's private
container. So on iOS there is **no `~/Library/Messages/chat.db`** for a
third-party app to open, and `default_db_path()` (the macOS default) does not
apply. Only a jailbroken device could bypass this — not supported here.

### The workable model: import a `chat.db`

The app operates on a `chat.db` **the user provides into the app's own sandbox**.
Realistic ways to get one:

- Copy `~/Library/Messages/chat.db` from a Mac and AirDrop / iCloud / Files it to
  the phone, then pick it with the in-app document picker.
- Extract `chat.db` from an **unencrypted** Finder/iTunes device backup on a
  computer and transfer it the same way.

The app then runs the exporter on that file and writes the output **inside its
sandbox**, where the user can preview it (e.g. `WKWebView` for HTML) and share it
out via the system share sheet.

## What this repo already provides

- **Portable C++ core** (`imsg_core`) + **SQLite reader / streaming export
  engine** (`imsg_db`) — no platform assumptions.
- A **pure-C ABI** in [`include/imsg/imsg_bridge.h`](../include/imsg/imsg_bridge.h):

  ```c
  int  imsg_export(const char *db_path, const char *out_dir,
                   const char *format, const char *me_label,
                   char *err_buf, int err_buf_len);
  const char *imsg_version(void);
  ```

- **Streaming export** (`export_database`) that holds only one conversation in
  memory at a time — important because iOS aggressively kills memory-hungry apps.
- A **SwiftPM package** ([`Package.swift`](../Package.swift)) exposing the bridge
  as the Swift-importable module `ImsgExporter`.

## Integration path A — Swift Package (simplest)

In Xcode: **File ▸ Add Package Dependencies… ▸ Add Local…** and select this repo
(or add it by its Git URL). Add the `ImsgExporter` product to your app target.
SwiftPM compiles the C++ sources and links `sqlite3`.

```swift
import ImsgExporter

/// Runs the exporter on a picked database. Returns the output directory.
func exportDatabase(at dbURL: URL, format: String) throws -> URL {
    let outDir = FileManager.default.temporaryDirectory
        .appendingPathComponent("imessage-export-\(UUID().uuidString)")

    var errBuf = [CChar](repeating: 0, count: 512)
    let n = imsg_export(dbURL.path, outDir.path, format, "Me",
                        &errBuf, Int32(errBuf.count))
    guard n >= 0 else {
        throw NSError(domain: "ImsgExporter", code: Int(n),
                      userInfo: [NSLocalizedDescriptionKey: String(cString: errBuf)])
    }
    return outDir
}
```

> If Xcode's Swift importer trips over the C++ sources, confirm the module map
> (`include/module.modulemap`) is the one in effect — it deliberately exposes
> only the C bridge header, keeping the C++ headers private. C++↔Swift packaging
> can need small tweaks per Xcode version; path B avoids it entirely.

## Integration path B — prebuilt XCFramework (most robust)

Build the embeddable static library with an iOS CMake toolchain
([leetal/ios-cmake](https://github.com/leetal/ios-cmake)) for device and
simulator, then bundle them into an `.xcframework` you drop into the app:

```bash
# Device (arm64)
cmake -S . -B build-ios -G Xcode \
  -DCMAKE_TOOLCHAIN_FILE=ios.toolchain.cmake -DPLATFORM=OS64
cmake --build build-ios --config Release --target imsg_mobile

# Simulator (arm64 + x86_64)
cmake -S . -B build-sim -G Xcode \
  -DCMAKE_TOOLCHAIN_FILE=ios.toolchain.cmake -DPLATFORM=SIMULATOR64
cmake --build build-sim --config Release --target imsg_mobile

xcodebuild -create-xcframework \
  -library build-ios/Release/libimsg_mobile.a -headers include \
  -library build-sim/Release/libimsg_mobile.a -headers include \
  -output ImsgExporter.xcframework
```

Add `ImsgExporter.xcframework` to your target, link `libsqlite3.tbd`, and add a
bridging header that `#include "imsg/imsg_bridge.h"`.

## UI wiring (sketch)

1. **Pick the database** with `UIDocumentPickerViewController` /
   SwiftUI `.fileImporter`. Picked files are outside the sandbox, so wrap access:

   ```swift
   guard dbURL.startAccessingSecurityScopedResource() else { /* handle */ }
   defer { dbURL.stopAccessingSecurityScopedResource() }
   // ... call exportDatabase(at: dbURL, ...)
   ```

2. **Export** off the main thread (`Task.detached`) so the UI stays responsive.
3. **Preview** an HTML export by loading a file from the output directory into a
   `WKWebView` (`loadFileURL:allowingReadAccessTo:`).
4. **Share** results via `UIActivityViewController` (zip the output directory for
   multi-file exports).

## Notes

- Prefer the **HTML** format for on-device viewing; **JSON** for re-import; **TXT**
  for plain sharing.
- Large databases: export streams per conversation, but a single huge group chat
  still loads fully — keep an eye on memory if you target very old devices.
- The exporter opens the database read-only and never writes to it.

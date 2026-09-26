# Building from source

How to build the CLI and the desktop GUI on each platform, and how to run the
CLI in Docker. See [ARCHITECTURE.md](ARCHITECTURE.md) for how the code is
organised.

## Building from source

Requires **CMake ≥ 3.16** and a C++17 compiler.

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build      # 142 unit tests, no SQLite needed
```

**Platform notes:**

- **macOS** — SQLite3 ships with the system; found automatically.
- **Linux** — install `libsqlite3-dev`; without it, only the core library and
  tests build (CMake warns but does not error).
- **Windows** — SQLite is fetched via vcpkg in CI. For a local build, install
  vcpkg and `vcpkg install sqlite3:x64-windows`.
- **Desktop GUI** — requires **Qt 6** (`qt6-base-dev` on Debian/Ubuntu; official
  Qt installer on macOS/Windows). If Qt is not found, CMake builds only the CLI.

The CLI binary is at `build/imessage-exporter`; the GUI at
`build/imessage-exporter-gui` (or `"iMessage Exporter.app"` on macOS).

## Docker (Linux CLI)

```bash
docker build -t imessage-exporter .

# Mount your data directory; results appear inside it
docker run --rm -v "$PWD:/data" imessage-exporter \
    --db /data/chat.db --format html --output /data/export \
    --contacts-db /data/contacts.vcf
```

The container image is also pushed to
[`ghcr.io/grioghar/imessage-exporter-redux`](https://github.com/grioghar/imessage-exporter-redux/pkgs/container/imessage-exporter-redux)
on every `v*` release tag.

## iOS / embedding

A pure-C bridge ([`include/imsg/imsg_bridge.h`](../include/imsg/imsg_bridge.h)) and
a SwiftPM package ([`Package.swift`](../Package.swift)) let you embed the engine in
any app. An example SwiftUI iOS app lives in [`ios/`](../ios/).

> iOS sandboxing prevents reading the live Messages database. The app must import
> a `chat.db` the user supplies (e.g. from a backup). See
> [**IOS.md**](IOS.md) for details.

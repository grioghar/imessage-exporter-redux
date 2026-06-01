# HANDOFF — context for continuing this project

Detailed notes so any agent (including a local model run via **ollama**) can pick
up where the last session left off **without re-reading the whole history**.
Keep this file CURRENT: update the "Status" and "In flight / next" sections at
the end of every working session. `CLAUDE.md` is the deeper architecture guide;
this file is the running state + how-to-continue.

## What this project is
`grioghar/imessage-exporter-redux` — a C++17 tool that exports a macOS Messages
database (`chat.db`) to TXT / JSON / HTML. One shared engine, many front-ends:
CLI, Qt desktop GUI (macOS/Windows/Linux), and an iOS SwiftUI app (via a pure-C
bridge). MIT licensed. Clean-room; shares no code with the Rust
`ReagentX/imessage-exporter`.

## Architecture (the load-bearing idea)
- **`imsg_core`** (static lib, NO SQLite, unit-tested anywhere): `log`, `models`,
  `time_util`, `attributed_body`, `exporters`, `contact_book`, `vcard`.
- **`imsg_db`** (static lib, +SQLite): `database`, `export_job`, `contacts`,
  `backup`. Only this + `main.cpp` touch SQLite.
- **`imsg_mobile`** / `imsg_bridge.cpp`: pure-C ABI (`include/imsg/imsg_bridge.h`)
  for Swift/embedding. SwiftPM `Package.swift` builds the same sources for iOS.
- **GUI** (`gui/`, Qt6 Widgets, optional via `find_package(Qt6)`): `main_window`,
  `icloud_contacts` (CardDAV), `updater` (self-update), `resources.qrc` (icon).
- **Rule:** put parsing/formatting/IO in the engine; keep front-ends thin. Prefer
  the SQLite-free core so logic stays testable. Match the terse house style;
  comments say *why*, not *what*.

## Where the data comes from (and the hard limits — already decided, don't relitigate)
- Messages: a Mac's synced `~/Library/Messages/chat.db` (default), or extracted
  from an **unencrypted** iTunes/Finder backup (`--backup`). **No iCloud pull for
  messages** — no API + end-to-end encrypted. Encrypted-backup decryption is NOT
  implemented (would need a crypto dep + real data).
- Contacts: macOS AddressBook (`.abcddb`), a vCard `.vcf`, the iOS AddressBook
  from a backup, or **iCloud CardDAV via an app-specific password** (GUI button).
  Raw Apple-ID/2FA/SRP login is intentionally NOT built.
- Ephemeral virtualized-macOS puller (UTM/QEMU): **rejected** — running macOS on
  non-Apple hardware violates Apple's SLA. (Cloud Apple hardware would be the
  only license-clean route; not built.)

## Build / test / validate
```bash
cmake -S . -B build && cmake --build build && ctest --test-dir build   # core tests
cmake --build build --target imessage-exporter        # CLI (needs sqlite)
cmake --build build --target imessage-exporter-gui    # GUI (needs Qt6)
```
- **No local C++ toolchain in the dev sandbox** — CI is the source of truth.
  Validate by pushing a branch/PR and reading GitHub Actions.
- Tests are a hand-rolled harness in `tests/test_core.cpp` (no framework), core
  only. Add assertions there for any new core logic.

## CI / workflows (GitHub Actions)
- `ci.yml`: `build-test` (ubuntu+macos engine/CLI), `gui` matrix
  (ubuntu/macos/windows via `jurplel/install-qt-action`; Windows SQLite via
  vcpkg), `docker`.
- `package.yml` (manual dispatch or `v*` tag): builds installers — macOS `.dmg`,
  Windows Inno `.exe`, Linux `.AppImage` + `.deb` + `.rpm` + Snap — and a
  `release` job publishes them on a real `v*` tag (skips tags containing `test`).
- `docker-publish.yml`: pushes the CLI image to GHCR (and Docker Hub, enabled).
- **Validation trick:** push a throwaway tag `vX-test...` to exercise
  `package.yml` without creating a release; delete the tag after.

## Conventions / gotchas learned the hard way
- Commit msgs: bash here-strings break on apostrophes — write the message to a
  file and `git commit -F`, or avoid `'` in `-m`.
- Don't push straight to `main` (auto-mode blocks it). Land via PRs. The PR stack
  was merged by fast-forwarding earlier; rebase-merging a stack rewrites SHAs and
  causes conflicts — rebase the tip onto updated `main` and merge once.
- MSVC: use `/W3`, not `-Wall -Wextra` (guarded in CMake).
- Windows GUI must be `WIN32_EXECUTABLE` (no console window) + SQLite via vcpkg.
- `CMAKE_POSITION_INDEPENDENT_CODE ON` so static libs link into the Qt PIE exe.
- AppImage: name the output via `OUTPUT` env so `mv *.AppImage` doesn't grab the
  linuxdeploy tools.
- Version lives in `include/imsg/version.hpp` (`IMSG_VERSION`). Keep it in sync
  with CMake `project(VERSION)`, `packaging/windows/installer.iss`,
  `man/imessage-exporter.1`, `snap/snapcraft.yaml`, and the Homebrew/Choco defs.

## Status (UPDATE THIS EACH SESSION)
- Released: **v0.2.2** (latest; six installers; brew/choco checksums refreshed)
  — HTML URL links open in a new window; YouTube/Spotify/
  Vimeo embeds; `--embed-attachments` inlines media as base64 data URIs; the
  displayed version now carries a build stamp `IMSG_VERSION-DDMMYYHHMM`
  (CMake `string(TIMESTAMP)` -> generated `imsg/build_stamp.hpp`, consumed only
  by the CLI/GUI; bridge/core keep bare IMSG_VERSION). Version base = 0.2.2.
- **0.2.3 IN PROGRESS:**
  - DONE: persistent contacts store `ContactStore` (`src/contact_store.cpp`,
    imsg_db) — plain SQLite at `default_contact_store_path()` (per-user data
    dir), persists across updates. Wired as a contacts source: CLI
    `--contact-store`, GUI "Saved contacts database" option, and
    `ExportOptions.use_contact_store` (merged into the resolution ContactBook).
  - NEXT: Google Contacts connect/download — OAuth 2.0 (PKCE, loopback redirect)
    + People API, **client ID from config/env (blank default)** so it ships and
    works once supplied; downloaded contacts saved into the ContactStore.
  - NEXT: OAuth tokens in the **OS keychain** (macOS `security` CLI / Windows
    DPAPI / Linux file-fallback 0600). The contacts cache stays plain SQLite
    (user chose keychain-for-tokens-only, not full DB encryption).
- Previously released: **v0.2.1** — six installers (macOS `.dmg`, Windows
  `Setup.exe`, Linux `.AppImage` + `.deb` + `.rpm` + `.snap`). v0.2.1 adds:
  Unicode-preserving export filenames (slugify keeps UTF-8 + `fs::u8path`),
  macOS Full Disk Access guidance on DB-open denial, and a GUI error dialog with
  Copy error / Open log file / Open Settings. v0.2.0 and v0.1.0 also exist.
  IMSG_VERSION = 0.2.1; Homebrew/Choco checksums refreshed for v0.2.1.
- `main`: everything merged — features, backup, logging, Docker (+Hub publish),
  Qt GUI with Help menu / iCloud CardDAV import / app icon / **auto-update** /
  hidden Windows console, all six installer pipelines, Homebrew + Chocolatey
  defs, this file. Version centralized in `include/imsg/version.hpp` = 0.2.0.
- **Open PRs:** the brew/choco-0.2.1 checksum-refresh PR (this) only.

## In flight / next
- Publish to the actual registries (one-time, needs accounts): create
  `grioghar/homebrew-tap` and copy the two `.rb` files; `choco pack` +
  `choco push` with a community API key; GHCR/Docker Hub already automated.
- For the NEXT release, bump `IMSG_VERSION` + CMake `project(VERSION)` +
  installer.iss + man page + snapcraft.yaml, tag `vX.Y.Z`, then re-fill the
  Homebrew/Choco checksums against the new assets (see "Build/validate").
- iOS app is source-only (needs Xcode + Apple Developer acct; not CI-built).
- Installers are UNSIGNED (macOS notarization / Windows cert need credentials).
- Still unbuilt by design: encrypted-backup decryption; attachment extraction
  from backups; ephemeral cloud-Mac puller.

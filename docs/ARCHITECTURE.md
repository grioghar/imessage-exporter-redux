# Architecture

How the source tree is laid out and why the engine is split into layers.
Deeper contributor notes live in [CLAUDE.md](CLAUDE.md) (architecture guide)
and [HANDOFF.md](HANDOFF.md) (running project state).

## Project layout

```
imessage-exporter/
├── include/imsg/          # public C++ headers
│   ├── models.hpp         # Chat / Message / Attachment / Participant
│   ├── exporters.hpp      # TXT / JSON / HTML / Android renderers
│   ├── theme.hpp          # pluggable HTML themes
│   ├── stats.hpp          # statistics aggregation & rendering
│   ├── export_job.hpp     # ExportOptions + export_database()
│   ├── database.hpp       # read-only chat.db reader (SQLite)
│   ├── contacts.hpp       # AddressBook / vCard contact resolution
│   ├── backup.hpp         # iTunes/Finder backup extraction
│   └── imsg_bridge.h      # pure-C ABI for iOS / embedding
├── src/                   # one .cpp per header + main.cpp (CLI)
├── gui/                   # Qt 6 desktop application
│   ├── main_window.*      # main UI: tabs, preferences pane, export control
│   ├── google_auth.*      # OAuth 2.0 flow
│   ├── google_contacts.*  # Google Contacts sync
│   ├── google_drive.*     # Google Drive upload
│   ├── icloud_contacts.*  # iCloud CardDAV fetch
│   ├── link_preview.*     # Open Graph / Twitter Card fetcher
│   ├── secret_store.*     # encrypted credential storage (platform keychain)
│   └── updater.*          # auto-update (GitHub Releases)
├── ios/                   # example SwiftUI iOS app
├── tests/
│   ├── test_core.cpp      # dependency-free unit tests (imsg_core only)
│   ├── run_golden.sh      # golden-file tests: CLI output vs tests/golden/
│   ├── fixtures/          # synthetic chat.db (fake data) + its generator
│   └── golden/            # expected CLI output per format
├── packaging/             # Homebrew, Chocolatey, Inno Setup, Snap, .desktop
├── docs/                  # user guide, CLI, building, schema, themes, roadmap…
└── CMakeLists.txt
```

The engine is split into layers so that the tricky parsing/formatting logic stays
fully testable without SQLite:

- **`imsg_core`** — models, time conversion, `attributedBody` decoder, exporters,
  themes, stats. Zero external dependencies; used by the unit tests directly.
- **`imsg_db`** — adds SQLite: database reader, contact loader, backup extractor,
  export job orchestration.
- **`imsg_mobile`** — pure-C ABI over `imsg_db`, consumed by iOS / SwiftPM.
- **`imessage-exporter`** — thin CLI wrapper.
- **`imessage-exporter-gui`** — Qt 6 desktop app; the only layer that does
  network I/O (contacts sync, Drive upload, link previews, updates).

## Two technical quirks worth knowing

**1. Timestamps.** `message.date` is seconds since 2001-01-01 UTC — but
nanoseconds on macOS 10.13+. The engine auto-detects by magnitude (≥ 10¹¹ →
nanoseconds) and converts correctly for both old and new databases.

**2. `attributedBody`.** Modern macOS frequently leaves `message.text` as `NULL`
and stores the visible text in an `attributedBody` BLOB — an `NSAttributedString`
serialized with the legacy NeXTSTEP typedstream format. The engine decodes the
UTF-8 payload from the typedstream, validates it, strips invisible Unicode
control characters, and falls back gracefully on unexpected layouts.

See [`SCHEMA.md`](SCHEMA.md) for the full schema reference.

# iMessage Exporter

[![CI](https://github.com/grioghar/imessage-exporter-redux/actions/workflows/ci.yml/badge.svg)](https://github.com/grioghar/imessage-exporter-redux/actions/workflows/ci.yml)
[![Latest release](https://img.shields.io/github/v/release/grioghar/imessage-exporter-redux?label=release)](https://github.com/grioghar/imessage-exporter-redux/releases/latest)
[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)

A cross-platform **iMessage & SMS archival suite** built in C++17. It reads Apple's
Messages database **read-only** and produces beautiful, portable exports — from a
native desktop app for everyday users all the way to an embeddable C library for iOS
developers.

| | macOS | Windows | Linux |
|---|---|---|---|
| **Desktop app** | ✅ `.app` / `.dmg` | ✅ `.exe` installer | ✅ AppImage / deb / rpm / Snap |
| **CLI** | ✅ | ✅ | ✅ |
| **Docker** | ✅ host | ✅ host | ✅ native |

---

## Quick start

Pick your platform. Every route ends with the same tool reading the Messages
database **read-only** and writing an export folder; see the
[user guide](docs/USER-GUIDE.md) for the desktop app and
[CLI.md](docs/CLI.md) for every flag.

### macOS

```bash
# CLI via Homebrew (or grab the .dmg from Releases for the desktop app)
brew tap grioghar/tap https://github.com/grioghar/homebrew-tap
brew install grioghar/tap/imessage-exporter

# Export every conversation from this Mac's Messages database as styled HTML
imessage-exporter --format html --contacts --output ~/Desktop/imessage-export
```

The terminal (or the app) needs **Full Disk Access** to read
`~/Library/Messages/chat.db` (System Settings → Privacy & Security → Full Disk
Access). Alternatively export from an unencrypted iPhone backup:
`imessage-exporter --backup latest --contacts --format html --output ./export`.

### Windows

```powershell
# Desktop app via Chocolatey (or run the .exe installer from Releases)
choco install imessage-exporter
```

Launch **iMessage Exporter** from the Start menu. Windows has no Messages
database, so pick **Device backup** as the source: an unencrypted iTunes backup
under `%APPDATA%\Apple\MobileSync\Backup` is found automatically. The Windows
installer ships the desktop app only; for the CLI build from source with vcpkg
(see [docs/BUILDING.md](docs/BUILDING.md)), then
`imessage-exporter --backup latest --contacts --format html --output .\export`.

### Linux

Download the `.deb` or `.rpm` (desktop app + CLI), or the AppImage / Snap
(desktop app) from [Releases](../../releases/latest); Homebrew on Linux
(`brew install grioghar/tap/imessage-exporter`) gives the CLI. Then point it at
a `chat.db` copied from a Mac or at an iTunes/Finder backup folder:

```bash
imessage-exporter --db ./chat.db --format html --contacts-db ./contacts.vcf --output ./export
imessage-exporter --backup /path/to/backup/<UDID> --format txt --output ./export
```

### Docker (Linux CLI)

```bash
docker build -t imessage-exporter .
docker run --rm -v "$PWD:/data" imessage-exporter \
    --db /data/chat.db --format html --output /data/export
```

### iOS / embedding

The engine ships as a pure-C library and a SwiftPM package for use inside your
own app (iOS apps cannot read the live Messages database and must import a
`chat.db`). See [docs/IOS.md](docs/IOS.md).

### Building from source

```bash
cmake -S . -B build && cmake --build build && ctest --test-dir build
```

Needs CMake ≥ 3.16, a C++17 compiler and SQLite3 (`libsqlite3-dev` on Linux;
Qt 6 for the desktop GUI). Details per platform in
[docs/BUILDING.md](docs/BUILDING.md).

---

## What it does

- **Exports** your iMessage, SMS, and RCS conversation history to **HTML**,
  **PDF**, **JSON**, **TXT**, or **Android SMS XML**.
- **Beautifies** HTML exports: iOS-style message bubbles, contact photos, group
  chat headers, inline images & video, YouTube / Spotify embeds (playable),
  Open Graph rich link previews, and a choice of **five visual themes**.
- **Connects** to your contacts: macOS AddressBook, iCloud CardDAV,
  **Google Contacts** (OAuth), or any vCard `.vcf` — with an optional persistent
  store that survives updates.
- **Uploads** finished exports directly to **Google Drive**.
- **Reads** from your Mac's live database or an iTunes/Finder **device backup**
  (no iCloud required for messages).
- **Analyses** your history with an optional `00-statistics.html` cover page:
  hourly/weekly charts, top texters, streaks, and fun facts.
- **Filters** by date range, or by choosing specific people from a smart list
  that can sort, search, and hide contacts you haven't heard from in ages.

---

## Download

Grab a pre-built installer from the [**Releases**](../../releases/latest) page —
no compiler required.

> **Note:** Installers are currently **unsigned**. macOS will show a Gatekeeper
> warning (right-click → Open to bypass); Windows may show a SmartScreen prompt.
> Code signing is pending Apple Developer ID / Windows cert setup.

### Package managers

```bash
# Homebrew (macOS / Linux) — CLI
brew tap grioghar/tap https://github.com/grioghar/homebrew-tap
brew install grioghar/tap/imessage-exporter

# Homebrew — desktop GUI (.app)
brew install --cask grioghar/tap/imessage-exporter-app   # add --no-quarantine while unsigned

# Chocolatey (Windows)
choco install imessage-exporter
```

---

## Documentation

| Page | Contents |
|---|---|
| [docs/USER-GUIDE.md](docs/USER-GUIDE.md) | Desktop app walkthrough, where messages and contacts come from, export formats, themes, statistics page |
| [docs/CLI.md](docs/CLI.md) | CLI examples and the full option reference |
| [docs/BUILDING.md](docs/BUILDING.md) | Building on macOS / Windows / Linux, Docker, iOS embedding |
| [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) | Source layout, engine layers, the timestamp and `attributedBody` quirks |
| [docs/SCHEMA.md](docs/SCHEMA.md) | Messages database schema notes |
| [docs/THEMES.md](docs/THEMES.md) | Authoring HTML themes |
| [docs/GOOGLE.md](docs/GOOGLE.md) | Google Contacts / Google Drive setup |
| [docs/IOS.md](docs/IOS.md) | Embedding the engine in an iOS app |
| [docs/ROADMAP.md](docs/ROADMAP.md) | Planned features |
| [docs/CLAUDE.md](docs/CLAUDE.md), [docs/HANDOFF.md](docs/HANDOFF.md) | Contributor / agent context and running project state |

---

## Disclaimer

This tool is for exporting **your own** message data. Respect the privacy of the
people you've communicated with and any applicable laws when handling exported
conversations.

---

## License

MIT — see [LICENSE](LICENSE).

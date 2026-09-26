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

## Privacy

Everything below was checked against the source in this repository (`src/`,
`include/`, `gui/`, `ios/`), not just the docs.

### What it reads

- **Messages database** — `~/Library/Messages/chat.db` by default, or the file
  you pass with `--db`. It is opened **read-only and immutable**
  (`mode=ro&immutable=1`), so the live database is never modified or locked.
  Tables used: `message`, `handle`, `chat`, `chat_message_join`,
  `chat_handle_join`, `attachment`, `message_attachment_join`. From `message`
  it reads `guid`, `text`, `attributedBody`, `date`, `date_read`, `is_from_me`,
  `handle_id` and `service`; from `attachment` only the metadata
  (`filename`, `mime_type`, `transfer_name`, `total_bytes`).
- **Attachment files** — the paths stored in `attachment.filename` (normally
  under `~/Library/Messages/Attachments/`) are opened **only** with
  `--copy-attachments` or `--embed-attachments` (or the matching desktop-app
  options). Otherwise attachments appear in the export as names only.
- **Contacts (optional)** — `--contacts` scans `~/Library/Application
  Support/AddressBook/` for `*.abcddb` files (`ZABCDRECORD`,
  `ZABCDPHONENUMBER`, `ZABCDEMAILADDRESS`); `--contacts-db` reads the
  `.abcddb` / `.vcf` file or folder you name; `--contact-store` reads the
  saved contacts cache described below. All are opened read-only.
- **Device backups (optional)** — `--backup` reads `Manifest.db` and the
  content-addressed blobs of an unencrypted iTunes/Finder backup
  (`~/Library/Application Support/MobileSync/Backup/<UDID>/` on macOS,
  `%APPDATA%\Apple\MobileSync\Backup\<UDID>\` on Windows, or a path you
  give). `sms.db` (and, with `--contacts`, `AddressBook.sqlitedb`) are
  extracted to a temporary folder `imessage-exporter-<UDID>` in the system temp
  directory, which is deleted when the tool exits.
- **Location data (optional)** — `--location takeout:PATH` reads a local Google
  Takeout `Records.json`; nothing is fetched.

### What it writes

- **The export folder** (`--output`, default `./imessage-export`): one file per
  conversation (`<name>.txt` / `.md` / `.json` / `.html` / `.xml`), or a single
  `conversations.<ext>` with `--combined`; `00-statistics.html` and
  `00-timeline.html` when requested; copied attachments in a per-conversation
  sub-folder `<name>/` (`.<name>/` with `--hidden-attachments`); with
  `--encrypt`, HTML is rewritten as a self-decrypting page and other formats
  become `<file>.enc` (AES-256-GCM, key derived from your password, which is
  never stored). The desktop app's optional media A/B comparison additionally
  writes an `image-movie-comparison/` folder of sample re-encodes, with a
  README saying it is safe to delete.
- **Logs** — the CLI logs to **stderr only** (`--log-level`, `-v`); it writes no
  log file. The desktop app appends each run's log to `imessage-exporter.log`
  in its per-user application-data folder (`~/Library/Application Support`,
  `%APPDATA%`, or `~/.local/share` under an `iMessage Exporter` /
  `imessage-exporter` directory) and shows the same text in its log pane.
- **Desktop-app state** — window settings and preferences via the platform
  settings store (`QSettings`: plist / registry / `~/.config`); a local copy of
  `chat.db` under `<app data>/messages/` **only** when you click "Copy Messages
  data to a local cache"; the optional persistent contacts cache
  `imessage-exporter/contacts.db` (handle → name/photo) under the user data
  directory; and iCloud / Google credentials in the OS keychain (macOS
  Keychain, Windows Credential Manager) or, on Linux, owner-only files under
  `<app data>/secrets/`.
- **Temporary files** — only the backup-extraction folder above (removed on
  exit) and, when the desktop app downloads an update, the installer in the
  system temp folder.
- **Helper processes** — on macOS HEIC attachments are converted with the
  system `sips` tool; the media comparison shells out to `ffmpeg` if it is on
  `PATH`. Both run locally on files inside the export folder.

### Does anything leave the machine?

**CLI, Docker image, C library and iOS bridge: no.** The engine (`imsg_core`,
`imsg_db`), the `imessage-exporter` binary and `imsg_bridge` contain no network
code at all — no sockets, HTTP client, telemetry, analytics, update check or
crash reporter (checked with a grep over `src/`, `include/` and `ios/`). A CLI
export can run on an air-gapped machine.

**Exported HTML, when you open it in a browser**, may itself load third-party
resources for links that appear in your messages: favicons from
`https://www.google.com/s2/favicons?domain=<host>` for link cards, YouTube
thumbnails from `i.ytimg.com`, and YouTube / Spotify / Vimeo embed iframes.
Those requests are made by your browser at viewing time (revealing the linked
hosts, not your message text); the exporter itself fetches nothing. TXT, JSON,
Markdown and Android XML exports reference no remote resources.

**Desktop app (Qt GUI): yes, but only for the features below, and none of
them ever sends your messages except Google Drive upload.**

| Feature | When | What is contacted / sent |
|---|---|---|
| Update check | On launch, default **on** (Help → "Automatically check for updates" to disable; "Check now" to run manually) | `GET https://api.github.com/repos/grioghar/imessage-exporter-redux/releases/latest`. No data about you or your messages is sent; the installer is downloaded only if you click Install. |
| iCloud Contacts import | Only when you click "Import iCloud Contacts" | CardDAV requests to `contacts.icloud.com` with your Apple ID and an app-specific password; contacts are saved locally. |
| Google Contacts | Only after you connect it | OAuth via `accounts.google.com` / `oauth2.googleapis.com`, then `people.googleapis.com` with the read-only `contacts.readonly` scope. |
| Google Drive upload | Only with "Upload export to Drive when finished" checked | Uploads the **entire export folder** (messages and attachments) to your Drive via `www.googleapis.com/drive/v3` (`drive.file` scope). This is the one feature that sends message content off the machine. |
| Rich link previews | Only with "Rich link previews (online)" checked (default **off**) | Fetches each URL found in your messages (page HTML plus its Open Graph image) so the card can be embedded; the sites you linked see the request. |
| Help menu | On click | Opens this README / the issue tracker in your browser. |

There is no telemetry, analytics or crash reporting in any front-end.

---

## Disclaimer

This tool is for exporting **your own** message data. Respect the privacy of the
people you've communicated with and any applicable laws when handling exported
conversations.

---

## License

MIT — see [LICENSE](LICENSE).

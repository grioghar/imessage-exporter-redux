# imessage-exporter

[![CI](https://github.com/grioghar/imessage-exporter-redux/actions/workflows/ci.yml/badge.svg)](https://github.com/grioghar/imessage-exporter-redux/actions/workflows/ci.yml)

A small, fast, **C++17** command-line tool that exports your macOS iMessage / SMS
history to portable formats (**TXT**, **JSON**, **HTML**).

It reads the local Messages SQLite database (`~/Library/Messages/chat.db`)
**read-only** — it never modifies your data — and links directly against the
system `libsqlite3` for a lean, native binary with no third-party runtime
dependencies.

> Inspired by [ReagentX/imessage-exporter](https://github.com/ReagentX/imessage-exporter)
> (a Rust project). This is an independent, clean-room C++ implementation and
> shares no code with it.

## Why C++

- Native binary, no interpreter or runtime — fast startup, low overhead.
- Links the SQLite C API directly; the only dependency (`libsqlite3`) ships with
  macOS.
- The non-trivial logic (the `attributedBody` typedstream decoder, Apple
  timestamp conversion, and the exporters) lives in a dependency-free core
  library that is fully unit-tested.

## Features

- Exports to **TXT**, **JSON**, or **HTML** — one file per conversation, or a
  single **combined** file (`--combined`).
- Recovers message text from the `attributedBody` blob (modern macOS frequently
  leaves the plain `text` column `NULL`).
- Resolves participants, display names, service (iMessage vs SMS), and attachment
  metadata.
- **Date-range filtering** with `--since` / `--until`.
- **Copies attachment files** into the export and links them (HTML embeds images)
  with `--copy-attachments`; or **inlines** pictures/videos/files as base64 with
  `--embed-attachments` so each HTML/JSON file is self-contained.
- **Rich HTML** — URLs become links that open in a new window, and YouTube /
  Spotify / Vimeo links render as inline embeds (other hosts get a link).
- **Contact-name resolution** so senders show names instead of phone numbers /
  emails — from the macOS Contacts (AddressBook) database (`--contacts`), a vCard
  `.vcf` exported from iCloud.com (`--contacts-db contacts.vcf`), iCloud CardDAV
  or **Google Contacts** (desktop app), or a **persistent saved database**
  (`--contact-store`) that remembers downloaded contacts across updates.
- **Reads from a device backup** — point `--backup` at an unencrypted
  iTunes/Finder backup and it extracts the messages (and contacts) to export,
  no live database needed. `--list-backups` discovers them.
- Correct Apple "Mac absolute time" conversion (nanoseconds **and** legacy
  seconds).
- `--list-chats` to preview conversations without exporting.
- Opens the database with `mode=ro&immutable=1`, so the live Messages data is
  never touched.

## Building

Requires CMake ≥ 3.16, a C++17 compiler, and SQLite3.

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build      # runs the core unit tests
```

- **macOS:** SQLite3 ships with the system and is found automatically.
- **Linux:** install `libsqlite3-dev` to build the `imessage-exporter` binary.
  Without it, CMake still builds the core library and tests.
- **Desktop GUI (optional):** if **Qt 6** is found at configure time, CMake also
  builds the `imessage-exporter-gui` app (see [Desktop app](#desktop-app)).
  Install Qt 6 (`qt6-base-dev` on Debian/Ubuntu, or the official Qt installer on
  macOS/Windows). Without Qt, only the CLI is built.

The CLI binary is produced at `build/imessage-exporter`; a man page is installed
to `share/man/man1/imessage-exporter.1` (`man imessage-exporter` after install).

## Usage

```bash
# Export every conversation to ./export as HTML (default DB location)
./build/imessage-exporter --format html --output ./export

# Export to plain text from a copied database
./build/imessage-exporter --db ./chat.db --format txt --output ./export

# Export structured JSON
./build/imessage-exporter --format json --output ./export

# Just list conversations and how many messages each has
./build/imessage-exporter --list-chats

# One combined HTML file, names resolved from Contacts, attachments copied in
./build/imessage-exporter --format html --combined --contacts \
    --copy-attachments --output ./export

# Only messages from 2023, as JSON
./build/imessage-exporter --format json --since 2023-01-01 --until 2023-12-31

# Resolve names from a vCard exported at iCloud.com -> Contacts -> Export vCard
./build/imessage-exporter --format html --contacts-db ~/Downloads/iCloud-Contacts.vcf

# Export from the most recent iTunes/Finder backup (with that device's contacts)
./build/imessage-exporter --list-backups
./build/imessage-exporter --backup latest --contacts --format html --output ./export
```

### Where the messages come from

This tool reads an existing Messages database — it does **not** log into iCloud
(Apple exposes no API for Messages, and Messages in iCloud is end-to-end
encrypted). There are two practical sources:

1. **A Mac's local database** (default). If you're signed into Messages on a Mac
   with **Messages in iCloud** enabled, your full history syncs down to
   `~/Library/Messages/chat.db`, which is what the tool reads by default. The
   terminal needs **Full Disk Access** to read it.
2. **A device backup** (`--backup`). Make a local **unencrypted** backup of your
   iPhone/iPad in Finder (or iTunes), then run with `--backup latest` (or a
   backup directory / device UDID). The messages database — and, with
   `--contacts`, the device's address book — are extracted from the backup
   automatically. Encrypted backups aren't supported yet; turn off "Encrypt
   local backup" and back up again.

For contacts specifically, you can also skip both and feed a vCard exported from
iCloud.com (see `--contacts-db` above).

### Options

| Flag | Description | Default |
| --- | --- | --- |
| `--db PATH` | Path to the Messages database. | `$HOME/Library/Messages/chat.db` |
| `--format FMT` | Output format: `txt`, `json`, or `html`. | `txt` |
| `--output DIR` | Directory to write export files into. | `./imessage-export` |
| `--me NAME` | Label used for messages you sent. | `Me` |
| `--since DATE` | Only messages on/after `DATE` (`YYYY-MM-DD[ HH:MM:SS]`). | — |
| `--until DATE` | Only messages on/before `DATE`; a date alone means end of that day. | — |
| `--combined` | Write one combined file instead of one per conversation. | — |
| `--copy-attachments` | Copy attachment files into `<output>/attachments` and link them. | — |
| `--contacts` | Resolve names via the default macOS Contacts database. | — |
| `--contacts-db PATH` | Resolve names via a specific `.abcddb` or vCard `.vcf` file (or a directory of them). | — |
| `--backup SPEC` | Source from an iTunes/Finder backup: a directory path, a device UDID, or `latest`. Unencrypted backups only. | — |
| `--list-backups` | List discovered device backups and exit. | — |
| `--list-chats` | List conversations and exit (no export). | — |
| `--log-level LVL` | Log verbosity: `error`, `warn`, `info`, `debug` (or `IMSG_LOG_LEVEL`). | `warn` |
| `-v` / `-vv` | Shortcuts for `--log-level info` / `--log-level debug`. | — |
| `--version` | Print version and exit. | — |
| `--help` | Show help and exit. | — |

Diagnostic logs are written to **stderr** at four levels (error, warn, info,
debug); normal output (the export summary, `--list-chats`) stays on stdout, so
you can separate them. The same levels are available to embedders via
`imsg_set_log_level()` in the C bridge.

On modern macOS the terminal running this tool needs **Full Disk Access**
(System Settings → Privacy & Security → Full Disk Access) to read `chat.db`.

## Project layout

```
imessage-exporter/
├── include/imsg/        # public headers
│   ├── models.hpp       # Chat / Message / Attachment
│   ├── time_util.hpp    # Apple timestamp conversion
│   ├── attributed_body.hpp
│   ├── exporters.hpp    # txt / json / html renderers
│   └── database.hpp     # read-only chat.db reader (SQLite)
├── src/                 # implementations (+ main.cpp)
├── tests/test_core.cpp  # dependency-free unit tests
├── docs/SCHEMA.md       # database notes & quirks
└── CMakeLists.txt
```

`models`, `time_util`, `attributed_body`, and `exporters` form the SQLite-free
core (`imsg_core`); only `database.cpp` and `main.cpp` depend on SQLite. This
keeps the tricky parsing/formatting logic testable on any platform.

## How it works

The Messages database stores each message in the `message` table, linked to
conversations via `chat_message_join` and to contacts via the `handle` table.
The tool opens `chat.db` read-only, loads chats / participants / attachments,
loads messages (decoding `attributedBody` when `text` is empty), groups them by
conversation, and renders each with the selected exporter.

See [`docs/SCHEMA.md`](docs/SCHEMA.md) for the schema details and the timestamp /
`attributedBody` quirks handled here.

Conversations are exported one at a time (`export_database` streams chat-by-chat),
so peak memory stays bounded by the largest single conversation rather than the
whole database.

## Desktop app

A Qt 6 desktop GUI (`gui/`) wraps the same engine in a window: pick a source
(auto-detected Messages DB on a Mac, a database file, or a device backup),
choose format / output folder / date range / combined / attachment-copy /
contact-name options, and click **Export** — the work runs off the UI thread and
the log pane shows progress. It builds from the **one** C++ codebase for macOS,
Windows, and Linux.

A **Help** menu (How to get your data / online docs / About) explains, with
clickable links, where to obtain a `chat.db` and how to get your contacts. The
**Import iCloud Contacts…** button (handy on Windows/Linux, which have no local
Contacts database) fetches your contacts over CardDAV using an Apple ID and an
**app-specific password** (create one at
[account.apple.com](https://account.apple.com) → Sign-In and Security →
App-Specific Passwords) — your normal Apple ID password is not used, and there is
no raw Apple-ID/2FA login (Apple does not support that for third-party apps).

```bash
cmake -S . -B build            # detects Qt6 and adds the GUI target
cmake --build build --target imessage-exporter-gui
```

On macOS the build produces **`iMessage Exporter.app`** (double-clickable; drag
to `/Applications`). CI compiles the GUI on macOS, Windows, and Linux.

### Packaged downloads

The [`package`](.github/workflows/package.yml) workflow builds self-contained
installers for all three desktops. Run it from the Actions tab to just build the
artifacts, or push a `v*` tag to also publish them as a **GitHub Release**:

- **macOS** — `iMessage Exporter.app` inside a `.dmg` (Qt bundled via `macdeployqt`).
- **Windows** — an **Inno Setup** installer, `iMessage-Exporter-Setup.exe`, with
  Qt + SQLite bundled (`windeployqt`) and Start-menu / desktop shortcuts.
- **Linux** — a single-file `.AppImage`, plus a **`.deb`** (Debian/Ubuntu), an
  **`.rpm`** (Fedora/RHEL), and a **Snap** (`snap/snapcraft.yaml`).

The desktop app can **update itself**: Help → "Automatically check for updates"
(on by default) checks GitHub Releases on launch, quietly downloads the right
installer, and offers to install + restart. On Windows it runs the installer
silently; on a Linux AppImage it self-replaces and re-execs; on macOS and distro
packages (deb/rpm/snap) it opens the download / defers to your package manager.

These are **unsigned**: macOS Gatekeeper and Windows SmartScreen will warn until
code signing / notarization are configured (those need an Apple Developer ID and
a Windows signing certificate — credentials this repo doesn't hold). Grab the
latest builds from the [Releases](../../releases) page.

### Package managers

Packaging definitions live in [`packaging/`](packaging/):

- **Homebrew** (macOS/Linux) — a formula for the CLI and a cask for the GUI:

  ```bash
  brew tap grioghar/tap https://github.com/grioghar/homebrew-tap
  brew install grioghar/tap/imessage-exporter           # CLI
  brew install --cask grioghar/tap/imessage-exporter-app # GUI (.app; add --no-quarantine while unsigned)
  ```

- **Chocolatey** (Windows) — installs the Inno Setup build silently:

  ```powershell
  choco install imessage-exporter
  ```

Publishing requires a one-time setup: create a `grioghar/homebrew-tap` repo and
copy the two `.rb` files in (filling the release `sha256`); for Chocolatey,
`choco pack packaging/chocolatey/imessage-exporter.nuspec` then `choco push` with
your community API key (set the installer checksum first).

## Docker

A multi-stage [`Dockerfile`](Dockerfile) builds the Linux CLI (the image runs
the unit tests during the build). Bind-mount a directory containing your data;
results are written back into it for download:

```bash
docker build -t imessage-exporter .

# Mounted workflow — chat.db in $PWD, export written to ./export
docker run --rm -v "$PWD:/data" imessage-exporter \
    --db /data/chat.db --format html --output /data/export --contacts-db /data/contacts.vcf
```

Prefer copy-in / copy-out over a mount? Upload your files, run, then download
the results:

```bash
id=$(docker create imessage-exporter --db /data/chat.db --output /data/export --format html)
docker cp ./chat.db "$id:/data/chat.db"
docker start -a "$id"
docker cp "$id:/data/export" ./export
docker rm "$id"
```

The container reads only the files you provide (it has no access to a host
Messages database or Contacts), so it works the same on Linux, macOS, and
Windows hosts.

### Publishing the image

To push to **Docker Hub** by hand:

```bash
docker build -t YOUR_USER/imessage-exporter:0.1.0 -t YOUR_USER/imessage-exporter:latest .
docker login                                   # Docker Hub username + an access token
docker push YOUR_USER/imessage-exporter:0.1.0
docker push YOUR_USER/imessage-exporter:latest
```

(Create the access token at Docker Hub → Account Settings → Security.) The
[`docker-publish`](.github/workflows/docker-publish.yml) workflow does this
automatically on a `v*` tag, pushing to the GitHub Container Registry
(`ghcr.io/grioghar/imessage-exporter-redux`) with no extra secrets. To also push
to Docker Hub from CI, add `DOCKERHUB_USERNAME` / `DOCKERHUB_TOKEN` secrets and
uncomment the Docker Hub steps in that workflow.

## iPhone / iOS

The core is portable C++ and iOS ships `libsqlite3`, so the engine runs on iOS.
There's a pure-C bridge ([`include/imsg/imsg_bridge.h`](include/imsg/imsg_bridge.h))
and a SwiftPM package ([`Package.swift`](Package.swift)) for embedding it in an
app. Note that iOS sandboxing means an app **cannot read the live Messages
database** — the app instead imports a `chat.db` the user supplies. See
[`docs/IOS.md`](docs/IOS.md) for the full guide (constraints, build, and UI
wiring). Building an iOS app requires a Mac with Xcode.

## Disclaimer

This tool is for exporting **your own** message data. Respect the privacy of the
people you've communicated with and any applicable laws when handling exported
conversations.

## License

MIT — see [LICENSE](LICENSE).

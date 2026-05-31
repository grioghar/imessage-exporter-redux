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
  with `--copy-attachments`; otherwise only metadata is exported.
- **Contact-name resolution** so senders show names instead of phone numbers /
  emails — from the macOS Contacts (AddressBook) database (`--contacts`) or from
  a vCard `.vcf` file exported from iCloud.com (`--contacts-db contacts.vcf`).
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

The binary is produced at `build/imessage-exporter`.

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

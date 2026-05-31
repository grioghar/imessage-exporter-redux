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
- **Contact-name resolution** from the macOS Contacts (AddressBook) database, so
  senders show names instead of phone numbers / emails (`--contacts`).
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
```

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
| `--contacts-db PATH` | Resolve names via a specific AddressBook `.abcddb` file or directory. | — |
| `--list-chats` | List conversations and exit (no export). | — |
| `--version` | Print version and exit. | — |
| `--help` | Show help and exit. | — |

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

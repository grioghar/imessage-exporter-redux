# CLAUDE.md — imessage-exporter

Context for Claude Code (and humans) working in this repo. Read this first.

## What this is

A small, fast **C++17** command-line tool that exports macOS iMessage / SMS
history to portable formats (**TXT**, **JSON**, **HTML**). It reads the local
Messages SQLite database (`~/Library/Messages/chat.db`) **read-only** and links
the system `libsqlite3` directly — a lean native binary with no third-party
runtime dependencies.

- **Language:** C++17, CMake build.
- **License:** MIT (see `LICENSE`).
- **Status:** v0.1.0. Builds, all tests pass, verified end-to-end against a
  synthetic `chat.db`. Not yet run against a real macOS database — see
  "Known gaps".

### Origin / provenance

This is an **independent, clean-room C++ reimplementation** inspired by the Rust
project [ReagentX/imessage-exporter](https://github.com/ReagentX/imessage-exporter).
It shares **no code** with it. We chose C++ deliberately ("close to the metal":
native binary, direct SQLite C API, no interpreter) and MIT licensing
specifically to avoid the reference project's GPL-3.0.

This repo (`grioghar/imessage-exporter-redux`) was extracted from a subdirectory
of `grioghar/macos-rdp-server`, where it was originally developed on branch
`claude/imessage-exporter-library-t6NVE` (PR #2). The history was split out with
`git subtree split` so the project sits at the repo root here. The original
macos-rdp-server repo is an unrelated C/FreeRDP project — none of its code is here.

## Layout

```
.
├── include/imsg/        # public headers
│   ├── models.hpp       # Chat / Message / Attachment structs
│   ├── time_util.hpp    # Apple "Mac absolute time" conversion
│   ├── attributed_body.hpp
│   ├── contact_book.hpp # handle→name map (SQLite-free, in imsg_core)
│   ├── contacts.hpp     # AddressBook loader → ContactBook (SQLite)
│   ├── exporters.hpp    # txt / json / html renderers (+ combined)
│   ├── export_job.hpp   # ExportOptions + export_database()
│   └── database.hpp     # read-only chat.db reader (SQLite)
├── src/                 # one .cpp per header, + main.cpp (CLI)
├── tests/test_core.cpp  # dependency-free unit tests (33 assertions)
├── docs/SCHEMA.md       # database schema notes & quirks — READ THIS
├── CMakeLists.txt
├── README.md
└── LICENSE              # MIT
```

### Architecture: the core/SQLite split (important)

Tricky logic lives in a **SQLite-free core library** (`imsg_core`):
`models`, `time_util`, `attributed_body`, `exporters`. Only `database.cpp` and
`main.cpp` depend on SQLite. This is intentional — it lets the parsing/formatting
logic be unit-tested on **any** platform, even without `libsqlite3` headers.

CMake reflects this:
- `imsg_core` (static lib) + `imsg_tests` always build (no SQLite needed).
- The `imessage-exporter` binary only builds when `find_package(SQLite3)`
  succeeds. On macOS SQLite3 ships with the system; on Linux install
  `libsqlite3-dev`. If SQLite isn't found, CMake prints a warning and builds
  core + tests only.

**When adding logic, prefer putting it in the SQLite-free core so it stays
testable.** Don't pull SQLite into `exporters`/`models`/`time_util`.

## Build / test / run

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build          # runs core unit tests
```

Run (binary at `build/imessage-exporter`):

```bash
./build/imessage-exporter --format html --output ./export       # default DB
./build/imessage-exporter --db ./chat.db --format json --output ./export
./build/imessage-exporter --list-chats
```

### CLI options

| Flag | Description | Default |
| --- | --- | --- |
| `--db PATH` | Path to the Messages database. | `$HOME/Library/Messages/chat.db` |
| `--format FMT` | `txt`, `json`, or `html`. | `txt` |
| `--output DIR` | Output directory (one file per conversation). | `./imessage-export` |
| `--me NAME` | Label for messages you sent. | `Me` |
| `--since DATE` / `--until DATE` | Date-range filter (`YYYY-MM-DD[ HH:MM:SS]`); date-only `--until` is end-of-day. | — |
| `--combined` | One combined file instead of one per conversation. | — |
| `--copy-attachments` | Copy attachment files into `<output>/attachments` and link them. | — |
| `--contacts` / `--contacts-db PATH` | Resolve handles to names via the default / a specific AddressBook DB. | — |
| `--list-chats` | List conversations and exit. | — |
| `--version` / `--help` | Print and exit. | — |

On modern macOS the terminal needs **Full Disk Access** to read `chat.db`.

## The two quirks that matter (details in docs/SCHEMA.md)

1. **Timestamps** — `message.date` is an offset from **2001-01-01 UTC**, in
   **nanoseconds** on macOS 10.13+ and **seconds** on older DBs.
   `apple_time_to_epoch` (time_util) auto-detects by magnitude
   (threshold 1e11) and converts to Unix epoch seconds.

2. **`attributedBody`** — on modern macOS the plain `message.text` column is
   often `NULL` and the visible text lives in the `attributedBody` BLOB: an
   `NSAttributedString` serialized with the legacy NeXTSTEP **typedstream**
   (`streamtyped`) archiver. `decode_attributed_body` (attributed_body) extracts
   the length-prefixed UTF-8 payload after the `NSString`/`NSMutableString`
   class marker (a `0x81` length byte ⇒ following little-endian uint16 length),
   validates UTF-8, rejects control-char noise, and returns `""` on unexpected
   layouts so export never crashes. **This decoder is heuristic** — the most
   likely place to need hardening against a real-world `chat.db`.

The reader also adapts to schema differences across macOS versions via
`PRAGMA table_info(...)`, falling back to `NULL` for absent columns
(`attributedBody`, `chat.service_name`, `message.service`).

## Conventions

- C++17, `-Wall -Wextra` clean. Match the existing terse style.
- Comments explain *why*, not *what*; keep density similar to surrounding code.
- The DB is opened `mode=ro&immutable=1` — **never** add write paths to the
  live database.
- Tests are a tiny hand-rolled harness in `tests/test_core.cpp` (no framework);
  add assertions there. Keep them SQLite-free.

## Known gaps / good next tasks

- ⚠️ **Verify against a real macOS `chat.db`.** Only synthetic data has been
  exercised so far; the `attributedBody` decoder and the Contacts/attachment
  paths are the likely weak points. The Contacts schema (`ZABCDRECORD` /
  `ZABCDPHONENUMBER` / `ZABCDEMAILADDRESS`) and the phone-matching heuristic
  (last-10-digits) haven't been tried against real AddressBook data.

## Done since v0.1.0

- **CI** — `.github/workflows/ci.yml` builds + runs ctest on ubuntu and macos.
- **Streaming export** — the DB API is now `load_chat_index()` +
  `load_messages(Chat&)`, and `export_database()` (in `src/export_job.cpp`)
  writes one conversation at a time so peak memory is bounded by the largest
  single chat, not the whole DB. The old monolithic `load_chats()` is gone.
- **iOS bridge** — pure-C ABI in `include/imsg/imsg_bridge.h`
  (`src/imsg_bridge.cpp`), a `Package.swift` SwiftPM target, and `docs/IOS.md`.
  CMake builds an `imsg_mobile` static lib for embedding. See `docs/IOS.md`;
  note iOS apps cannot read the live Messages DB (sandbox) — they import a copy.
- **Security/robustness** — the `--db` path is percent-encoded before going into
  the SQLite `file:` URI; timestamp magnitude uses unsigned math (no
  `llabs(LLONG_MIN)` UB); the `attributedBody` decoder works over `string_view`.
- **Date-range filtering** — `--since` / `--until` (`parse_date` in time_util);
  filtering is applied in `load_messages` after Apple-time conversion, so it
  works uniformly for nanosecond and legacy-seconds databases.
- **Combined export** — `--combined` streams every conversation into one file via
  the `combined_prologue` / `combined_item` / `combined_epilogue` fragment API in
  exporters, preserving the one-chat-at-a-time memory bound.
- **Attachment copying** — `--copy-attachments` copies files into
  `<output>/attachments/<chat-slug>/` (dedup by source, unique dest names) and
  sets `Attachment::copied_path`; HTML embeds images / links others, txt+json
  reference the path. Logic lives in `export_job.cpp` (the only place with `fs`).
- **Contact-name resolution** — `ContactBook` (SQLite-free core) maps handles to
  names; `contacts.cpp` loads it from the macOS AddressBook (`*.abcddb`).
  `--contacts` uses the default location, `--contacts-db PATH` a specific one;
  `MessagesDatabase::set_contacts` resolves senders + participants. Phone keys
  match on the last 10 digits; emails case-insensitively.

  All four added options share an `ExportOptions` struct (export_job.hpp) rather
  than growing the `export_database` parameter list; the C ABI (`imsg_export`)
  is unchanged.

## Git

- Default branch in this repo: `main`.
- The original development PR (for reference/history) is
  `grioghar/macos-rdp-server` PR #2.

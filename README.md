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

## Desktop app quick start

1. Launch **iMessage Exporter** (`.app` on macOS, Start menu on Windows).
2. **Source** — leave on "Auto-detect on this Mac" if Messages is synced here,
   or pick a database file / device backup.
3. **Contacts** — connect iCloud or Google Contacts for name resolution, or skip
   (handles show as phone numbers / emails).
4. **Output** — choose a folder, a format, and optionally a theme.
5. Click **Export**. Progress appears in the status bar; the log pane shows
   detail. Pause or Stop at any time.

The app checks for updates automatically (Help → "Automatically check for
updates") and installs them with one click.

---

## CLI quick start

```bash
# Export every conversation as styled HTML
./imessage-exporter --format html --output ./export

# Plain text from a specific database
./imessage-exporter --db ./chat.db --format txt --output ./export

# Date range, with contact-name resolution and attachments copied in
./imessage-exporter --format html --since 2023-01-01 --until 2023-12-31 \
    --contacts --copy-attachments --output ./export

# From an iTunes/Finder backup (with the device's own contacts)
./imessage-exporter --list-backups
./imessage-exporter --backup latest --contacts --format html --output ./export

# Android-compatible XML (SMS Backup & Restore format)
./imessage-exporter --format android --output ./export

# Export with a statistics cover page and LCARS theme
./imessage-exporter --format html --theme lcars --stats --output ./export

# List conversations without exporting
./imessage-exporter --list-chats
```

### All CLI options

| Flag | Description | Default |
|---|---|---|
| `--db PATH` | Path to the Messages database. | `~/Library/Messages/chat.db` |
| `--format FMT` | `txt`, `json`, `html`, `pdf`, `android`. | `txt` |
| `--output DIR` | Directory to write export files into. | `./imessage-export` |
| `--me NAME` | Label used for messages you sent. | `Me` |
| `--since DATE` | Only messages on/after `DATE` (`YYYY-MM-DD[ HH:MM:SS]`). | — |
| `--until DATE` | Only messages on/before `DATE`; a date alone means end-of-day. | — |
| `--combined` | One combined file instead of one per conversation. | — |
| `--theme NAME` | HTML/PDF theme: `ios`, `lcars`, `matrix`, `dot-matrix`, `atari`. | `ios` |
| `--stats` | Also write a `00-statistics.html` cover page. | — |
| `--copy-attachments` | Copy attachment files into `<output>/attachments/` and link them. | — |
| `--embed-attachments` | Inline attachments as base64 data URIs (self-contained HTML). | — |
| `--contacts` | Resolve names via the default macOS Contacts database. | — |
| `--contacts-db PATH` | Resolve names via a `.abcddb`, `.vcf`, or directory of them. | — |
| `--backup SPEC` | Source from a backup: a path, UDID, or `latest`. Unencrypted only. | — |
| `--list-backups` | List discovered device backups and exit. | — |
| `--list-chats` | List conversations and exit (no export). | — |
| `--log-level LVL` | `error`, `warn`, `info`, `debug` (or env `IMSG_LOG_LEVEL`). | `warn` |
| `-v` / `-vv` | Shortcuts for `info` / `debug` log level. | — |
| `--version` | Print version and exit. | — |
| `--help` | Show help and exit. | — |

---

## Where the messages come from

iMessage is end-to-end encrypted and Apple has no export API, so the tool
reads an existing database rather than talking to iCloud.

**Option 1 — Mac's local database (default).** If your Mac has Messages signed in
with iCloud sync enabled, your full history lives at
`~/Library/Messages/chat.db`. The CLI needs **Full Disk Access**
(System Settings → Privacy & Security → Full Disk Access). The desktop app
guides you through granting it.

**Option 2 — Device backup.** Make a local **unencrypted** backup in Finder or
iTunes (turn off "Encrypt local backup" first). Then use `--backup latest` (or
pick the backup in the app). Contacts from the device are extracted automatically
when `--contacts` is set.

---

## Export formats

| Format | What you get |
|---|---|
| **HTML** | One styled `.html` per conversation (or `--combined`). iOS-style bubbles, contact photos, inline media, YouTube / Spotify embeds, Open Graph cards, 5 themes. |
| **PDF** | Same as HTML — rendered to PDF with page-break-safe layout (images never split). |
| **JSON** | Structured data: every message, attachment path, and participant with full metadata. |
| **TXT** | Plain-text transcript, one file per conversation. |
| **Android XML** | [SMS Backup & Restore](https://synctech.com.au/sms-backup-restore/) format — import directly onto an Android device. |

---

## HTML themes

Choose with `--theme NAME` (CLI) or the theme menu in the desktop app:

| Theme | Description |
|---|---|
| `ios` | Clean, familiar iOS Messages look (default). |
| `lcars` | Star Trek LCARS interface palette. |
| `matrix` | Green-on-black terminal aesthetic. |
| `dot-matrix` | Retro dot-matrix printer output. |
| `atari` | ATARI 8-bit colour scheme. |

Adding a new theme is a single CSS file — no engine changes needed.

---

## Contacts & cloud

### iCloud Contacts
Click **Import iCloud Contacts** in the desktop app and enter your Apple ID
app-specific password (create one at [account.apple.com](https://account.apple.com)
→ Sign-In and Security → App-Specific Passwords). Your normal Apple password is
never used. Contacts are fetched via CardDAV and saved locally for offline use.

### Google Contacts & Google Drive
See [**docs/GOOGLE.md**](docs/GOOGLE.md) for a step-by-step guide with direct
links to each Google Cloud Console page. Once set up:

- **Google Contacts** resolves names from your Google address book.
- **Google Drive** uploads the finished export folder automatically after each run.

Both credentials are stored encrypted using the platform keychain (macOS
Keychain, Windows Credential Manager, Linux Secret Service).

---

## Statistics cover page

Pass `--stats` (CLI) or check the **Statistics** box in the desktop app to get a
standalone `00-statistics.html` alongside your export:

- Messages by **hour of day** and **day of week** (CSS bar charts).
- **Top texters** ranked by volume.
- Date range, total counts, sent vs. received, attachment tallies.
- Playful **"Fun facts"** — longest message, most-used emoji, busiest day, and more.

---

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

---

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

---

## iOS / embedding

A pure-C bridge ([`include/imsg/imsg_bridge.h`](include/imsg/imsg_bridge.h)) and
a SwiftPM package ([`Package.swift`](Package.swift)) let you embed the engine in
any app. An example SwiftUI iOS app lives in [`ios/`](ios/).

> iOS sandboxing prevents reading the live Messages database. The app must import
> a `chat.db` the user supplies (e.g. from a backup). See
> [**docs/IOS.md**](docs/IOS.md) for details.

---

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
├── tests/test_core.cpp    # 142 dependency-free unit tests
├── packaging/             # Homebrew, Chocolatey, Inno Setup, Snap, .desktop
├── docs/                  # SCHEMA.md, GOOGLE.md, IOS.md
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

---

## Two technical quirks worth knowing

**1. Timestamps.** `message.date` is seconds since 2001-01-01 UTC — but
nanoseconds on macOS 10.13+. The engine auto-detects by magnitude (≥ 10¹¹ →
nanoseconds) and converts correctly for both old and new databases.

**2. `attributedBody`.** Modern macOS frequently leaves `message.text` as `NULL`
and stores the visible text in an `attributedBody` BLOB — an `NSAttributedString`
serialized with the legacy NeXTSTEP typedstream format. The engine decodes the
UTF-8 payload from the typedstream, validates it, strips invisible Unicode
control characters, and falls back gracefully on unexpected layouts.

See [`docs/SCHEMA.md`](docs/SCHEMA.md) for the full schema reference.

---

## Roadmap

The 0.6.0-0.6.1 cycle shipped themes, statistics, inactive-contact filtering,
per-conversation stats, an interactive timeline page, SMS/RCS green-text style,
and the granular Statistics and Timeline preferences tabs. Below is the planned
scope for 0.7.0:

- **AI relationship analysis engine.** Analyse the patterns and dynamics within
  conversations — sentiment arcs, topic clustering, response-time graphs, dominant
  speakers, relationship strength over time, and statistical correlations between
  contacts and time/date/context. Produces a rich analytical report alongside the
  export.
- **AI service OAuth + message summarisation.** An OAuth sign-in flow for major
  AI providers (OpenAI/ChatGPT, Anthropic/Claude, Google Gemini, and others).
  Once authenticated, selected conversations or date ranges can be submitted for
  AI-generated summary, sentiment analysis, or Q&A. Credentials are stored
  encrypted in the platform keychain alongside existing Google credentials.
- **Reply engine (HTML → iMessage).** A way to reply to conversations directly
  from the exported HTML page. Each message bubble gains a reply affordance that
  opens a compose area; sending routes through the macOS Messages URL scheme
  (or AppleScript on macOS) to deliver the reply via iMessage without leaving
  the export.
- **Low-footprint backup streaming.** For large archives (hundreds of GB on an
  external drive or iPhone backup), process and export one conversation at a time
  directly from the backup without ever extracting the full `sms.db` to the local
  machine. The export engine already streams one conversation at a time in memory;
  this extends that to the source side — read a conversation's rows from the
  backup's SQLite blob, write the HTML, move on. Lets you export a 250 GB message
  history to an external drive without the laptop ever holding more than one
  conversation's worth of data.
- **Location context on the timeline.** Correlate message timestamps with location
  records from three sources the tool can already reach: the macOS Photos library
  (Photos.sqlite, same Full Disk Access permission already required), the
  com.apple.routined/Local.sqlite significant-location database inside an iPhone
  backup (which the tool already parses), and a Google Takeout Records.json export
  (parseable with the Google OAuth infrastructure already built). The result: each
  message on the timeline can optionally show where you were when you sent it.
  Waze and the real-time Google Maps Timeline API have no public endpoint; the
  Takeout JSON is the only Google export path.
- **Media compression and cloud offload** (via [grioghar/lightpress](https://github.com/grioghar/lightpress)).
  A new sister library — `lightpress` — provides a zero-dependency C++17 JPEG/PNG
  encoder, EXIF metadata stripper, bilinear/bicubic resize, and MP4/MOV container
  rewriter that removes thumbnail tracks and cover-art atoms without re-encoding the
  video bitstream. Its benchmark suite compares output size and SSIM quality against
  ffmpeg baselines on CI. Integration into the exporter gives two compression modes:
  (1) *local transcode* — re-encode images at a configurable quality level and strip
  all EXIF metadata, typically achieving 30–60% size reduction; strip MP4 metadata
  atoms for immediate gains without touching the video bitstream; for full video
  transcoding, optionally shell out to `ffmpeg` when present; (2) *cloud offload* —
  upload attachments to Google Drive (using the existing Drive connector) and rewrite
  `src=` paths to Drive URLs, keeping the HTML self-contained and lightweight.
  Both modes are opt-in and combinable.
- **Encryption-at-rest with self-decrypting exports.** Protect sensitive exports
  with a password. The implementation uses AES-256-GCM with a PBKDF2-derived key:
  for HTML exports the ciphertext is embedded directly in the file alongside an
  inline Web Crypto API decryption loop — open in any modern browser, enter your
  key, it decrypts in memory and renders, with no server, plugin, or special app
  required. JSON and TXT exports write a standard encrypted container. Keys are
  never stored; all cryptography runs in the browser or in the native binary.
- **Conversation replay / message player.** A "play back this conversation" mode
  embedded in each exported HTML file: messages appear one at a time in
  chronological order as animated bubbles, with a speed control (1× to 20×) and a
  scrubber to jump to any point. If location data is available (from the location
  context feature), a mini-map in the corner pans and zooms to where each party
  was when they sent their message. Pure HTML/CSS/JS, no server required.
- **Extended statistics — unique, unexpected, and fun.** A greatly expanded stats
  engine covering relationships and patterns that no other export tool surfaces:
  - *Conversation dynamics:* who initiates more often; who ends conversations; the
    "ghost ratio" (how often one side stops replying); double-texter frequency;
    fastest-ever response time (to the second); average response time by hour of day.
  - *Language fingerprints:* word frequency and phrase clouds per person; emoji
    personality profile (top 10 emoji per sender); exclamation-point density;
    question-asker vs answer-giver ratio; longest message ever sent ("The Novel");
    total word count across all history ("N books' worth of conversation").
  - *Temporal oddities:* night-owl index (% of messages sent midnight–4 am);
    busiest single minute ever; the exact timestamp of the very first message to
    each person; "comeback conversations" (threads silent for 6+ months that
    restarted); seasonal activity — which month of the year you text most.
  - *Relationship arcs:* a heat-map of message frequency over the years per contact
    (fading friendships and growing ones visible at a glance); "dormant contacts"
    (daily texters who went quiet); who you've known the longest based on first
    message date.
  - *Silly but accurate:* how many times you sent "on my way" (and to whom); "I
    love you" count per relationship; most-used swear word (shown with partial
    censor); longest unbroken chain of single-word replies; the contact most likely
    to respond with just "ok"; your personal peak texting hour; and a "phone
    addict score" (% of replies sent within 60 seconds of receiving).

Suggestions and votes welcome — open an issue or add a 👍 to an existing one.

---

## Disclaimer

This tool is for exporting **your own** message data. Respect the privacy of the
people you've communicated with and any applicable laws when handling exported
conversations.

---

## License

MIT — see [LICENSE](LICENSE).

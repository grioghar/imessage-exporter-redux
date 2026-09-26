# User guide

Everything about using iMessage Exporter day to day: the desktop app, where
the messages and contacts come from, the export formats, themes and the
statistics cover page. CLI flags are listed in [CLI.md](CLI.md).

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

## Export formats

| Format | What you get |
|---|---|
| **HTML** | One styled `.html` per conversation (or `--combined`). iOS-style bubbles, contact photos, inline media, YouTube / Spotify embeds, Open Graph cards, 5 themes. |
| **PDF** | Same as HTML — rendered to PDF with page-break-safe layout (images never split). |
| **JSON** | Structured data: every message, attachment path, and participant with full metadata. |
| **TXT** | Plain-text transcript, one file per conversation. |
| **Android XML** | [SMS Backup & Restore](https://synctech.com.au/sms-backup-restore/) format — import directly onto an Android device. |

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

## Contacts & cloud

### iCloud Contacts
Click **Import iCloud Contacts** in the desktop app and enter your Apple ID
app-specific password (create one at [account.apple.com](https://account.apple.com)
→ Sign-In and Security → App-Specific Passwords). Your normal Apple password is
never used. Contacts are fetched via CardDAV and saved locally for offline use.

### Google Contacts & Google Drive
See [**GOOGLE.md**](GOOGLE.md) for a step-by-step guide with direct
links to each Google Cloud Console page. Once set up:

- **Google Contacts** resolves names from your Google address book.
- **Google Drive** uploads the finished export folder automatically after each run.

Both credentials are stored encrypted using the platform keychain (macOS
Keychain, Windows Credential Manager, Linux Secret Service).

## Statistics cover page

Pass `--stats` (CLI) or check the **Statistics** box in the desktop app to get a
standalone `00-statistics.html` alongside your export:

- Messages by **hour of day** and **day of week** (CSS bar charts).
- **Top texters** ranked by volume.
- Date range, total counts, sent vs. received, attachment tallies.
- Playful **"Fun facts"** — longest message, most-used emoji, busiest day, and more.

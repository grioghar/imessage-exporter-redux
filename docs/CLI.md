# Command-line usage

The `imessage-exporter` CLI is the reference front-end for the export engine.
Build it or install it as described in [BUILDING.md](BUILDING.md); the
desktop app exposes the same options through its UI (see
[USER-GUIDE.md](USER-GUIDE.md)).

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

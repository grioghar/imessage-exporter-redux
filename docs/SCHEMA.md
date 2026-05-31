# Notes on the macOS Messages database

The Messages app stores data in a SQLite database at
`~/Library/Messages/chat.db`. This documents the parts this tool relies on and
the quirks it handles.

## Relevant tables

| Table | Purpose |
| --- | --- |
| `message` | One row per message. Key columns below. |
| `handle` | Maps a `ROWID` to a contact address (`id` = phone/email). |
| `chat` | One row per conversation (1:1 or group). |
| `chat_message_join` | Links `chat.ROWID` ↔ `message.ROWID`. |
| `chat_handle_join` | Links `chat.ROWID` ↔ `handle.ROWID` (participants). |
| `attachment` | Attachment file metadata. |
| `message_attachment_join` | Links `message.ROWID` ↔ `attachment.ROWID`. |

### Important `message` columns

- `text` — plain text body. **Often `NULL` on modern macOS.**
- `attributedBody` — `BLOB` containing the styled text when `text` is `NULL`.
- `date`, `date_read` — Apple "Mac absolute time" timestamps.
- `is_from_me` — `1` if you sent the message, else `0`.
- `handle_id` — FK into `handle` for the other party.
- `service` — `iMessage` or `SMS`.

## Quirk 1: timestamps

`date` is an offset from **2001-01-01 00:00:00 UTC**:

- **nanoseconds** on macOS 10.13+ (values ~1e17 today)
- **seconds** on older databases

`apple_time_to_epoch` (in `time_util.hpp`) detects which by magnitude, converts
to Unix epoch seconds, and `format_timestamp` renders local time.

## Quirk 2: `attributedBody`

When `text` is `NULL`, the visible text lives in `attributedBody` as an
`NSAttributedString` serialized with the legacy NeXTSTEP **typedstream**
(`streamtyped`) archiver. The string payload follows an `NSString` /
`NSMutableString` class marker as a length-prefixed UTF-8 run (a `0x81` length
byte signals a following little-endian `uint16` length).

`decode_attributed_body` (in `attributed_body.hpp`) extracts that payload
heuristically and returns `""` when the layout is unexpected, so export never
crashes on an unusual blob. It validates UTF-8 and rejects control-character
noise to avoid emitting archiver metadata as text.

## Read-only access

The database is opened with `mode=ro&immutable=1` so the live Messages data is
never modified or locked. On modern macOS the running terminal needs **Full Disk
Access** to read `chat.db`.

## Contacts (AddressBook) — optional name resolution

With `--contacts` / `--contacts-db`, handles (phone numbers / emails) are
resolved to display names from the macOS Contacts store, separate SQLite
databases under:

```
~/Library/Application Support/AddressBook/        # scanned recursively for *.abcddb
~/Library/Application Support/AddressBook/Sources/<UUID>/AddressBook-v22.abcddb
```

Relevant tables (`load_contacts` in `contacts.cpp`):

| Table | Columns used |
| --- | --- |
| `ZABCDRECORD` | `Z_PK`, `ZFIRSTNAME`, `ZLASTNAME`, `ZORGANIZATION` |
| `ZABCDPHONENUMBER` | `ZFULLNUMBER`, `ZOWNER` → `ZABCDRECORD.Z_PK` |
| `ZABCDEMAILADDRESS` | `ZADDRESS`, `ZOWNER` → `ZABCDRECORD.Z_PK` |

The display name is `first + last`, falling back to the organization. Matching
is heuristic (`ContactBook::key_for`): emails compare case-insensitively; phone
numbers are reduced to digits and keyed on their **last 10 digits**, so
formatting and country-code differences between Messages and Contacts collapse
together. Unreadable/foreign databases are skipped — a miss just falls back to
the raw handle, and these `.abcddb` files are opened `mode=ro&immutable=1` too.

## Schema differences across versions

Column availability varies between macOS releases (e.g. `attributedBody` and
`chat.service_name` are not present on very old databases). The reader inspects
`PRAGMA table_info(...)` and adapts its queries, falling back to `NULL` for
columns that are absent.

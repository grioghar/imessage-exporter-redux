#!/usr/bin/env python3
"""Builds the synthetic tests/fixtures/chat.db used by the golden-file tests.

Everything in here is fake: two made-up handles, three conversations, a dozen
short messages dated in 2024, and one attachment row whose file does not exist
(so exports show attachment metadata only). The schema mirrors the parts of a
macOS Messages database the exporter reads (see docs/SCHEMA.md); columns the
tool never touches are left out to keep the file tiny.

Usage:
    python3 tests/fixtures/make_chat_db.py [tests/fixtures/chat.db]

The committed chat.db is the source of truth for the golden files; rerun this
script only when the fixture itself needs to change, then regenerate the
goldens with `tests/run_golden.sh --update`.
"""
import datetime as dt
import os
import sqlite3
import sys

# Apple "Mac absolute time": nanoseconds since 2001-01-01 00:00:00 UTC on
# modern macOS. The exporter auto-detects nanoseconds by magnitude.
APPLE_EPOCH = dt.datetime(2001, 1, 1, tzinfo=dt.timezone.utc)


def apple_ns(y, mo, d, h=0, mi=0, s=0):
    when = dt.datetime(y, mo, d, h, mi, s, tzinfo=dt.timezone.utc)
    return int((when - APPLE_EPOCH).total_seconds()) * 1_000_000_000


def apple_seconds(y, mo, d, h=0, mi=0, s=0):
    """Legacy (pre-10.13) seconds-resolution timestamp, to exercise autodetect."""
    when = dt.datetime(y, mo, d, h, mi, s, tzinfo=dt.timezone.utc)
    return int((when - APPLE_EPOCH).total_seconds())


def attributed_body(text):
    """A minimal NeXTSTEP typedstream NSAttributedString archive.

    Layout copied from what Messages writes: streamtyped header, class chain
    NSAttributedString -> NSObject, then the NSString payload as a
    length-prefixed UTF-8 run after the '+' type byte, then the attribute-run
    NSDictionary the decoder is expected to ignore.
    """
    payload = text.encode("utf-8")
    if len(payload) < 0x80:
        length = bytes([len(payload)])
    else:
        length = b"\x81" + len(payload).to_bytes(2, "little")
    return (
        b"\x04\x0bstreamtyped\x81\xe8\x03\x84\x01@\x84\x84\x84\x12NSAttributedString\x00"
        b"\x84\x84\x08NSObject\x00\x85\x92\x84\x84\x84\x08NSString\x01\x94\x84\x01+"
        + length
        + payload
        + b"\x86\x84\x02iI\x01"
        + bytes([len(payload)])
        + b"\x92\x84\x84\x84\x0cNSDictionary\x00\x94\x84\x01i\x01\x92\x84\x96\x97"
        b"\x1d__kIMMessagePartAttributeName\x86\x92\x84\x84\x84\x08NSNumber\x00"
        b"\x84\x84\x07NSValue\x00\x94\x84\x01*\x84\x99\x99\x00\x86\x86\x86"
    )


SCHEMA = """
CREATE TABLE handle (
    ROWID INTEGER PRIMARY KEY AUTOINCREMENT,
    id TEXT NOT NULL,
    country TEXT,
    service TEXT NOT NULL,
    uncanonicalized_id TEXT,
    UNIQUE (id, service)
);
CREATE TABLE chat (
    ROWID INTEGER PRIMARY KEY AUTOINCREMENT,
    guid TEXT UNIQUE NOT NULL,
    style INTEGER,
    state INTEGER,
    account_id TEXT,
    chat_identifier TEXT,
    service_name TEXT,
    room_name TEXT,
    display_name TEXT
);
CREATE TABLE message (
    ROWID INTEGER PRIMARY KEY AUTOINCREMENT,
    guid TEXT UNIQUE NOT NULL,
    text TEXT,
    attributedBody BLOB,
    handle_id INTEGER DEFAULT 0,
    service TEXT,
    date INTEGER,
    date_read INTEGER,
    date_delivered INTEGER,
    is_from_me INTEGER DEFAULT 0,
    is_read INTEGER DEFAULT 0,
    cache_has_attachments INTEGER DEFAULT 0
);
CREATE TABLE chat_message_join (
    chat_id INTEGER REFERENCES chat (ROWID),
    message_id INTEGER REFERENCES message (ROWID),
    message_date INTEGER DEFAULT 0,
    PRIMARY KEY (chat_id, message_id)
);
CREATE TABLE chat_handle_join (
    chat_id INTEGER REFERENCES chat (ROWID),
    handle_id INTEGER REFERENCES handle (ROWID),
    UNIQUE (chat_id, handle_id)
);
CREATE TABLE attachment (
    ROWID INTEGER PRIMARY KEY AUTOINCREMENT,
    guid TEXT UNIQUE NOT NULL,
    created_date INTEGER DEFAULT 0,
    filename TEXT,
    uti TEXT,
    mime_type TEXT,
    transfer_name TEXT,
    total_bytes INTEGER DEFAULT 0
);
CREATE TABLE message_attachment_join (
    message_id INTEGER REFERENCES message (ROWID),
    attachment_id INTEGER REFERENCES attachment (ROWID),
    UNIQUE (message_id, attachment_id)
);
"""

HANDLES = [
    # ROWID, id, service
    (1, "+15550100001", "iMessage"),
    (2, "test.bob@example.com", "iMessage"),
    (3, "+15550100002", "SMS"),
]

CHATS = [
    # ROWID, guid, style (45 = 1:1, 43 = group), chat_identifier, service_name, display_name
    (1, "iMessage;-;+15550100001", 45, "+15550100001", "iMessage", None),
    (2, "SMS;-;+15550100002", 45, "+15550100002", "SMS", None),
    (3, "iMessage;+;chat000000000000000001", 43, "chat000000000000000001", "iMessage",
     "Test Group"),
]

CHAT_HANDLES = [(1, 1), (2, 3), (3, 1), (3, 2)]

# (ROWID, chat, guid, text, attributedBody, handle_id, service, date, date_read, is_from_me)
NS = apple_ns
MESSAGES = [
    (1, 1, "FAKE-0001", "hello from test", None, 1, "iMessage",
     NS(2024, 1, 15, 9, 30, 0), NS(2024, 1, 15, 9, 31, 5), 0),
    (2, 1, "FAKE-0002", "hi there, this is a reply from me", None, 0, "iMessage",
     NS(2024, 1, 15, 9, 32, 10), 0, 1),
    # text NULL, body only in attributedBody (the modern macOS layout)
    (3, 1, "FAKE-0003", None, attributed_body("this text lives in attributedBody"), 1,
     "iMessage", NS(2024, 1, 15, 9, 33, 0), 0, 0),
    # attachment-only message (no text at all)
    (4, 1, "FAKE-0004", None, None, 0, "iMessage",
     NS(2024, 2, 1, 18, 0, 0), 0, 1),
    (5, 1, "FAKE-0005", "multi\nline\nmessage with \"quotes\" & <angles>", None, 1,
     "iMessage", NS(2024, 3, 3, 12, 0, 0), 0, 0),
    (6, 1, "FAKE-0006", "a link: https://example.com/test-page", None, 0, "iMessage",
     NS(2024, 3, 3, 12, 5, 0), 0, 1),
    (7, 2, "FAKE-0007", "sms hello from test", None, 3, "SMS",
     NS(2024, 4, 10, 8, 0, 0), 0, 0),
    (8, 2, "FAKE-0008", "sms reply from me", None, 0, "SMS",
     NS(2024, 4, 10, 8, 1, 0), 0, 1),
    # legacy seconds-resolution timestamp, to exercise the magnitude autodetect
    (9, 2, "FAKE-0009", "old-style seconds timestamp", None, 3, "SMS",
     apple_seconds(2024, 4, 9, 8, 0, 0), 0, 0),
    (10, 3, "FAKE-0010", "group hello from test", None, 1, "iMessage",
     NS(2024, 5, 20, 20, 0, 0), 0, 0),
    (11, 3, "FAKE-0011", "group reply from bob", None, 2, "iMessage",
     NS(2024, 5, 20, 20, 1, 0), 0, 0),
    (12, 3, "FAKE-0012", "group reply from me with emoji \U0001F44B", None, 0, "iMessage",
     NS(2024, 5, 20, 20, 2, 0), 0, 1),
]

ATTACHMENTS = [
    # ROWID, guid, filename (deliberately nonexistent), uti, mime_type, transfer_name, total_bytes
    (1, "FAKE-ATT-0001", "~/Library/Messages/Attachments/00/00/FAKE/test-photo.jpg",
     "public.jpeg", "image/jpeg", "test-photo.jpg", 12345),
]
MESSAGE_ATTACHMENTS = [(4, 1)]


def build(path):
    if os.path.exists(path):
        os.remove(path)
    db = sqlite3.connect(path)
    db.execute("PRAGMA page_size = 1024")  # keep the committed file small
    db.executescript(SCHEMA)
    db.executemany(
        "INSERT INTO handle (ROWID, id, service) VALUES (?, ?, ?)", HANDLES)
    db.executemany(
        "INSERT INTO chat (ROWID, guid, style, chat_identifier, service_name, "
        "display_name) VALUES (?, ?, ?, ?, ?, ?)", CHATS)
    db.executemany(
        "INSERT INTO chat_handle_join (chat_id, handle_id) VALUES (?, ?)", CHAT_HANDLES)
    for (rowid, chat, guid, text, body, handle, service, date, date_read,
         from_me) in MESSAGES:
        db.execute(
            "INSERT INTO message (ROWID, guid, text, attributedBody, handle_id, "
            "service, date, date_read, is_from_me, cache_has_attachments) "
            "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)",
            (rowid, guid, text, body, handle, service, date, date_read, from_me,
             1 if any(m == rowid for m, _ in MESSAGE_ATTACHMENTS) else 0))
        db.execute(
            "INSERT INTO chat_message_join (chat_id, message_id, message_date) "
            "VALUES (?, ?, ?)", (chat, rowid, date))
    db.executemany(
        "INSERT INTO attachment (ROWID, guid, filename, uti, mime_type, "
        "transfer_name, total_bytes) VALUES (?, ?, ?, ?, ?, ?, ?)", ATTACHMENTS)
    db.executemany(
        "INSERT INTO message_attachment_join (message_id, attachment_id) VALUES (?, ?)",
        MESSAGE_ATTACHMENTS)
    db.commit()
    db.execute("VACUUM")
    db.close()


if __name__ == "__main__":
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(
        os.path.dirname(os.path.abspath(__file__)), "chat.db")
    build(out)
    print(f"wrote {out} ({os.path.getsize(out)} bytes)")

// Minimal vCard (.vcf) parser for resolving handles to names. SQLite-free so it
// can be unit-tested anywhere; the file/dir plumbing lives in `contacts.hpp`.
#pragma once

#include <string>

#include "imsg/contact_book.hpp"

namespace imsg {

// Parses one or more vCard records from `text` (as exported by iCloud.com ->
// Contacts -> Export vCard) and adds each contact's phone numbers and email
// addresses to `book`, keyed to a display name (FN, else the N name, else ORG).
//
// Handles vCard 3.0/4.0 essentials: multiple BEGIN/END:VCARD blocks, CRLF or LF
// line endings, line folding (continuation lines starting with space/tab),
// property parameters (TEL;TYPE=CELL:...), and item-group prefixes
// (item1.TEL:...). Quoted-printable encoding is not decoded.
void parse_vcards(const std::string& text, ContactBook& book);

}  // namespace imsg

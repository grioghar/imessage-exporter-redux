// Loads macOS Contacts (AddressBook) data into a ContactBook for resolving
// phone numbers / emails to display names. SQLite-backed (part of imsg_db).
#pragma once

#include <string>

#include "imsg/contact_book.hpp"

namespace imsg {

// Builds a ContactBook from an AddressBook source. `path` may be a single
// `.abcddb` SQLite file or a directory, which is scanned recursively for
// `*.abcddb` files (a Mac typically has several "Sources"). Unreadable or
// malformed databases are skipped, so the result is best-effort and never
// throws — a contact lookup that misses just falls back to the raw handle.
ContactBook load_contacts(const std::string& path);

// Builds a ContactBook from the current user's default AddressBook location
// ($HOME/Library/Application Support/AddressBook). Returns an empty book when
// that directory does not exist (e.g. off macOS).
ContactBook load_contacts_default();

}  // namespace imsg

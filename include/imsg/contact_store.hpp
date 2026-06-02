// Persistent contacts cache: a plain SQLite database that survives version
// updates, holding handle->name rows (e.g. downloaded from Google Contacts) so
// names resolve on later runs without re-fetching. SQLite-backed (imsg_db).
//
// Per the project's storage decision this DB is NOT encrypted (contacts are
// lower-risk); sensitive OAuth tokens live in the OS keychain (see the GUI's
// secret store), not here.
#pragma once

#include <string>

#include "imsg/contact_book.hpp"

namespace imsg {

// Default per-user location of the persistent contacts DB
// (e.g. ~/.local/share, ~/Library/Application Support, or %APPDATA%).
std::string default_contact_store_path();

class ContactStore {
   public:
    explicit ContactStore(std::string path);
    ~ContactStore();
    ContactStore(const ContactStore&) = delete;
    ContactStore& operator=(const ContactStore&) = delete;

    // Opens (creating the file + schema if needed). Returns false on failure.
    bool open();
    void close();

    // Inserts or updates one handle's name + source tag, and optionally a photo
    // (a "data:..." URI or an https photo URL). An empty photo won't clobber an
    // existing one.
    void upsert(const std::string& handle, const std::string& name,
                const std::string& source, const std::string& photo = std::string());

    // Number of stored handles.
    int count() const;

    // Builds a ContactBook from every stored row.
    ContactBook load() const;

    // Adds every stored row into an existing book (for merging sources).
    void load_into(ContactBook& book) const;

   private:
    std::string path_;
    void* db_ = nullptr;  // sqlite3*
};

}  // namespace imsg

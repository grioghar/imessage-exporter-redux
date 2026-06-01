#include "imsg/contact_store.hpp"

#include <sqlite3.h>

#include <cstdlib>
#include <filesystem>

#include "imsg/log.hpp"

namespace fs = std::filesystem;

namespace imsg {
namespace {

sqlite3* as_db(void* p) { return static_cast<sqlite3*>(p); }

std::string env(const char* k) {
    const char* v = std::getenv(k);
    return v ? std::string(v) : std::string();
}

std::string column_text(sqlite3_stmt* stmt, int col) {
    const unsigned char* t = sqlite3_column_text(stmt, col);
    return t ? std::string(reinterpret_cast<const char*>(t)) : std::string();
}

}  // namespace

std::string default_contact_store_path() {
    std::string base;
#if defined(_WIN32)
    base = env("APPDATA");
#elif defined(__APPLE__)
    std::string home = env("HOME");
    if (!home.empty()) base = home + "/Library/Application Support";
#else
    base = env("XDG_DATA_HOME");
    if (base.empty()) {
        std::string home = env("HOME");
        if (!home.empty()) base = home + "/.local/share";
    }
#endif
    if (base.empty()) base = ".";
    return (fs::path(base) / "imessage-exporter" / "contacts.db").string();
}

ContactStore::ContactStore(std::string path) : path_(std::move(path)) {}
ContactStore::~ContactStore() { close(); }

bool ContactStore::open() {
    std::error_code ec;
    fs::create_directories(fs::path(path_).parent_path(), ec);
    sqlite3* db = nullptr;
    if (sqlite3_open_v2(path_.c_str(), &db,
                        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) !=
        SQLITE_OK) {
        log_warn("contact store: cannot open " + path_ +
                 (db ? std::string(": ") + sqlite3_errmsg(db) : ""));
        sqlite3_close(db);
        return false;
    }
    const char* schema =
        "CREATE TABLE IF NOT EXISTS contacts ("
        "handle TEXT PRIMARY KEY, name TEXT NOT NULL, source TEXT, updated_at INTEGER)";
    char* err = nullptr;
    if (sqlite3_exec(db, schema, nullptr, nullptr, &err) != SQLITE_OK) {
        log_warn(std::string("contact store: schema: ") + (err ? err : "?"));
        sqlite3_free(err);
        sqlite3_close(db);
        return false;
    }
    db_ = db;
    return true;
}

void ContactStore::close() {
    if (db_) {
        sqlite3_close(as_db(db_));
        db_ = nullptr;
    }
}

void ContactStore::upsert(const std::string& handle, const std::string& name,
                          const std::string& source) {
    if (!db_ || handle.empty() || name.empty()) return;
    const char* sql =
        "INSERT INTO contacts(handle, name, source, updated_at) "
        "VALUES(?,?,?,strftime('%s','now')) "
        "ON CONFLICT(handle) DO UPDATE SET name=excluded.name, "
        "source=excluded.source, updated_at=excluded.updated_at";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(as_db(db_), sql, -1, &stmt, nullptr) != SQLITE_OK) return;
    sqlite3_bind_text(stmt, 1, handle.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, source.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

int ContactStore::count() const {
    if (!db_) return 0;
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(as_db(db_), "SELECT COUNT(*) FROM contacts", -1, &stmt,
                           nullptr) != SQLITE_OK)
        return 0;
    int n = (sqlite3_step(stmt) == SQLITE_ROW) ? sqlite3_column_int(stmt, 0) : 0;
    sqlite3_finalize(stmt);
    return n;
}

ContactBook ContactStore::load() const {
    ContactBook book;
    load_into(book);
    return book;
}

void ContactStore::load_into(ContactBook& book) const {
    if (!db_) return;
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(as_db(db_), "SELECT handle, name FROM contacts", -1,
                           &stmt, nullptr) != SQLITE_OK)
        return;
    while (sqlite3_step(stmt) == SQLITE_ROW)
        book.add(column_text(stmt, 0), column_text(stmt, 1));
    sqlite3_finalize(stmt);
}

}  // namespace imsg

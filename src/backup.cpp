#include "imsg/backup.hpp"

#include <sqlite3.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

#include "imsg/sqlite_uri.hpp"

namespace fs = std::filesystem;

namespace imsg {

const char* kMessagesDomain = "HomeDomain";
const char* kMessagesRelativePath = "Library/SMS/sms.db";
const char* kContactsDomain = "HomeDomain";
const char* kContactsRelativePath = "Library/AddressBook/AddressBook.sqlitedb";

namespace {

std::string getenv_str(const char* name) {
    const char* v = std::getenv(name);
    return v ? std::string(v) : std::string();
}

// Opens a backup's Manifest.db read-only. Returns nullptr if it isn't a
// readable SQLite database (the usual sign of an encrypted backup).
sqlite3* open_manifest(const std::string& backup_dir) {
    std::string manifest = (fs::path(backup_dir) / "Manifest.db").string();
    std::error_code ec;
    if (!fs::exists(manifest, ec)) return nullptr;
    sqlite3* db = nullptr;
    if (sqlite3_open_v2(sqlite_ro_uri(manifest).c_str(), &db,
                        SQLITE_OPEN_READONLY | SQLITE_OPEN_URI, nullptr) != SQLITE_OK) {
        sqlite3_close(db);
        return nullptr;
    }
    // An encrypted backup's Manifest.db opens lazily but isn't a real DB; confirm
    // we can actually read the file table before declaring it usable. The
    // "not a database" error can surface at prepare or step time.
    sqlite3_stmt* stmt = nullptr;
    bool ok = false;
    if (sqlite3_prepare_v2(db, "SELECT fileID FROM Files LIMIT 1", -1, &stmt,
                           nullptr) == SQLITE_OK) {
        int rc = sqlite3_step(stmt);
        ok = (rc == SQLITE_ROW || rc == SQLITE_DONE);
    }
    sqlite3_finalize(stmt);
    if (!ok) {
        sqlite3_close(db);
        return nullptr;
    }
    return db;
}

// Backup-relative blob location for a fileID: modern backups bucket by the
// first two hex chars ("ab/abcdef..."); very old ones store it flat.
std::string locate_blob(const std::string& backup_dir, const std::string& file_id) {
    std::error_code ec;
    if (file_id.size() >= 2) {
        fs::path bucketed = fs::path(backup_dir) / file_id.substr(0, 2) / file_id;
        if (fs::exists(bucketed, ec)) return bucketed.string();
    }
    fs::path flat = fs::path(backup_dir) / file_id;
    if (fs::exists(flat, ec)) return flat.string();
    return "";
}

}  // namespace

std::vector<std::string> default_backup_roots() {
    std::vector<std::string> roots;
#if defined(_WIN32)
    std::string appdata = getenv_str("APPDATA");
    std::string profile = getenv_str("USERPROFILE");
    if (!appdata.empty()) {
        roots.push_back(appdata + "\\Apple\\MobileSync\\Backup");
        roots.push_back(appdata + "\\Apple Computer\\MobileSync\\Backup");
    }
    if (!profile.empty())
        roots.push_back(profile + "\\Apple\\MobileSync\\Backup");
#else
    std::string home = getenv_str("HOME");
    if (!home.empty())
        roots.push_back(home + "/Library/Application Support/MobileSync/Backup");
#endif
    return roots;
}

BackupInfo inspect_backup(const std::string& dir) {
    BackupInfo info;
    info.path = dir;
    info.udid = fs::path(dir).filename().string();

    std::error_code ec;
    std::string manifest = (fs::path(dir) / "Manifest.db").string();
    if (!fs::exists(manifest, ec)) return info;  // not a (modern) backup
    info.valid = true;

    sqlite3* db = open_manifest(dir);
    info.encrypted = (db == nullptr);  // present but unreadable => encrypted
    sqlite3_close(db);
    return info;
}

std::vector<BackupInfo> list_backups(const std::string& extra_root) {
    std::vector<std::string> roots = default_backup_roots();
    if (!extra_root.empty()) roots.push_back(extra_root);

    std::vector<std::pair<fs::file_time_type, BackupInfo>> found;
    std::error_code ec;
    for (const std::string& root : roots) {
        if (!fs::is_directory(root, ec)) continue;
        for (const auto& entry : fs::directory_iterator(root, ec)) {
            if (ec) break;
            if (!entry.is_directory(ec)) continue;
            BackupInfo info = inspect_backup(entry.path().string());
            if (!info.valid) continue;
            auto when = fs::last_write_time(
                fs::path(info.path) / "Manifest.db", ec);
            found.emplace_back(when, info);
        }
    }
    std::sort(found.begin(), found.end(),
              [](const auto& a, const auto& b) { return a.first > b.first; });

    std::vector<BackupInfo> out;
    out.reserve(found.size());
    for (auto& f : found) out.push_back(std::move(f.second));
    return out;
}

std::string resolve_backup(const std::string& spec) {
    if (spec.empty()) return "";
    std::error_code ec;

    if (spec == "latest") {
        std::vector<BackupInfo> all = list_backups();
        return all.empty() ? "" : all.front().path;
    }
    // An explicit directory containing a Manifest.db.
    if (fs::is_directory(spec, ec) &&
        fs::exists(fs::path(spec) / "Manifest.db", ec))
        return spec;
    // Otherwise treat it as a UDID under one of the default roots.
    for (const std::string& root : default_backup_roots()) {
        fs::path candidate = fs::path(root) / spec;
        if (fs::is_directory(candidate, ec) &&
            fs::exists(candidate / "Manifest.db", ec))
            return candidate.string();
    }
    return "";
}

bool extract_backup_file(const std::string& backup_dir, const std::string& domain,
                         const std::string& relative_path, const std::string& dest,
                         std::string& err) {
    sqlite3* db = open_manifest(backup_dir);
    if (!db) {
        err = "cannot read Manifest.db (the backup is encrypted or not a "
              "modern iOS backup)";
        return false;
    }

    sqlite3_stmt* stmt = nullptr;
    const char* sql =
        "SELECT fileID FROM Files WHERE domain = ? AND relativePath = ? LIMIT 1";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        err = std::string("query Manifest.db: ") + sqlite3_errmsg(db);
        sqlite3_close(db);
        return false;
    }
    sqlite3_bind_text(stmt, 1, domain.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, relative_path.c_str(), -1, SQLITE_TRANSIENT);

    std::string file_id;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const unsigned char* t = sqlite3_column_text(stmt, 0);
        if (t) file_id = reinterpret_cast<const char*>(t);
    }
    sqlite3_finalize(stmt);
    sqlite3_close(db);

    if (file_id.empty()) {
        err = "'" + relative_path + "' is not present in this backup";
        return false;
    }

    std::string blob = locate_blob(backup_dir, file_id);
    if (blob.empty()) {
        err = "backup is missing the data file for '" + relative_path + "'";
        return false;
    }

    std::error_code ec;
    fs::create_directories(fs::path(dest).parent_path(), ec);
    fs::copy_file(blob, dest, fs::copy_options::overwrite_existing, ec);
    if (ec) {
        err = "cannot copy extracted file to '" + dest + "': " + ec.message();
        return false;
    }
    return true;
}

}  // namespace imsg

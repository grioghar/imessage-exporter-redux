#include "imsg/contacts.hpp"

#include <sqlite3.h>

#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "imsg/sqlite_uri.hpp"
#include "imsg/vcard.hpp"

namespace fs = std::filesystem;

namespace imsg {
namespace {

std::string column_text(sqlite3_stmt* stmt, int col) {
    const unsigned char* t = sqlite3_column_text(stmt, col);
    return t ? std::string(reinterpret_cast<const char*>(t)) : std::string();
}

std::string trim(const std::string& s) {
    std::size_t b = 0, e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    return s.substr(b, e - b);
}

// first + last, falling back to the organization name for company contacts.
std::string display_name(const std::string& first, const std::string& last,
                         const std::string& org) {
    std::string name = trim(first + " " + last);
    if (!name.empty()) return name;
    return trim(org);
}

// Runs one handle->name query (address column 0, name columns 1..3) into `book`.
void load_query(sqlite3* db, const char* sql, ContactBook& book) {
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) return;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        std::string addr = column_text(stmt, 0);
        std::string name = display_name(column_text(stmt, 1), column_text(stmt, 2),
                                        column_text(stmt, 3));
        if (!addr.empty() && !name.empty()) book.add(addr, name);
    }
    sqlite3_finalize(stmt);
}

void load_one_db(const std::string& db_path, ContactBook& book) {
    std::string uri = sqlite_ro_uri(db_path);
    sqlite3* db = nullptr;
    if (sqlite3_open_v2(uri.c_str(), &db,
                        SQLITE_OPEN_READONLY | SQLITE_OPEN_URI, nullptr) != SQLITE_OK) {
        sqlite3_close(db);
        return;  // best-effort: skip databases we can't open
    }
    // macOS Contacts schema (AddressBook-v22.abcddb): ZABCDRECORD + linked
    // ZABCD{PHONENUMBER,EMAILADDRESS}.
    load_query(db,
               "SELECT p.ZFULLNUMBER, r.ZFIRSTNAME, r.ZLASTNAME, r.ZORGANIZATION "
               "FROM ZABCDPHONENUMBER p JOIN ZABCDRECORD r ON p.ZOWNER = r.Z_PK",
               book);
    load_query(db,
               "SELECT e.ZADDRESS, r.ZFIRSTNAME, r.ZLASTNAME, r.ZORGANIZATION "
               "FROM ZABCDEMAILADDRESS e JOIN ZABCDRECORD r ON e.ZOWNER = r.Z_PK",
               book);
    // iOS AddressBook.sqlitedb (as extracted from a device backup): ABPerson +
    // ABMultiValue, where property 3 = phone and 4 = email. Whichever schema is
    // absent simply fails to prepare and is skipped.
    load_query(db,
               "SELECT mv.value, p.First, p.Last, p.Organization "
               "FROM ABMultiValue mv JOIN ABPerson p ON mv.record_id = p.ROWID "
               "WHERE mv.property IN (3, 4)",
               book);
    sqlite3_close(db);
}

// Reads an entire vCard file and parses it into `book`.
void load_vcf(const std::string& path, ContactBook& book) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return;
    std::string text((std::istreambuf_iterator<char>(in)),
                     std::istreambuf_iterator<char>());
    parse_vcards(text, book);
}

bool is_contact_file(const fs::path& p) {
    return p.extension() == ".abcddb" || p.extension() == ".vcf";
}

// All contact source files (`.abcddb` / `.vcf`) at or under `path`, which may
// itself be a single file (used regardless of extension).
std::vector<std::string> find_contact_files(const std::string& path) {
    std::vector<std::string> out;
    std::error_code ec;
    if (fs::is_regular_file(path, ec)) {
        out.push_back(path);
        return out;
    }
    if (!fs::is_directory(path, ec)) return out;
    for (fs::recursive_directory_iterator it(path, ec), end; it != end;
         it.increment(ec)) {
        if (ec) break;
        if (it->is_regular_file(ec) && is_contact_file(it->path()))
            out.push_back(it->path().string());
    }
    return out;
}

}  // namespace

ContactBook load_contacts(const std::string& path) {
    ContactBook book;
    for (const std::string& file : find_contact_files(path)) {
        if (fs::path(file).extension() == ".vcf")
            load_vcf(file, book);
        else
            load_one_db(file, book);  // .abcddb, or an explicit file of any name
    }
    return book;
}

ContactBook load_contacts_default() {
    const char* home = std::getenv("HOME");
    if (!home || !*home) return ContactBook();
    return load_contacts(std::string(home) +
                         "/Library/Application Support/AddressBook");
}

}  // namespace imsg

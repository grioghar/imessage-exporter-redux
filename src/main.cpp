// Command-line entry point for imessage-exporter.
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include "imsg/backup.hpp"
#include "imsg/contacts.hpp"
#include "imsg/database.hpp"
#include "imsg/export_job.hpp"
#include "imsg/exporters.hpp"
#include "imsg/log.hpp"
#include "imsg/time_util.hpp"

namespace fs = std::filesystem;

namespace {

constexpr const char* kVersion = "0.1.0";

void print_usage(std::ostream& os) {
    os << "Usage: imessage-exporter [options]\n\n"
       << "Export macOS iMessage/SMS history to TXT, JSON, or HTML.\n\n"
       << "Options:\n"
       << "  --db PATH        Path to the Messages database\n"
       << "                   (default: $HOME/Library/Messages/chat.db)\n"
       << "  --format FMT     Output format: " << imsg::available_formats()
       << " (default: txt)\n"
       << "  --output DIR     Directory for exported files (default: ./imessage-export)\n"
       << "  --me NAME        Label for messages you sent (default: Me)\n"
       << "  --since DATE     Only messages on/after DATE (YYYY-MM-DD[ HH:MM:SS])\n"
       << "  --until DATE     Only messages on/before DATE (date-only = end of day)\n"
       << "  --combined       Write one combined file instead of one per chat\n"
       << "  --copy-attachments  Copy attachment files into <output>/attachments\n"
       << "  --contacts       Resolve names via the default macOS Contacts DB\n"
       << "  --contacts-db P  Resolve names via a specific .abcddb / .vcf file or dir\n"
       << "  --backup SPEC    Source from an iTunes/Finder backup: a path, a device\n"
       << "                   UDID, or 'latest' (unencrypted backups only)\n"
       << "  --list-backups   List available device backups and exit\n"
       << "  --list-chats     List conversations and exit\n"
       << "  --log-level LVL  Logging verbosity: error, warn, info, debug\n"
       << "                   (default: warn; or set IMSG_LOG_LEVEL)\n"
       << "  -v / -vv         Shortcuts for --log-level info / debug\n"
       << "  --version        Print version and exit\n"
       << "  --help           Show this help and exit\n";
}

// Removes its directory on scope exit; holds files extracted from a backup.
struct ScopedTempDir {
    fs::path path;
    bool active = false;
    ~ScopedTempDir() {
        if (active) {
            std::error_code ec;
            fs::remove_all(path, ec);
        }
    }
};

void print_backups() {
    std::vector<imsg::BackupInfo> backups = imsg::list_backups();
    if (backups.empty()) {
        std::cerr << "No device backups found in the default locations.\n"
                     "Pass an explicit path with --backup <dir>.\n";
        return;
    }
    std::cout << "Device backups (most recent first):\n";
    bool first = true;
    for (const auto& b : backups) {
        std::cout << "  " << b.udid << (b.encrypted ? "  [encrypted]" : "")
                  << (first ? "  (most recent)" : "") << "\n    " << b.path << "\n";
        first = false;
    }
}

// Resolves `spec` to a backup and extracts sms.db (and, when names are
// requested, the device Contacts) into `work`. Sets db_path/opts on success.
// Prints its own diagnostics; returns false on failure.
bool prepare_backup(const std::string& spec, std::string& db_path,
                    imsg::ExportOptions& opts, ScopedTempDir& work) {
    std::string dir = imsg::resolve_backup(spec);
    if (dir.empty()) {
        std::cerr << "error: no backup found for '" << spec
                  << "'. Try --list-backups, or pass a backup directory path.\n";
        return false;
    }
    imsg::log_info("using backup '" + dir + "'");
    imsg::BackupInfo info = imsg::inspect_backup(dir);
    if (info.encrypted) {
        std::cerr << "error: backup '" << info.udid << "' is encrypted, which "
                     "isn't supported yet.\n"
                     "       In Finder/iTunes, turn off \"Encrypt local backup\", "
                     "back up again, then retry.\n";
        return false;
    }

    std::error_code ec;
    work.path = fs::temp_directory_path(ec) / ("imessage-exporter-" + info.udid);
    fs::create_directories(work.path, ec);
    work.active = true;

    std::string err;
    std::string sms = (work.path / "sms.db").string();
    if (!imsg::extract_backup_file(dir, imsg::kMessagesDomain,
                                   imsg::kMessagesRelativePath, sms, err)) {
        std::cerr << "error: " << err << "\n";
        return false;
    }
    db_path = sms;

    if (opts.use_contacts && opts.contacts_path.empty()) {
        std::string ab = (work.path / "AddressBook.sqlitedb").string();
        if (imsg::extract_backup_file(dir, imsg::kContactsDomain,
                                      imsg::kContactsRelativePath, ab, err)) {
            opts.contacts_path = ab;   // use the device's contacts...
            opts.use_contacts = false;  // ...not this Mac's
        } else {
            std::cerr << "warning: could not read Contacts from backup (" << err
                      << "); names may stay unresolved.\n";
        }
    }
    if (opts.copy_attachments)
        std::cerr << "warning: --copy-attachments has no effect with --backup "
                     "(attachment files aren't resolved from backups yet).\n";
    return true;
}

// Reads the value following a flag, erroring if it's missing.
bool take_value(int argc, char** argv, int& i, const char* flag, std::string& out) {
    if (i + 1 >= argc) {
        std::cerr << "error: " << flag << " requires a value\n";
        return false;
    }
    out = argv[++i];
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    std::string db_path = imsg::default_db_path();
    std::string format_name = "txt";
    std::string output_dir = "imessage-export";
    bool list_chats = false;
    bool list_backups = false;
    std::string backup_spec;
    imsg::ExportOptions opts;

    // Environment first, so an explicit flag can still override it.
    if (const char* env = std::getenv("IMSG_LOG_LEVEL")) {
        imsg::LogLevel lvl;
        if (imsg::parse_log_level(env, lvl)) imsg::set_log_level(lvl);
    }

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            print_usage(std::cout);
            return 0;
        } else if (arg == "--version") {
            std::cout << "imessage-exporter " << kVersion << "\n";
            return 0;
        } else if (arg == "--list-chats") {
            list_chats = true;
        } else if (arg == "--list-backups") {
            list_backups = true;
        } else if (arg == "--backup") {
            if (!take_value(argc, argv, i, "--backup", backup_spec)) return 2;
        } else if (arg == "--log-level") {
            std::string v;
            if (!take_value(argc, argv, i, "--log-level", v)) return 2;
            imsg::LogLevel lvl;
            if (!imsg::parse_log_level(v, lvl)) {
                std::cerr << "error: invalid --log-level '" << v
                          << "' (use error, warn, info, or debug)\n";
                return 2;
            }
            imsg::set_log_level(lvl);
        } else if (arg == "-v" || arg == "--verbose") {
            imsg::set_log_level(imsg::LogLevel::Info);
        } else if (arg == "-vv" || arg == "--debug") {
            imsg::set_log_level(imsg::LogLevel::Debug);
        } else if (arg == "--combined") {
            opts.combined = true;
        } else if (arg == "--copy-attachments") {
            opts.copy_attachments = true;
        } else if (arg == "--contacts") {
            opts.use_contacts = true;
        } else if (arg == "--db") {
            if (!take_value(argc, argv, i, "--db", db_path)) return 2;
        } else if (arg == "--format") {
            if (!take_value(argc, argv, i, "--format", format_name)) return 2;
        } else if (arg == "--output") {
            if (!take_value(argc, argv, i, "--output", output_dir)) return 2;
        } else if (arg == "--me") {
            if (!take_value(argc, argv, i, "--me", opts.me_label)) return 2;
        } else if (arg == "--contacts-db") {
            if (!take_value(argc, argv, i, "--contacts-db", opts.contacts_path)) return 2;
        } else if (arg == "--since") {
            std::string v;
            if (!take_value(argc, argv, i, "--since", v)) return 2;
            if (!imsg::parse_date(v, opts.since)) {
                std::cerr << "error: invalid --since date '" << v << "'\n";
                return 2;
            }
            opts.has_since = true;
        } else if (arg == "--until") {
            std::string v;
            if (!take_value(argc, argv, i, "--until", v)) return 2;
            if (!imsg::parse_date(v, opts.until, /*end_of_day=*/true)) {
                std::cerr << "error: invalid --until date '" << v << "'\n";
                return 2;
            }
            opts.has_until = true;
        } else {
            std::cerr << "error: unknown argument '" << arg << "'\n\n";
            print_usage(std::cerr);
            return 2;
        }
    }

    if (list_backups) {
        print_backups();
        return 0;
    }

    imsg::Format format;
    if (!imsg::parse_format(format_name, format)) {
        std::cerr << "error: unknown format '" << format_name << "'; choose from "
                  << imsg::available_formats() << "\n";
        return 2;
    }

    // Source the database from a device backup, if requested.
    ScopedTempDir backup_work;
    if (!backup_spec.empty()) {
        if (!prepare_backup(backup_spec, db_path, opts, backup_work)) return 1;
    } else if (db_path == imsg::default_db_path() && !fs::exists(db_path)) {
        std::cerr << "error: no Messages database at " << db_path << "\n"
                     "Set up Messages on this Mac (enable \"Messages in iCloud\" to "
                     "pull your full history), or\n"
                     "pass --db <path>, or extract from a device backup with "
                     "--backup (see --list-backups).\n"
                     "On macOS this tool also needs Full Disk Access.\n";
        return 1;
    }

    if (list_chats) {
        std::vector<imsg::Chat> chats;
        imsg::ContactBook contacts;
        if (!opts.contacts_path.empty())
            contacts = imsg::load_contacts(opts.contacts_path);
        else if (opts.use_contacts)
            contacts = imsg::load_contacts_default();
        try {
            imsg::MessagesDatabase db(db_path, opts.me_label);
            if (!contacts.empty()) db.set_contacts(&contacts);
            db.open();
            chats = db.load_chat_index();
        } catch (const imsg::DatabaseError& e) {
            std::cerr << "error: " << e.what() << "\n";
            return 1;
        }
        chats.erase(std::remove_if(chats.begin(), chats.end(),
                                   [](const imsg::Chat& c) {
                                       return c.message_count == 0;
                                   }),
                    chats.end());
        if (chats.empty()) {
            std::cerr << "No conversations with messages were found.\n";
            return 1;
        }
        std::sort(chats.begin(), chats.end(),
                  [](const imsg::Chat& a, const imsg::Chat& b) {
                      return a.message_count > b.message_count;
                  });
        for (const auto& c : chats) {
            std::cout << c.message_count << "\t" << c.title() << "\n";
        }
        return 0;
    }

    imsg::ExportSummary summary =
        imsg::export_database(db_path, output_dir, format, opts);
    if (!summary.ok) {
        std::cerr << "error: " << summary.error << "\n";
        return 1;
    }
    if (summary.conversations == 0) {
        std::cerr << "No conversations with messages were found"
                  << ((opts.has_since || opts.has_until) ? " in the given date range"
                                                          : "")
                  << ".\n";
        return 1;
    }

    std::cout << "Exported " << summary.conversations << " conversation(s) to "
              << output_dir << " as " << format_name;
    if (opts.combined) std::cout << " (combined)";
    if (opts.copy_attachments)
        std::cout << ", copied " << summary.attachments_copied << " attachment(s)";
    std::cout << ".\n";
    return 0;
}

#include "imsg/export_job.hpp"

#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "imsg/contacts.hpp"
#include "imsg/database.hpp"
#include "imsg/log.hpp"

namespace fs = std::filesystem;

namespace imsg {
namespace {

// Reduces a conversation title to a safe, lowercase filename stem. Keeping only
// alphanumerics and single dashes also prevents path traversal (e.g. "../").
std::string slugify(const std::string& value) {
    std::string out;
    bool last_dash = false;
    for (char ch : value) {
        unsigned char c = static_cast<unsigned char>(ch);
        if (std::isalnum(c)) {
            out += static_cast<char>(std::tolower(c));
            last_dash = false;
        } else if (!last_dash && !out.empty()) {
            out += '-';
            last_dash = true;
        }
        if (out.size() >= 80) break;
    }
    while (!out.empty() && out.back() == '-') out.pop_back();
    return out;
}

// Expands a leading "~" to $HOME so attachment paths stored as "~/Library/..."
// resolve to a real file. Other paths are returned unchanged.
std::string expand_user_path(const std::string& path) {
    if (path.empty() || path[0] != '~') return path;
    const char* home = std::getenv("HOME");
    if (!home) return path;
    return std::string(home) + path.substr(1);
}

// Copies a conversation's attachment files into <out_dir>/attachments/<slug>/
// and records each copied file's output-relative path on the Attachment, so the
// renderers can link to it. Returns the number of files actually copied.
// `seen_src` dedupes across conversations; `used_rel` keeps destination names
// unique. Missing source files are skipped (only metadata is then exported).
int copy_chat_attachments(Chat& chat, const std::string& out_dir,
                          const std::string& slug,
                          std::unordered_map<std::string, std::string>& seen_src,
                          std::unordered_set<std::string>& used_rel) {
    int copied = 0;
    const fs::path dest_dir = fs::path(out_dir) / "attachments" / slug;
    bool dir_ready = false;

    for (Message& m : chat.messages) {
        for (Attachment& a : m.attachments) {
            if (a.filename.empty()) continue;
            std::string src = expand_user_path(a.filename);

            auto prev = seen_src.find(src);
            if (prev != seen_src.end()) {  // already copied for another message
                a.copied_path = prev->second;
                continue;
            }

            std::error_code ec;
            if (!fs::exists(src, ec) || ec) continue;  // source unavailable

            std::string base = fs::path(src).filename().string();
            if (base.empty()) base = "file";
            std::string rel = "attachments/" + slug + "/" + base;
            for (int n = 2; used_rel.count(rel); ++n)
                rel = "attachments/" + slug + "/" + std::to_string(n) + "-" + base;

            if (!dir_ready) {
                fs::create_directories(dest_dir, ec);
                dir_ready = true;
            }
            fs::path dest = fs::path(out_dir) / rel;
            fs::copy_file(src, dest, fs::copy_options::overwrite_existing, ec);
            if (ec) continue;  // copy failed: leave as metadata-only

            used_rel.insert(rel);
            seen_src.emplace(src, rel);
            a.copied_path = rel;
            ++copied;
        }
    }
    return copied;
}

}  // namespace

ExportSummary export_database(const std::string& db_path,
                              const std::string& out_dir, Format fmt,
                              const ExportOptions& opts) {
    ExportSummary summary;
    try {
        // Resolve handles to names up front, if requested.
        ContactBook contacts;
        if (!opts.contacts_path.empty())
            contacts = load_contacts(opts.contacts_path);
        else if (opts.use_contacts)
            contacts = load_contacts_default();
        if (!contacts.empty())
            log_info("loaded " + std::to_string(contacts.size()) +
                     " contact handle(s) for name resolution");

        MessagesDatabase db(db_path, opts.me_label);
        db.set_date_range(opts.has_since, opts.since, opts.has_until, opts.until);
        if (!contacts.empty()) db.set_contacts(&contacts);
        db.open();
        std::vector<Chat> chats = db.load_chat_index();

        std::error_code ec;
        fs::create_directories(out_dir, ec);
        if (ec) {
            summary.error =
                "cannot create output directory '" + out_dir + "': " + ec.message();
            return summary;
        }

        const std::string ext = extension_for(fmt);
        std::unordered_set<std::string> used_names;          // output file names
        std::unordered_set<std::string> used_attach;         // attachment rel paths
        std::unordered_map<std::string, std::string> seen_src;  // src -> rel path
        int written = 0;

        // For combined export, one file streams every conversation in turn.
        std::ofstream combined;
        if (opts.combined) {
            fs::path path =
                fs::path(out_dir) / (std::string(combined_stem()) + "." + ext);
            combined.open(path, std::ios::binary);
            if (!combined) {
                summary.error = "cannot write '" + path.string() + "'";
                return summary;
            }
            combined << combined_prologue(fmt);
        }

        for (Chat& chat : chats) {
            if (chat.message_count == 0) continue;

            db.load_messages(chat);  // bodies for just this conversation
            if (chat.messages.empty()) continue;  // e.g. filtered out by date

            std::string slug = slugify(chat.title());
            if (slug.empty()) slug = "chat-" + std::to_string(chat.rowid);

            if (opts.copy_attachments)
                summary.attachments_copied +=
                    copy_chat_attachments(chat, out_dir, slug, seen_src, used_attach);

            if (opts.combined) {
                combined << combined_item(chat, fmt, static_cast<std::size_t>(written));
            } else {
                std::string name = slug + "." + ext;
                for (int n = 2; used_names.count(name); ++n)
                    name = slug + "-" + std::to_string(n) + "." + ext;
                used_names.insert(name);

                fs::path path = fs::path(out_dir) / name;
                std::ofstream out(path, std::ios::binary);
                if (!out) {
                    summary.error = "cannot write '" + path.string() + "'";
                    return summary;
                }
                out << render(chat, fmt);
            }

            // Release this conversation's bodies before moving to the next one.
            chat.messages.clear();
            chat.messages.shrink_to_fit();
            ++written;
        }

        if (opts.combined) combined << combined_epilogue(fmt);

        summary.ok = true;
        summary.conversations = written;
        log_info("exported " + std::to_string(written) + " conversation(s) to " +
                 out_dir + (opts.copy_attachments
                                ? ", " + std::to_string(summary.attachments_copied) +
                                      " attachment(s)"
                                : ""));
        return summary;
    } catch (const DatabaseError& e) {
        summary.error = e.what();
        return summary;
    }
}

}  // namespace imsg

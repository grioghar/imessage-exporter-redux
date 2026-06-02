#include "imsg/export_job.hpp"

#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "imsg/contact_store.hpp"
#include "imsg/contacts.hpp"
#include "imsg/database.hpp"
#include "imsg/log.hpp"
#include "imsg/time_util.hpp"

namespace fs = std::filesystem;

namespace imsg {
namespace {

// Reduces a conversation title to a filesystem-safe stem, PRESERVING Unicode
// letters: UTF-8 multibyte bytes (>= 0x80) are kept verbatim, so names like
// "José" or non-Latin scripts survive. ASCII control chars and the characters
// illegal in Windows/macOS filenames are dropped; runs of other punctuation and
// whitespace collapse to a single '-'. The result is truncated on a UTF-8 code
// point boundary so it stays valid UTF-8 (important for the Windows path
// conversion). Keeping no raw '/'/'\\' also prevents path traversal.
std::string slugify(const std::string& value) {
    auto is_unsafe = [](unsigned char c) {
        return c < 0x20 || c == '/' || c == '\\' || c == ':' || c == '*' ||
               c == '?' || c == '"' || c == '<' || c == '>' || c == '|';
    };
    std::string out;
    bool last_dash = false;
    for (unsigned char c : value) {
        if (c >= 0x80) {  // part of a UTF-8 sequence: keep as-is
            out += static_cast<char>(c);
            last_dash = false;
        } else if (std::isalnum(c)) {
            out += static_cast<char>(std::tolower(c));
            last_dash = false;
        } else if (is_unsafe(c)) {
            // drop entirely
        } else if (!last_dash && !out.empty()) {  // space/punct -> single dash
            out += '-';
            last_dash = true;
        }
    }
    if (out.size() > 80) {  // truncate without splitting a UTF-8 sequence
        std::size_t cut = 80;
        while (cut > 0 && (static_cast<unsigned char>(out[cut]) & 0xC0) == 0x80) --cut;
        out.resize(cut);
    }
    while (!out.empty() && out.back() == '-') out.pop_back();
    return out;
}

// Joins a UTF-8 output dir + UTF-8 relative path into a filesystem path encoded
// correctly for the platform (on Windows, std::string is otherwise treated as
// the ANSI codepage, mangling Unicode names).
fs::path out_path(const std::string& out_dir, const std::string& rel_utf8) {
    return fs::u8path(out_dir) / fs::u8path(rel_utf8);
}

// True if the chat should be exported given a people filter (empty = all).
// Matching is substring-both-ways so a picker entry like "Jane — +15551234567"
// matches whether the export shows the resolved name or the raw handle.
bool matches_participants(const Chat& chat, const std::vector<std::string>& only) {
    if (only.empty()) return true;
    auto related = [](const std::string& a, const std::string& b) {
        return !a.empty() && !b.empty() &&
               (a.find(b) != std::string::npos || b.find(a) != std::string::npos);
    };
    for (const std::string& sel : only) {
        if (related(chat.title(), sel)) return true;
        for (const std::string& p : chat.participants)
            if (related(p, sel)) return true;
    }
    return false;
}

// Expands a leading "~" to $HOME so attachment paths stored as "~/Library/..."
// resolve to a real file. Other paths are returned unchanged.
std::string expand_user_path(const std::string& path) {
    if (path.empty() || path[0] != '~') return path;
    const char* home = std::getenv("HOME");
    if (!home) return path;
    return std::string(home) + path.substr(1);
}

// Copies a conversation's attachment files into a per-conversation folder
// <out_dir>/<adir>/ (adir is the conversation's file stem, optionally dot-hidden)
// and records each copied file's output-relative path on the Attachment, so the
// renderers can link to it. Returns the number of files actually copied.
// `seen_src` dedupes across conversations; `used_rel` keeps destination names
// unique. Missing source files are skipped (only metadata is then exported).
int copy_chat_attachments(Chat& chat, const std::string& out_dir,
                          const std::string& adir,
                          std::unordered_map<std::string, std::string>& seen_src,
                          std::unordered_set<std::string>& used_rel) {
    int copied = 0;
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

            auto unique_rel = [&](const std::string& b) {
                std::string r = adir + "/" + b;
                for (int n = 2; used_rel.count(r); ++n)
                    r = adir + "/" + std::to_string(n) + "-" + b;
                return r;
            };

            bool ok = false;
            std::string rel;
#ifdef __APPLE__
            // HEIC/HEIF won't display in browsers or the PDF engine; transcode it
            // to JPEG with macOS `sips` so the picture actually shows.
            std::string ext = fs::path(base).extension().string();
            for (char& c : ext) c = static_cast<char>(std::tolower((unsigned char)c));
            if ((ext == ".heic" || ext == ".heif") &&
                src.find('"') == std::string::npos) {
                rel = unique_rel(fs::path(base).stem().string() + ".jpg");
                fs::path dest = out_path(out_dir, rel);
                fs::create_directories(dest.parent_path(), ec);
                if (dest.string().find('"') == std::string::npos) {
                    const std::string cmd = "sips -s format jpeg \"" + src +
                                            "\" --out \"" + dest.string() +
                                            "\" >/dev/null 2>&1";
                    if (std::system(cmd.c_str()) == 0 && fs::exists(dest, ec)) ok = true;
                }
            }
#endif
            if (!ok) {  // non-HEIC, or transcode unavailable/failed: plain copy
                rel = unique_rel(base);
                fs::path dest = out_path(out_dir, rel);
                fs::create_directories(dest.parent_path(), ec);
                fs::copy_file(src, dest, fs::copy_options::overwrite_existing, ec);
                ok = !ec;
            }
            if (!ok) continue;  // leave as metadata-only

            used_rel.insert(rel);
            seen_src.emplace(src, rel);
            a.copied_path = rel;
            ++copied;
            log_info("copied attachment: " + fs::path(rel).filename().string());
        }
    }
    return copied;
}

std::string base64_encode(const std::string& in) {
    static const char* T =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve((in.size() + 2) / 3 * 4);
    std::size_t i = 0;
    for (; i + 3 <= in.size(); i += 3) {
        unsigned n = static_cast<unsigned char>(in[i]) << 16 |
                     static_cast<unsigned char>(in[i + 1]) << 8 |
                     static_cast<unsigned char>(in[i + 2]);
        out += T[(n >> 18) & 63];
        out += T[(n >> 12) & 63];
        out += T[(n >> 6) & 63];
        out += T[n & 63];
    }
    if (i < in.size()) {
        unsigned n = static_cast<unsigned char>(in[i]) << 16;
        const bool two = (i + 1 < in.size());
        if (two) n |= static_cast<unsigned char>(in[i + 1]) << 8;
        out += T[(n >> 18) & 63];
        out += T[(n >> 12) & 63];
        out += two ? T[(n >> 6) & 63] : '=';
        out += '=';
    }
    return out;
}

// MIME from the DB, or a guess from the extension, or octet-stream.
std::string guess_mime(const std::string& filename, const std::string& known) {
    if (!known.empty()) return known;
    std::string ext = fs::path(filename).extension().string();
    for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (ext == ".jpg" || ext == ".jpeg") return "image/jpeg";
    if (ext == ".png") return "image/png";
    if (ext == ".gif") return "image/gif";
    if (ext == ".heic") return "image/heic";
    if (ext == ".webp") return "image/webp";
    if (ext == ".mp4" || ext == ".m4v") return "video/mp4";
    if (ext == ".mov") return "video/quicktime";
    if (ext == ".m4a") return "audio/mp4";
    if (ext == ".pdf") return "application/pdf";
    return "application/octet-stream";
}

// Inlines each attachment's bytes as a base64 data URI (--embed-attachments).
// Caches by source path so a file shared across messages is read once.
void embed_chat_attachments(Chat& chat,
                            std::unordered_map<std::string, std::string>& cache) {
    for (Message& m : chat.messages) {
        for (Attachment& a : m.attachments) {
            if (a.filename.empty()) continue;
            std::string src = expand_user_path(a.filename);
            auto it = cache.find(src);
            if (it != cache.end()) {
                a.data_uri = it->second;
                continue;
            }
            std::error_code ec;
            if (!fs::exists(src, ec) || ec) continue;
            std::ifstream f(fs::u8path(src), std::ios::binary);
            if (!f) continue;
            std::string bytes((std::istreambuf_iterator<char>(f)),
                              std::istreambuf_iterator<char>());
            std::string uri = "data:" + guess_mime(a.filename, a.mime_type) +
                              ";base64," + base64_encode(bytes);
            cache.emplace(src, uri);
            a.data_uri = uri;
            log_info("embedded attachment: " + fs::path(a.filename).filename().string());
        }
    }
}

}  // namespace

ExportSummary export_database(const std::string& db_path,
                              const std::string& out_dir, Format fmt,
                              const ExportOptions& opts) {
    ExportSummary summary;
    try {
        // Arm the selected HTML theme (no-op for non-HTML formats; an unknown
        // name falls back to "ios"). Mirrors the link-preview install contract.
        set_html_theme(opts.html_theme);

        // Resolve handles to names up front, if requested.
        ContactBook contacts;
        if (!opts.contacts_path.empty())
            contacts = load_contacts(opts.contacts_path);
        else if (opts.use_contacts)
            contacts = load_contacts_default();
        if (opts.use_contact_store) {
            ContactStore store(opts.contact_store_path.empty()
                                   ? default_contact_store_path()
                                   : opts.contact_store_path);
            if (store.open()) store.load_into(contacts);
        }
        if (!contacts.empty())
            log_info("loaded " + std::to_string(contacts.size()) +
                     " contact handle(s) for name resolution");

        MessagesDatabase db(db_path, opts.me_label);
        db.set_date_range(opts.has_since, opts.since, opts.has_until, opts.until);
        // Make the active window visible in the log so it's obvious whether the
        // date filter is in effect (a frequent point of confusion).
        if (opts.has_since || opts.has_until) {
            std::string when = "date filter:";
            if (opts.has_since) when += " from " + format_timestamp(opts.since);
            if (opts.has_until) when += " to " + format_timestamp(opts.until);
            log_info(when);
        } else {
            log_info("date filter: none (exporting all dates)");
        }
        if (!contacts.empty()) db.set_contacts(&contacts);
        db.open();
        std::vector<Chat> chats = db.load_chat_index();

        std::error_code ec;
        fs::create_directories(fs::u8path(out_dir), ec);
        if (ec) {
            summary.error =
                "cannot create output directory '" + out_dir + "': " + ec.message();
            return summary;
        }

        const std::string ext = extension_for(fmt);
        std::unordered_set<std::string> used_names;          // output file names
        std::unordered_set<std::string> used_attach;         // attachment rel paths
        std::unordered_map<std::string, std::string> seen_src;  // src -> rel path
        std::unordered_map<std::string, std::string> embed_cache;  // src -> data URI
        int written = 0;

        int total = 0;
        for (const Chat& c : chats)
            if (c.message_count > 0 && matches_participants(c, opts.only_participants))
                ++total;

        // For combined export, one file streams every conversation in turn.
        std::ofstream combined;
        if (opts.combined) {
            fs::path path =
                out_path(out_dir, std::string(combined_stem()) + "." + ext);
            combined.open(path, std::ios::binary);
            if (!combined) {
                summary.error = "cannot write '" + path.string() + "'";
                return summary;
            }
            combined << combined_prologue(fmt);
        }

        for (Chat& chat : chats) {
            if (chat.message_count == 0) continue;
            if (!matches_participants(chat, opts.only_participants)) continue;

            // Cooperative pause/stop from the front-end (blocks while paused).
            if (opts.should_stop && opts.should_stop()) break;

            std::string slug = slugify(chat.title());
            if (slug.empty()) slug = "chat-" + std::to_string(chat.rowid);

            // Resume: skip a per-conversation file already written by a prior run.
            if (opts.skip_existing && !opts.combined) {
                std::string nm = slug + "." + ext;
                if (used_names.count(nm) == 0 && fs::exists(out_path(out_dir, nm), ec)) {
                    used_names.insert(nm);
                    ++written;
                    if (opts.on_progress) opts.on_progress(written, total);
                    continue;
                }
            }

            db.load_messages(chat);  // bodies for just this conversation
            if (chat.messages.empty()) continue;  // e.g. filtered out by date

            if (opts.copy_attachments) {
                // Per-conversation folder named like the file stem (optionally hidden).
                const std::string adir =
                    (opts.hidden_attachment_dir ? "." : "") + slug;
                summary.attachments_copied +=
                    copy_chat_attachments(chat, out_dir, adir, seen_src, used_attach);
            }
            if (opts.embed_attachments) embed_chat_attachments(chat, embed_cache);

            if (opts.combined) {
                combined << combined_item(chat, fmt, static_cast<std::size_t>(written));
            } else {
                std::string name = slug + "." + ext;
                for (int n = 2; used_names.count(name); ++n)
                    name = slug + "-" + std::to_string(n) + "." + ext;
                used_names.insert(name);

                fs::path path = out_path(out_dir, name);
                std::ofstream out(path, std::ios::binary);
                if (!out) {
                    summary.error = "cannot write '" + name + "' in " + out_dir;
                    return summary;
                }
                out << render(chat, fmt);
            }

            // Release this conversation's bodies before moving to the next one.
            chat.messages.clear();
            chat.messages.shrink_to_fit();
            ++written;
            if (opts.on_progress) opts.on_progress(written, total);
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

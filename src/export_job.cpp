#include "imsg/export_job.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "imsg/compress.hpp"
#include "imsg/contact_store.hpp"
#include "imsg/contacts.hpp"
#include "imsg/crypto.hpp"
#include "imsg/database.hpp"
#include "imsg/location.hpp"
#include "imsg/log.hpp"
#include "imsg/stats.hpp"
#include "imsg/timeline.hpp"
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

// Lower-cased file extension (with the dot), for media-type classification.
std::string lower_extension(const std::string& path) {
    std::string ext = fs::path(fs::u8path(path)).extension().string();
    for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return ext;
}

// Coarse media classification used by the A/B comparison sampler.
enum class MediaKind { Other, Image, Video };
MediaKind classify_media(const std::string& path) {
    const std::string e = lower_extension(path);
    if (e == ".jpg" || e == ".jpeg" || e == ".png" || e == ".gif" ||
        e == ".heic" || e == ".heif" || e == ".webp")
        return MediaKind::Image;
    if (e == ".mp4" || e == ".m4v" || e == ".mov")
        return MediaKind::Video;
    return MediaKind::Other;
}

// Builds the TEMPORARY "image-movie-comparison/" A/B tuning folder. For up to N
// files of each media type it copies the ORIGINAL and writes the alternative
// encodings side by side (NAME.original.ext / NAME.lightpress.ext /
// NAME.ffmpeg.ext) so the user can eyeball lightpress-vs-ffmpeg quality/size
// and tune the compression settings. Entirely best-effort: a missing ffmpeg
// simply omits the .ffmpeg.* variants; any failure is logged and skipped.
class MediaComparison {
  public:
    MediaComparison(const std::string& out_dir, int samples_per_type)
        : dir_(fs::u8path(out_dir) / fs::u8path("image-movie-comparison")),
          limit_(samples_per_type) {}

    // Offers `src` (an existing source file of kind `kind`) for sampling. The
    // first `limit_` of each kind are materialized; the rest are ignored.
    void offer(const std::string& src, MediaKind kind, const ExportOptions& opts) {
        if (kind == MediaKind::Image) {
            if (img_done_ >= limit_) return;
        } else if (kind == MediaKind::Video) {
            if (vid_done_ >= limit_) return;
        } else {
            return;
        }
        if (!ensure_dir()) return;

        std::error_code ec;
        const fs::path srcp = fs::u8path(src);
        const std::string stem = srcp.stem().string();
        const std::string ext = srcp.extension().string();
        // Unique-ish base name so two "IMG.jpg" from different folders don't clash.
        const int idx = (kind == MediaKind::Image ? img_done_ : vid_done_) + 1;
        const std::string base =
            (stem.empty() ? "file" : stem) + "-" + std::to_string(idx);

        // 1) Original, verbatim.
        const fs::path orig_dest = dir_ / fs::u8path(base + ".original" + ext);
        fs::copy_file(srcp, orig_dest, fs::copy_options::overwrite_existing, ec);
        if (ec) {
            log_warn("comparison: could not copy original " + srcp.filename().string());
            return;  // nothing to compare against; don't count this sample
        }

        if (kind == MediaKind::Image) {
            // 2) lightpress: copy the original, then re-encode that copy in place.
            const fs::path lp_dest = dir_ / fs::u8path(base + ".lightpress" + ext);
            fs::copy_file(srcp, lp_dest, fs::copy_options::overwrite_existing, ec);
            if (!ec && is_compressible_image(lp_dest.string())) {
                compress_image(lp_dest.string(), opts.compress_quality,
                               opts.compress_strip_exif);
            }
            // 3) ffmpeg image re-encode (JPEG), only if ffmpeg is on PATH.
            ffmpeg_variant(src, dir_ / fs::u8path(base + ".ffmpeg.jpg"), "-q:v 3");
            ++img_done_;
        } else {  // Video: lightpress has no video codec, so only original + ffmpeg.
            ffmpeg_variant(src, dir_ / fs::u8path(base + ".ffmpeg.mp4"),
                           "-c:v libx265 -crf 28");
            ++vid_done_;
        }
    }

  private:
    bool ensure_dir() {
        if (created_) return ok_;
        created_ = true;
        std::error_code ec;
        fs::create_directories(dir_, ec);
        ok_ = !ec;
        if (!ok_) {
            log_warn("comparison: could not create " + dir_.string());
            return false;
        }
        // Drop a README explaining this folder is a throwaway tuning aid.
        std::ofstream rd(dir_ / fs::u8path("README.txt"), std::ios::binary);
        if (rd) {
            rd << "image-movie-comparison/ — TEMPORARY A/B tuning aid\n"
                  "====================================================\n\n"
                  "This folder is NOT part of your export. It holds a small sample of\n"
                  "media encoded three ways so you can compare quality and file size:\n\n"
                  "  NAME.original.<ext>    the untouched source file\n"
                  "  NAME.lightpress.<ext>  re-encoded by lightpress (images only)\n"
                  "  NAME.ffmpeg.<ext>      re-encoded by ffmpeg, if ffmpeg is installed\n"
                  "                         (images -> JPEG -q:v 3; videos -> H.265 crf 28)\n\n"
                  "Open the originals next to the re-encodes, pick the quality/size\n"
                  "trade-off you like, then set the compression options accordingly.\n\n"
                  "SAFE TO DELETE: once you are happy with the settings, delete this\n"
                  "entire folder. Nothing else in the export references it.\n";
        }
        return ok_;
    }

    // Quiet redirect for child processes (stdout+stderr to the null device).
    static const char* dev_null() {
#ifdef _WIN32
        return " >NUL 2>&1";
#else
        return " >/dev/null 2>&1";
#endif
    }

    // Probe for ffmpeg once (std::system("ffmpeg -version")), cache the result.
    bool ffmpeg_ok() {
        if (ffmpeg_probed_) return ffmpeg_present_;
        ffmpeg_probed_ = true;
        ffmpeg_present_ =
            std::system((std::string("ffmpeg -version") + dev_null()).c_str()) == 0;
        if (!ffmpeg_present_)
            log_info("comparison: ffmpeg not found on PATH — emitting "
                     "original + lightpress variants only");
        return ffmpeg_present_;
    }

    // Writes one ffmpeg-encoded variant `in` -> `out` with the given codec args.
    // No-op when ffmpeg is unavailable. Skips paths containing a double quote so
    // the shell command can't be broken out of. Best-effort: failures are logged
    // and ignored (the original + lightpress variants still stand).
    void ffmpeg_variant(const std::string& in, const fs::path& out_path,
                        const std::string& codec_args) {
        if (!ffmpeg_ok()) return;
        const std::string out = out_path.string();
        if (in.find('"') != std::string::npos || out.find('"') != std::string::npos) {
            log_warn("comparison: skipping ffmpeg variant for a path with a quote");
            return;
        }
        const std::string cmd = "ffmpeg -loglevel error -y -i \"" + in + "\" " +
                                codec_args + " \"" + out + "\"" + dev_null();
        if (std::system(cmd.c_str()) != 0)
            log_warn("comparison: ffmpeg variant failed (skipped)");
    }

    fs::path dir_;
    int limit_ = 0;
    int img_done_ = 0;
    int vid_done_ = 0;
    bool created_ = false;
    bool ok_ = false;
    bool ffmpeg_probed_ = false;
    bool ffmpeg_present_ = false;
};

// Copies a conversation's attachment files into a per-conversation folder
// <out_dir>/<adir>/ (adir is the conversation's file stem, optionally dot-hidden)
// and records each copied file's output-relative path on the Attachment, so the
// renderers can link to it. Returns the number of files actually copied.
// `seen_src` dedupes across conversations; `used_rel` keeps destination names
// unique. Missing source files are skipped (only metadata is then exported).
int copy_chat_attachments(Chat& chat, const std::string& out_dir,
                          const std::string& adir,
                          std::unordered_map<std::string, std::string>& seen_src,
                          std::unordered_set<std::string>& used_rel,
                          const ExportOptions& opts, ExportSummary& summary,
                          MediaComparison* cmp) {
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

            // Feed the A/B comparison sampler the ORIGINAL source (best-effort,
            // bounded to opts.media_comparison_samples of each media kind).
            if (cmp) cmp->offer(src, classify_media(src), opts);

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

            // Re-compress the just-copied image in place (JPEG/PNG only; other
            // types are skipped inside compress_image). Non-fatal: on any error
            // the original copy is kept. Accumulate the savings for the report.
            if (opts.compress_media) {
                const std::string dest = out_path(out_dir, rel).string();
                if (is_compressible_image(dest)) {
                    CompressResult cr =
                        compress_image(dest, opts.compress_quality,
                                       opts.compress_strip_exif);
                    summary.media_bytes_before += cr.bytes_before;
                    summary.media_bytes_after += cr.bytes_after;
                    if (cr.changed) {
                        ++summary.media_files_compressed;
                        log_info("compressed " + fs::path(rel).filename().string() +
                                 ": " + std::to_string(cr.bytes_before) + " -> " +
                                 std::to_string(cr.bytes_after) + " bytes");
                    }
                }
            }
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

// PBKDF2 round count for encryption-at-rest. Matches crypto.hpp's default so the
// self-decrypting HTML and the .enc containers use the same work factor.
constexpr int kEncryptIterations = 250000;

// Reads a whole file into a string (binary). Returns false if it can't be read.
bool read_file(const fs::path& path, std::string& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    out.assign((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    return true;
}

// Encrypts one already-written output file in place / alongside it, dispatching
// by extension. HTML files are rewrapped as a self-decrypting page that still
// opens in any browser; everything else becomes a "<file>.enc" JSON container
// (AES-256-GCM via PBKDF2) and the plaintext original is removed. The password
// is never logged. A failure here is non-fatal: the (plaintext) file is left in
// place and a warning is logged, since the conversation data is already on disk.
// Takes an fs::path (not a string) so Unicode names survive on Windows, where
// path.string() would lossily round-trip through the active code page.
void encrypt_output_file(const fs::path& path, const ExportOptions& opts) {
    std::error_code ec;
    const std::string name = path.filename().string();  // for logging only
    std::string contents;
    if (!read_file(path, contents)) {
        log_warn("could not read for encryption: " + name);
        return;
    }

    std::string ext = path.extension().string();
    for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

    if (ext == ".html" || ext == ".htm") {
        // Re-wrap the page so it decrypts in-browser; overwrite the same .html.
        std::string wrapped =
            self_decrypting_html(opts.encrypt_password, contents, kEncryptIterations);
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        if (!out) {
            log_warn("could not rewrite encrypted HTML: " + name);
            return;
        }
        out << wrapped;
        log_info("encrypted (self-decrypting HTML): " + name);
        return;
    }

    // Non-HTML: write "<file>.enc" JSON container, then delete the plaintext.
    EncryptedBlob blob =
        encrypt_with_password(opts.encrypt_password, contents, kEncryptIterations);
    fs::path enc_path = path;
    enc_path += ".enc";  // append to the full name (keeps the original extension)
    std::ofstream out(enc_path, std::ios::binary | std::ios::trunc);
    if (!out) {
        log_warn("could not write encrypted container: " + enc_path.filename().string());
        return;
    }
    // The base64 alphabet (A-Za-z0-9+/=) contains no JSON-special characters, so
    // these values need no escaping.
    out << "{\"alg\":\"AES-256-GCM\",\"kdf\":\"PBKDF2-SHA256\",\"iter\":"
        << blob.iterations << ",\"salt\":\"" << blob.salt_b64 << "\",\"iv\":\""
        << blob.iv_b64 << "\",\"ct\":\"" << blob.ciphertext_b64 << "\"}";
    out.close();
    fs::remove(path, ec);  // drop the plaintext original
    if (ec) log_warn("encrypted container written but plaintext remains: " + name);
    log_info("encrypted (container): " + enc_path.filename().string());
}

// Resolves a per-conversation background for `chat` from opts.chat_backgrounds,
// keyed by the chat identifier/guid/title or by any participant handle (so a
// background set on a person applies to the 1:1 chat). When the match is a local
// file path and the export is self-contained (embed_attachments), the file is
// inlined as a data URI; otherwise the value (path or data URI) passes through.
void apply_chat_background(Chat& chat, const ExportOptions& opts) {
    if (opts.chat_backgrounds.empty()) return;

    const auto& bg = opts.chat_backgrounds;
    const std::string* hit = nullptr;
    auto look = [&](const std::string& key) {
        if (hit || key.empty()) return;
        auto it = bg.find(key);
        if (it != bg.end()) hit = &it->second;
    };
    look(chat.chat_identifier);
    look(chat.guid);
    look(chat.title());
    for (const std::string& p : chat.participants) look(p);
    if (!hit) return;

    std::string value = *hit;
    // Inline a local file only for self-contained exports; leave URLs/data URIs
    // and pass-through paths untouched.
    const bool is_uri = value.rfind("data:", 0) == 0 ||
                        value.rfind("http://", 0) == 0 ||
                        value.rfind("https://", 0) == 0;
    if (opts.embed_attachments && !is_uri) {
        std::string src = expand_user_path(value);
        std::error_code ec;
        std::string bytes;
        if (fs::exists(fs::u8path(src), ec) && !ec && read_file(fs::u8path(src), bytes)) {
            value = "data:" + guess_mime(src, "") + ";base64," + base64_encode(bytes);
        }
    }
    chat.background_uri = value;
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
        // A/B comparison sampler (lightpress vs ffmpeg) — only when requested and
        // attachments are being copied (it samples copied-attachment sources).
        std::unique_ptr<MediaComparison> comparison;
        if (opts.media_comparison && opts.copy_attachments)
            comparison = std::make_unique<MediaComparison>(
                out_dir, std::max(0, opts.media_comparison_samples));
        int written = 0;
        Stats stats;  // accumulated only when opts.stats_cover (written at the end)
        // Chats with messages loaded; populated when timeline_page is enabled.
        std::vector<Chat> timeline_chats;

        // Location correlation: load the external fix set ONCE (not per chat).
        // An empty/unknown source yields no fixes, so annotate_locations no-ops.
        std::vector<LocationFix> location_fixes;
        if (opts.location_correlate && !opts.location_data_path.empty()) {
            location_fixes =
                load_location_fixes(opts.location_source, opts.location_data_path);
            log_info("location: loaded " + std::to_string(location_fixes.size()) +
                     " fix(es) from " + opts.location_source);
        }

        // Derive render options from the export options flags once up front.
        StatsRenderOpts srOpts;
        srOpts.timeline    = opts.stats_timeline;
        srOpts.hourly      = opts.stats_hourly;
        srOpts.weekday     = opts.stats_weekday;
        srOpts.top_texters = opts.stats_top_texters;
        srOpts.word_stats  = opts.stats_word_stats;
        srOpts.fun_facts   = opts.stats_fun_facts;

        int total = 0;
        for (const Chat& c : chats)
            if (c.message_count > 0 && matches_participants(c, opts.only_participants))
                ++total;

        // For combined export, one file streams every conversation in turn.
        std::ofstream combined;
        fs::path combined_path;
        if (opts.combined) {
            combined_path =
                out_path(out_dir, std::string(combined_stem()) + "." + ext);
            combined.open(combined_path, std::ios::binary);
            if (!combined) {
                summary.error = "cannot write '" + combined_path.string() + "'";
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

            // Annotate this conversation's messages with best-guess locations
            // before anything renders or stashes it (timeline copy included), so
            // the HTML shows location badges. annotate_locations takes a vector;
            // wrap the single chat, then read the labels back onto it.
            if (!location_fixes.empty()) {
                std::vector<Chat> one{std::move(chat)};
                annotate_locations(one, location_fixes);
                chat = std::move(one.front());
            }

            // Per-conversation background (by chat identifier or participant).
            apply_chat_background(chat, opts);

            // Fold this conversation into the cover-page accumulator while its
            // bodies are in memory (they're cleared at the bottom of the loop).
            if (opts.stats_cover) stats_add(stats, chat);

            // Stash a copy for the timeline page (messages still in memory).
            if (opts.timeline_page) timeline_chats.push_back(chat);

            if (opts.copy_attachments) {
                // Per-conversation folder named like the file stem (optionally hidden).
                const std::string adir =
                    (opts.hidden_attachment_dir ? "." : "") + slug;
                summary.attachments_copied +=
                    copy_chat_attachments(chat, out_dir, adir, seen_src, used_attach,
                                          opts, summary, comparison.get());
            }
            if (opts.embed_attachments) embed_chat_attachments(chat, embed_cache);

            if (opts.combined) {
                combined << combined_item(chat, fmt, static_cast<std::size_t>(written));
            } else {
                std::string name = slug + "." + ext;
                for (int n = 2; used_names.count(name); ++n)
                    name = slug + "-" + std::to_string(n) + "." + ext;
                used_names.insert(name);

                // For cover-page top-texters, link 1:1 conversations to their file.
                if (opts.stats_cover && chat.participants.size() == 1)
                    stats.handle_to_file[chat.participants[0]] = name;

                fs::path path = out_path(out_dir, name);
                std::ofstream out(path, std::ios::binary);
                if (!out) {
                    summary.error = "cannot write '" + name + "' in " + out_dir;
                    return summary;
                }
                // For HTML format, optionally append per-conversation stats.
                if (fmt == Format::Html && opts.stats_per_conversation) {
                    Stats chatStats;
                    stats_add(chatStats, chat);
                    out << render_html(chat, &chatStats, srOpts);
                } else {
                    out << render(chat, fmt);
                }
                out.close();  // flush before any in-place encryption
                if (opts.encrypt_output && !opts.encrypt_password.empty())
                    encrypt_output_file(path, opts);
            }

            // Release this conversation's bodies before moving to the next one.
            chat.messages.clear();
            chat.messages.shrink_to_fit();
            ++written;
            if (opts.on_progress) opts.on_progress(written, total);
        }

        if (opts.combined) {
            combined << combined_epilogue(fmt);
            combined.close();  // flush before any in-place encryption
            if (opts.encrypt_output && !opts.encrypt_password.empty())
                encrypt_output_file(combined_path, opts);
        }

        // Statistics cover page: a self-contained HTML recap of the whole run.
        // Named "00-..." so it sorts to the top of the output folder. A write
        // failure here is non-fatal — the conversations are already on disk.
        if (opts.stats_cover) {
            fs::path path = out_path(out_dir, "00-statistics.html");
            std::string page = render_stats_html(stats, srOpts);
            // If media compression ran, splice the crypto-free "space saved"
            // report into the page (inside the .wrap, just before </body>) so it
            // appears on the cover. Done before any encryption rewraps the file.
            if (opts.compress_media && summary.media_bytes_before > 0) {
                std::string frag = render_space_saved_html(summary.media_bytes_before,
                                                           summary.media_bytes_after,
                                                           summary.media_files_compressed);
                if (!frag.empty()) {
                    std::size_t pos = page.rfind("</body>");
                    if (pos != std::string::npos) {
                        // Tuck it inside the closing .wrap </div> when present.
                        std::size_t div = page.rfind("</div>", pos);
                        std::size_t at = (div != std::string::npos && div < pos) ? div : pos;
                        page.insert(at, frag);
                    } else {
                        page += frag;  // unexpected layout: append as a fallback
                    }
                }
            }
            std::ofstream out(path, std::ios::binary);
            if (out) {
                out << page;
                out.close();  // flush before any in-place encryption
                log_info("wrote statistics cover page: " + path.filename().string());
                if (opts.encrypt_output && !opts.encrypt_password.empty())
                    encrypt_output_file(path, opts);
            } else {
                log_warn("could not write statistics cover page to " + path.string());
            }
        }

        // Timeline page: swimlane visualisation of every conversation.
        // Named "00-timeline.html" so it sorts near the top of the output folder.
        // A write failure is non-fatal.
        if (opts.timeline_page) {
            TimelineOptions tlOpts;
            tlOpts.show_photos   = opts.timeline_photos;
            tlOpts.show_me_photo = opts.timeline_me_photo;
            tlOpts.show_previews = opts.timeline_previews;
            tlOpts.density       = opts.timeline_density;

            fs::path path = out_path(out_dir, "00-timeline.html");
            std::ofstream tout(path, std::ios::binary);
            if (tout) {
                tout << render_timeline_html(timeline_chats, tlOpts, opts.me_photo_uri);
                tout.close();  // flush before any in-place encryption
                log_info("wrote timeline page: " + path.filename().string());
                if (opts.encrypt_output && !opts.encrypt_password.empty())
                    encrypt_output_file(path, opts);
            } else {
                log_warn("could not write timeline page to " + path.string());
            }
        }

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

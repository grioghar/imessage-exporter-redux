// High-level "export the whole database" operation, shared by the CLI and the
// iOS/embedding bridge so the streaming loop lives in one place.
#pragma once

#include <cstdint>
#include <ctime>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "imsg/exporters.hpp"  // Format

namespace imsg {

// Tunable behaviour for export_database(). Defaults reproduce the original
// one-file-per-conversation, metadata-only, no-filter export.
struct ExportOptions {
    std::string me_label = "Me";  // sender label for messages you sent

    // Visual theme for HTML export (see theme.hpp); ignored by other formats.
    std::string html_theme = "ios";

    // Inclusive timestamp window (epoch seconds). Either bound may be disabled.
    bool has_since = false;
    std::time_t since = 0;
    bool has_until = false;
    std::time_t until = 0;

    // Write a single combined file containing every conversation, instead of
    // one file per conversation.
    bool combined = false;

    // Also write a standalone "00-statistics.html" cover page summarizing the
    // export (totals, time-of-day/weekday charts, top texters, fun facts).
    bool stats_cover = false;
    // Per-conversation stats appended to each conversation's HTML page.
    bool stats_per_conversation = false;
    // Individual stat sections — apply to both the cover page and per-conversation.
    bool stats_timeline = true;   // activity timeline chart (month by month)
    bool stats_hourly = true;     // messages by hour of day
    bool stats_weekday = true;    // messages by day of week
    bool stats_top_texters = true;// top texters ranking
    bool stats_word_stats = true; // word count, longest message, emoji
    bool stats_fun_facts = true;  // fun facts section

    // Write a standalone "00-timeline.html" swimlane visualisation of every
    // conversation: one horizontal lane per contact, time on the X-axis,
    // messages as clickable dots with a dual-handle range slider for zoom.
    bool timeline_page     = false;
    bool timeline_photos   = true;   // show contact photos in lane headers
    bool timeline_me_photo = true;   // use Me's own contact photo when available
    bool timeline_previews = true;   // hover popup with message preview
    std::string timeline_density = "auto";  // auto | hour | day | week | month
    std::string me_photo_uri;               // optional data URI for the Me avatar

    // ── Media compression (lightpress) ────────────────────────────────────
    // Re-encode copied/embedded images through the lightpress codec to shrink
    // the export. Requires copy_attachments or embed_attachments to have media.
    bool compress_media = false;
    int  compress_quality = 80;        // 1 (smallest) – 100 (best); slider in GUI
    bool compress_strip_exif = true;   // drop EXIF/metadata while re-encoding
    // Produce a side-by-side "image-movie-comparison/" folder: a random sample
    // of each media type encoded by lightpress AND by ffmpeg, for A/B tuning.
    // TEMPORARY tuning aid — remove the folder once happy with the settings.
    bool media_comparison = false;
    int  media_comparison_samples = 5;  // per media type

    // ── Security: encryption-at-rest + signing ────────────────────────────
    // Password-protect the export. For HTML the ciphertext is embedded with an
    // inline Web Crypto decryptor (self-decrypting page); JSON/TXT get an
    // encrypted container. Empty password = no encryption.
    bool encrypt_output = false;
    std::string encrypt_password;      // transient; never persisted in plaintext
    // Sign generated PDFs with a PKCS#12 (.p12) certificate.
    bool sign_pdf = false;
    std::string pdf_cert_path;
    std::string pdf_cert_password;     // transient
    // Write the export onto a newly-created, password-protected encrypted disk
    // image (macOS .sparseimage via hdiutil). Empty password = disabled.
    bool encrypted_volume = false;
    std::string encrypted_volume_password;  // transient

    // ── Location correlation ──────────────────────────────────────────────
    // Annotate messages/timeline with a best-guess location + confidence by
    // cross-referencing timestamps against a location source.
    bool location_correlate = false;
    std::string location_source;       // "photos" | "routined" | "takeout"
    std::string location_data_path;    // path to the chosen source

    // ── Per-conversation backgrounds (configured in the GUI's Select People) ─
    // Maps a chat identifier (or participant handle) to a background/poster
    // image (file path or data URI) rendered behind that conversation.
    std::map<std::string, std::string> chat_backgrounds;

    // Copy each attachment's file into <out_dir>/attachments/... and link to it
    // from the export, instead of exporting only attachment metadata.
    bool copy_attachments = false;

    // Inline each attachment's bytes as a base64 data URI so the HTML/JSON output
    // is self-contained (larger files). Independent of copy_attachments.
    bool embed_attachments = false;

    // When copying attachments, name the per-conversation folder with a leading
    // dot (hidden on macOS/Linux) instead of a plain name.
    bool hidden_attachment_dir = false;

    // Resolve handles to names. If `contacts_path` is non-empty it is used (a
    // .abcddb file or a directory of them); otherwise, when `use_contacts` is
    // set, the default macOS AddressBook location is loaded.
    bool use_contacts = false;
    std::string contacts_path;

    // Also merge names from the persistent contacts store (e.g. downloaded Google
    // Contacts). Empty store_path means the default per-user location.
    bool use_contact_store = false;
    std::string contact_store_path;

    // Only export conversations that include at least one of these participants
    // (matched against the chat's participant list / title). Empty = all chats.
    std::vector<std::string> only_participants;

    // Skip conversations whose output file already exists (used to resume an
    // interrupted job without redoing finished files).
    bool skip_existing = false;

    // Called after each conversation with (processed, total) for progress / for
    // persisting a resume index. May be null.
    std::function<void(int processed, int total)> on_progress;

    // Called before each conversation (on the calling thread); return true to
    // stop the export early — conversations already written are kept and the run
    // still reports success. May block internally to implement pause/resume. May
    // be null (never stops). Used by the GUI's Stop/Pause buttons.
    std::function<bool()> should_stop;
};

struct ExportSummary {
    bool ok = false;
    int conversations = 0;      // number of conversations written on success
    int attachments_copied = 0;  // files copied when copy_attachments is set
    // Media-compression accounting (populated when compress_media is set), used
    // by the statistics page's before/after "space saved" report.
    std::int64_t media_bytes_before = 0;
    std::int64_t media_bytes_after = 0;
    int media_files_compressed = 0;
    std::string error;          // human-readable message when ok == false
};

// Opens the Messages database at `db_path` (read-only) and writes the export
// into `out_dir` in the given format per `opts`. Conversations are streamed one
// at a time, so peak memory is bounded by the largest single conversation
// rather than the whole database. Never throws — failures are reported in the
// returned summary's `error`.
ExportSummary export_database(const std::string& db_path,
                              const std::string& out_dir, Format fmt,
                              const ExportOptions& opts = {});

}  // namespace imsg

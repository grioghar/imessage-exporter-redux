// High-level "export the whole database" operation, shared by the CLI and the
// iOS/embedding bridge so the streaming loop lives in one place.
#pragma once

#include <ctime>
#include <functional>
#include <string>
#include <vector>

#include "imsg/exporters.hpp"  // Format

namespace imsg {

// Tunable behaviour for export_database(). Defaults reproduce the original
// one-file-per-conversation, metadata-only, no-filter export.
struct ExportOptions {
    std::string me_label = "Me";  // sender label for messages you sent

    // Inclusive timestamp window (epoch seconds). Either bound may be disabled.
    bool has_since = false;
    std::time_t since = 0;
    bool has_until = false;
    std::time_t until = 0;

    // Write a single combined file containing every conversation, instead of
    // one file per conversation.
    bool combined = false;

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
};

struct ExportSummary {
    bool ok = false;
    int conversations = 0;      // number of conversations written on success
    int attachments_copied = 0;  // files copied when copy_attachments is set
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

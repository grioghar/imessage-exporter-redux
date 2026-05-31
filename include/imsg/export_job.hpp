// High-level "export the whole database" operation, shared by the CLI and the
// iOS/embedding bridge so the streaming loop lives in one place.
#pragma once

#include <string>

#include "imsg/exporters.hpp"  // Format

namespace imsg {

struct ExportSummary {
    bool ok = false;
    int conversations = 0;  // number of files written on success
    std::string error;      // human-readable message when ok == false
};

// Opens the Messages database at `db_path` (read-only) and writes one file per
// conversation into `out_dir` in the given format. Conversations are streamed
// one at a time, so peak memory is bounded by the largest single conversation
// rather than the whole database. Never throws — failures are reported in the
// returned summary's `error`.
ExportSummary export_database(const std::string& db_path,
                              const std::string& out_dir, Format fmt,
                              const std::string& me_label = "Me");

}  // namespace imsg

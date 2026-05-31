#include "imsg/imsg_bridge.h"

#include <algorithm>
#include <cstring>
#include <string>

#include "imsg/export_job.hpp"
#include "imsg/exporters.hpp"

namespace {

// Copies `msg` into the caller's buffer, NUL-terminated and truncated to fit.
void set_error(char* err_buf, int err_buf_len, const std::string& msg) {
    if (!err_buf || err_buf_len <= 0) return;
    std::size_t n = std::min(static_cast<std::size_t>(err_buf_len - 1), msg.size());
    std::memcpy(err_buf, msg.data(), n);
    err_buf[n] = '\0';
}

}  // namespace

extern "C" int imsg_export(const char* db_path, const char* out_dir,
                           const char* format, const char* me_label,
                           char* err_buf, int err_buf_len) {
    if (!db_path || !out_dir || !format) {
        set_error(err_buf, err_buf_len, "null argument");
        return -1;
    }

    imsg::Format fmt;
    if (!imsg::parse_format(format, fmt)) {
        set_error(err_buf, err_buf_len, std::string("unknown format: ") + format);
        return -1;
    }

    imsg::ExportSummary summary = imsg::export_database(
        db_path, out_dir, fmt, me_label ? me_label : "Me");
    if (!summary.ok) {
        set_error(err_buf, err_buf_len, summary.error);
        return -1;
    }
    return summary.conversations;
}

extern "C" const char* imsg_version(void) { return "0.1.0"; }

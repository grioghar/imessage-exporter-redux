/*
 * imsg_bridge.h — C ABI for embedding the exporter in other languages/apps.
 *
 * This is deliberately a pure C header (no C++ types) so it can be imported
 * directly from Swift/Objective-C as a Clang module. See docs/IOS.md for how an
 * iOS app calls these. The implementation links the C++ core + SQLite.
 */
#ifndef IMSG_BRIDGE_H
#define IMSG_BRIDGE_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Export every conversation in the SQLite Messages database at `db_path` into
 * the directory `out_dir` (created if needed), one file per conversation.
 *
 *   db_path   path to a chat.db the app can read (e.g. a file the user imported
 *             into the app's sandbox via the document picker). Required.
 *   out_dir   destination directory inside the app's sandbox. Required.
 *   format    "txt", "json", or "html". Required.
 *   me_label  label for messages you sent; may be NULL ("Me" is used).
 *   err_buf   optional buffer that receives a NUL-terminated error message on
 *             failure (truncated to err_buf_len). May be NULL.
 *   err_buf_len  size of err_buf in bytes.
 *
 * Returns the number of conversations written (>= 0) on success, or -1 on
 * failure (with a message in err_buf when provided). Never throws.
 */
int imsg_export(const char *db_path, const char *out_dir, const char *format,
                const char *me_label, char *err_buf, int err_buf_len);

/*
 * Sets the diagnostic log threshold (messages go to stderr). Levels:
 *   0 = error, 1 = warn (default), 2 = info, 3 = debug.
 * Out-of-range values are clamped. Safe to call before imsg_export.
 */
void imsg_set_log_level(int level);

/* Library version string, e.g. "0.1.0". Never NULL. */
const char *imsg_version(void);

#ifdef __cplusplus
}  /* extern "C" */
#endif

#endif /* IMSG_BRIDGE_H */

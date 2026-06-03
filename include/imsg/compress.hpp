// In-place media (image) re-compression via the lightpress codec, used by the
// export engine to shrink copied attachments. Lives in imsg_db (it does file
// I/O); the only place besides export_job.cpp that touches the filesystem here.
//
// lightpress is an OPTIONAL build dependency pulled in as the vendor/lightpress
// git submodule. CMake defines IMSG_HAVE_LIGHTPRESS=1 and links the "lightpress"
// target only when the submodule is present; in a source checkout without it,
// compress_image() degrades to a graceful no-op so the tree still builds.
#pragma once

#include <string>

namespace imsg {

// Outcome of a compress_image() call. `changed` is true only when the file was
// actually re-encoded smaller and overwritten; otherwise the original is kept
// and bytes_before == bytes_after == the original size.
struct CompressResult {
    long long bytes_before = 0;
    long long bytes_after = 0;
    bool changed = false;
};

// Re-encode the image at `path` in place at the given `quality` (1..100),
// optionally stripping EXIF/metadata. JPEG and PNG only; any other type (or an
// unreadable/undecodable file) is skipped — returns changed=false with
// bytes_before == bytes_after == the file's original size. Never enlarges: if
// the re-encoded result is not strictly smaller, the original is left untouched.
// Never throws — failures are reported as changed=false.
CompressResult compress_image(const std::string& path, int quality, bool strip_exif);

// True if `path` has an image extension lightpress can handle (jpg/jpeg/png),
// matched case-insensitively. Independent of whether lightpress is compiled in.
bool is_compressible_image(const std::string& path);

}  // namespace imsg

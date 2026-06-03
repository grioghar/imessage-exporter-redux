#include "imsg/compress.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "imsg/log.hpp"

// lightpress is pulled in via the vendor/lightpress submodule and linked only
// when present (see CMakeLists.txt, which defines IMSG_HAVE_LIGHTPRESS). Guard
// every lightpress include so a source checkout without the submodule still
// compiles this file — compress_image() then degrades to a no-op.
#if IMSG_HAVE_LIGHTPRESS
#include "lightpress/image.hpp"
#include "lightpress/jpeg.hpp"
#include "lightpress/png.hpp"
#endif

namespace fs = std::filesystem;

namespace imsg {
namespace {

// Lower-cased file extension including the leading dot (".jpg"), or "" if none.
std::string lower_ext(const std::string& path) {
    std::string ext = fs::path(fs::u8path(path)).extension().string();
    for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return ext;
}

#if IMSG_HAVE_LIGHTPRESS
// Reads a whole file into a byte buffer (Unicode-safe path on Windows via
// u8path). Returns false if the file can't be opened.
bool read_all_bytes(const std::string& path, std::vector<std::uint8_t>& out) {
    std::ifstream f(fs::u8path(path), std::ios::binary);
    if (!f) return false;
    out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    return true;
}

// Overwrites `path` with `bytes`. Returns false on any I/O error.
bool write_all_bytes(const std::string& path, const std::vector<std::uint8_t>& bytes) {
    std::ofstream f(fs::u8path(path), std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f.write(reinterpret_cast<const char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
    return static_cast<bool>(f);
}
#endif  // IMSG_HAVE_LIGHTPRESS

}  // namespace

bool is_compressible_image(const std::string& path) {
    const std::string ext = lower_ext(path);
    return ext == ".jpg" || ext == ".jpeg" || ext == ".png";
}

CompressResult compress_image(const std::string& path, int quality, bool strip_exif) {
    CompressResult r;

#if IMSG_HAVE_LIGHTPRESS
    // Clamp quality to lightpress's valid 1..100 range.
    quality = std::max(1, std::min(100, quality));

    const std::string ext = lower_ext(path);
    const bool is_jpeg = (ext == ".jpg" || ext == ".jpeg");
    const bool is_png = (ext == ".png");
    if (!is_jpeg && !is_png) return r;  // unsupported type: skip, leave {0,0,false}

    std::error_code ec;
    const std::uintmax_t orig = fs::file_size(fs::u8path(path), ec);
    if (ec) return r;  // can't stat — skip silently
    r.bytes_before = static_cast<long long>(orig);
    r.bytes_after = r.bytes_before;  // default: unchanged → after == before

    // Read + decode the source ourselves (Unicode-safe; lp::read_file would use
    // a narrow-path fopen). On any decode failure, keep the original.
    std::vector<std::uint8_t> src;
    if (!read_all_bytes(path, src) || src.empty()) return r;

    lp::Image img = is_jpeg ? lp::jpeg_decode(src.data(), src.size())
                            : lp::png_decode(src.data(), src.size());
    if (!img.valid()) {
        log_warn("compress: could not decode " +
                 fs::path(fs::u8path(path)).filename().string() + " — kept original");
        return r;
    }

    std::vector<std::uint8_t> out;
    if (is_jpeg) {
        lp::JpegEncodeOptions jo;
        jo.quality = quality;
        jo.strip_exif = strip_exif;
        out = lp::jpeg_encode(img, jo);
    } else {  // PNG: lossless. Map the 1..100 "quality" knob onto the zlib level
              // (higher quality → harder compression). PNG metadata stripping is
              // governed by the same strip_exif flag.
        lp::PngEncodeOptions po;
        po.compression = std::max(0, std::min(9, quality / 11));  // 1..100 → ~0..9
        po.strip_metadata = strip_exif;
        out = lp::png_encode(img, po);
    }

    // Never enlarge: only overwrite when the re-encode is strictly smaller.
    if (out.empty() || out.size() >= orig) return r;
    if (!write_all_bytes(path, out)) {
        log_warn("compress: re-encoded but could not rewrite " +
                 fs::path(fs::u8path(path)).filename().string() + " — kept original");
        return r;
    }
    r.bytes_after = static_cast<long long>(out.size());
    r.changed = true;
    return r;
#else
    // lightpress not compiled in: graceful no-op so a source checkout without the
    // submodule still builds. Returns {0, 0, false} per the documented contract.
    (void)path;
    (void)quality;
    (void)strip_exif;
    return r;
#endif  // IMSG_HAVE_LIGHTPRESS
}

}  // namespace imsg

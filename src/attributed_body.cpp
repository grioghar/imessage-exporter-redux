#include "imsg/attributed_body.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace imsg {
namespace {

// Class markers that precede the inline string payload, most specific first.
const std::array<std::string, 3> kMarkers = {
    "NSMutableString", "NSString", "NSAttributedString"};

// Candidate counts of archiver header bytes between the marker and the length
// prefix. Real databases use 5; nearby values are tried for robustness.
constexpr std::array<std::size_t, 3> kHeaderOffsets = {5, 6, 4};

// Reject runs containing low control characters (excluding tab/newline), which
// indicate the payload start was mis-located.
bool looks_like_garbage(std::string_view s) {
    for (unsigned char c : s) {
        if (c < 0x09) return true;
    }
    return false;
}

// Validates that the bytes form well-formed UTF-8.
bool is_valid_utf8(std::string_view s) {
    std::size_t i = 0;
    const std::size_t n = s.size();
    while (i < n) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        std::size_t extra;
        if (c < 0x80) {
            extra = 0;
        } else if ((c >> 5) == 0x06) {
            extra = 1;
        } else if ((c >> 4) == 0x0E) {
            extra = 2;
        } else if ((c >> 3) == 0x1E) {
            extra = 3;
        } else {
            return false;
        }
        if (i + extra >= n) return false;
        for (std::size_t k = 1; k <= extra; ++k) {
            if ((static_cast<unsigned char>(s[i + k]) & 0xC0) != 0x80) return false;
        }
        i += extra + 1;
    }
    return true;
}

// Parses the length-prefixed UTF-8 string that follows a class marker.
// Operates on a view into the original blob; only the returned text allocates.
std::string read_inline_string(std::string_view payload) {
    for (std::size_t offset : kHeaderOffsets) {
        if (offset >= payload.size()) continue;

        unsigned char marker = static_cast<unsigned char>(payload[offset]);
        std::size_t length;
        std::size_t body_start;
        if (marker == 0x81) {
            if (offset + 2 >= payload.size()) continue;
            length = static_cast<unsigned char>(payload[offset + 1]) |
                     (static_cast<unsigned char>(payload[offset + 2]) << 8);
            body_start = offset + 3;
        } else {
            length = marker;
            body_start = offset + 1;
        }
        if (length == 0) continue;
        if (body_start + length > payload.size()) continue;

        std::string_view text = payload.substr(body_start, length);
        if (!text.empty() && is_valid_utf8(text) && !looks_like_garbage(text)) {
            return std::string(text);
        }
    }
    return std::string();
}

// Shared implementation over a view, so neither public overload copies the blob.
std::string decode_impl(std::string_view data) {
    if (data.empty()) return std::string();

    // Trailing attribute runs follow an NSDictionary/NSNumber; dropping from the
    // first such marker avoids decoding archiver metadata as text.
    for (const char* tail : {"NSDictionary", "NSNumber"}) {
        std::size_t idx = data.find(tail);
        if (idx != std::string_view::npos) data = data.substr(0, idx);
    }

    for (const std::string& marker : kMarkers) {
        std::size_t idx = data.find(marker);
        if (idx == std::string_view::npos) continue;
        std::string text = read_inline_string(data.substr(idx + marker.size()));
        if (!text.empty()) return text;
    }
    return std::string();
}

}  // namespace

std::string decode_attributed_body(const unsigned char* data, std::size_t len) {
    if (data == nullptr || len == 0) return std::string();
    return decode_impl(std::string_view(reinterpret_cast<const char*>(data), len));
}

std::string decode_attributed_body(const std::string& blob) {
    return decode_impl(std::string_view(blob));
}

std::string sanitize_text(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    const std::size_t n = s.size();
    std::size_t i = 0;
    while (i < n) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        if (c < 0x80) {  // ASCII: keep tab/newline + printable, drop other C0
            if (c == '\t' || c == '\n' || c >= 0x20) out += static_cast<char>(c);
            ++i;
            continue;
        }
        std::size_t len = (c >> 5) == 0x06   ? 2
                          : (c >> 4) == 0x0E ? 3
                          : (c >> 3) == 0x1E ? 4
                                             : 1;
        if (len == 1 || i + len > n) {  // invalid lead / truncated: drop the byte
            ++i;
            continue;
        }
        std::uint32_t cp = c & (0xFF >> (len + 1));
        for (std::size_t k = 1; k < len; ++k)
            cp = (cp << 6) | (static_cast<unsigned char>(s[i + k]) & 0x3F);
        // C1 controls (U+0080–U+009F) and NBSP (U+00A0): garbled typedstream
        // fragments that look like random characters; NBSP causes phantom text.
        if (len == 2 && c == 0xC2) {
            unsigned char c1 = static_cast<unsigned char>(s[i + 1]);
            if (c1 <= 0xA0) { i += 2; continue; }  // strip C1 + NBSP
        }
        // U+2028 LINE SEPARATOR (0xE2 0x80 0xA8) and U+2029 PARAGRAPH SEPARATOR
        // (0xE2 0x80 0xA9): invisible line-break signals that break text flow.
        if (len == 3 && c == 0xE2 &&
            static_cast<unsigned char>(s[i + 1]) == 0x80) {
            unsigned char c2 = static_cast<unsigned char>(s[i + 2]);
            if (c2 == 0xA8 || c2 == 0xA9) { i += 3; continue; }
        }
        // U+FFFE and U+FFFF (BMP non-characters, 0xEF 0xBF 0xBE/0xBF).
        if (len == 3 && c == 0xEF &&
            static_cast<unsigned char>(s[i + 1]) == 0xBF) {
            unsigned char c2 = static_cast<unsigned char>(s[i + 2]);
            if (c2 == 0xBE || c2 == 0xBF) { i += 3; continue; }
        }
        const bool drop = cp == 0xFFFC || cp == 0xFFFD || cp == 0xFEFF ||
                          (cp >= 0x200B && cp <= 0x200F) ||
                          (cp >= 0xFFF9 && cp <= 0xFFFB);
        if (!drop) out.append(s, i, len);
        i += len;
    }
    return out;
}

}  // namespace imsg

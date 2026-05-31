// Shared helper for opening SQLite databases safely read-only.
#pragma once

#include <cctype>
#include <string>

namespace imsg {

// Builds a read-only/immutable SQLite "file:" URI, percent-encoding the path so
// a stray '?' or '#' can't be parsed as a URI query/fragment and inject
// parameters that override the open flags (e.g. "...?mode=rwc"). Everything
// outside the RFC 3986 unreserved set (and '/') is escaped.
inline std::string sqlite_ro_uri(const std::string& path) {
    static const char* kHex = "0123456789ABCDEF";
    std::string out = "file:";
    out.reserve(path.size() + 24);
    for (unsigned char c : path) {
        if (std::isalnum(c) || c == '-' || c == '.' || c == '_' || c == '~' ||
            c == '/') {
            out += static_cast<char>(c);
        } else {
            out += '%';
            out += kHex[c >> 4];
            out += kHex[c & 0x0F];
        }
    }
    out += "?mode=ro&immutable=1";
    return out;
}

}  // namespace imsg

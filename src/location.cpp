#include "imsg/location.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <sstream>

#include "imsg/log.hpp"

namespace imsg {
namespace {

// --- Minimal Google-Takeout location scanner -------------------------------
// Google Takeout's "Records.json" (and the older "Location History.json") wraps
// an array of samples under a "locations" key, each like:
//   {"timestampMs":"1614556800000","latitudeE7":487654321,"longitudeE7":21234567}
// (newer exports use an ISO "timestamp" string instead of "timestampMs"). We
// don't pull in a JSON library; instead we walk the text record-by-record and
// pull the three fields out of each top-level object inside the array. Tolerant
// of field order, extra fields, whitespace, and string-or-number timestamps.

// Reads the JSON value (number or quoted string) immediately after the first
// occurrence of "key": at or beyond `from` within [begin,end). Returns the raw
// token text (no quotes) in `out` and true on success, leaving `out` empty on
// miss. Search is bounded so a record only sees its own fields.
bool field_after(const std::string& s, std::size_t begin, std::size_t end,
                 const char* key, std::string& out) {
    out.clear();
    const std::string needle = std::string("\"") + key + "\"";
    std::size_t k = s.find(needle, begin);
    if (k == std::string::npos || k >= end) return false;
    std::size_t i = k + needle.size();
    while (i < end && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r'))
        ++i;
    if (i >= end || s[i] != ':') return false;
    ++i;
    while (i < end && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r'))
        ++i;
    if (i >= end) return false;
    if (s[i] == '"') {  // quoted string value
        ++i;
        while (i < end && s[i] != '"') {
            if (s[i] == '\\' && i + 1 < end) ++i;  // skip escaped char
            out += s[i++];
        }
        return true;
    }
    // Bare number (possibly negative / decimal / exponent): read its run.
    while (i < end) {
        const char c = s[i];
        if ((c >= '0' && c <= '9') || c == '-' || c == '+' || c == '.' ||
            c == 'e' || c == 'E')
            out += c;
        else
            break;
        ++i;
    }
    return !out.empty();
}

// Parses an ISO-8601 timestamp ("2021-03-01T00:00:00Z") to epoch seconds via a
// UTC civil-day computation. Returns false on a shape we don't recognise. Only
// used for newer exports; the classic format gives epoch millis directly.
bool iso_to_epoch(const std::string& s, long long& out) {
    int y = 0, mo = 0, d = 0, h = 0, mi = 0, se = 0;
    // Accept the leading "YYYY-MM-DDTHH:MM:SS"; ignore any fractional/zone tail.
    if (std::sscanf(s.c_str(), "%d-%d-%dT%d:%d:%d", &y, &mo, &d, &h, &mi, &se) < 3)
        return false;
    // days_from_civil (Howard Hinnant's algorithm), then add the time of day.
    int yy = y - (mo <= 2);
    const long long era = (yy >= 0 ? yy : yy - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(yy - era * 400);
    const unsigned doy =
        (153 * (mo + (mo > 2 ? -3 : 9)) + 2) / 5 + static_cast<unsigned>(d) - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    const long long days = era * 146097LL + static_cast<long long>(doe) - 719468;
    out = days * 86400LL + h * 3600LL + mi * 60LL + se;
    return true;
}

// Pulls the timestamp (epoch seconds) out of a record's [begin,end) span. Tries
// "timestampMs" (millis) first, then an ISO "timestamp" string.
bool record_epoch(const std::string& s, std::size_t begin, std::size_t end,
                  long long& epoch) {
    std::string v;
    if (field_after(s, begin, end, "timestampMs", v) && !v.empty()) {
        epoch = std::strtoll(v.c_str(), nullptr, 10) / 1000;  // ms -> s
        return true;
    }
    if (field_after(s, begin, end, "timestamp", v) && !v.empty())
        return iso_to_epoch(v, epoch);
    return false;
}

}  // namespace

std::vector<LocationFix> parse_takeout_records(const std::string& json_path) {
    std::vector<LocationFix> fixes;
    std::ifstream in(json_path, std::ios::binary);
    if (!in) {
        log_warn("location: cannot open Takeout file: " + json_path);
        return fixes;
    }
    std::ostringstream buf;
    buf << in.rdbuf();
    const std::string s = buf.str();

    // Confine the scan to the "locations" array when present (older exports nest
    // the samples there); otherwise scan the whole document so a bare array of
    // records still works.
    std::size_t scan = 0;
    std::size_t loc = s.find("\"locations\"");
    if (loc != std::string::npos) {
        std::size_t lb = s.find('[', loc);
        if (lb != std::string::npos) scan = lb + 1;
    }

    // Walk top-level objects within the array span. We track brace depth so a
    // record is exactly one depth-1 object; latE7/lonE7 only count when both are
    // present (a record without coordinates is skipped).
    int depth = 0;
    std::size_t rec_begin = std::string::npos;
    for (std::size_t i = scan; i < s.size(); ++i) {
        const char c = s[i];
        if (c == '{') {
            if (depth == 0) rec_begin = i;
            ++depth;
        } else if (c == '}') {
            --depth;
            if (depth == 0 && rec_begin != std::string::npos) {
                const std::size_t rec_end = i + 1;
                std::string lat_s, lon_s;
                if (field_after(s, rec_begin, rec_end, "latitudeE7", lat_s) &&
                    field_after(s, rec_begin, rec_end, "longitudeE7", lon_s)) {
                    LocationFix f;
                    // E7 fixed-point: degrees * 1e7 (signed). strtoll keeps sign.
                    f.lat = std::strtoll(lat_s.c_str(), nullptr, 10) / 1e7;
                    f.lon = std::strtoll(lon_s.c_str(), nullptr, 10) / 1e7;
                    long long ep = 0;
                    if (record_epoch(s, rec_begin, rec_end, ep)) f.epoch = ep;
                    fixes.push_back(f);
                }
                rec_begin = std::string::npos;
            }
            if (depth < 0) break;  // left the array
        } else if (c == ']' && depth == 0) {
            break;  // end of the locations array
        }
    }
    if (log_debug_enabled())
        log_debug("location: parsed " + std::to_string(fixes.size()) +
                  " Takeout fixes from " + json_path);
    return fixes;
}

std::vector<LocationFix> load_location_fixes(const std::string& source,
                                             const std::string& path) {
    if (source == "takeout") return parse_takeout_records(path);
    // TODO(0.7.x): "photos" (Apple Photos library ZASSET geo columns) and
    // "routined" (the macOS routined cache) are SQLite-backed; they will be
    // wired from the SQLite layer (imsg_db) and call back into guess/annotate
    // here. Until then they yield no fixes.
    if (source == "photos" || source == "routined") {
        log_warn("location: source '" + source +
                 "' is not yet implemented (SQLite-backed); no fixes loaded");
        return {};
    }
    log_warn("location: unknown source '" + source + "'");
    return {};
}

LocationGuess guess_location(const std::vector<LocationFix>& fixes, long long epoch) {
    LocationGuess g;
    if (fixes.empty()) return g;  // valid stays false

    // Nearest fix in time (linear scan; fix lists are modest and unsorted).
    const LocationFix* best = nullptr;
    long long best_gap = 0;
    for (const LocationFix& f : fixes) {
        const long long gap = std::llabs(epoch - f.epoch);
        if (!best || gap < best_gap) {
            best = &f;
            best_gap = gap;
        }
    }
    if (best_gap > 86400) return g;  // nearest fix > 1 day away: not confident

    if (best_gap <= 300) g.confidence = 95;        // <= 5 min
    else if (best_gap <= 3600) g.confidence = 75;  // <= 1 h
    else if (best_gap <= 21600) g.confidence = 40; // <= 6 h
    else g.confidence = 10;                        // <= 1 day

    if (!best->label.empty()) {
        g.label = best->label;
    } else {
        // "lat,lon" to 3 decimals when we have only coordinates.
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%.3f,%.3f", best->lat, best->lon);
        g.label = buf;
    }
    g.valid = true;
    return g;
}

void annotate_locations(std::vector<Chat>& chats, const std::vector<LocationFix>& fixes) {
    if (fixes.empty()) return;
    for (Chat& chat : chats) {
        for (Message& m : chat.messages) {
            if (!m.has_date) continue;
            const LocationGuess g =
                guess_location(fixes, static_cast<long long>(m.date));
            if (g.valid && !g.label.empty()) {
                m.location_label = g.label;
                m.location_confidence = g.confidence;
            }
        }
    }
}

}  // namespace imsg

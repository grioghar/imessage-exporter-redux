// Correlates messages with an external location history so the HTML export can
// annotate each message with where it was likely sent. chat.db itself records
// no location (see stats.hpp), so the fixes come from an outside source the user
// supplies — today a Google Takeout "Records.json" location export.
//
// SQLite-free (lives in imsg_core), so it stays unit-testable anywhere. Sources
// that need SQLite (Apple Photos' library, the macOS routined cache) are stubbed
// to return {} here and will be wired in later from the SQLite-backed layer.
#pragma once

#include <string>
#include <vector>

#include "imsg/models.hpp"

namespace imsg {

// A single timestamped location sample.
struct LocationFix {
    long long epoch = 0;   // Unix epoch seconds
    double lat = 0.0;
    double lon = 0.0;
    std::string label;     // optional place name; empty when only lat/lon known
};

// The result of correlating one timestamp against a set of fixes.
struct LocationGuess {
    std::string label;     // place name, or "lat,lon" to 3 decimals
    int confidence = 0;    // 0..100; higher = a fix nearer in time
    bool valid = false;    // false when there were no fixes / nearest gap > 1 day
};

// Parses a Google Takeout location-history JSON file. Reads the "locations"
// array of objects carrying integer "timestampMs", "latitudeE7", "longitudeE7"
// (degrees * 1e7). Robust to surrounding fields and formatting; returns the
// fixes in file order (an unreadable/!-matching file yields {}).
std::vector<LocationFix> parse_takeout_records(const std::string& json_path);

// Loads fixes from a named source: "takeout" -> parse_takeout_records(path).
// "photos" / "routined" are recognised but SQLite-backed and not yet wired, so
// they return {} (a TODO marker — see the .cpp). An unknown source returns {}.
std::vector<LocationFix> load_location_fixes(const std::string& source,
                                             const std::string& path);

// Finds the fix nearest in time to `epoch` and grades the match. Confidence by
// the time gap to that nearest fix: <= 5 min ~95, <= 1 h ~75, <= 6 h ~40, else
// <= 10. valid is false when there are no fixes or the nearest gap exceeds one
// day (too far apart to claim a location).
LocationGuess guess_location(const std::vector<LocationFix>& fixes, long long epoch);

// Annotates every dated message across `chats` with its best-guess location,
// setting Message::location_label and Message::location_confidence. Messages
// with no date, or with no confident guess, are left untouched. No-op when
// `fixes` is empty.
void annotate_locations(std::vector<Chat>& chats, const std::vector<LocationFix>& fixes);

}  // namespace imsg

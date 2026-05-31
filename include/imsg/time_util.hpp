// Apple ("Mac absolute time") timestamp conversion.
#pragma once

#include <ctime>
#include <string>

namespace imsg {

// Converts a Messages timestamp (offset from 2001-01-01 UTC, in nanoseconds on
// modern macOS or whole seconds on legacy databases) to epoch seconds.
// Returns false for zero/missing values.
bool apple_time_to_epoch(long long value, std::time_t& out);

// Formats epoch seconds as local "YYYY-MM-DD HH:MM:SS".
std::string format_timestamp(std::time_t t);

// Parses a "YYYY-MM-DD" or "YYYY-MM-DD HH:MM:SS" string as local time into epoch
// seconds. When `end_of_day` is set and the string carries no time component,
// the result is that day's 23:59:59 (so a date-only `--until` is inclusive of
// the whole day). Returns false on a malformed string.
bool parse_date(const std::string& text, std::time_t& out, bool end_of_day = false);

}  // namespace imsg

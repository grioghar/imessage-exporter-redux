#include "imsg/time_util.hpp"

#include <cstdio>
#include <ctime>

namespace imsg {
namespace {
// Seconds between the Unix epoch (1970-01-01) and Apple's epoch (2001-01-01).
constexpr long long kAppleToUnixOffset = 978307200LL;
// Values at/above this magnitude are nanoseconds (macOS 10.13+); below, seconds.
constexpr long long kNanosecondThreshold = 100000000000LL;  // 1e11
}  // namespace

bool apple_time_to_epoch(long long value, std::time_t& out) {
    if (value == 0) return false;
    // Compute magnitude via unsigned to avoid the UB of std::llabs(LLONG_MIN)
    // on a corrupt/hostile date column.
    unsigned long long mag = (value < 0)
                                 ? ~static_cast<unsigned long long>(value) + 1ULL
                                 : static_cast<unsigned long long>(value);
    long long seconds =
        (mag >= static_cast<unsigned long long>(kNanosecondThreshold))
            ? value / 1000000000LL
            : value;
    out = static_cast<std::time_t>(kAppleToUnixOffset + seconds);
    return true;
}

std::string format_timestamp(std::time_t t) {
    std::tm tm_buf{};
#if defined(_WIN32)
    localtime_s(&tm_buf, &t);
#else
    localtime_r(&t, &tm_buf);
#endif
    char buffer[32];
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &tm_buf);
    return std::string(buffer);
}

bool parse_date(const std::string& text, std::time_t& out, bool end_of_day) {
    int y = 0, mo = 0, d = 0, h = 0, mi = 0, s = 0;
    int n = std::sscanf(text.c_str(), "%d-%d-%d %d:%d:%d", &y, &mo, &d, &h, &mi, &s);
    if (n < 3) return false;  // need at least a full date
    if (mo < 1 || mo > 12 || d < 1 || d > 31) return false;
    bool has_time = n >= 4;
    if (!has_time && end_of_day) { h = 23; mi = 59; s = 59; }

    std::tm tm{};
    tm.tm_year = y - 1900;
    tm.tm_mon = mo - 1;
    tm.tm_mday = d;
    tm.tm_hour = h;
    tm.tm_min = mi;
    tm.tm_sec = s;
    tm.tm_isdst = -1;  // let mktime resolve DST for the local zone
    std::time_t t = std::mktime(&tm);
    if (t == static_cast<std::time_t>(-1)) return false;
    out = t;
    return true;
}

}  // namespace imsg

#include "imsg/stats.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace imsg {
namespace {

// HTML-escapes the five significant characters. Self-contained so stats.cpp
// stays independent of exporters.cpp.
std::string esc(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            case '\'': out += "&#39;"; break;
            default: out += c;
        }
    }
    return out;
}

// Thousands-separated integer ("1234567" -> "1,234,567"). Plain decimal, locale
// independent so the output is reproducible in tests.
std::string group(long long n) {
    bool neg = n < 0;
    unsigned long long u = neg ? 0ULL - static_cast<unsigned long long>(n)
                               : static_cast<unsigned long long>(n);
    std::string d = std::to_string(u);
    std::string out;
    int c = 0;
    for (auto it = d.rbegin(); it != d.rend(); ++it) {
        if (c && c % 3 == 0) out.push_back(',');
        out.push_back(*it);
        ++c;
    }
    if (neg) out.push_back('-');
    std::reverse(out.begin(), out.end());
    return out;
}

// One decimal place, e.g. 12.3. Used for per-message averages / rates.
std::string fixed1(double v) {
    if (!std::isfinite(v)) v = 0.0;
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.1f", v);
    return std::string(buf);
}

// Counts whitespace-delimited tokens in a UTF-8 string (bytes < 0x80 only need
// checking for ASCII whitespace; multibyte UTF-8 bytes are never whitespace).
long long count_words(const std::string& text) {
    long long n = 0;
    bool in_word = false;
    for (unsigned char c : text) {
        const bool ws = (c == ' ' || c == '\t' || c == '\n' || c == '\r' ||
                         c == '\f' || c == '\v');
        if (ws) {
            in_word = false;
        } else if (!in_word) {
            in_word = true;
            ++n;
        }
    }
    return n;
}

// Decodes the next UTF-8 code point at `i`, advancing `i` past it. Returns the
// code point, or the raw byte (and a single-byte advance) on malformed input so
// the loop always terminates.
std::uint32_t next_codepoint(const std::string& s, std::size_t& i) {
    const auto n = s.size();
    unsigned char c = static_cast<unsigned char>(s[i]);
    auto cont = [&](std::size_t k) {
        return k < n && (static_cast<unsigned char>(s[k]) & 0xC0) == 0x80;
    };
    if (c < 0x80) {
        ++i;
        return c;
    }
    if ((c & 0xE0) == 0xC0 && cont(i + 1)) {
        std::uint32_t cp = (c & 0x1F) << 6 | (static_cast<unsigned char>(s[i + 1]) & 0x3F);
        i += 2;
        return cp;
    }
    if ((c & 0xF0) == 0xE0 && cont(i + 1) && cont(i + 2)) {
        std::uint32_t cp = (c & 0x0F) << 12 |
                           (static_cast<unsigned char>(s[i + 1]) & 0x3F) << 6 |
                           (static_cast<unsigned char>(s[i + 2]) & 0x3F);
        i += 3;
        return cp;
    }
    if ((c & 0xF8) == 0xF0 && cont(i + 1) && cont(i + 2) && cont(i + 3)) {
        std::uint32_t cp = (c & 0x07) << 18 |
                           (static_cast<unsigned char>(s[i + 1]) & 0x3F) << 12 |
                           (static_cast<unsigned char>(s[i + 2]) & 0x3F) << 6 |
                           (static_cast<unsigned char>(s[i + 3]) & 0x3F);
        i += 4;
        return cp;
    }
    ++i;  // malformed lead byte: skip one and keep going
    return c;
}

// Heuristic "is this code point an emoji / pictograph". Covers the common
// emoji blocks plus Misc Symbols / Dingbats and the regional-indicator letters
// (flags). Skin-tone modifiers and the variation selector aren't counted on
// their own, so a single emoji counts once even when modified.
bool is_emoji(std::uint32_t cp) {
    if (cp == 0xFE0F || cp == 0xFE0E) return false;            // variation selectors
    if (cp >= 0x1F3FB && cp <= 0x1F3FF) return false;          // skin-tone modifiers
    return (cp >= 0x1F300 && cp <= 0x1FAFF) ||   // Misc Symbols & Pictographs … Symbols Ext-A
           (cp >= 0x1F000 && cp <= 0x1F02F) ||   // Mahjong tiles
           (cp >= 0x1F0A0 && cp <= 0x1F0FF) ||   // Playing cards
           (cp >= 0x2600 && cp <= 0x26FF) ||     // Misc symbols (☀ ☕ ⚡ …)
           (cp >= 0x2700 && cp <= 0x27BF) ||     // Dingbats (✂ ✅ ✨ …)
           (cp >= 0x1F1E6 && cp <= 0x1F1FF) ||   // Regional indicators (flags)
           cp == 0x2B50 || cp == 0x2B55 ||       // star, heavy circle
           cp == 0x2122 || cp == 0x2139 ||       // ™ ℹ
           (cp >= 0x2194 && cp <= 0x21AA) ||     // arrows used as emoji
           (cp >= 0x231A && cp <= 0x231B) ||     // ⌚ ⌛
           (cp >= 0x23E9 && cp <= 0x23FA);       // media-control symbols
}

long long count_emoji(const std::string& text) {
    long long n = 0;
    for (std::size_t i = 0; i < text.size();) {
        if (is_emoji(next_codepoint(text, i))) ++n;
    }
    return n;
}

// Days since the Unix epoch for a calendar date, used to detect consecutive
// days regardless of month/year boundaries (a proleptic Gregorian day number).
long long days_from_civil(int y, unsigned m, unsigned d) {
    y -= m <= 2;
    const long long era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(y - era * 400);
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097LL + static_cast<long long>(doe) - 719468;
}

// Longest run of consecutive calendar days that each had >= 1 message.
long long longest_streak(const std::map<std::string, long long>& by_day) {
    long long best = 0, run = 0, prev = 0;
    bool have_prev = false;
    for (const auto& kv : by_day) {
        int y = 0, mo = 0, da = 0;
        if (std::sscanf(kv.first.c_str(), "%d-%d-%d", &y, &mo, &da) != 3) continue;
        const long long day = days_from_civil(y, static_cast<unsigned>(mo),
                                              static_cast<unsigned>(da));
        run = (have_prev && day == prev + 1) ? run + 1 : 1;
        best = std::max(best, run);
        prev = day;
        have_prev = true;
    }
    return best;
}

const char* kWeekdayLong[7] = {"Sunday",   "Monday", "Tuesday", "Wednesday",
                               "Thursday", "Friday", "Saturday"};
const char* kWeekdayShort[7] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};

// A horizontal CSS bar (a labelled row: label, a proportional fill, a count).
// `max` scales the fill width; guard against div-by-zero with at least 1.
void bar_row(std::ostringstream& os, const std::string& label, long long value,
             long long max) {
    const long long denom = std::max<long long>(max, 1);
    const int pct = static_cast<int>(value * 100 / denom);
    os << "<div class=\"bar\"><span class=\"lbl\">" << esc(label) << "</span>"
       << "<span class=\"track\"><span class=\"fill\" style=\"width:" << pct
       << "%\"></span></span>"
       << "<span class=\"val\">" << group(value) << "</span></div>\n";
}

// --- Multi-representation charts (Bar | Line | Pie | Heatmap) ---------------
// Each labelled series renders four interchangeable views; a tiny inline script
// (see kChartScript) toggles which one is visible. No external libraries: every
// view is plain inline SVG / CSS-grid. The series is passed pre-formatted so the
// same machinery serves the hour, weekday, and top-texter charts.

struct Datum {
    std::string label;
    long long value = 0;
};

// A stable, readable hue per slice index — used by the pie legend and slices so
// the same category keeps the same color across the legend and the wheel.
std::string slice_color(std::size_t i) {
    static const char* kPalette[] = {
        "#8b5cf6", "#ec4899", "#f59e0b", "#10b981", "#3b82f6", "#ef4444",
        "#14b8a6", "#a855f7", "#eab308", "#22c55e", "#0ea5e9", "#f43f5e"};
    return kPalette[i % (sizeof(kPalette) / sizeof(kPalette[0]))];
}

// Bar view: the existing horizontal CSS bars (kept as the default look).
void chart_bars(std::ostringstream& os, const std::vector<Datum>& data, long long mx) {
    for (const Datum& d : data) bar_row(os, d.label, d.value, mx);
}

// Line view: an inline SVG polyline scaled to the data over a baseline axis.
// viewBox space is 100 wide x 60 tall; points are evenly spaced horizontally.
void chart_line(std::ostringstream& os, const std::vector<Datum>& data, long long mx) {
    const std::size_t n = data.size();
    const long long denom = std::max<long long>(mx, 1);
    os << "<svg class=\"chart-svg\" viewBox=\"0 0 100 64\" preserveAspectRatio=\"none\" "
          "role=\"img\" aria-label=\"line chart\"><polyline class=\"chart-axis\" "
          "points=\"0,58 100,58\"/><polyline class=\"chart-line\" points=\"";
    for (std::size_t i = 0; i < n; ++i) {
        const double x = (n <= 1) ? 50.0 : (100.0 * i / (n - 1));
        const double y = 58.0 - 54.0 * data[i].value / denom;  // 4px top margin
        char buf[40];
        std::snprintf(buf, sizeof(buf), "%s%.2f,%.2f", i ? " " : "", x, y);
        os << buf;
    }
    os << "\"/></svg>\n";
}

// Pie view: one stacked conic-gradient wheel + a small legend. conic-gradient is
// far simpler (and crisper) than juggling stroke-dasharray on SVG circles.
void chart_pie(std::ostringstream& os, const std::vector<Datum>& data) {
    long long total = 0;
    for (const Datum& d : data) total += d.value;
    if (total <= 0) total = 1;
    os << "<div class=\"chart-pie\"><div class=\"pie-wheel\" style=\"background:conic-gradient(";
    double acc = 0.0;
    bool first = true;
    for (std::size_t i = 0; i < data.size(); ++i) {
        const double start = acc * 100.0 / total;
        acc += static_cast<double>(data[i].value);
        const double end = acc * 100.0 / total;
        char seg[96];
        std::snprintf(seg, sizeof(seg), "%s%s %.3f%% %.3f%%", first ? "" : ",",
                      slice_color(i).c_str(), start, end);
        os << seg;
        first = false;
    }
    os << ")\"></div><ul class=\"pie-legend\">";
    for (std::size_t i = 0; i < data.size(); ++i) {
        const int pct = static_cast<int>(data[i].value * 100 / total);
        os << "<li><span class=\"sw\" style=\"background:" << slice_color(i) << "\"></span>"
           << esc(data[i].label) << " <span class=\"c\">" << pct << "%</span></li>";
    }
    os << "</ul></div>\n";
}

// Heatmap view: a CSS-grid 7x24 (weekday rows x hour columns) keyed off the
// joint hour_by_weekday tally; cell lightness scales inversely with volume.
// Independent of the per-chart `data`, so all three groups share one heatmap.
void chart_heatmap(std::ostringstream& os,
                   const std::array<std::array<int, 24>, 7>& grid) {
    int mx = 0;
    for (const auto& row : grid)
        for (int v : row) mx = std::max(mx, v);
    if (mx <= 0) mx = 1;
    os << "<div class=\"heatmap\"><div class=\"hm-grid\">";
    // Corner spacer, then hour ticks across the top (every 6h for legibility).
    os << "<div class=\"hm-corner\"></div>";
    for (int h = 0; h < 24; ++h) {
        os << "<div class=\"hm-tick\">" << (h % 6 == 0 ? std::to_string(h) : std::string())
           << "</div>";
    }
    for (int wd = 0; wd < 7; ++wd) {
        os << "<div class=\"hm-day\">" << kWeekdayShort[wd] << "</div>";
        for (int h = 0; h < 24; ++h) {
            const int v = grid[wd][h];
            const int lightness = 92 - static_cast<int>(64.0 * v / mx);  // 92%→28%
            char cell[96];
            std::snprintf(cell, sizeof(cell),
                          "<div class=\"hm-cell\" style=\"background:hsl(265,70%%,%d%%)\" "
                          "title=\"%s %02d:00 — %d\"></div>",
                          lightness, kWeekdayShort[wd], h, v);
            os << cell;
        }
    }
    os << "</div></div>\n";
}

// A full switchable chart group: a "Bar | Line | Pie | Heatmap" button row over
// four stacked views, only one shown at a time (Bar by default). `gid` scopes
// the group so several can coexist on one page; the inline script keys off it.
// The section heading is emitted by the caller (so it can live in a <summary>),
// not here.
void chart_group(std::ostringstream& os, int gid, const std::vector<Datum>& data,
                 long long mx, const std::array<std::array<int, 24>, 7>& heat) {
    os << "<div class=\"chart-group\" data-cg=\"" << gid << "\">\n"
       << "<div class=\"chart-tabs\" role=\"tablist\">";
    static const char* kViews[] = {"bar", "line", "pie", "heatmap"};
    static const char* kLabels[] = {"Bar", "Line", "Pie", "Heatmap"};
    for (int v = 0; v < 4; ++v)
        os << "<button type=\"button\" class=\"chart-tab" << (v == 0 ? " active" : "")
           << "\" data-view=\"" << kViews[v] << "\">" << kLabels[v] << "</button>";
    os << "</div>\n";
    os << "<div class=\"chart-view active\" data-view=\"bar\">";
    chart_bars(os, data, mx);
    os << "</div>\n<div class=\"chart-view\" data-view=\"line\">";
    chart_line(os, data, mx);
    os << "</div>\n<div class=\"chart-view\" data-view=\"pie\">";
    chart_pie(os, data);
    os << "</div>\n<div class=\"chart-view\" data-view=\"heatmap\">";
    chart_heatmap(os, heat);
    os << "</div>\n</div>\n";
}

// Inline switcher script: for each .chart-group, clicking a tab shows the view
// whose data-view matches and hides the rest. Scoped per group via [data-cg] so
// multiple groups on one page toggle independently. ~20 lines, no libraries.
const char* kChartScript =
    "<script>(function(){"
    "document.querySelectorAll('.chart-group').forEach(function(g){"
    "g.querySelectorAll('.chart-tab').forEach(function(b){"
    "b.addEventListener('click',function(){"
    "var v=b.getAttribute('data-view');"
    "g.querySelectorAll('.chart-tab').forEach(function(x){"
    "x.classList.toggle('active',x===b)});"
    "g.querySelectorAll('.chart-view').forEach(function(x){"
    "x.classList.toggle('active',x.getAttribute('data-view')===v)});"
    "})})})})();</script>\n";

// CSS for the chart views + the collapsible <details> sections. Appended into
// whichever <style> block the caller already emits.
const char* kChartCss =
    ".chart-tabs{display:flex;gap:4px;margin:0 0 12px;flex-wrap:wrap}"
    ".chart-tab{font:inherit;font-size:.78rem;padding:4px 12px;border:1px solid #d9d9e0;"
    "background:#f5f5f7;color:#48484a;border-radius:999px;cursor:pointer}"
    ".chart-tab.active{background:#7c3aed;color:#fff;border-color:#7c3aed}"
    ".chart-view{display:none}.chart-view.active{display:block}"
    ".chart-svg{width:100%;height:160px;display:block;overflow:visible}"
    ".chart-axis{fill:none;stroke:#d9d9e0;stroke-width:.5}"
    ".chart-line{fill:none;stroke:#7c3aed;stroke-width:1.5;vector-effect:non-scaling-stroke;"
    "stroke-linejoin:round;stroke-linecap:round}"
    ".chart-pie{display:flex;gap:18px;align-items:center;flex-wrap:wrap}"
    ".pie-wheel{width:140px;height:140px;border-radius:50%;flex:0 0 auto}"
    ".pie-legend{list-style:none;margin:0;padding:0;font-size:.85rem}"
    ".pie-legend li{display:flex;align-items:center;gap:6px;margin:3px 0}"
    ".pie-legend .sw{width:12px;height:12px;border-radius:3px;flex:0 0 auto}"
    ".pie-legend .c{color:#6b6b70}"
    ".heatmap{overflow-x:auto}"
    ".hm-grid{display:grid;grid-template-columns:auto repeat(24,1fr);gap:2px;min-width:420px}"
    ".hm-corner,.hm-tick{font-size:.6rem;color:#6b6b70;text-align:center;height:14px}"
    ".hm-day{font-size:.7rem;color:#48484a;padding-right:6px;display:flex;align-items:center}"
    ".hm-cell{aspect-ratio:1;border-radius:2px;min-height:12px}"
    // Collapsible sections: a tappable summary with a chevron that rotates open.
    "details.sec{background:inherit;border:0;padding:0;margin:0}"
    "details.sec>summary{cursor:pointer;list-style:none;font-size:1.15rem;font-weight:600;"
    "margin:0 0 14px;display:flex;align-items:center;gap:8px;user-select:none}"
    "details.sec>summary::-webkit-details-marker{display:none}"
    "details.sec>summary::before{content:'';border:solid #7c3aed;border-width:0 2px 2px 0;"
    "display:inline-block;padding:3px;transform:rotate(-45deg);transition:transform .15s}"
    "details.sec[open]>summary::before{transform:rotate(45deg)}";

// "Month D, YYYY" from epoch seconds, local time (the page reports local
// activity, matching how the conversations themselves are timestamped).
std::string pretty_date(std::time_t t) {
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    static const char* mon[12] = {"January", "February", "March",     "April",
                                  "May",     "June",     "July",      "August",
                                  "September", "October", "November", "December"};
    int mi = tm.tm_mon;
    if (mi < 0 || mi > 11) mi = 0;
    std::ostringstream os;
    os << mon[mi] << ' ' << tm.tm_mday << ", " << (tm.tm_year + 1900);
    return os.str();
}

}  // namespace

void Stats::add(const Chat& chat) {
    ++conversations;
    for (const Message& m : chat.messages) {
        ++total;
        if (m.is_from_me)
            ++sent;
        else
            ++received;

        if (!m.attachments.empty()) {
            ++with_attachment;
            attachments += static_cast<long long>(m.attachments.size());
        }

        words += count_words(m.text);
        emoji += count_emoji(m.text);

        ++per_sender[m.sender.empty() ? "Unknown" : m.sender];
        ++handle_count[m.sender.empty() ? "Unknown" : m.sender];

        if (!m.has_date) continue;  // undated messages: counted, but not time-binned

        std::tm tm{};
#if defined(_WIN32)
        localtime_s(&tm, &m.date);
#else
        localtime_r(&m.date, &tm);
#endif
        if (tm.tm_wday >= 0 && tm.tm_wday < 7) ++by_weekday[tm.tm_wday];
        if (tm.tm_hour >= 0 && tm.tm_hour < 24) ++by_hour[tm.tm_hour];
        if (tm.tm_wday >= 0 && tm.tm_wday < 7 && tm.tm_hour >= 0 && tm.tm_hour < 24)
            ++hour_by_weekday[tm.tm_wday][tm.tm_hour];

        char ybuf[8];
        std::snprintf(ybuf, sizeof(ybuf), "%04d", tm.tm_year + 1900);
        ++by_year[ybuf];

        char dbuf[16];
        std::snprintf(dbuf, sizeof(dbuf), "%04d-%02d-%02d", tm.tm_year + 1900,
                      tm.tm_mon + 1, tm.tm_mday);
        ++by_day[dbuf];

        char mbuf[8];
        std::snprintf(mbuf, sizeof(mbuf), "%04d-%02d", tm.tm_year + 1900, tm.tm_mon + 1);
        ++monthly[mbuf];

        if (!has_dates || m.date < first) first = m.date;
        if (!has_dates || m.date > last) last = m.date;
        has_dates = true;
    }

    if (has_dates) {
        const long long secs = static_cast<long long>(last) - static_cast<long long>(first);
        days_span = secs / 86400 + 1;  // inclusive whole-day span
    }
}

void stats_add(Stats& s, const Chat& chat) { s.add(chat); }

namespace {

// Wraps each section so it is independently collapsible when opts.collapsible:
// <details class="sec" open><summary>Title</summary> … </details>. Otherwise a
// plain <section><h2>Title</h2> … </section>. Passing the same title to both
// keeps the toggle header and the heading identical across modes.
void open_section(std::ostringstream& os, const std::string& title, bool collapsible) {
    if (collapsible)
        os << "<details class=\"sec\" open><summary>" << esc(title) << "</summary>\n";
    else
        os << "<section><h2>" << esc(title) << "</h2>\n";
}
void close_section(std::ostringstream& os, bool collapsible) {
    os << (collapsible ? "</details>\n" : "</section>\n");
}

// Renders a CSS month-grid heatmap timeline into `os`. Each year is a row of
// 12 cells, colored by relative message volume using green hues.
void render_timeline(std::ostringstream& os, const std::map<std::string, int>& monthly,
                     bool collapsible) {
    if (monthly.empty()) return;

    // Find max for color scaling.
    int max_val = 0;
    for (const auto& kv : monthly)
        if (kv.second > max_val) max_val = kv.second;
    if (max_val == 0) return;

    // Collect years present.
    std::vector<int> years;
    for (const auto& kv : monthly) {
        int y = 0;
        std::sscanf(kv.first.c_str(), "%d", &y);
        if (years.empty() || years.back() != y) years.push_back(y);
    }

    static const char* kMonAbbr[12] = {"J","F","M","A","M","J","J","A","S","O","N","D"};

    open_section(os, "Activity Timeline", collapsible);
    os << "<style>"
          ".tl-grid{display:table;border-collapse:separate;border-spacing:3px}"
          ".tl-row{display:table-row}"
          ".tl-cell{display:table-cell;width:28px;height:28px;border-radius:4px;"
          "text-align:center;vertical-align:middle;font-size:.7rem;cursor:default}"
          ".tl-label{display:table-cell;font-size:.75rem;padding-right:6px;"
          "vertical-align:middle;color:#6b6b70;white-space:nowrap}"
          ".tl-hdr{display:table-cell;width:28px;text-align:center;font-size:.7rem;"
          "color:#6b6b70;padding-bottom:2px}"
          "</style>\n"
       << "<div class=\"tl-grid\">\n"
       // Header row with month abbreviations
       << "<div class=\"tl-row\"><div class=\"tl-label\"></div>";
    for (int m = 0; m < 12; ++m)
        os << "<div class=\"tl-hdr\">" << kMonAbbr[m] << "</div>";
    os << "</div>\n";

    for (int y : years) {
        os << "<div class=\"tl-row\"><div class=\"tl-label\">" << y << "</div>";
        for (int m = 1; m <= 12; ++m) {
            char key[8];
            std::snprintf(key, sizeof(key), "%04d-%02d", y, m);
            auto it = monthly.find(key);
            const int count = (it != monthly.end()) ? it->second : 0;
            // Lightness: 90% for 0 messages → 30% for max messages.
            const int lightness = 90 - static_cast<int>(60.0 * count / max_val);
            char style[64];
            std::snprintf(style, sizeof(style),
                          "background:hsl(142,60%%,%d%%)", lightness);
            char title[32];
            std::snprintf(title, sizeof(title), "%04d-%02d: %d messages", y, m, count);
            os << "<div class=\"tl-cell\" style=\"" << style
               << "\" title=\"" << title << "\"></div>";
        }
        os << "</div>\n";
    }
    os << "</div>";
    close_section(os, collapsible);
}

// Shared body of sections (no full HTML wrapper), used by both render_stats_html
// and render_stats_section_html.
void render_stats_body(std::ostringstream& os, const Stats& st,
                       const StatsRenderOpts& opts) {
    const bool col = opts.collapsible;
    // --- Derived headline figures ------------------------------------------
    const double words_per_msg = st.total ? static_cast<double>(st.words) / st.total : 0.0;
    const double emoji_per_100 = st.total ? static_cast<double>(st.emoji) * 100.0 / st.total : 0.0;
    const double msgs_per_day = st.days_span ? static_cast<double>(st.total) / st.days_span : 0.0;

    // Busiest weekday / hour.
    int peak_wd = 0;
    for (int i = 1; i < 7; ++i)
        if (st.by_weekday[i] > st.by_weekday[peak_wd]) peak_wd = i;
    int peak_hr = 0;
    for (int i = 1; i < 24; ++i)
        if (st.by_hour[i] > st.by_hour[peak_hr]) peak_hr = i;

    // Top texter (by raw sender label).
    std::string top_sender;
    long long top_sender_n = 0;
    for (const auto& kv : st.per_sender)
        if (kv.second > top_sender_n) {
            top_sender_n = kv.second;
            top_sender = kv.first;
        }

    // Busiest single day.
    std::string busy_day;
    long long busy_day_n = 0;
    for (const auto& kv : st.by_day)
        if (kv.second > busy_day_n) {
            busy_day_n = kv.second;
            busy_day = kv.first;
        }

    const long long streak = longest_streak(st.by_day);

    // --- Headline totals ----------------------------------------------------
    os << "<div class=\"cards\">\n";
    auto card = [&](long long n, const char* k) {
        os << "<div class=\"card\"><div class=\"n\">" << group(n)
           << "</div><div class=\"k\">" << k << "</div></div>\n";
    };
    card(st.total, "messages");
    card(st.sent, "sent");
    card(st.received, "received");
    card(st.with_attachment, "with media");
    if (opts.word_stats) {
        card(st.words, "words");
        card(st.emoji, "emoji");
    }
    os << "</div>\n";

    // --- Date span ----------------------------------------------------------
    if (st.has_dates) {
        open_section(os, "Time span", col);
        os << "<p>From <strong>" << esc(pretty_date(st.first))
           << "</strong> to <strong>" << esc(pretty_date(st.last)) << "</strong> &mdash; "
           << group(st.days_span) << " day" << (st.days_span == 1 ? "" : "s") << ".</p>";
        close_section(os, col);
    }

    // --- Activity timeline --------------------------------------------------
    if (opts.timeline && !st.monthly.empty()) {
        render_timeline(os, st.monthly, col);
    }

    // --- Messages by hour of day (switchable chart) -------------------------
    // gid is unique per group so the inline switcher scopes correctly when all
    // three chart groups appear together.
    if (opts.hourly) {
        long long mx = 0;
        for (long long v : st.by_hour) mx = std::max(mx, v);
        if (mx > 0) {
            std::vector<Datum> data;
            for (int h = 0; h < 24; ++h) {
                char lbl[8];
                std::snprintf(lbl, sizeof(lbl), "%02d:00", h);
                data.push_back({lbl, st.by_hour[h]});
            }
            open_section(os, "Messages by hour of day", col);
            chart_group(os, 1, data, mx, st.hour_by_weekday);
            close_section(os, col);
        }
    }

    // --- Messages by day of week (switchable chart) -------------------------
    if (opts.weekday) {
        long long mx = 0;
        for (long long v : st.by_weekday) mx = std::max(mx, v);
        if (mx > 0) {
            std::vector<Datum> data;
            for (int i = 0; i < 7; ++i) data.push_back({kWeekdayShort[i], st.by_weekday[i]});
            open_section(os, "Messages by day of week", col);
            chart_group(os, 2, data, mx, st.hour_by_weekday);
            close_section(os, col);
        }
    }

    // --- By year chart ------------------------------------------------------
    if (st.by_year.size() > 1) {
        long long mx = 0;
        for (const auto& kv : st.by_year) mx = std::max(mx, kv.second);
        open_section(os, "Messages by year", col);
        for (const auto& kv : st.by_year) bar_row(os, kv.first, kv.second, mx);
        close_section(os, col);
    }

    // --- Top texters (switchable chart) -------------------------------------
    if (opts.top_texters && !st.per_sender.empty()) {
        std::vector<std::pair<std::string, long long>> ranked(st.per_sender.begin(),
                                                              st.per_sender.end());
        std::stable_sort(ranked.begin(), ranked.end(),
                         [](const auto& a, const auto& b) { return a.second > b.second; });
        const std::size_t show = std::min<std::size_t>(ranked.size(), 10);
        // The bar view links each name to its conversation; the line/pie views
        // need only the value, so build a plain Datum series alongside the list.
        std::vector<Datum> data;
        for (std::size_t i = 0; i < show; ++i)
            data.push_back({ranked[i].first, ranked[i].second});
        long long mx = data.empty() ? 0 : data.front().value;

        open_section(os, "Top texters", col);
        os << "<div class=\"chart-group\" data-cg=\"3\">\n"
           << "<div class=\"chart-tabs\" role=\"tablist\">";
        static const char* kViews[] = {"bar", "line", "pie", "heatmap"};
        static const char* kLabels[] = {"Bar", "Line", "Pie", "Heatmap"};
        for (int v = 0; v < 4; ++v)
            os << "<button type=\"button\" class=\"chart-tab" << (v == 0 ? " active" : "")
               << "\" data-view=\"" << kViews[v] << "\">" << kLabels[v] << "</button>";
        os << "</div>\n";
        // Bar view = the ranked list with conversation links (richer than plain
        // bars, and what readers expect for "top texters").
        os << "<div class=\"chart-view active\" data-view=\"bar\"><ol class=\"senders\">\n";
        for (std::size_t i = 0; i < show; ++i) {
            const std::string& name = ranked[i].first;
            auto file_it = st.handle_to_file.find(name);
            os << "<li>";
            if (file_it != st.handle_to_file.end() && !file_it->second.empty())
                os << "<a href=\"" << esc(file_it->second) << "\">" << esc(name) << "</a>";
            else
                os << esc(name);
            os << " <span class=\"c\">(" << group(ranked[i].second) << ")</span></li>\n";
        }
        os << "</ol></div>\n";
        os << "<div class=\"chart-view\" data-view=\"line\">";
        chart_line(os, data, mx);
        os << "</div>\n<div class=\"chart-view\" data-view=\"pie\">";
        chart_pie(os, data);
        os << "</div>\n<div class=\"chart-view\" data-view=\"heatmap\">";
        chart_heatmap(os, st.hour_by_weekday);
        os << "</div>\n</div>\n";
        close_section(os, col);
    }

    // --- Fun facts ----------------------------------------------------------
    if (opts.fun_facts) {
        open_section(os, "Fun facts", col);
        os << "<ul class=\"facts\">\n";
        auto fact = [&](const char* emoji_char, const std::string& html) {
            os << "<li><span class=\"e\">" << emoji_char << "</span>" << html << "</li>\n";
        };
        if (st.has_dates) {
            char hr[8];
            std::snprintf(hr, sizeof(hr), "%02d:00", peak_hr);
            fact("\xE2\x8F\xB0",  // ⏰
                 "Your most active hour is <strong>" + std::string(hr) + "</strong> ("
                     + group(st.by_hour[peak_hr]) + " messages).");
            fact("\xF0\x9F\x93\x85",  // 📅
                 "Your busiest day of the week is <strong>" + std::string(kWeekdayLong[peak_wd])
                     + "</strong>.");
            if (busy_day_n > 0)
                fact("\xF0\x9F\x94\xA5",  // 🔥
                     "Your chattiest single day was <strong>" + esc(busy_day) + "</strong> with "
                         + group(busy_day_n) + " messages.");
            if (streak > 1)
                fact("\xF0\x9F\x93\x88",  // 📈
                     "Longest daily texting streak: <strong>" + group(streak)
                         + " days</strong> in a row.");
        }
        if (st.days_span > 0)
            fact("\xF0\x9F\x93\xAC",  // 📬
                 "That's about <strong>" + fixed1(msgs_per_day) + "</strong> messages a day.");
        if (!top_sender.empty())
            fact("\xF0\x9F\x91\x91",  // 👑
                 "Top texter: <strong>" + esc(top_sender) + "</strong> (" + group(top_sender_n)
                     + " messages).");
        if (opts.word_stats && st.total > 0) {
            fact("\xE2\x9C\x8D\xEF\xB8\x8F",  // ✍️
                 "You average <strong>" + fixed1(words_per_msg) + "</strong> words per message.");
            if (st.emoji > 0)
                fact("\xF0\x9F\x98\x80",  // 😀
                     "Emoji rate: <strong>" + fixed1(emoji_per_100) + "</strong> per 100 messages ("
                         + group(st.emoji) + " total).");
            if (st.with_attachment > 0) {
                const long long ratio = st.total / st.with_attachment;
                fact("\xF0\x9F\x93\xB7",  // 📷
                     "You shared <strong>" + group(st.attachments) + "</strong> attachment"
                         + (st.attachments == 1 ? "" : "s") + " (roughly 1 in every " + group(ratio)
                         + " messages had media).");
            }
        }
        if (!st.has_dates)
            fact("\xF0\x9F\x95\xB0\xEF\xB8\x8F",  // 🕰️
                 "These messages carried no timestamps, so time-of-day charts are unavailable.");
        os << "</ul>\n"
           << "<p style=\"color:#86868b;font-size:.8rem;margin:12px 0 0\">"
              "Note: chat.db records no location or weather, so this recap focuses on "
              "time, volume, people, words, and emoji.</p>\n";
        close_section(os, col);
    }
}

}  // namespace

std::string render_stats_html(const Stats& st, const StatsRenderOpts& opts) {
    std::ostringstream os;
    os << "<!DOCTYPE html>\n<html lang=\"en\">\n<head>\n"
       << "<meta charset=\"utf-8\">\n"
       << "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n"
       << "<title>Your iMessage Statistics</title>\n"
       // Own minimal theme — independent of the conversation pages' CSS.
       << "<style>\n"
          ":root{color-scheme:light dark}\n"
          "*{box-sizing:border-box}\n"
          "body{margin:0;font:16px/1.5 -apple-system,BlinkMacSystemFont,'Segoe UI',"
          "Roboto,Helvetica,Arial,sans-serif;color:#1d1d1f;"
          "background:linear-gradient(160deg,#eef2ff,#fdf2f8 60%,#fff)}\n"
          ".wrap{max-width:880px;margin:0 auto;padding:32px 20px 64px}\n"
          "header{text-align:center;margin:24px 0 8px}\n"
          "header h1{font-size:2rem;margin:0 0 4px}\n"
          "header p{margin:0;color:#6b6b70}\n"
          ".cards{display:grid;grid-template-columns:repeat(auto-fit,minmax(150px,1fr));"
          "gap:14px;margin:28px 0}\n"
          ".card{background:#fff;border-radius:16px;padding:18px 16px;text-align:center;"
          "box-shadow:0 1px 3px rgba(0,0,0,.08)}\n"
          ".card .n{font-size:1.8rem;font-weight:700;color:#7c3aed}\n"
          ".card .k{font-size:.8rem;text-transform:uppercase;letter-spacing:.04em;color:#6b6b70}\n"
          "section{background:#fff;border-radius:16px;padding:20px 22px;margin:20px 0;"
          "box-shadow:0 1px 3px rgba(0,0,0,.08)}\n"
          "section h2{margin:0 0 14px;font-size:1.15rem}\n"
          ".bar{display:flex;align-items:center;gap:10px;margin:5px 0}\n"
          ".bar .lbl{flex:0 0 86px;font-size:.85rem;color:#48484a;text-align:right}\n"
          ".bar .track{flex:1;background:#eee;border-radius:6px;overflow:hidden;height:16px}\n"
          ".bar .fill{display:block;height:100%;border-radius:6px;"
          "background:linear-gradient(90deg,#8b5cf6,#ec4899);min-width:2px}\n"
          ".bar .val{flex:0 0 64px;font-size:.8rem;color:#6b6b70;text-align:left}\n"
          "ol.senders{margin:0;padding-left:1.3em}\n"
          "ol.senders li{margin:3px 0}\n"
          "ol.senders .c{color:#6b6b70;font-size:.85rem}\n"
          "ul.facts{list-style:none;margin:0;padding:0}\n"
          "ul.facts li{padding:8px 0;border-bottom:1px solid #f0f0f2}\n"
          "ul.facts li:last-child{border-bottom:0}\n"
          "ul.facts .e{margin-right:8px}\n"
          "footer{text-align:center;color:#86868b;font-size:.8rem;margin-top:28px}\n"
          "@media(prefers-color-scheme:dark){body{color:#f5f5f7;"
          "background:linear-gradient(160deg,#1c1c2e,#2a1830 60%,#111)}"
          ".card,section{background:#1f1f24;box-shadow:none}"
          ".bar .track{background:#333}ul.facts li{border-color:#2a2a30}"
          "header p,.card .k,.bar .val,ol.senders .c{color:#a1a1a6}"
          ".bar .lbl{color:#c7c7cc}"
          ".chart-tab{background:#2a2a30;color:#c7c7cc;border-color:#3a3a40}"
          ".chart-axis{stroke:#3a3a40}.pie-legend .c{color:#a1a1a6}"
          ".hm-corner,.hm-tick{color:#a1a1a6}.hm-day{color:#c7c7cc}}\n"
       << kChartCss << "\n"
       << "</style>\n"
       << "</head>\n<body>\n<div class=\"wrap\">\n";

    os << "<header><h1>Your iMessage Statistics</h1>\n"
       << "<p>A look back across " << group(st.conversations) << " conversation"
       << (st.conversations == 1 ? "" : "s") << "</p></header>\n";

    render_stats_body(os, st, opts);

    os << "<footer>Generated by iMessage Exporter &middot; no data left this device</footer>\n"
       << "</div>\n"
       << kChartScript
       << "</body>\n</html>\n";
    return os.str();
}

std::string render_stats_section_html(const Stats& st, const StatsRenderOpts& opts) {
    std::ostringstream os;
    os << "<div class=\"stats-section\">\n"
       << "<style>"
          ".stats-section .cards{display:grid;"
          "grid-template-columns:repeat(auto-fit,minmax(120px,1fr));"
          "gap:10px;margin:16px 0}"
          ".stats-section .card{background:rgba(0,0,0,.04);border-radius:12px;"
          "padding:12px;text-align:center}"
          ".stats-section .card .n{font-size:1.4rem;font-weight:700;color:#7c3aed}"
          ".stats-section .card .k{font-size:.75rem;text-transform:uppercase;"
          "letter-spacing:.04em;color:#6b6b70}"
          ".stats-section section{background:rgba(0,0,0,.03);border-radius:12px;"
          "padding:16px 18px;margin:14px 0}"
          ".stats-section section h2{margin:0 0 10px;font-size:1rem}"
          ".stats-section .bar{display:flex;align-items:center;gap:8px;margin:4px 0}"
          ".stats-section .bar .lbl{flex:0 0 72px;font-size:.8rem;color:#48484a;text-align:right}"
          ".stats-section .bar .track{flex:1;background:#ddd;border-radius:4px;"
          "overflow:hidden;height:13px}"
          ".stats-section .bar .fill{display:block;height:100%;border-radius:4px;"
          "background:linear-gradient(90deg,#8b5cf6,#ec4899);min-width:2px}"
          ".stats-section .bar .val{flex:0 0 54px;font-size:.75rem;color:#6b6b70}"
          ".stats-section ol.senders{margin:0;padding-left:1.2em}"
          ".stats-section ol.senders li{margin:2px 0}"
          ".stats-section ol.senders .c{color:#6b6b70;font-size:.8rem}"
          ".stats-section ul.facts{list-style:none;margin:0;padding:0}"
          ".stats-section ul.facts li{padding:6px 0;border-bottom:1px solid #eee}"
          ".stats-section ul.facts li:last-child{border-bottom:0}"
          ".stats-section ul.facts .e{margin-right:6px}"
       << kChartCss << "\n"
       << "</style>\n"
       << "<h2>Conversation Stats</h2>\n";

    render_stats_body(os, st, opts);

    os << kChartScript << "</div>\n";
    return os.str();
}

namespace {

// "1.2 MB" / "640 KB" / "512 B" — binary (1024) units, one decimal above KB.
std::string human_bytes(long long bytes) {
    if (bytes < 1024) return std::to_string(bytes) + " B";
    double v = static_cast<double>(bytes);
    static const char* kUnit[] = {"KB", "MB", "GB", "TB"};
    int u = -1;
    do {
        v /= 1024.0;
        ++u;
    } while (v >= 1024.0 && u < 3);
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.1f %s", v, kUnit[u]);
    return buf;
}

}  // namespace

std::string render_space_saved_html(long long bytes_before, long long bytes_after,
                                    int files_compressed) {
    if (bytes_before == 0) return "";  // nothing was compressed — no report
    const long long saved = bytes_before - bytes_after;
    const int pct = static_cast<int>(saved * 100 / bytes_before);  // may be 0 or negative
    // Before vs after bar widths, scaled to the larger of the two so the longer
    // bar fills the track. Guard the divide.
    const long long denom = std::max<long long>(std::max(bytes_before, bytes_after), 1);
    const int before_pct = static_cast<int>(bytes_before * 100 / denom);
    const int after_pct = static_cast<int>(bytes_after * 100 / denom);

    std::ostringstream os;
    os << "<details class=\"sec\" open><summary>Media compression</summary>\n"
       << "<style>.space-saved .ss-row{display:flex;align-items:center;gap:10px;margin:6px 0}"
          ".space-saved .ss-lbl{flex:0 0 70px;font-size:.82rem;color:#48484a;text-align:right}"
          ".space-saved .ss-track{flex:1;background:#eee;border-radius:6px;overflow:hidden;"
          "height:18px}.space-saved .ss-fill{display:block;height:100%;border-radius:6px}"
          ".space-saved .ss-before .ss-fill{background:#bbb}"
          ".space-saved .ss-after .ss-fill{background:linear-gradient(90deg,#10b981,#34d399)}"
          ".space-saved .ss-val{flex:0 0 84px;font-size:.8rem;color:#6b6b70}"
          ".space-saved .ss-head{font-size:1.4rem;font-weight:700;color:#10b981}"
          ".space-saved .ss-sub{color:#6b6b70;font-size:.85rem;margin:0 0 12px}</style>\n"
       << "<div class=\"space-saved\">\n"
       << "<div class=\"ss-head\">" << human_bytes(saved < 0 ? 0 : saved) << " saved</div>\n"
       << "<p class=\"ss-sub\">" << group(saved) << " bytes (" << pct << "%) across <strong>"
       << files_compressed << "</strong> file" << (files_compressed == 1 ? "" : "s")
       << " compressed.</p>\n"
       << "<div class=\"ss-row ss-before\"><span class=\"ss-lbl\">Before</span>"
       << "<span class=\"ss-track\"><span class=\"ss-fill\" style=\"width:" << before_pct
       << "%\"></span></span><span class=\"ss-val\">" << human_bytes(bytes_before)
       << "</span></div>\n"
       << "<div class=\"ss-row ss-after\"><span class=\"ss-lbl\">After</span>"
       << "<span class=\"ss-track\"><span class=\"ss-fill\" style=\"width:" << after_pct
       << "%\"></span></span><span class=\"ss-val\">" << human_bytes(bytes_after)
       << "</span></div>\n"
       << "</div></details>\n";
    return os.str();
}

}  // namespace imsg

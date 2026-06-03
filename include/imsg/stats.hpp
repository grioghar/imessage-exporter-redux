// Aggregate statistics over exported conversations, plus a self-contained HTML
// "cover page" rendering them as charts + fun facts.
//
// Deliberately SQLite-free (it folds already-loaded `Chat` objects) so it lives
// in imsg_core and is unit-testable anywhere. The renderer carries its OWN tiny
// embedded CSS — it does not depend on exporters.cpp's kHtmlStyle/theme, so the
// cover page stands alone even when the rest of the export is themed.
//
// What we can measure: time/volume/people/word/emoji. chat.db carries no
// location or weather, so those are intentionally absent (the page says so).
#pragma once

#include <array>
#include <cstdint>
#include <ctime>
#include <map>
#include <string>

#include "imsg/models.hpp"

namespace imsg {

// Running totals accumulated across every exported conversation.
struct Stats {
    long long total = 0;            // messages folded in
    long long sent = 0;             // is_from_me
    long long received = 0;         // !is_from_me
    long long with_attachment = 0;  // messages carrying >= 1 attachment
    long long attachments = 0;      // total attachment count
    long long words = 0;            // whitespace-delimited tokens across bodies
    long long emoji = 0;            // emoji code points seen across bodies
    long long conversations = 0;    // distinct chats folded in

    // Per-handle message count for top-texters; keyed by raw handle string.
    std::map<std::string, int> handle_count;
    // Relative URL for each handle's conversation file (set by export_job).
    std::map<std::string, std::string> handle_to_file;
    // Monthly activity: key = "YYYY-MM", value = message count.
    std::map<std::string, int> monthly;

    // Distribution over local weekday (0 = Sunday .. 6 = Saturday) and hour
    // (0..23), from localtime of each dated message.
    std::array<long long, 7> by_weekday{};
    std::array<long long, 24> by_hour{};

    // Joint weekday x hour distribution ([weekday][hour]), powering the heatmap
    // chart. Same localtime binning as by_weekday/by_hour above.
    std::array<std::array<int, 24>, 7> hour_by_weekday{};

    // Volume per calendar year ("2024") and per sender label, sorted by key.
    std::map<std::string, long long> by_year;
    std::map<std::string, long long> per_sender;

    // Volume per local calendar day ("YYYY-MM-DD"): powers the busiest-day and
    // longest-daily-streak fun facts. Kept SQLite-free, just an ordered map.
    std::map<std::string, long long> by_day;

    // Date span of the dated messages.
    bool has_dates = false;
    std::time_t first = 0;
    std::time_t last = 0;
    long long days_span = 0;  // inclusive whole-day span first..last

    // Fold one conversation's messages into this accumulator.
    void add(const Chat& chat);
};

// Folds one conversation's messages into `s` (free-function form mirroring the
// task's API; delegates to Stats::add).
void stats_add(Stats& s, const Chat& chat);

// Options controlling which sections appear in rendered stats output.
struct StatsRenderOpts {
    bool timeline    = true;
    bool hourly      = true;
    bool weekday     = true;
    bool top_texters = true;
    bool word_stats  = true;
    bool fun_facts   = true;
    // Wrap each section in <details open><summary> so readers can fold sections
    // independently (a tappable header on mobile). Disable for a flat document.
    bool collapsible = true;
};

// Renders a complete standalone HTML document summarizing `s`: headline totals,
// CSS-only bar charts for the weekday/hour distributions, a top-senders list,
// the date span, and a "Fun facts" section. No JavaScript, no external assets;
// all text is HTML-escaped.
std::string render_stats_html(const Stats& st, const StatsRenderOpts& opts = {});

// Condensed stats block (no <html>/<head>) for embedding in conversation pages.
std::string render_stats_section_html(const Stats& st, const StatsRenderOpts& opts = {});

// A standalone <details> fragment summarizing media-compression savings: human
// readable before/after sizes, bytes + percent saved, file count, and a
// before-vs-after bar. Returns "" when bytes_before == 0 (nothing to report).
std::string render_space_saved_html(long long bytes_before, long long bytes_after,
                                    int files_compressed);

}  // namespace imsg

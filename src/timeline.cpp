#include "imsg/timeline.hpp"

#include <algorithm>
#include <cstdio>
#include <ctime>
#include <map>
#include <sstream>
#include <vector>

#include "imsg/models.hpp"

namespace imsg {

namespace {

// HTML-escapes the five characters that break HTML attribute values / text.
static std::string tl_html_escape(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        switch (c) {
            case '&':  out += "&amp;";  break;
            case '<':  out += "&lt;";   break;
            case '>':  out += "&gt;";   break;
            case '"':  out += "&quot;"; break;
            case '\'': out += "&#39;";  break;
            default:   out += c;
        }
    }
    return out;
}

// FNV-ish hash → stable hue per sender name (same algorithm as avatar_html in
// exporters.cpp so the timeline dot colours match the conversation avatars).
static std::string sender_color(const std::string& sender) {
    unsigned h = 2166136261u;
    for (unsigned char c : sender) h = (h ^ c) * 16777619u;
    char buf[40];
    std::snprintf(buf, sizeof(buf), "hsl(%u,55%%,50%%)", h % 360u);
    return buf;
}

// Small circular avatar: either a photo <img> or an initials monogram.
static std::string tl_avatar(const std::string& name, const std::string& photo_uri) {
    if (!photo_uri.empty())
        return "<span class=\"avatar\"><img loading=\"lazy\" alt=\"\" src=\"" +
               photo_uri + "\"></span>";
    // Collect up to 2 initial letters.
    std::string initials;
    bool boundary = true;
    for (unsigned char c : name) {
        if (std::isalnum(c)) {
            if (boundary && initials.size() < 2)
                initials += static_cast<char>(std::toupper(c));
            boundary = false;
        } else {
            boundary = true;
        }
    }
    if (initials.empty()) initials = "?";
    return "<span class=\"avatar\" style=\"background:" + sender_color(name) + "\">" +
           tl_html_escape(initials) + "</span>";
}

// Formats an epoch second as "YYYY-MM-DD" for the summary line.
static std::string fmt_date_iso(std::time_t t) {
    char buf[32] = {};
    struct tm* tm_info;
#ifdef _WIN32
    struct tm tmp;
    gmtime_s(&tmp, &t);
    tm_info = &tmp;
#else
    tm_info = std::gmtime(&t);
#endif
    if (!tm_info) return "?";
    std::strftime(buf, sizeof(buf), "%Y-%m-%d", tm_info);
    return buf;
}

// Formats an epoch second for an axis tick label.
static std::string fmt_tick(std::time_t t, const std::string& density) {
    char buf[32] = {};
    struct tm* tm_info;
#ifdef _WIN32
    struct tm tmp;
    gmtime_s(&tmp, &t);
    tm_info = &tmp;
#else
    tm_info = std::gmtime(&t);
#endif
    if (!tm_info) return "?";
    const char* fmt = (density == "month") ? "%b %Y" : "%b %d";
    std::strftime(buf, sizeof(buf), fmt, tm_info);
    return buf;
}

struct TLMsg {
    std::string guid;
    std::string sender;
    std::string chat_file;  // slug + ".html"
    std::string preview;    // first 80 chars of text, html-escaped
    std::string avatar_uri;
    std::time_t date = 0;
    bool is_from_me = false;
};

// Reduces a title to a filesystem-safe slug that matches export_job's slugify.
// Replicates the core logic (keep UTF-8 multibytes, alnum lower, collapse
// punctuation to '-', trim trailing '-', cap at 80 chars).
static std::string tl_slugify(const std::string& value) {
    auto is_unsafe = [](unsigned char c) {
        return c < 0x20 || c == '/' || c == '\\' || c == ':' || c == '*' ||
               c == '?' || c == '"' || c == '<' || c == '>' || c == '|';
    };
    std::string out;
    bool last_dash = false;
    for (unsigned char c : value) {
        if (c >= 0x80) {
            out += static_cast<char>(c);
            last_dash = false;
        } else if (std::isalnum(c)) {
            out += static_cast<char>(std::tolower(c));
            last_dash = false;
        } else if (is_unsafe(c)) {
            // drop
        } else if (!last_dash && !out.empty()) {
            out += '-';
            last_dash = true;
        }
    }
    if (out.size() > 80) {
        std::size_t cut = 80;
        while (cut > 0 && (static_cast<unsigned char>(out[cut]) & 0xC0) == 0x80) --cut;
        out.resize(cut);
    }
    while (!out.empty() && out.back() == '-') out.pop_back();
    return out;
}

}  // namespace

std::string render_timeline_html(const std::vector<Chat>& chats,
                                 const TimelineOptions& /*opts*/,
                                 const std::string& me_photo_uri) {
    // ------------------------------------------------------------------ 1.
    // Flatten all messages across all chats.
    // ------------------------------------------------------------------ 1.
    std::vector<TLMsg> all;
    for (const Chat& chat : chats) {
        // Derive the output filename the same way export_job does.
        std::string slug = tl_slugify(chat.title());
        if (slug.empty()) slug = "chat-" + std::to_string(chat.rowid);
        const std::string chat_file = slug + ".html";

        for (const Message& m : chat.messages) {
            if (!m.has_date) continue;

            TLMsg t;
            t.guid      = m.guid;
            t.sender    = m.is_from_me ? "Me" : m.sender;
            t.chat_file = chat_file;
            t.date      = m.date;
            t.is_from_me = m.is_from_me;
            t.avatar_uri = m.avatar_uri;

            // First 80 characters of text as the hover preview.
            if (!m.text.empty()) {
                std::string txt = m.text.size() > 80 ? m.text.substr(0, 80) : m.text;
                t.preview = tl_html_escape(txt);
            } else if (!m.attachments.empty()) {
                t.preview = tl_html_escape(m.attachments[0].display_name());
            }
            all.push_back(std::move(t));
        }
    }

    if (all.empty()) {
        // Nothing to show: emit a minimal page.
        return "<!DOCTYPE html>\n<html lang=\"en\"><head><meta charset=\"utf-8\">"
               "<title>Message Timeline</title></head><body>"
               "<div id=\"tl-root\"><p class=\"tl-legend\">No dated messages to display.</p>"
               "</div></body></html>\n";
    }

    std::sort(all.begin(), all.end(),
              [](const TLMsg& a, const TLMsg& b) { return a.date < b.date; });

    const std::time_t t_min = all.front().date;
    const std::time_t t_max = all.back().date;
    const double span_days  = (t_max > t_min)
        ? static_cast<double>(t_max - t_min) / 86400.0
        : 1.0;

    // ------------------------------------------------------------------ 2.
    // Build lanes keyed by sender, ordered most-recently-active first, "Me" last.
    // ------------------------------------------------------------------ 2.
    // last_date per sender for ordering
    std::map<std::string, std::time_t> sender_last;
    for (const TLMsg& m : all) {
        auto it = sender_last.find(m.sender);
        if (it == sender_last.end() || m.date > it->second)
            sender_last[m.sender] = m.date;
    }
    // collect senders (excluding "Me"), sort by most-recent first
    std::vector<std::string> senders;
    for (auto& kv : sender_last)
        if (kv.first != "Me") senders.push_back(kv.first);
    std::sort(senders.begin(), senders.end(),
              [&](const std::string& a, const std::string& b) {
                  return sender_last[a] > sender_last[b];
              });
    if (sender_last.count("Me")) senders.push_back("Me");

    // messages grouped by sender
    std::map<std::string, std::vector<const TLMsg*>> lanes;
    for (const TLMsg& m : all) lanes[m.sender].push_back(&m);

    // ------------------------------------------------------------------ 3.
    // Auto density.
    // ------------------------------------------------------------------ 3.
    std::string density = "hour";
    if      (span_days > 730) density = "month";
    else if (span_days > 90)  density = "week";
    else if (span_days > 14)  density = "day";

    // ------------------------------------------------------------------ 4. / 5.
    // Axis ticks: 6 evenly-spaced across the full span.
    // ------------------------------------------------------------------ 4. / 5.
    std::vector<std::pair<double, std::string>> ticks;  // {pct, label}
    if (t_max > t_min) {
        for (int i = 0; i <= 5; ++i) {
            double f   = i / 5.0;
            std::time_t ts = t_min + static_cast<std::time_t>(f * (t_max - t_min));
            double pct = f * 100.0;
            ticks.push_back({pct, fmt_tick(ts, density)});
        }
    } else {
        ticks.push_back({50.0, fmt_tick(t_min, density)});
    }

    // ------------------------------------------------------------------ 6.
    // Emit HTML.
    // ------------------------------------------------------------------ 6.
    std::ostringstream os;

    // --- head ---
    os << "<!DOCTYPE html>\n<html lang=\"en\">\n<head>\n"
       << "<meta charset=\"utf-8\">\n"
       << "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n"
       << "<title>Message Timeline</title>\n"
       << "<style>\n"
    // ------------------------------------------------------------------ 7.
    // CSS
    // ------------------------------------------------------------------ 7.
       << "body{font-family:-apple-system,sans-serif;margin:0;background:#fff;}\n"
       << "#tl-root{max-width:1200px;margin:0 auto;}\n"
       << "#tl-slider{position:sticky;top:0;background:#f5f5f7;padding:12px 16px;"
          "z-index:10;border-bottom:1px solid #e5e5ea;}\n"
       << "#tl-track{position:relative;height:24px;margin-bottom:4px;}\n"
       << "#tl-lo,#tl-hi{position:absolute;width:100%;height:6px;"
          "-webkit-appearance:none;background:transparent;pointer-events:none;}\n"
       << "#tl-lo{pointer-events:auto;}\n"
       << "#tl-lo::-webkit-slider-thumb,#tl-hi::-webkit-slider-thumb{"
          "-webkit-appearance:none;width:18px;height:18px;border-radius:50%;"
          "background:#007aff;cursor:pointer;pointer-events:auto;}\n"
       << "#tl-fill{position:absolute;height:6px;background:#007aff;"
          "border-radius:3px;top:9px;}\n"
       << "#tl-track-bg{position:absolute;width:100%;height:6px;background:#e5e5ea;"
          "border-radius:3px;top:9px;}\n"
       << "#tl-labels{display:flex;justify-content:space-between;"
          "font-size:12px;color:#666;}\n"
       << "#tl-axis{position:relative;height:28px;margin-left:130px;"
          "border-bottom:1px solid #e5e5ea;}\n"
       << ".tl-tick{position:absolute;font-size:11px;color:#8e8e93;"
          "transform:translateX(-50%);white-space:nowrap;}\n"
       << ".tl-lane{display:flex;border-bottom:1px solid #f2f2f7;min-height:40px;}\n"
       << ".tl-lane-header{width:130px;flex-shrink:0;display:flex;align-items:center;"
          "gap:8px;padding:0 10px;font-size:13px;overflow:hidden;}\n"
       << ".tl-lane-body{flex:1;position:relative;min-height:40px;}\n"
       << ".msg-dot{position:absolute;width:8px;height:8px;border-radius:50%;"
          "transform:translate(-50%,-50%);top:50%;cursor:pointer;"
          "transition:opacity 0.1s,transform 0.1s;text-decoration:none;display:block;}\n"
       << ".msg-dot:hover{transform:translate(-50%,-50%) scale(1.8);z-index:5;}\n"
       << ".msg-dot.filtered{opacity:0.06;pointer-events:none;}\n"
       << "#tl-preview{position:fixed;background:#fff;border:1px solid #e5e5ea;"
          "border-radius:12px;box-shadow:0 4px 20px rgba(0,0,0,.14);"
          "padding:12px 14px;max-width:260px;z-index:100;font-size:13px;"
          "pointer-events:none;}\n"
       << "#tl-preview-sender{font-weight:600;margin-bottom:2px;}\n"
       << "#tl-preview-time{color:#8e8e93;font-size:11px;margin-bottom:6px;}\n"
       << "#tl-preview-text{color:#1c1c1e;margin-bottom:8px;line-height:1.4;}\n"
       << "#tl-preview-link{color:#007aff;font-size:12px;text-decoration:none;"
          "pointer-events:auto;}\n"
       << ".tl-legend{padding:12px 16px;font-size:13px;color:#8e8e93;}\n"
       << ".avatar{width:24px;height:24px;border-radius:50%;display:inline-flex;"
          "align-items:center;justify-content:center;font-size:10px;font-weight:600;"
          "color:#fff;flex-shrink:0;}\n"
       << ".avatar img{width:100%;height:100%;border-radius:50%;object-fit:cover;}\n"
       << "</style>\n</head>\n<body>\n";

    // --- root ---
    os << "<div id=\"tl-root\">\n";

    // --- slider ---
    os << "<div id=\"tl-slider\">\n"
       << "<div id=\"tl-track\">\n"
       << "<div id=\"tl-track-bg\"></div>\n"
       << "<div id=\"tl-fill\"></div>\n"
       << "<input type=\"range\" id=\"tl-lo\" min=\"0\" max=\"10000\" value=\"0\">\n"
       << "<input type=\"range\" id=\"tl-hi\" min=\"0\" max=\"10000\" value=\"10000\">\n"
       << "</div>\n"
       << "<div id=\"tl-labels\">"
       << "<span id=\"tl-label-lo\"></span>"
       << "<span id=\"tl-label-hi\"></span>"
       << "</div>\n"
       << "</div>\n";

    // --- axis ---
    os << "<div id=\"tl-axis\">\n";
    for (const auto& tk : ticks) {
        char pct_buf[32];
        std::snprintf(pct_buf, sizeof(pct_buf), "%.2f", tk.first);
        os << "<span class=\"tl-tick\" style=\"left:" << pct_buf << "%\">"
           << tl_html_escape(tk.second) << "</span>\n";
    }
    os << "</div>\n";

    // --- lanes ---
    for (const std::string& sender : senders) {
        const auto& msgs = lanes[sender];
        const std::string color = sender_color(sender);
        // Avatar: for "Me" use me_photo_uri, for others try the first message's avatar.
        std::string photo;
        if (sender == "Me") {
            photo = me_photo_uri;
        } else if (!msgs.empty()) {
            photo = msgs[0]->avatar_uri;
        }

        os << "<div class=\"tl-lane\">\n"
           << "<div class=\"tl-lane-header\">"
           << tl_avatar(sender, photo)
           << "<span>" << tl_html_escape(sender) << "</span>"
           << "</div>\n"
           << "<div class=\"tl-lane-body\">\n";

        for (const TLMsg* m : msgs) {
            double pct = 0.0;
            if (t_max > t_min)
                pct = static_cast<double>(m->date - t_min) * 100.0 /
                      static_cast<double>(t_max - t_min);
            char pct_buf[32];
            std::snprintf(pct_buf, sizeof(pct_buf), "%.2f", pct);
            // epoch as long long for the data-t attribute
            char epoch_buf[32];
            std::snprintf(epoch_buf, sizeof(epoch_buf), "%lld",
                          static_cast<long long>(m->date));

            os << "<a class=\"msg-dot\""
               << " id=\"msg-" << tl_html_escape(m->guid) << "\""
               << " href=\"" << tl_html_escape(m->chat_file)
               << "#msg-" << tl_html_escape(m->guid) << "\""
               << " data-t=\"" << epoch_buf << "\""
               << " data-sender=\"" << tl_html_escape(m->sender) << "\""
               << " data-preview=\"" << tl_html_escape(m->preview) << "\""
               << " style=\"left:" << pct_buf << "%;background:" << color << "\""
               << " tabindex=\"0\"></a>\n";
        }

        os << "</div>\n</div>\n";  // lane-body, lane
    }

    // --- preview card ---
    os << "<div id=\"tl-preview\" hidden>\n"
       << "<div id=\"tl-preview-sender\"></div>\n"
       << "<div id=\"tl-preview-time\"></div>\n"
       << "<div id=\"tl-preview-text\"></div>\n"
       << "<a id=\"tl-preview-link\" href=\"#\">Open conversation &rarr;</a>\n"
       << "</div>\n";

    // --- legend + totals ---
    const std::string date_start = fmt_date_iso(t_min);
    const std::string date_end   = fmt_date_iso(t_max);
    os << "<p class=\"tl-legend\">Each dot = one message. "
       << "Drag handles to zoom. &nbsp;"
       << all.size() << " messages, " << senders.size() << " conversations, "
       << tl_html_escape(date_start) << " to " << tl_html_escape(date_end)
       << ".</p>\n";

    // ------------------------------------------------------------------ 8.
    // JS — embed actual t_min/t_max epoch values
    // ------------------------------------------------------------------ 8.
    os << "<script>(function(){\n"
       << "var msgs=[].slice.call(document.querySelectorAll('.msg-dot'));\n"
       << "var lo=document.getElementById('tl-lo');\n"
       << "var hi=document.getElementById('tl-hi');\n"
       << "var fill=document.getElementById('tl-fill');\n"
       << "var preview=document.getElementById('tl-preview');\n"
       << "var MAX=10000;\n"
       << "var tMin=" << static_cast<long long>(t_min) << ";"
       << "var tMax=" << static_cast<long long>(t_max) << ";\n"
       << "function fmtDate(ep){\n"
       << "  var d=new Date(ep*1000);\n"
       << "  return d.toLocaleDateString(undefined,{month:'short',year:'numeric'});\n"
       << "}\n"
       << "function update(){\n"
       << "  var l=+lo.value,h=+hi.value;\n"
       << "  if(l>=h-50){if(lo===document.activeElement){lo.value=h-50;}"
          "else{hi.value=l+50;}}\n"
       << "  fill.style.left=(l/MAX*100)+'%';\n"
       << "  fill.style.width=((h-l)/MAX*100)+'%';\n"
       << "  var sT=tMin+(tMax-tMin)*(l/MAX);\n"
       << "  var eT=tMin+(tMax-tMin)*(h/MAX);\n"
       << "  msgs.forEach(function(m){\n"
       << "    var t=+m.dataset.t;\n"
       << "    m.classList.toggle('filtered',t<sT||t>eT);\n"
       << "  });\n"
       << "  document.getElementById('tl-label-lo').textContent=fmtDate(sT);\n"
       << "  document.getElementById('tl-label-hi').textContent=fmtDate(eT);\n"
       << "}\n"
       << "lo.addEventListener('input',update);\n"
       << "hi.addEventListener('input',update);\n"
       << "msgs.forEach(function(m){\n"
       << "  m.addEventListener('mouseenter',function(e){\n"
       << "    document.getElementById('tl-preview-sender').textContent="
          "m.dataset.sender;\n"
       << "    document.getElementById('tl-preview-time').textContent="
          "new Date(+m.dataset.t*1000).toLocaleString();\n"
       << "    document.getElementById('tl-preview-text').textContent="
          "m.dataset.preview;\n"
       << "    document.getElementById('tl-preview-link').href=m.getAttribute('href');\n"
       << "    preview.hidden=false;\n"
       << "    var r=m.getBoundingClientRect();\n"
       << "    preview.style.left=Math.min(r.left+12,window.innerWidth-280)+'px';\n"
       << "    preview.style.top=Math.max(r.top-100,8)+'px';\n"
       << "  });\n"
       << "  m.addEventListener('mouseleave',function(){preview.hidden=true;});\n"
       << "});\n"
       << "update();\n"
       << "})();</script>\n";

    os << "</div>\n</body>\n</html>\n";
    return os.str();
}

}  // namespace imsg

#include "imsg/exporters.hpp"

#include <cctype>
#include <cstdio>
#include <sstream>
#include <string>
#include <utility>

#include "imsg/stats.hpp"
#include "imsg/theme.hpp"
#include "imsg/time_util.hpp"

namespace imsg {

// Optional front-end-supplied rich-link-preview fetcher (see exporters.hpp).
// Null by default so the engine stays network-free; media_embeds_html falls
// back to the offline favicon card.
static LinkPreviewFn g_link_preview;

void set_link_preview_resolver(LinkPreviewFn fn) { g_link_preview = std::move(fn); }

// Selected HTML export theme (see theme.hpp). Set by the front-end before
// export; html_head() emits this theme's CSS. Same install-before-export
// contract as the link-preview resolver above (not safe to change mid-render).
static std::string g_html_theme = "ios";

void set_html_theme(const std::string& name) {
    g_html_theme = is_theme(name) ? name : "ios";
}

namespace {

std::string format_when(const Message& m) {
    return m.has_date ? format_timestamp(m.date) : "unknown time";
}

std::string join(const std::vector<std::string>& items, const std::string& sep) {
    std::string out;
    for (std::size_t i = 0; i < items.size(); ++i) {
        if (i) out += sep;
        out += items[i];
    }
    return out;
}

std::string json_escape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (unsigned char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out += static_cast<char>(c);
                }
        }
    }
    return out;
}

std::string html_escape(const std::string& s) {
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

// Escapes a string for an XML attribute value (& < > " '). html_escape already
// covers exactly this set — alias it so the Android renderer reads as XML, not
// HTML, at call sites.
std::string xml_attr(const std::string& s) { return html_escape(s); }

// MIME family of a media file from its extension, or "" when unknown. The
// Messages DB's attachment.mime_type column is frequently empty, so the
// renderers fall back to this to decide whether to inline a picture/movie.
std::string mime_from_ext(const std::string& path) {
    const std::size_t dot = path.find_last_of('.');
    if (dot == std::string::npos) return "";
    std::string ext = path.substr(dot);
    for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (ext == ".jpg" || ext == ".jpeg") return "image/jpeg";
    if (ext == ".png") return "image/png";
    if (ext == ".gif") return "image/gif";
    if (ext == ".heic" || ext == ".heif") return "image/heic";
    if (ext == ".webp") return "image/webp";
    if (ext == ".bmp") return "image/bmp";
    if (ext == ".tif" || ext == ".tiff") return "image/tiff";
    if (ext == ".mp4" || ext == ".m4v") return "video/mp4";
    if (ext == ".mov") return "video/quicktime";
    if (ext == ".3gp") return "video/3gpp";
    if (ext == ".m4a") return "audio/mp4";
    if (ext == ".mp3") return "audio/mpeg";
    if (ext == ".aac") return "audio/aac";
    if (ext == ".wav") return "audio/wav";
    if (ext == ".caf") return "audio/x-caf";
    if (ext == ".amr") return "audio/amr";
    return "";
}

// The attachment's declared MIME, or a guess from the file/transfer name so
// pictures and movies still render inline when the DB omitted the type.
std::string effective_mime(const Attachment& a) {
    if (!a.mime_type.empty()) return a.mime_type;
    std::string m = mime_from_ext(a.filename);
    if (m.empty()) m = mime_from_ext(a.transfer_name);
    if (m.empty()) m = mime_from_ext(a.copied_path);
    return m;
}

bool is_image_mime(const std::string& mime) { return mime.compare(0, 6, "image/") == 0; }
bool is_video_mime(const std::string& mime) { return mime.compare(0, 6, "video/") == 0; }
bool is_audio_mime(const std::string& mime) { return mime.compare(0, 6, "audio/") == 0; }

// Escapes the characters most likely to break inline Markdown.
std::string md_escape(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        if (c == '\\' || c == '`' || c == '*' || c == '_' || c == '[' || c == ']' ||
            c == '<' || c == '>' || c == '|')
            out += '\\';
        out += c;
    }
    return out;
}

}  // namespace

bool parse_format(const std::string& name, Format& out) {
    if (name == "txt" || name == "text") { out = Format::Text; return true; }
    if (name == "json") { out = Format::Json; return true; }
    if (name == "html") { out = Format::Html; return true; }
    if (name == "md" || name == "markdown") { out = Format::Markdown; return true; }
    if (name == "android" || name == "xml") { out = Format::Android; return true; }
    return false;
}

std::string extension_for(Format fmt) {
    switch (fmt) {
        case Format::Json: return "json";
        case Format::Html: return "html";
        case Format::Markdown: return "md";
        case Format::Android: return "xml";
        case Format::Text: default: return "txt";
    }
}

std::string available_formats() { return "txt, md, json, html, android"; }

std::string render_text(const Chat& chat) {
    std::ostringstream os;
    os << chat.title() << "\n"
       << "Service: " << (chat.service.empty() ? "unknown" : chat.service) << "\n"
       << "Participants: "
       << (chat.participants.empty() ? "unknown" : join(chat.participants, ", ")) << "\n"
       << "Messages: " << chat.messages.size() << "\n"
       << std::string(60, '=') << "\n\n";

    for (const Message& m : chat.messages) {
        os << "[" << format_when(m) << "] " << m.sender << ":\n";
        bool wrote = false;
        if (m.has_text()) { os << "  " << m.text << "\n"; wrote = true; }
        for (const Attachment& a : m.attachments) {
            os << "  <attachment: " << a.display_name();
            if (!a.copied_path.empty()) os << " -> " << a.copied_path;
            os << ">\n";
            wrote = true;
        }
        if (!wrote) os << "  (no content)\n";
        os << "\n";
    }
    return os.str();
}

std::string render_markdown(const Chat& chat) {
    std::ostringstream os;
    os << "# " << md_escape(chat.title()) << "\n\n"
       << "*" << md_escape(chat.service.empty() ? "unknown" : chat.service) << " · "
       << md_escape(chat.participants.empty() ? "unknown" : join(chat.participants, ", "))
       << " · " << chat.messages.size() << " messages*\n\n";
    for (const Message& m : chat.messages) {
        os << "**" << md_escape(m.sender) << "** — " << format_when(m) << "  \n";
        if (m.has_text()) os << md_escape(m.text) << "\n";
        for (const Attachment& a : m.attachments) {
            const std::string name = md_escape(a.display_name());
            // Markdown viewers render ![](path) inline, so pictures show as a
            // preview; everything else becomes a link to the copied file.
            const std::string ref =
                !a.copied_path.empty() ? a.copied_path : a.data_uri;
            const std::string mime = effective_mime(a);
            if (!ref.empty() && is_image_mime(mime))
                os << "![" << name << "](" << ref << ")\n";
            else if (!a.copied_path.empty())
                os << "- \xF0\x9F\x93\x8E [" << name << "](" << a.copied_path << ")\n";
            else
                os << "- \xF0\x9F\x93\x8E " << name
                   << (a.data_uri.empty() ? "" : " (embedded)") << "\n";
        }
        os << "\n";
    }
    return os.str();
}

std::string render_json(const Chat& chat) {
    std::ostringstream os;
    os << "{\n";
    os << "  \"guid\": \"" << json_escape(chat.guid) << "\",\n";
    os << "  \"title\": \"" << json_escape(chat.title()) << "\",\n";
    os << "  \"chat_identifier\": \"" << json_escape(chat.chat_identifier) << "\",\n";
    os << "  \"display_name\": \"" << json_escape(chat.display_name) << "\",\n";
    os << "  \"service\": \"" << json_escape(chat.service) << "\",\n";

    os << "  \"participants\": [";
    for (std::size_t i = 0; i < chat.participants.size(); ++i) {
        if (i) os << ", ";
        os << "\"" << json_escape(chat.participants[i]) << "\"";
    }
    os << "],\n";

    os << "  \"message_count\": " << chat.messages.size() << ",\n";
    os << "  \"messages\": [";
    for (std::size_t i = 0; i < chat.messages.size(); ++i) {
        const Message& m = chat.messages[i];
        os << (i ? ",\n" : "\n");
        os << "    {\n";
        os << "      \"guid\": \"" << json_escape(m.guid) << "\",\n";
        os << "      \"date\": "
           << (m.has_date ? "\"" + json_escape(format_timestamp(m.date)) + "\"" : "null")
           << ",\n";
        os << "      \"date_read\": "
           << (m.has_date_read ? "\"" + json_escape(format_timestamp(m.date_read)) + "\""
                               : "null")
           << ",\n";
        os << "      \"is_from_me\": " << (m.is_from_me ? "true" : "false") << ",\n";
        os << "      \"sender\": \"" << json_escape(m.sender) << "\",\n";
        os << "      \"service\": \"" << json_escape(m.service) << "\",\n";
        os << "      \"text\": \"" << json_escape(m.text) << "\",\n";
        os << "      \"attachments\": [";
        for (std::size_t j = 0; j < m.attachments.size(); ++j) {
            const Attachment& a = m.attachments[j];
            os << (j ? ", " : "") << "{";
            os << "\"filename\": \"" << json_escape(a.filename) << "\", ";
            os << "\"mime_type\": \"" << json_escape(a.mime_type) << "\", ";
            os << "\"transfer_name\": \"" << json_escape(a.transfer_name) << "\", ";
            os << "\"total_bytes\": " << a.total_bytes << ", ";
            os << "\"copied_path\": "
               << (a.copied_path.empty() ? "null"
                                         : "\"" + json_escape(a.copied_path) + "\"");
            if (!a.data_uri.empty())
                os << ", \"data_uri\": \"" << json_escape(a.data_uri) << "\"";
            os << "}";
        }
        os << "]\n";
        os << "    }";
    }
    os << (chat.messages.empty() ? "" : "\n  ") << "]\n";
    os << "}\n";
    return os.str();
}

namespace {

// --- Android "SMS Backup & Restore" XML ------------------------------------

// One <sms/> row per text message in `chat`, for the SMS Backup & Restore app.
// Shared by render_android (full single-chat doc) and combined_item (rows only,
// no <smses> wrapper). Messages with no text are skipped; MMS/attachments are
// out of scope for v1, so attachment-only messages produce nothing.
std::string android_sms_rows(const Chat& chat) {
    // SMS Backup & Restore keys every message in a thread by the conversation
    // peer's RAW phone/email (Android matches the number), not a display name —
    // so use the participant's raw handle for `address`, and the resolved name
    // only for `contact_name`. For a 1:1 that's the single participant; fall
    // back to the chat identifier. Group threads are MMS — out of scope here.
    std::string peer_handle, peer_name;
    if (chat.participant_details.size() == 1) {
        peer_handle = chat.participant_details.front().handle;
        peer_name = chat.participant_details.front().name;
    }
    if (peer_handle.empty())
        peer_handle =
            !chat.participants.empty() ? chat.participants.front() : chat.chat_identifier;
    if (peer_name.empty()) peer_name = peer_handle;
    const std::string contact = peer_name.empty() ? "(Unknown)" : peer_name;
    std::ostringstream os;
    for (const Message& m : chat.messages) {
        if (!m.has_text()) continue;  // MMS/attachment-only: not exported in v1
        const std::string& address = peer_handle;  // raw number, both directions
        const long long date_ms = m.has_date ? static_cast<long long>(m.date) * 1000LL : 0;
        os << "  <sms protocol=\"0\" address=\"" << xml_attr(address) << "\" date=\""
           << date_ms << "\" type=\"" << (m.is_from_me ? "2" : "1")
           << "\" subject=\"null\" body=\"" << xml_attr(m.text)
           << "\" toa=\"null\" sc_toa=\"null\" service_center=\"null\" read=\"1\" "
              "status=\"-1\" locked=\"0\" readable_date=\""
           << xml_attr(format_when(m)) << "\" contact_name=\"" << xml_attr(contact)
           << "\" />\n";
    }
    return os.str();
}

// --- URL detection / linkifying / provider embeds --------------------------

bool url_starts(const std::string& s, std::size_t i) {
    return s.compare(i, 7, "http://") == 0 || s.compare(i, 8, "https://") == 0;
}

// End of the URL token starting at i, trimming trailing sentence punctuation.
std::size_t url_end(const std::string& s, std::size_t i) {
    std::size_t j = i;
    while (j < s.size()) {
        unsigned char c = static_cast<unsigned char>(s[j]);
        if (c <= ' ' || c == '<' || c == '>' || c == '"' || c == '\'') break;
        ++j;
    }
    while (j > i) {
        char c = s[j - 1];
        if (c == '.' || c == ',' || c == ')' || c == '!' || c == '?' || c == ';' ||
            c == ':')
            --j;
        else
            break;
    }
    return j;
}

// Token of id chars (alnum, '_', '-') at position p.
std::string take_id(const std::string& s, std::size_t p) {
    std::string id;
    while (p < s.size()) {
        unsigned char c = static_cast<unsigned char>(s[p]);
        if (std::isalnum(c) || c == '_' || c == '-')
            id += static_cast<char>(c);
        else
            break;
        ++p;
    }
    return id;
}

std::string youtube_id(const std::string& url) {
    std::size_t p = url.find("youtu.be/");
    if (p != std::string::npos) return take_id(url, p + 9);
    if (url.find("youtube.com") != std::string::npos) {
        p = url.find("v=");
        if (p != std::string::npos) return take_id(url, p + 2);
        // /shorts/<id>, /embed/<id>, /live/<id>, /v/<id>
        for (const std::string& seg : {"/shorts/", "/embed/", "/live/", "/v/"}) {
            p = url.find(seg);
            if (p != std::string::npos) return take_id(url, p + seg.size());
        }
    }
    return "";
}

// "type/id" for an open.spotify.com link, or "".
std::string spotify_path(const std::string& url) {
    std::size_t p = url.find("open.spotify.com/");
    if (p == std::string::npos) return "";
    p += 17;
    std::size_t slash = url.find('/', p);
    if (slash == std::string::npos) return "";
    std::string type = url.substr(p, slash - p);
    if (type != "track" && type != "album" && type != "playlist" &&
        type != "episode" && type != "show")
        return "";
    std::string id = take_id(url, slash + 1);
    return id.empty() ? "" : (type + "/" + id);
}

std::string vimeo_id(const std::string& url) {
    std::size_t p = url.find("vimeo.com/");
    if (p == std::string::npos) return "";
    p += 10;
    std::string id;
    while (p < url.size() && std::isdigit(static_cast<unsigned char>(url[p])))
        id += url[p++];
    return id;
}

// A provider iframe with an explicit height (px) so each embed type gets a
// sensible, consistent box instead of one fixed size that's wrong for some.
std::string iframe(const std::string& src, int height) {
    return "<iframe class=\"embed\" style=\"height:" + std::to_string(height) +
           "px\" src=\"" + src + "\" loading=\"lazy\" allowfullscreen></iframe>";
}

// Spotify embed height: compact for a single track/episode, taller for
// albums/playlists/shows (which list multiple items).
int spotify_embed_height(const std::string& path) {
    return (path.rfind("track/", 0) == 0 || path.rfind("episode/", 0) == 0) ? 152 : 352;
}

// A YouTube hero card: its thumbnail (always previews, in HTML and PDF) with a
// play button, linking to the video. In HTML a tiny script (see html_head)
// swaps it for an inline autoplay player on click; in PDF/no-JS it stays a
// clickable thumbnail.
std::string youtube_card(const std::string& id, const std::string& url) {
    return "<a class=\"ytcard\" data-yt=\"" + id + "\" href=\"" + html_escape(url) +
           "\" target=\"_blank\" rel=\"noopener noreferrer\">"
           "<img class=\"embed ytthumb\" loading=\"lazy\" alt=\"YouTube video\" src=\""
           "https://i.ytimg.com/vi/" + id +
           "/hqdefault.jpg\"><span class=\"ytplay\" aria-hidden=\"true\">&#9654;</span></a>";
}

// Host of a URL, without scheme or "www." (e.g. "facebook.com").
std::string host_of(const std::string& url) {
    std::size_t p = url.find("://");
    std::size_t start = (p == std::string::npos) ? 0 : p + 3;
    std::size_t end = url.find('/', start);
    std::string host =
        url.substr(start, end == std::string::npos ? std::string::npos : end - start);
    if (host.compare(0, 4, "www.") == 0) host = host.substr(4);
    return host;
}

// A "pretty" link-preview card (favicon + host + URL) for links we can't embed
// as a player. The favicon is fetched by the browser when the file is viewed.
std::string link_card(const std::string& url) {
    const std::string ehost = html_escape(host_of(url));
    const std::string eurl = html_escape(url);
    return "<a class=\"linkcard\" href=\"" + eurl +
           "\" target=\"_blank\" rel=\"noopener noreferrer\">"
           "<img class=\"linkcard-icon\" alt=\"\" "
           "src=\"https://www.google.com/s2/favicons?sz=64&amp;domain=" + ehost +
           "\">"
           "<span class=\"linkcard-body\"><span class=\"linkcard-host\">" + ehost +
           "</span><span class=\"linkcard-url\">" + eurl + "</span></span></a>";
}

// Returns the non-URL portions of `text`, HTML-escaped. When a message
// contains a URL that was rendered as an embed/card we show only the
// surrounding user-typed annotation — not the raw URL again above the card.
// If the message is URL-only (no surrounding text) this returns an empty
// string, meaning nothing is shown above the embed.
static std::string text_sans_urls(const std::string& text) {
    std::string out;
    for (std::size_t i = 0; i < text.size();) {
        if (url_starts(text, i)) {
            i = url_end(text, i);  // skip the URL entirely
        } else {
            const unsigned char c = static_cast<unsigned char>(text[i]);
            switch (c) {
                case '&': out += "&amp;"; break;
                case '<': out += "&lt;";  break;
                case '>': out += "&gt;";  break;
                default:  out += static_cast<char>(c); break;
            }
            ++i;
        }
    }
    // Trim edges so a pure-URL message with no surrounding text yields "".
    const std::size_t a = out.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return {};
    const std::size_t b = out.find_last_not_of(" \t\r\n");
    return out.substr(a, b - a + 1);
}

// A small circular avatar beside each message, iOS-style: the contact's photo
// (a data URI from the contact source) when available, else a monogram of
// initials over a stable per-name color.
std::string avatar_html(const std::string& name, const std::string& photo_uri,
                        const std::string& extra_class = "") {
    const std::string cls =
        extra_class.empty() ? "avatar" : "avatar " + extra_class;
    if (!photo_uri.empty())
        return "<span class=\"" + cls + "\"><img loading=\"lazy\" alt=\"\" src=\"" +
               photo_uri + "\"></span>";
    std::string initials;
    bool boundary = true;
    for (unsigned char c : name) {
        if (std::isalnum(c)) {
            if (boundary && initials.size() < 2)
                initials += static_cast<char>(std::toupper(c));
            boundary = false;
        } else {
            boundary = true;  // whitespace/punctuation starts a new word
        }
    }
    if (initials.empty()) initials = "?";
    unsigned h = 2166136261u;  // FNV-ish hash → stable hue per name
    for (unsigned char c : name) h = (h ^ c) * 16777619u;
    char color[40];
    std::snprintf(color, sizeof(color), "hsl(%u,55%%,50%%)", h % 360u);
    return "<span class=\"" + cls + "\" style=\"background:" + std::string(color) +
           "\">" + html_escape(initials) + "</span>";
}

// One conversation as a self-contained <div class="conversation"> block, shared
// by the single-chat document and the combined multi-chat document.
std::string html_conversation(const Chat& chat) {
    std::ostringstream os;
    const std::string title = html_escape(chat.title());
    const auto& people = chat.participant_details;
    const bool is_sms = (chat.service == "SMS" || chat.service == "RCS");
    os << "<div class=\"conversation" << (is_sms ? " sms-style" : "") << "\">\n"
       << "<header class=\"chat-header\">\n";
    if (people.size() == 1) {
        // 1:1 chat: a large avatar beside the contact's name, with their raw
        // handle (phone/email) on a second line.
        const Participant& p = people[0];
        const std::string label = p.name.empty() ? p.handle : p.name;
        os << "<div class=\"contact-card\">"
           << avatar_html(label.empty() ? p.handle : label, p.avatar_uri, "avatar-lg")
           << "<div class=\"contact-info\"><div class=\"contact-name\">"
           // `title` is already escaped; only the raw label needs escaping here.
           << (label.empty() ? title : html_escape(label)) << "</div>";
        if (!p.handle.empty())
            os << "<div class=\"contact-handle\">" << html_escape(p.handle) << "</div>";
        os << "</div></div>\n";
    } else if (people.size() > 1) {
        // Group chat: a row of everyone's avatars, the chat title, and the
        // member list under a "Group chat · N people" line.
        os << "<div class=\"contact-card\"><div class=\"avatar-stack\">";
        for (const Participant& p : people) {
            const std::string label = p.name.empty() ? p.handle : p.name;
            os << avatar_html(label, p.avatar_uri, "avatar-lg");
        }
        os << "</div><div class=\"contact-info\"><div class=\"contact-name\">" << title
           << "</div><div class=\"contact-handle\">Group chat &middot; " << people.size()
           << " people</div><div class=\"contact-handle\">"
           << html_escape(join(chat.participants, ", ")) << "</div></div></div>\n";
    } else {
        os << "<h1>" << title << "</h1>\n";
    }
    os << "<div class=\"meta\">Service: "
       << html_escape(chat.service.empty() ? "unknown" : chat.service)
       << " &middot; Participants: "
       << html_escape(chat.participants.empty() ? "unknown" : join(chat.participants, ", "))
       << " &middot; " << chat.messages.size() << " messages</div>\n</header>\n";

    for (const Message& m : chat.messages) {
        const char* side = m.is_from_me ? "me" : "them";
        os << "<div class=\"msg " << side << "\" id=\"msg-" << html_escape(m.guid) << "\">"
           << "<div class=\"info\">" << avatar_html(m.sender, m.avatar_uri) << "<span>"
           << html_escape(m.sender) << " &middot; " << html_escape(format_when(m))
           << "</span></div>"
           << "<div class=\"bubble\">";
        bool wrote = false;
        // Compute embed cards first (YouTube/Spotify/Vimeo iframes + link
        // cards) so we know whether any URLs in the text were consumed.
        const std::string embeds =
            m.has_text() ? media_embeds_html(m.text) : std::string();
        if (m.has_text()) {
            if (embeds.empty()) {
                // No URLs produced embeds — show the full text with links.
                os << linkify_html(m.text);
                wrote = true;
            } else {
                // Some URLs became embed cards. Show only the non-URL
                // portion so the link doesn't appear twice (once as a
                // plain/linkified URL and once as the rich card below).
                // iOS Messages never shows the bare URL alongside a preview.
                const std::string sans = text_sans_urls(m.text);
                if (!sans.empty()) { os << sans; wrote = true; }
            }
        }
        for (const Attachment& a : m.attachments) {
            if (wrote) os << "<br>";
            const std::string name = html_escape(a.display_name());
            // Prefer an inlined data URI (--embed-attachments); else a copied
            // file link; else just the name. Pictures/movies render inline (and
            // link to the copied file); the MIME is guessed from the name when
            // the DB didn't record it, so they don't degrade to bare links.
            const std::string src =
                !a.data_uri.empty() ? a.data_uri : html_escape(a.copied_path);
            const std::string mime = effective_mime(a);
            if (src.empty()) {
                os << "<span class=\"attachment\">\xF0\x9F\x93\x8E " << name << "</span>";
            } else if (is_image_mime(mime)) {
                os << "<a href=\"" << src << "\"><img class=\"attachment\" loading=\"lazy\" "
                   << "src=\"" << src << "\" alt=\"" << name << "\"></a>";
            } else if (is_video_mime(mime)) {
                os << "<video class=\"attachment\" controls preload=\"none\" src=\"" << src
                   << "\"><a href=\"" << src << "\">\xF0\x9F\x93\x8E " << name
                   << "</a></video>";
            } else if (is_audio_mime(mime)) {
                os << "<audio controls preload=\"none\" src=\"" << src << "\"></audio>";
            } else {
                os << "<a class=\"attachment\" href=\"" << src << "\" download=\"" << name
                   << "\">\xF0\x9F\x93\x8E " << name << "</a>";
            }
            wrote = true;
        }
        if (!embeds.empty()) { os << embeds; wrote = true; }
        if (!wrote) os << "<span class=\"empty\">(no content)</span>";
        os << "</div></div>\n";
    }
    os << "</div>\n";
    return os.str();
}

// Opening boilerplate for an HTML document with the given <title>.
std::string html_head(const std::string& title) {
    std::ostringstream os;
    os << "<!DOCTYPE html>\n<html lang=\"en\">\n<head>\n"
       << "<meta charset=\"utf-8\">\n"
       << "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n"
       << "<title>" << html_escape(title) << "</title>\n"
       << "<style>" << theme_css(g_html_theme) << "</style>\n"
       // Click a YouTube hero card to swap its thumbnail for an inline player.
       // No external scripts; ignored by PDF/no-JS viewers (thumbnail stays).
       << "<script>document.addEventListener('click',function(e){"
          "var a=e.target.closest&&e.target.closest('a.ytcard[data-yt]');if(!a)return;"
          "e.preventDefault();var f=document.createElement('iframe');f.className='embed';"
          "f.src='https://www.youtube.com/embed/'+a.getAttribute('data-yt')+'?autoplay=1';"
          "f.allow='autoplay; encrypted-media';f.setAttribute('allowfullscreen','');"
          "a.parentNode.replaceChild(f,a);});</script>\n"
       << "</head>\n<body>\n";
    return os.str();
}

const char* kHtmlTail = "</body>\n</html>\n";

}  // namespace

std::string linkify_html(const std::string& text) {
    std::string out;
    for (std::size_t i = 0; i < text.size();) {
        if (url_starts(text, i)) {
            std::size_t e = url_end(text, i);
            const std::string esc = html_escape(text.substr(i, e - i));
            out += "<a href=\"" + esc +
                   "\" target=\"_blank\" rel=\"noopener noreferrer\">" + esc + "</a>";
            i = e;
        } else {
            switch (text[i]) {
                case '&': out += "&amp;"; break;
                case '<': out += "&lt;"; break;
                case '>': out += "&gt;"; break;
                case '"': out += "&quot;"; break;
                case '\'': out += "&#39;"; break;
                default: out += text[i];
            }
            ++i;
        }
    }
    return out;
}

std::string media_embeds_html(const std::string& text) {
    std::string out;
    for (std::size_t i = 0; i < text.size();) {
        if (!url_starts(text, i)) { ++i; continue; }
        std::size_t e = url_end(text, i);
        const std::string url = text.substr(i, e - i);
        i = e;
        std::string id;
        if (!(id = youtube_id(url)).empty())
            out += youtube_card(id, url);  // thumbnail hero, click-to-play
        else if (!(id = spotify_path(url)).empty())
            out += iframe("https://open.spotify.com/embed/" + id, spotify_embed_height(id));
        else if (!(id = vimeo_id(url)).empty())
            out += iframe("https://player.vimeo.com/video/" + id, 315);
        else {
            // Facebook, news, etc.: a rich Open Graph card when a resolver is
            // installed (GUI, opt-in) and the fetch succeeds, else the offline
            // favicon+host card.
            std::string card = g_link_preview ? g_link_preview(url) : std::string();
            out += card.empty() ? link_card(url) : card;
        }
    }
    return out;
}

std::string render_html(const Chat& chat) {
    return html_head(chat.title()) + html_conversation(chat) + kHtmlTail;
}

std::string render_html(const Chat& chat, const Stats* per_chat_stats,
                        const StatsRenderOpts& stats_opts) {
    std::string html = html_head(chat.title()) + html_conversation(chat);
    if (per_chat_stats) {
        html += render_stats_section_html(*per_chat_stats, stats_opts);
    }
    html += kHtmlTail;
    return html;
}

std::string render_android(const Chat& chat) {
    const std::string rows = android_sms_rows(chat);
    std::size_t count = 0;
    for (const Message& m : chat.messages)
        if (m.has_text()) ++count;
    std::ostringstream os;
    os << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
       << "<smses count=\"" << count << "\">\n"
       << rows << "</smses>\n";
    return os.str();
}

std::string render(const Chat& chat, Format fmt) {
    switch (fmt) {
        case Format::Json: return render_json(chat);
        case Format::Html: return render_html(chat);
        case Format::Markdown: return render_markdown(chat);
        case Format::Android: return render_android(chat);
        case Format::Text: default: return render_text(chat);
    }
}

std::string combined_prologue(Format fmt) {
    switch (fmt) {
        case Format::Json: return "{\n  \"conversations\": [";
        case Format::Html: return html_head("iMessage export");
        // Omitting count is fine for combined — SMS Backup & Restore tolerates it
        // and we'd otherwise need every chat loaded to total it up front.
        case Format::Android:
            return "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<smses>\n";
        case Format::Text: default: return "";
    }
}

std::string combined_item(const Chat& chat, Format fmt, std::size_t index) {
    switch (fmt) {
        case Format::Json:
            // render_json yields a standalone object; join them into the array.
            return (index ? ",\n" : "\n") + render_json(chat);
        case Format::Html:
            return html_conversation(chat);
        case Format::Markdown:
            return (index ? std::string("\n---\n\n") : "") + render_markdown(chat);
        case Format::Android:
            // Just this chat's <sms> rows; the <smses> wrapper is in the
            // prologue/epilogue. (index unused — rows simply concatenate.)
            return android_sms_rows(chat);
        case Format::Text:
        default:
            // Each render_text already ends in a blank line; separate chats with
            // a rule so the boundaries are obvious in one file.
            return (index ? std::string(60, '#') + "\n\n" : "") + render_text(chat);
    }
}

std::string combined_epilogue(Format fmt) {
    switch (fmt) {
        case Format::Json: return "\n  ]\n}\n";
        case Format::Html: return kHtmlTail;
        case Format::Android: return "</smses>\n";
        case Format::Text: default: return "";
    }
}

}  // namespace imsg

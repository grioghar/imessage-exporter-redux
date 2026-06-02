#include "imsg/exporters.hpp"

#include <cctype>
#include <cstdio>
#include <sstream>
#include <string>
#include <utility>

#include "imsg/time_util.hpp"

namespace imsg {

// Optional front-end-supplied rich-link-preview fetcher (see exporters.hpp).
// Null by default so the engine stays network-free; media_embeds_html falls
// back to the offline favicon card.
static LinkPreviewFn g_link_preview;

void set_link_preview_resolver(LinkPreviewFn fn) { g_link_preview = std::move(fn); }

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

const char* kHtmlStyle =
    "body{font-family:-apple-system,Segoe UI,Helvetica,Arial,sans-serif;"
    "background:#f0f0f3;margin:0;padding:2rem;color:#1d1d1f}"
    ".conversation{max-width:720px;margin:0 auto}"
    "header h1{margin:0 0 .25rem;font-size:1.4rem}"
    "header .meta{color:#6e6e73;font-size:.85rem;margin-bottom:1.5rem}"
    ".msg{margin:.35rem 0;display:flex;flex-direction:column}"
    ".msg .bubble{display:inline-block;padding:.5rem .75rem;border-radius:1rem;"
    "max-width:75%;word-wrap:break-word;white-space:pre-wrap}"
    ".msg.them{align-items:flex-start}"
    ".msg.them .bubble{background:#e5e5ea;color:#000;border-bottom-left-radius:.25rem}"
    ".msg.me{align-items:flex-end}"
    ".msg.me .bubble{background:#0b84ff;color:#fff;border-bottom-right-radius:.25rem}"
    ".msg .info{font-size:.7rem;color:#8e8e93;margin:0 .5rem .1rem;"
    "display:flex;align-items:center}"
    ".msg.me .info{flex-direction:row-reverse}"
    ".avatar{display:inline-flex;align-items:center;justify-content:center;"
    "width:22px;height:22px;border-radius:50%;color:#fff;font-size:.62rem;"
    "font-weight:700;margin:0 .35rem;flex:0 0 auto;text-transform:uppercase;"
    "overflow:hidden;background-size:cover;background-position:center}"
    ".avatar img{width:100%;height:100%;object-fit:cover}"
    ".attachment{font-style:italic;opacity:.85}.empty{font-style:italic;opacity:.7}"
    "img.attachment,video.attachment{max-width:100%;border-radius:.5rem;"
    "display:block;font-style:normal}"
    "a.attachment{color:inherit}.bubble a{color:inherit;text-decoration:underline}"
    ".embed{width:100%;max-width:560px;height:315px;border:0;border-radius:.6rem;"
    "margin-top:.4rem;display:block}"
    // YouTube hero card: 16:9 thumbnail + centered play button (click to play).
    ".ytcard{position:relative;display:block;max-width:560px;margin-top:.4rem}"
    ".ytcard .ytthumb{height:auto;aspect-ratio:16/9;object-fit:cover;margin-top:0}"
    ".ytplay{position:absolute;top:50%;left:50%;transform:translate(-50%,-50%);"
    "font-size:3.2rem;line-height:1;color:#fff;text-shadow:0 1px 10px rgba(0,0,0,.6);"
    "pointer-events:none}"
    // Keep messages/media from splitting across PDF pages.
    ".msg,.embed,.ytcard,img.attachment,video.attachment,.ogcard,.linkcard{"
    "page-break-inside:avoid;break-inside:avoid}"
    ".linkcard{display:flex;align-items:center;gap:.6rem;max-width:560px;margin-top:"
    ".4rem;padding:.5rem .7rem;border:1px solid rgba(0,0,0,.12);border-radius:.6rem;"
    "background:#fff;text-decoration:none!important;color:#1d1d1f}"
    ".linkcard-icon{width:32px;height:32px;border-radius:.3rem;flex:0 0 auto}"
    ".linkcard-body{display:flex;flex-direction:column;min-width:0}"
    ".linkcard-host{font-weight:600;font-size:.9rem}"
    ".linkcard-url{color:#6e6e73;font-size:.75rem;overflow:hidden;"
    "text-overflow:ellipsis;white-space:nowrap}"
    // Open Graph rich card (hero image + title + description), used when a link
    // preview resolver supplies fetched metadata. Falls back to .linkcard above.
    ".ogcard{display:block;max-width:560px;margin-top:.4rem;border:1px solid "
    "rgba(0,0,0,.12);border-radius:.6rem;overflow:hidden;background:#fff;"
    "text-decoration:none!important;color:#1d1d1f}"
    ".ogcard-img{display:block;width:100%;max-height:300px;object-fit:cover}"
    ".ogcard-body{padding:.55rem .75rem}"
    ".ogcard-title{font-weight:600;font-size:.95rem;line-height:1.25;"
    "display:-webkit-box;-webkit-line-clamp:2;-webkit-box-orient:vertical;"
    "overflow:hidden}"
    ".ogcard-desc{color:#3a3a3c;font-size:.8rem;margin-top:.2rem;"
    "display:-webkit-box;-webkit-line-clamp:2;-webkit-box-orient:vertical;"
    "overflow:hidden}"
    ".ogcard-host{color:#6e6e73;font-size:.72rem;margin-top:.3rem;"
    "text-transform:uppercase;letter-spacing:.02em}";

}  // namespace

bool parse_format(const std::string& name, Format& out) {
    if (name == "txt" || name == "text") { out = Format::Text; return true; }
    if (name == "json") { out = Format::Json; return true; }
    if (name == "html") { out = Format::Html; return true; }
    if (name == "md" || name == "markdown") { out = Format::Markdown; return true; }
    return false;
}

std::string extension_for(Format fmt) {
    switch (fmt) {
        case Format::Json: return "json";
        case Format::Html: return "html";
        case Format::Markdown: return "md";
        case Format::Text: default: return "txt";
    }
}

std::string available_formats() { return "txt, md, json, html"; }

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

// True if `text` (ignoring surrounding whitespace) is exactly one URL. Used to
// drop the bare link above a rich card/embed for link-only messages, the way
// Messages shows just the preview.
bool is_single_url(const std::string& text) {
    const std::size_t a = text.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return false;
    const std::size_t b = text.find_last_not_of(" \t\r\n");
    const std::string t = text.substr(a, b - a + 1);
    if (t.rfind("http://", 0) != 0 && t.rfind("https://", 0) != 0) return false;
    return t.find_first_of(" \t\r\n") == std::string::npos;
}

// A small circular avatar beside each message, iOS-style: the contact's photo
// (a data URI from the contact source) when available, else a monogram of
// initials over a stable per-name color.
std::string avatar_html(const std::string& name, const std::string& photo_uri) {
    if (!photo_uri.empty())
        return "<span class=\"avatar\"><img loading=\"lazy\" alt=\"\" src=\"" +
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
    return "<span class=\"avatar\" style=\"background:" + std::string(color) + "\">" +
           html_escape(initials) + "</span>";
}

// One conversation as a self-contained <div class="conversation"> block, shared
// by the single-chat document and the combined multi-chat document.
std::string html_conversation(const Chat& chat) {
    std::ostringstream os;
    const std::string title = html_escape(chat.title());
    os << "<div class=\"conversation\">\n<header>\n<h1>" << title << "</h1>\n"
       << "<div class=\"meta\">Service: "
       << html_escape(chat.service.empty() ? "unknown" : chat.service)
       << " &middot; Participants: "
       << html_escape(chat.participants.empty() ? "unknown" : join(chat.participants, ", "))
       << " &middot; " << chat.messages.size() << " messages</div>\n</header>\n";

    for (const Message& m : chat.messages) {
        const char* side = m.is_from_me ? "me" : "them";
        os << "<div class=\"msg " << side << "\">"
           << "<div class=\"info\">" << avatar_html(m.sender, m.avatar_uri) << "<span>"
           << html_escape(m.sender) << " &middot; " << html_escape(format_when(m))
           << "</span></div>"
           << "<div class=\"bubble\">";
        bool wrote = false;
        // Rich provider embeds (YouTube/Spotify/Vimeo iframes) and link cards for
        // any URLs in the text, computed first so a link-only message can show
        // just the card instead of a bare URL above it.
        const std::string embeds =
            m.has_text() ? media_embeds_html(m.text) : std::string();
        if (m.has_text() && !(!embeds.empty() && is_single_url(m.text))) {
            os << linkify_html(m.text);
            wrote = true;
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
       << "<style>" << kHtmlStyle << "</style>\n"
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

std::string render(const Chat& chat, Format fmt) {
    switch (fmt) {
        case Format::Json: return render_json(chat);
        case Format::Html: return render_html(chat);
        case Format::Markdown: return render_markdown(chat);
        case Format::Text: default: return render_text(chat);
    }
}

std::string combined_prologue(Format fmt) {
    switch (fmt) {
        case Format::Json: return "{\n  \"conversations\": [";
        case Format::Html: return html_head("iMessage export");
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
        case Format::Text: default: return "";
    }
}

}  // namespace imsg

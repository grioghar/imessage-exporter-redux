#include "imsg/theme.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>

namespace imsg {
namespace {

// Shared base layout. This is the original iOS stylesheet verbatim (formerly
// kHtmlStyle in exporters.cpp); themes append an override block that retints it.
// Keeping one base means structure, sizing, and the page-break rules that keep
// messages/media from splitting across PDF pages stay correct for every theme.
const char* kBaseCss =
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
    // Per-conversation contact header (1:1 card or group card). Kept whole on a
    // PDF page so the heading never splits from its first messages.
    ".chat-header{page-break-inside:avoid;break-inside:avoid}"
    ".contact-card{display:flex;gap:.8rem;align-items:center}"
    ".contact-info{min-width:0}"
    ".contact-name{font-size:1.3rem;font-weight:600}"
    ".contact-handle{color:#6e6e73;font-size:.85rem}"
    ".avatar-stack{display:flex}"
    ".avatar.avatar-lg{width:64px;height:64px;font-size:1.4rem}"
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
    "text-transform:uppercase;letter-spacing:.02em}"
    // Per-conversation background image (Chat::background_uri, applied inline by
    // the exporter). Cover + fixed so it sits behind the whole thread; the inset
    // box-shadow paints a readable light scrim over the image (a dark scrim in
    // dark mode) without needing a pseudo-element, which the LCARS rail uses.
    ".conversation.has-bg{background-size:cover;background-position:center;"
    "background-attachment:fixed;border-radius:1rem;padding:1.5rem;"
    "box-shadow:inset 0 0 0 100vmax rgba(255,255,255,.62)}"
    "@media(prefers-color-scheme:dark){.conversation.has-bg{"
    "box-shadow:inset 0 0 0 100vmax rgba(0,0,0,.55)}}"
    // Correlated-location badge under a bubble: a muted, rounded pill.
    ".loc-badge{display:inline-block;margin:.15rem .25rem 0;padding:.1rem .5rem;"
    "font-size:.66rem;border-radius:.7rem;background:rgba(0,0,0,.06);color:#6e6e73;"
    "white-space:nowrap}"
    ".msg.me .loc-badge{align-self:flex-end}"
    // SMS/RCS conversations: sent bubbles use Apple's iOS green (#34c759) so
    // they visually match the native Messages green-bubble experience.
    ".sms-style .msg.me .bubble{background:#34c759;color:#000}"
    ".sms-style .msg.me .bubble a{color:#006400}";

// --- Per-theme override blocks ---------------------------------------------
// Each retints the base by overriding background, fonts, bubble colors, and the
// accent (link cards). They only touch paint, never layout, so the page-break
// rules above stay intact. The leading /*theme:NAME*/ marker is a stable hook
// for tests and a hint when reading exported HTML source.

// LCARS: the Star Trek: TNG/Voyager "Library Computer Access/Retrieval System"
// console. Black field; the canonical LCARS palette (oranges #ff9900/#ffcc66,
// purples #cc99cc, blues #9999ff, reds #cc6666); a left rail with big rounded
// end-caps; a bold header bar with notched corners; pill bubbles tinted distinct
// for me vs them; and a Eurostile-ish condensed sans stack. Layout-only changes
// stay clear of the PDF page-break rules. The conversation gets a left padding
// (the rail) drawn with a fixed pseudo-element so it reads as an authentic LCARS
// frame without altering the message flow.
const char* kLcarsCss =
    "/*theme:lcars*/"
    "body{background:#000;color:#ffcc66;"
    "font-family:'Antonio','Oswald','Eurostile','Bebas Neue',"
    "'Helvetica Neue Condensed',Helvetica,Arial,sans-serif;"
    "letter-spacing:.02em;padding:0}"
    // The conversation sits to the right of a rounded LCARS rail.
    ".conversation{max-width:760px;margin:0 auto;padding:1.5rem 1.5rem 3rem 92px;"
    "position:relative;min-height:100vh}"
    // Left rail: a fat orange bar with big rounded end-caps (the LCARS elbow).
    ".conversation::before{content:'';position:absolute;left:18px;top:1.5rem;"
    "bottom:3rem;width:46px;background:#ff9900;"
    "border-radius:46px 0 0 46px / 80px 0 0 80px}"
    // Header: a bold condensed bar with a notched (cut) top-right corner, the
    // hallmark LCARS header shape, in the amber swatch.
    "header{background:#000;padding:0 0 1rem}"
    "header h1{color:#000;background:#ff9966;display:inline-block;"
    "padding:.35rem 2.2rem .35rem 1rem;font-weight:700;text-transform:uppercase;"
    "letter-spacing:.08em;font-size:1.5rem;border-radius:0 0 0 14px;"
    "clip-path:polygon(0 0,calc(100% - 18px) 0,100% 100%,0 100%)}"
    "header .meta{color:#cc99cc;text-transform:uppercase;letter-spacing:.06em;"
    "border-top:3px solid #9999ff;padding-top:.5rem;margin-top:.4rem}"
    // Sender/time line in the cool blue swatch, uppercased like LCARS labels.
    ".msg .info{color:#9999ff;text-transform:uppercase;letter-spacing:.05em}"
    // Pill bubbles, fully rounded, in distinct LCARS swatches. Them = purple,
    // me = orange; both on black with dark text for that high-contrast console
    // look. The big radius gives the LCARS "lozenge" feel.
    ".msg .bubble{border-radius:1.4rem;font-weight:600;border:2px solid #000;"
    "box-shadow:0 0 0 1px rgba(255,153,0,.25)}"
    ".msg.them .bubble{background:#cc99cc;color:#0a0010;"
    "border-bottom-left-radius:1.4rem}"
    ".msg.me .bubble{background:#ff9900;color:#1a0d00;"
    "border-bottom-right-radius:1.4rem}"
    ".bubble a{color:#9c2b00}"
    // Large avatars echo the rail's amber; small ones the cool blue.
    ".avatar.avatar-lg{background:#ffcc66;color:#000;border-radius:50% 50% 50% 14px}"
    ".contact-name{color:#ff9966;text-transform:uppercase;letter-spacing:.04em}"
    ".contact-handle{color:#9999ff}"
    // Link/OG cards as dark LCARS panels with an orange edge + red accents.
    ".linkcard,.ogcard{background:#140a00;color:#ffcc66;border-color:#ff9900;"
    "border-radius:1rem}"
    ".linkcard-url,.ogcard-host,.ogcard-desc{color:#cc99cc}"
    ".linkcard-host,.ogcard-title{color:#ff9966}"
    // SMS sent bubbles keep distinct from iMessage by using the LCARS red.
    ".sms-style .msg.me .bubble{background:#cc6666;color:#160000}"
    ".sms-style .msg.me .bubble a{color:#3a0000}";

// Matrix: black terminal, phosphor-green monospace. Bubbles are outlined rather
// than filled so the green-on-black "digital rain" feel reads cleanly.
const char* kMatrixCss =
    "/*theme:matrix*/"
    "body{background:#000;color:#00ff41;"
    "font-family:'Courier New',Consolas,monospace}"
    "header h1{color:#00ff41}header .meta{color:#00aa2b}"
    ".msg .bubble{border-radius:.2rem;border:1px solid #00ff41;background:#001a06}"
    ".msg.them .bubble{background:#001a06;color:#00ff41}"
    ".msg.me .bubble{background:#003b12;color:#00ff41}"
    ".msg .info{color:#00aa2b}.contact-name,.linkcard-host,.ogcard-title{color:#00ff41}"
    ".contact-handle,.linkcard-url,.ogcard-host,.ogcard-desc{color:#00aa2b}"
    ".linkcard,.ogcard{background:#001a06;color:#00ff41;border-color:#00ff41}";

// Dot-matrix: cream tractor-feed paper, monospace, subtle dotted borders — the
// old impact-printer printout look. Light theme, so dark text on paper.
const char* kDotMatrixCss =
    "/*theme:dot-matrix*/"
    "body{background:#f4ecd8;color:#2b2b2b;"
    "font-family:'Courier New',Consolas,monospace}"
    "header h1{color:#2b2b2b}header .meta{color:#6b6145}"
    ".msg .bubble{border-radius:.15rem;border:1px dotted #8a7f5c;background:#fffdf5}"
    ".msg.them .bubble{background:#fffdf5;color:#2b2b2b}"
    ".msg.me .bubble{background:#e7ddc0;color:#2b2b2b}"
    ".msg .info{color:#6b6145}.contact-name{color:#2b2b2b}"
    ".contact-handle,.linkcard-url,.ogcard-host,.ogcard-desc{color:#6b6145}"
    ".linkcard,.ogcard{background:#fffdf5;color:#2b2b2b;border-style:dotted;"
    "border-color:#8a7f5c}.linkcard-host,.ogcard-title{color:#2b2b2b}";

// ATARI: the 8-bit blue screen — #3b6ea5 field, chunky blocky monospace, hard
// edges (no rounding), high-contrast bubbles.
const char* kAtariCss =
    "/*theme:atari*/"
    "body{background:#3b6ea5;color:#fff;"
    "font-family:'Courier New',Consolas,monospace;font-weight:700}"
    "header h1{color:#fff}header .meta{color:#cfe0f2}"
    ".msg .bubble{border-radius:0;border:2px solid #0a2a4a}"
    ".msg.them .bubble{background:#e8b020;color:#1a1a1a}"
    ".msg.me .bubble{background:#fff;color:#1a3a5a}"
    ".msg .info{color:#cfe0f2}.contact-name{color:#fff}"
    ".contact-handle,.linkcard-url,.ogcard-host,.ogcard-desc{color:#cfe0f2}"
    ".linkcard,.ogcard{background:#1a3a5a;color:#fff;border-radius:0;"
    "border-color:#0a2a4a}.linkcard-host,.ogcard-title{color:#fff}";

const char* kBuiltins[] = {"ios", "lcars", "matrix", "dot-matrix", "atari"};

// A theme registered from JSON: just the color/font fields. CSS is generated on
// demand by json_theme_css() from these, layered over kBaseCss.
struct JsonTheme {
    std::string bg, text, bubble_me, bubble_them, accent, font;
};

// Registry of JSON-loaded themes, keyed by name. A file-scope map (not thread
// safe to mutate during render, same contract as the selected-theme global in
// exporters.cpp): load all themes up front, then render.
std::map<std::string, JsonTheme>& registry() {
    static std::map<std::string, JsonTheme> r;
    return r;
}

bool is_builtin(const std::string& name) {
    for (const char* b : kBuiltins)
        if (name == b) return true;
    return false;
}

// --- Tiny tolerant flat-JSON parser ----------------------------------------
// Parses a single JSON object whose values are all strings:
//   { "k" : "v" , "k2":"v\"2" }
// Handles surrounding/embedded whitespace, and \" / \\ escapes inside strings
// (other escapes are passed through verbatim — sufficient for theme fields,
// which are colors and font stacks). Nested objects/arrays/numbers are out of
// scope; encountering them makes the parse fail (returns false). Deliberately
// minimal: no external JSON dependency, lives in the SQLite-free core.

void skip_ws(const std::string& s, std::size_t& i) {
    while (i < s.size()) {
        const char c = s[i];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') ++i;
        else break;
    }
}

// Reads a JSON string starting at the opening quote (s[i] == '"'); on success
// `i` lands just past the closing quote and `out` holds the unescaped contents.
bool parse_string(const std::string& s, std::size_t& i, std::string& out) {
    if (i >= s.size() || s[i] != '"') return false;
    ++i;
    out.clear();
    while (i < s.size()) {
        const char c = s[i++];
        if (c == '"') return true;
        if (c == '\\') {
            if (i >= s.size()) return false;
            const char e = s[i++];
            switch (e) {
                case '"': out += '"'; break;
                case '\\': out += '\\'; break;
                case '/': out += '/'; break;
                case 'n': out += '\n'; break;
                case 't': out += '\t'; break;
                case 'r': out += '\r'; break;
                // Unknown escape: keep both chars so nothing is silently lost.
                default: out += '\\'; out += e; break;
            }
        } else {
            out += c;
        }
    }
    return false;  // unterminated string
}

bool parse_flat_object(const std::string& s, std::map<std::string, std::string>& out) {
    std::size_t i = 0;
    skip_ws(s, i);
    if (i >= s.size() || s[i] != '{') return false;
    ++i;
    skip_ws(s, i);
    if (i < s.size() && s[i] == '}') return true;  // empty object {}
    while (i < s.size()) {
        skip_ws(s, i);
        std::string key;
        if (!parse_string(s, i, key)) return false;
        skip_ws(s, i);
        if (i >= s.size() || s[i] != ':') return false;
        ++i;
        skip_ws(s, i);
        std::string val;
        if (!parse_string(s, i, val)) return false;  // only string values
        out[key] = val;
        skip_ws(s, i);
        if (i >= s.size()) return false;
        if (s[i] == ',') { ++i; continue; }
        if (s[i] == '}') return true;
        return false;  // unexpected token
    }
    return false;
}

// Generates a theme override block from JSON colors, layered over kBaseCss. The
// substitutions only retint paint (background, text, bubbles, accent, font), so
// the shared layout + page-break rules stay intact — exactly like the built-in
// override blocks. Empty fields are skipped so a partial theme still works.
std::string json_theme_css(const JsonTheme& t) {
    std::ostringstream os;
    os << "/*theme:json*/";
    if (!t.bg.empty() || !t.text.empty() || !t.font.empty()) {
        os << "body{";
        if (!t.bg.empty()) os << "background:" << t.bg << ";";
        if (!t.text.empty()) os << "color:" << t.text << ";";
        if (!t.font.empty()) os << "font-family:" << t.font << ";";
        os << "}";
    }
    if (!t.accent.empty()) {
        os << "header h1{color:" << t.accent << "}"
           << ".contact-name,.linkcard-host,.ogcard-title{color:" << t.accent << "}"
           << ".linkcard,.ogcard{border-color:" << t.accent << "}";
    }
    if (!t.text.empty())
        os << "header .meta,.msg .info,.contact-handle{color:" << t.text
           << ";opacity:.7}";
    if (!t.bubble_them.empty())
        os << ".msg.them .bubble{background:" << t.bubble_them << ";color:"
           << (t.text.empty() ? "#fff" : t.text) << "}";
    if (!t.bubble_me.empty())
        os << ".msg.me .bubble{background:" << t.bubble_me << ";color:#fff}";
    // Keep link/OG card bodies legible on the theme background.
    if (!t.bg.empty())
        os << ".linkcard,.ogcard{background:" << t.bg << "}";
    return os.str();
}

}  // namespace

std::vector<std::string> theme_names() {
    std::vector<std::string> out(std::begin(kBuiltins), std::end(kBuiltins));
    // Append JSON-loaded themes that aren't shadowing a built-in name.
    for (const auto& kv : registry())
        if (!is_builtin(kv.first)) out.push_back(kv.first);
    return out;
}

bool is_theme(const std::string& name) {
    if (is_builtin(name)) return true;
    return registry().find(name) != registry().end();
}

std::string theme_css(const std::string& name) {
    std::string css = kBaseCss;  // every theme starts from the shared layout
    // A JSON-registered theme (that isn't a built-in name) generates its block
    // from the stored colors. Built-in names always use the hardcoded blocks
    // below so the five built-ins keep identical behavior.
    if (!is_builtin(name)) {
        auto it = registry().find(name);
        if (it != registry().end()) return css + json_theme_css(it->second);
    }
    if (name == "lcars") css += kLcarsCss;
    else if (name == "matrix") css += kMatrixCss;
    else if (name == "dot-matrix") css += kDotMatrixCss;
    else if (name == "atari") css += kAtariCss;
    // "ios" (and any unknown name) uses the base alone.
    return css;
}

bool load_theme_from_json(const std::string& json_text, std::string* name_out) {
    std::map<std::string, std::string> kv;
    if (!parse_flat_object(json_text, kv)) return false;
    auto name_it = kv.find("name");
    if (name_it == kv.end() || name_it->second.empty()) return false;  // name required

    JsonTheme t;
    auto get = [&](const char* k) {
        auto it = kv.find(k);
        return it == kv.end() ? std::string() : it->second;
    };
    t.bg = get("bg");
    t.text = get("text");
    t.bubble_me = get("bubble_me");
    t.bubble_them = get("bubble_them");
    t.accent = get("accent");
    t.font = get("font");
    registry()[name_it->second] = t;  // re-registering overwrites
    if (name_out) *name_out = name_it->second;
    return true;
}

int load_themes_from_dir(const std::string& dir_path) {
    namespace fs = std::filesystem;
    std::error_code ec;
    if (!fs::is_directory(dir_path, ec)) return 0;
    int loaded = 0;
    for (const auto& entry : fs::directory_iterator(dir_path, ec)) {
        if (ec) break;
        if (!entry.is_regular_file()) continue;
        const fs::path& p = entry.path();
        std::string ext = p.extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (ext != ".json") continue;
        std::ifstream in(p, std::ios::binary);
        if (!in) continue;
        std::ostringstream buf;
        buf << in.rdbuf();
        if (load_theme_from_json(buf.str())) ++loaded;
    }
    return loaded;
}

}  // namespace imsg

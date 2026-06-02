#include "imsg/theme.hpp"

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
    "text-transform:uppercase;letter-spacing:.02em}";

// --- Per-theme override blocks ---------------------------------------------
// Each retints the base by overriding background, fonts, bubble colors, and the
// accent (link cards). They only touch paint, never layout, so the page-break
// rules above stay intact. The leading /*theme:NAME*/ marker is a stable hook
// for tests and a hint when reading exported HTML source.

// LCARS: the Star Trek LCARS console — black field, rounded amber/orange + a
// purple accent for "them", heavy weight. Bubbles get the big pill radius.
const char* kLcarsCss =
    "/*theme:lcars*/"
    "body{background:#000;color:#ffcc66;font-family:Helvetica,Arial,sans-serif;"
    "font-weight:700}"
    "header h1{color:#ff9966}header .meta{color:#cc99cc}"
    ".msg .bubble{border-radius:1.2rem;font-weight:700}"
    ".msg.them .bubble{background:#cc6699;color:#000}"
    ".msg.me .bubble{background:#ff9933;color:#000}"
    ".msg .info{color:#9999cc}.contact-name{color:#ff9966}"
    ".contact-handle{color:#cc99cc}"
    ".linkcard,.ogcard{background:#1a1a1a;color:#ffcc66;border-color:#ff9933}"
    ".linkcard-url,.ogcard-host,.ogcard-desc{color:#cc99cc}"
    ".linkcard-host,.ogcard-title{color:#ff9966}";

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

}  // namespace

std::vector<std::string> theme_names() {
    return {"ios", "lcars", "matrix", "dot-matrix", "atari"};
}

bool is_theme(const std::string& name) {
    for (const std::string& t : theme_names())
        if (t == name) return true;
    return false;
}

std::string theme_css(const std::string& name) {
    std::string css = kBaseCss;  // every theme starts from the shared layout
    if (name == "lcars") css += kLcarsCss;
    else if (name == "matrix") css += kMatrixCss;
    else if (name == "dot-matrix") css += kDotMatrixCss;
    else if (name == "atari") css += kAtariCss;
    // "ios" (and any unknown name) uses the base alone.
    return css;
}

}  // namespace imsg

// Renderers that turn a conversation into output text.
#pragma once

#include <functional>
#include <string>
#include <vector>

#include "imsg/models.hpp"

namespace imsg {

enum class Format { Text, Json, Html, Markdown, Android };

// Parses a format name ("txt"/"json"/"html"/"android"); returns false if unknown.
bool parse_format(const std::string& name, Format& out);

// File extension for a format ("txt"/"json"/"html"/"xml").
std::string extension_for(Format fmt);

// Comma-separated list of supported format names, for help text.
std::string available_formats();

std::string render_text(const Chat& chat);
std::string render_json(const Chat& chat);
std::string render_html(const Chat& chat);
std::string render_markdown(const Chat& chat);

// Renders one conversation as a "SMS Backup & Restore" (Android app
// com.riteshsahu.SMSBackupRestore) "smses" XML document: a full
// <?xml ...?><smses count="N">…</smses> with one <sms/> per text message.
// Lets a user move their texts onto Android. Attachments/MMS are out of scope
// for v1 (only messages with text are emitted).
std::string render_android(const Chat& chat);

// HTML-escapes `text` and turns http(s) URLs into links that open in a new
// window/tab (target="_blank" rel="noopener noreferrer").
std::string linkify_html(const std::string& text);

// Embed HTML (YouTube / Spotify / Vimeo iframes) for recognized URLs in `text`,
// or "" when none are present. Unknown hosts (incl. Facebook, which needs its JS
// SDK) get no iframe — just the clickable link from linkify_html.
std::string media_embeds_html(const std::string& text);

// Optional resolver that turns a non-embeddable URL into rich preview HTML
// (e.g. an Open Graph card with the page's hero image/title). When one is
// installed, media_embeds_html calls it for each such URL and uses a non-empty
// return verbatim; an empty return (or no resolver) falls back to the offline
// favicon+host link_card. The engine itself does no network I/O — a front-end
// (the Qt GUI) supplies a fetcher. Returned HTML should use the og-card CSS
// classes baked into the HTML document head. Install before export and clear
// after; not safe to change while a render is in flight. Returning rich cards
// can be slow (one network round-trip per URL), so it is opt-in.
using LinkPreviewFn = std::function<std::string(const std::string& url)>;
void set_link_preview_resolver(LinkPreviewFn fn);

// Selects the visual theme for HTML export (built-ins in theme.hpp; default
// "ios"). An unknown name falls back to "ios". Install before export and leave
// set for the run; same contract as set_link_preview_resolver above.
void set_html_theme(const std::string& name);

// Dispatches to the renderer for `fmt`.
std::string render(const Chat& chat, Format fmt);

// --- Combined (multi-conversation single-file) export ----------------------
// Written incrementally so memory stays bounded to one conversation at a time:
// emit the prologue, then one item per conversation, then the epilogue.
std::string combined_prologue(Format fmt);
// Renders one conversation as an element of the combined document. `index` is
// the 0-based position so item 0 omits the leading delimiter (e.g. JSON comma).
std::string combined_item(const Chat& chat, Format fmt, std::size_t index);
std::string combined_epilogue(Format fmt);

// File stem (without extension) used for the single combined output file.
inline const char* combined_stem() { return "conversations"; }

}  // namespace imsg

// Renderers that turn a conversation into output text.
#pragma once

#include <string>
#include <vector>

#include "imsg/models.hpp"

namespace imsg {

enum class Format { Text, Json, Html };

// Parses a format name ("txt"/"json"/"html"); returns false if unknown.
bool parse_format(const std::string& name, Format& out);

// File extension for a format ("txt"/"json"/"html").
std::string extension_for(Format fmt);

// Comma-separated list of supported format names, for help text.
std::string available_formats();

std::string render_text(const Chat& chat);
std::string render_json(const Chat& chat);
std::string render_html(const Chat& chat);

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

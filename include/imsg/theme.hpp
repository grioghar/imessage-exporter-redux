// Pluggable visual themes for the HTML export. SQLite-free (lives in imsg_core)
// so it stays unit-testable. The HTML structure and CSS class names are
// identical across themes — only the stylesheet text changes — so every theme
// works with the existing .msg/.bubble/.avatar/.embed/.ytcard/.contact-* markup.
#pragma once

#include <string>
#include <vector>

namespace imsg {

// Built-in theme names, in display order. "ios" is the default (today's look).
std::vector<std::string> theme_names();

// True if `name` is a known built-in theme.
bool is_theme(const std::string& name);

// The full <style> CSS for `name`: the shared base layout, with a per-theme
// override block appended for everything but "ios". An unknown name falls back
// to "ios" so callers never emit an empty stylesheet. Themes loaded from JSON
// (see below) are resolved here too, generating their override from the colors.
std::string theme_css(const std::string& name);

// Registers a theme from a flat JSON object of string fields:
//   {"name":"sunset","bg":"#1a0a2e","text":"#fce8d8","bubble_me":"#ff6b6b",
//    "bubble_them":"#2a1a4a","accent":"#ffd166","font":"Georgia, serif"}
// Only "name" is required; missing colors fall back to sensible defaults. On
// success the theme becomes available via theme_css()/is_theme()/theme_names();
// `name_out` (when non-null) receives the registered name. Returns false when
// the JSON has no "name" field or is otherwise unusable. Re-registering an
// existing name overwrites it (built-ins included).
bool load_theme_from_json(const std::string& json_text, std::string* name_out = nullptr);

// Loads every *.json file in `dir_path` (non-recursive) as a theme via
// load_theme_from_json. Returns the count successfully registered; a missing or
// unreadable directory yields 0. Uses <filesystem>.
int load_themes_from_dir(const std::string& dir_path);

}  // namespace imsg

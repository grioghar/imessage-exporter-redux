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
// to "ios" so callers never emit an empty stylesheet.
std::string theme_css(const std::string& name);

}  // namespace imsg

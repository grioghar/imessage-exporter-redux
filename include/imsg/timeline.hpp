// Standalone interactive timeline HTML page — one horizontal swimlane per
// contact, time on the X-axis, messages as clickable dots. A dual-handle range
// slider at the top controls the visible time window.
//
// Pure HTML/CSS/JS, no external libraries. SQLite-free (operates on already-
// loaded Chat objects) so it lives in imsg_core and is unit-testable anywhere.
#pragma once

#include <string>
#include <vector>

#include "imsg/models.hpp"

namespace imsg {

struct TimelineOptions {
    bool show_photos   = true;
    bool show_me_photo = true;
    bool show_previews = true;
    std::string density = "auto"; // auto | hour | day | week | month
};

// Renders a complete standalone HTML document visualising all chats as
// horizontal swimlanes. me_photo_uri is an optional data URI for the "Me"
// avatar; pass "" to use the monogram fallback.
std::string render_timeline_html(
    const std::vector<Chat>& chats,
    const TimelineOptions& opts = {},
    const std::string& me_photo_uri = "");

}  // namespace imsg

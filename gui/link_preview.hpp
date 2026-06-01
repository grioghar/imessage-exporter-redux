// Open Graph rich link previews for the GUI. Given a URL, synchronously fetch
// the page, parse its Open Graph / Twitter Card metadata, embed the hero image
// as a data URI, and return a self-contained HTML "card" using the og-card CSS
// classes baked into the export's document head (see exporters.cpp).
//
// This is the network-backed implementation of imsg::LinkPreviewFn. The export
// engine itself never touches the network; the GUI installs this via
// imsg::set_link_preview_resolver only when the user opts in. Returns "" on any
// failure so the engine falls back to the offline favicon card.
#pragma once

#include <string>

namespace linkpreview {

// Fetch + render the OG card HTML for `url`, or "" if it can't be built (no
// metadata, network error, timeout). Safe to call from a worker thread; results
// (including failures) are cached for the life of the process so repeated links
// aren't refetched.
std::string fetch_og_card(const std::string& url);

}  // namespace linkpreview

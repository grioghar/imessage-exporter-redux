// Unit tests for the SQLite-independent core (decoder, time, exporters).
//
// Deliberately free of any SQLite dependency so it builds and runs anywhere a
// C++17 compiler is available, even without libsqlite3 headers.
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "imsg/attributed_body.hpp"
#include "imsg/contact_book.hpp"
#include "imsg/exporters.hpp"
#include "imsg/log.hpp"
#include "imsg/models.hpp"
#include "imsg/time_util.hpp"
#include "imsg/vcard.hpp"

namespace {

int g_failures = 0;
int g_checks = 0;

void check(bool cond, const std::string& name) {
    ++g_checks;
    if (!cond) {
        ++g_failures;
        std::cerr << "FAIL: " << name << "\n";
    }
}

void check_eq(const std::string& got, const std::string& want, const std::string& name) {
    ++g_checks;
    if (got != want) {
        ++g_failures;
        std::cerr << "FAIL: " << name << "\n  got:  [" << got << "]\n  want: ["
                  << want << "]\n";
    }
}

bool contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

// Builds a typedstream-like blob the decoder should parse (header offset 5).
std::string make_attributed_body(const std::string& text) {
    std::string blob = "streamtyped";
    blob += '\x81';
    blob += "NSMutableString";
    blob += std::string("\x01\x94\x84\x01\x2b", 5);  // 5 archiver header bytes
    if (text.size() < 0x80) {
        blob += static_cast<char>(text.size());
    } else {
        blob += '\x81';
        blob += static_cast<char>(text.size() & 0xFF);
        blob += static_cast<char>((text.size() >> 8) & 0xFF);
    }
    blob += text;
    return blob;
}

void test_attributed_body() {
    check_eq(imsg::decode_attributed_body(make_attributed_body("Hello there")),
             "Hello there", "attributed: short");
    check_eq(imsg::decode_attributed_body(make_attributed_body("caf\xC3\xA9 \xE2\x98\x95")),
             "caf\xC3\xA9 \xE2\x98\x95", "attributed: unicode");
    check_eq(imsg::decode_attributed_body(make_attributed_body(std::string(300, 'x'))),
             std::string(300, 'x'), "attributed: long (0x81 length)");
    check_eq(imsg::decode_attributed_body(std::string()), "", "attributed: empty");
    check_eq(imsg::decode_attributed_body("not a typedstream blob"), "",
             "attributed: garbage");
}

void test_sanitize_text() {
    using imsg::sanitize_text;
    check_eq(sanitize_text("Hi\xEF\xBF\xBC there"), "Hi there",
             "sanitize: drops U+FFFC object-replacement");
    check_eq(sanitize_text("caf\xC3\xA9 \xE2\x98\x95"), "caf\xC3\xA9 \xE2\x98\x95",
             "sanitize: keeps accented text + emoji");
    check_eq(sanitize_text(std::string("a\x07" "b\tc\nd", 7)), "ab\tc\nd",
             "sanitize: drops control chars, keeps tab/newline");
    check_eq(sanitize_text("a\xE2\x80\x8B" "b"), "ab",
             "sanitize: drops zero-width space U+200B");
}

void test_time_util() {
    std::time_t out = 0;
    check(!imsg::apple_time_to_epoch(0, out), "time: zero is invalid");

    // 725803200 seconds after 2001-01-01 UTC == 2024-01-01 12:00:00 UTC
    // == Unix epoch 1704110400.
    check(imsg::apple_time_to_epoch(725803200LL * 1000000000LL, out) &&
              out == 1704110400,
          "time: nanoseconds");
    check(imsg::apple_time_to_epoch(725803200LL, out) && out == 1704110400,
          "time: legacy seconds");
}

imsg::Chat make_chat() {
    imsg::Chat c;
    c.guid = "chat-guid-1";
    c.chat_identifier = "+15551234567";
    c.service = "iMessage";
    c.participants = {"+15551234567"};

    imsg::Message m1;
    m1.guid = "m1";
    m1.text = "Hello there";
    m1.is_from_me = false;
    m1.sender = "+15551234567";
    m1.has_date = true;
    imsg::apple_time_to_epoch(725803200LL * 1000000000LL, m1.date);
    c.messages.push_back(m1);

    imsg::Message m2;
    m2.guid = "m2";
    m2.text = "General Kenobi";
    m2.is_from_me = true;
    m2.sender = "Me";
    c.messages.push_back(m2);

    imsg::Message m3;
    m3.guid = "m3";
    m3.is_from_me = false;
    m3.sender = "+15551234567";
    imsg::Attachment a;
    a.transfer_name = "photo.jpg";
    a.mime_type = "image/jpeg";
    a.total_bytes = 2048;
    m3.attachments.push_back(a);
    c.messages.push_back(m3);
    return c;
}

void test_format_parsing() {
    imsg::Format f;
    check(imsg::parse_format("txt", f) && f == imsg::Format::Text, "format: txt");
    check(imsg::parse_format("json", f) && f == imsg::Format::Json, "format: json");
    check(imsg::parse_format("html", f) && f == imsg::Format::Html, "format: html");
    check(!imsg::parse_format("pdf", f), "format: unknown rejected");
    check_eq(imsg::extension_for(imsg::Format::Html), "html", "format: extension");
}

void test_text_export() {
    std::string out = imsg::render_text(make_chat());
    check(contains(out, "Hello there"), "text: message 1");
    check(contains(out, "General Kenobi"), "text: message 2");
    check(contains(out, "<attachment: photo.jpg>"), "text: attachment");
    check(contains(out, "(no content)") == false, "text: no spurious empty");
}

void test_json_export() {
    std::string out = imsg::render_json(make_chat());
    check(contains(out, "\"message_count\": 3"), "json: count");
    check(contains(out, "\"text\": \"General Kenobi\""), "json: text");
    check(contains(out, "\"is_from_me\": true"), "json: from_me");
    check(contains(out, "\"transfer_name\": \"photo.jpg\""), "json: attachment");
}

void test_json_escaping() {
    imsg::Chat c = make_chat();
    c.messages[0].text = "quote \" and \\ backslash\nnewline";
    std::string out = imsg::render_json(c);
    check(contains(out, "quote \\\" and \\\\ backslash\\nnewline"), "json: escaping");
}

void test_html_export() {
    std::string out = imsg::render_html(make_chat());
    check(contains(out, "<!DOCTYPE html>"), "html: doctype");
    check(contains(out, "Hello there"), "html: message 1");
    check(contains(out, "General Kenobi"), "html: message 2");
    check(contains(out, "class=\"msg me\""), "html: me bubble");
    check(contains(out, "class=\"msg them\""), "html: them bubble");
}

void test_html_escaping() {
    imsg::Chat c = make_chat();
    c.messages[0].text = "a < b & c > d";
    std::string out = imsg::render_html(c);
    check(contains(out, "a &lt; b &amp; c &gt; d"), "html: escapes special chars");
    check(!contains(out, "a < b & c > d"), "html: no raw special chars");
}

void test_parse_date() {
    std::time_t t = 0;
    check(imsg::parse_date("2024-06-15 12:30:45", t) &&
              imsg::format_timestamp(t) == "2024-06-15 12:30:45",
          "date: full datetime round-trips");

    std::time_t start = 0, end = 0;
    check(imsg::parse_date("2024-06-15", start) &&
              imsg::format_timestamp(start) == "2024-06-15 00:00:00",
          "date: date-only is midnight");
    check(imsg::parse_date("2024-06-15", end, /*end_of_day=*/true) && end > start &&
              imsg::format_timestamp(end) == "2024-06-15 23:59:59",
          "date: end-of-day for date-only --until");

    check(!imsg::parse_date("not-a-date", t), "date: rejects garbage");
    check(!imsg::parse_date("2024-13-40", t), "date: rejects out-of-range month");
}

void test_contact_book() {
    imsg::ContactBook b;
    b.add("+15551234567", "Alice Example");
    b.add("bob@example.com", "Bob");
    check_eq(b.name_for("(555) 123-4567"), "Alice Example",
             "contacts: phone formatting ignored");
    check_eq(b.name_for("+1 555-123-4567"), "Alice Example",
             "contacts: country code ignored");
    check_eq(b.name_for("BOB@EXAMPLE.COM"), "Bob", "contacts: email case-insensitive");
    check_eq(b.name_for("nobody@example.com"), "", "contacts: miss returns empty");
    b.add("+15551234567", "Should Not Replace");  // first non-empty name wins
    check_eq(b.name_for("5551234567"), "Alice Example", "contacts: first name wins");
}

void test_log_level() {
    imsg::LogLevel lvl;
    check(imsg::parse_log_level("error", lvl) && lvl == imsg::LogLevel::Error,
          "log: error");
    check(imsg::parse_log_level("WARN", lvl) && lvl == imsg::LogLevel::Warn,
          "log: warn case-insensitive");
    check(imsg::parse_log_level("info", lvl) && lvl == imsg::LogLevel::Info,
          "log: info");
    check(imsg::parse_log_level("3", lvl) && lvl == imsg::LogLevel::Debug,
          "log: numeric debug");
    check(!imsg::parse_log_level("loud", lvl), "log: rejects unknown");
    check_eq(imsg::log_level_name(imsg::LogLevel::Debug), "debug", "log: level name");
}

void test_vcard() {
    // Two records: one with FN + phone + email, one company contact (ORG only).
    std::string vcf =
        "BEGIN:VCARD\r\n"
        "VERSION:3.0\r\n"
        "FN:Alice Example\r\n"
        "N:Example;Alice;;;\r\n"
        "item1.TEL;TYPE=CELL:+1 (555) 123-4567\r\n"
        "EMAIL;TYPE=INTERNET:Alice@Example.com\r\n"
        "END:VCARD\r\n"
        "BEGIN:VCARD\r\n"
        "ORG:Acme Inc.;Sales\r\n"
        "TEL:555.987.6543\r\n"
        "END:VCARD\r\n";
    imsg::ContactBook b;
    imsg::parse_vcards(vcf, b);
    check_eq(b.name_for("5551234567"), "Alice Example", "vcard: FN + grouped phone");
    check_eq(b.name_for("alice@example.com"), "Alice Example", "vcard: email lowercased");
    check_eq(b.name_for("(555) 987-6543"), "Acme Inc.", "vcard: ORG fallback");

    // N fallback (no FN) and a folded continuation line.
    std::string folded =
        "BEGIN:VCARD\nN:Smith;Bob;;;\nFN:Bob \n Smith\nTEL:+15550002222\nEND:VCARD\n";
    imsg::ContactBook b2;
    imsg::parse_vcards(folded, b2);
    check_eq(b2.name_for("5550002222"), "Bob Smith", "vcard: folded FN line");

    std::string n_only = "BEGIN:VCARD\nN:Jones;Carol;;;\nTEL:+15550003333\nEND:VCARD\n";
    imsg::ContactBook b3;
    imsg::parse_vcards(n_only, b3);
    check_eq(b3.name_for("5550003333"), "Carol Jones", "vcard: N name when no FN");
}

void test_combined_export() {
    std::vector<imsg::Chat> chats = {make_chat(), make_chat()};
    chats[1].display_name = "Second Chat";

    std::string j = imsg::combined_prologue(imsg::Format::Json);
    for (std::size_t i = 0; i < chats.size(); ++i)
        j += imsg::combined_item(chats[i], imsg::Format::Json, i);
    j += imsg::combined_epilogue(imsg::Format::Json);
    check(contains(j, "\"conversations\""), "combined json: wrapper key");
    check(contains(j, "Second Chat"), "combined json: includes second chat");

    std::string h = imsg::combined_prologue(imsg::Format::Html);
    for (std::size_t i = 0; i < chats.size(); ++i)
        h += imsg::combined_item(chats[i], imsg::Format::Html, i);
    h += imsg::combined_epilogue(imsg::Format::Html);
    check(h.find("<!DOCTYPE html>") != std::string::npos &&
              h.find("<!DOCTYPE html>") == h.rfind("<!DOCTYPE html>"),
          "combined html: exactly one document head");
    std::size_t sections = 0, p = 0;
    while ((p = h.find("class=\"conversation\"", p)) != std::string::npos) {
        ++sections;
        p += 1;
    }
    check(sections == 2, "combined html: two conversation sections");
}

void test_attachment_copied_path() {
    imsg::Chat c = make_chat();  // message 3 has an image/jpeg attachment
    c.messages[2].attachments[0].copied_path = "attachments/x/photo.jpg";
    check(contains(imsg::render_html(c),
                   "<img class=\"attachment\" loading=\"lazy\" "
                   "src=\"attachments/x/photo.jpg\""),
          "attach: html embeds copied image");
    check(contains(imsg::render_text(c), "-> attachments/x/photo.jpg"),
          "attach: text shows copied path");
    check(contains(imsg::render_json(c), "\"copied_path\": \"attachments/x/photo.jpg\""),
          "attach: json includes copied path");
}

void test_linkify_and_embeds() {
    std::string a = imsg::linkify_html("see https://example.com/x now");
    check(contains(a, "<a href=\"https://example.com/x\" target=\"_blank\""),
          "linkify: anchor opens new window");
    check(contains(a, "rel=\"noopener noreferrer\""), "linkify: rel noopener");
    check_eq(imsg::linkify_html("a < b & c"), "a &lt; b &amp; c",
             "linkify: escapes non-URL text");

    {
        const std::string yt =
            imsg::media_embeds_html("x https://youtu.be/dQw4w9WgXcQ y");
        check(contains(yt, "class=\"ytcard\"") && contains(yt, "data-yt=\"dQw4w9WgXcQ\"") &&
                  contains(yt, "i.ytimg.com/vi/dQw4w9WgXcQ"),
              "embed: youtube hero card (thumbnail + data-yt)");
        // /shorts/ and /watch?v= forms also resolve
        check(contains(imsg::media_embeds_html("https://www.youtube.com/shorts/ABCdef12345"),
                       "data-yt=\"ABCdef12345\""),
              "embed: youtube /shorts/ form");
    }
    check(contains(imsg::media_embeds_html("https://open.spotify.com/track/abc123"),
                   "open.spotify.com/embed/track/abc123"),
          "embed: spotify");
    check(contains(imsg::media_embeds_html("https://open.spotify.com/track/abc123"),
                   "height:152px"),
          "embed: spotify track uses compact height");
    check(contains(imsg::media_embeds_html("https://www.facebook.com/x"),
                   "class=\"linkcard\"") &&
              contains(imsg::media_embeds_html("https://www.facebook.com/x"),
                       "facebook.com"),
          "embed: link card for non-embeddable host");
    check_eq(imsg::media_embeds_html("no links here"), "", "embed: none without URLs");
}

void test_link_preview_resolver() {
    // A non-embeddable host normally renders the offline favicon card.
    check(contains(imsg::media_embeds_html("https://www.facebook.com/x"),
                   "class=\"linkcard\""),
          "preview: favicon card without a resolver");

    // With a resolver installed, its non-empty HTML is used verbatim instead.
    int calls = 0;
    imsg::set_link_preview_resolver([&calls](const std::string& url) {
        ++calls;
        return "<a class=\"ogcard\">card for " + url + "</a>";
    });
    std::string fb = imsg::media_embeds_html("https://www.facebook.com/x");
    check(contains(fb, "class=\"ogcard\"") && contains(fb, "facebook.com/x"),
          "preview: resolver output used for non-embeddable host");
    check(!contains(fb, "class=\"linkcard\""), "preview: favicon card replaced");

    // Embeddable hosts (YouTube) keep their iframe and never hit the resolver.
    int before = calls;
    check(contains(imsg::media_embeds_html("https://youtu.be/dQw4w9WgXcQ"),
                   "class=\"ytcard\""),
          "preview: youtube still embeds with a resolver set");
    check(calls == before, "preview: resolver not called for embeddable hosts");

    // An empty return falls back to the favicon card.
    imsg::set_link_preview_resolver([](const std::string&) { return std::string(); });
    check(contains(imsg::media_embeds_html("https://www.facebook.com/x"),
                   "class=\"linkcard\""),
          "preview: empty resolver result falls back to favicon card");

    imsg::set_link_preview_resolver(nullptr);  // restore for later tests
    check(contains(imsg::media_embeds_html("https://www.facebook.com/x"),
                   "class=\"linkcard\""),
          "preview: cleared resolver restores favicon card");
}

void test_attachment_embed_html() {
    imsg::Chat c = make_chat();
    c.messages[2].attachments[0].data_uri = "data:image/jpeg;base64,QUJD";
    std::string html = imsg::render_html(c);
    check(contains(html, "src=\"data:image/jpeg;base64,QUJD\""),
          "embed: inline image data URI in HTML");
}

void test_url_only_message_card() {
    // A message that is just a link should render the card/embed only — not the
    // bare URL above it (which looked like stray "weird text").
    imsg::Chat c;
    c.chat_identifier = "+15550002222";
    c.participants = {"+15550002222"};
    imsg::Message m;
    m.is_from_me = false;
    m.sender = "+15550002222";
    m.text = "https://www.facebook.com/x";
    c.messages.push_back(m);
    const std::string h = imsg::render_html(c);
    check(contains(h, "class=\"linkcard\""), "urlonly: link card is shown");
    // linkify_html would render the URL as the anchor's visible text; the card
    // never does, so this exact fragment proves the bare link was suppressed.
    check(!contains(h, ">https://www.facebook.com/x</a>"),
          "urlonly: bare URL link suppressed in favor of the card");

    // A message mixing words + a link still shows the text and the card.
    imsg::Chat c2;
    c2.participants = {"+15550002222"};
    imsg::Message m2;
    m2.sender = "+15550002222";
    m2.text = "look https://www.facebook.com/x";
    c2.messages.push_back(m2);
    const std::string h2 = imsg::render_html(c2);
    check(contains(h2, "look ") && contains(h2, "class=\"linkcard\""),
          "urlonly: mixed text keeps both the words and the card");
}

void test_avatar_in_recap() {
    const std::string h = imsg::render_html(make_chat());
    check(contains(h, "class=\"avatar\""), "avatar: present in the recap");
    check(contains(h, ">M</span>"), "avatar: initials 'M' for sender \"Me\"");
}

void test_vcard_photo() {
    imsg::ContactBook book;
    imsg::parse_vcards(
        "BEGIN:VCARD\r\nVERSION:3.0\r\nFN:Jane Doe\r\nTEL:+15551234567\r\n"
        "PHOTO;ENCODING=b;TYPE=JPEG:QUJD\r\nEND:VCARD\r\n",
        book);
    check_eq(book.photo_for("+15551234567"), "data:image/jpeg;base64,QUJD",
             "vcard: 3.0 base64 PHOTO -> data URI");

    imsg::ContactBook b2;
    imsg::parse_vcards(
        "BEGIN:VCARD\nFN:X\nEMAIL:x@y.com\nPHOTO:data:image/png;base64,ZZ\nEND:VCARD\n",
        b2);
    check_eq(b2.photo_for("x@y.com"), "data:image/png;base64,ZZ",
             "vcard: 4.0 inline data-URI PHOTO kept as-is");

    imsg::ContactBook b3;  // a URI-only PHOTO isn't embeddable -> no photo
    imsg::parse_vcards(
        "BEGIN:VCARD\nFN:Y\nTEL:5550000000\nPHOTO;VALUE=uri:https://x/p.jpg\nEND:VCARD\n",
        b3);
    check(b3.photo_for("5550000000").empty(), "vcard: URI PHOTO is skipped");

    // The entries API (used to merge iCloud into the store) carries name+photo.
    const auto entries = imsg::parse_vcard_entries(
        "BEGIN:VCARD\nFN:Jane\nTEL:+15551234567\n"
        "PHOTO;ENCODING=b;TYPE=JPEG:QUJD\nEND:VCARD\n");
    check(entries.size() == 1 && entries[0].handle == "+15551234567" &&
              entries[0].name == "Jane" &&
              entries[0].photo == "data:image/jpeg;base64,QUJD",
          "vcard: parse_vcard_entries yields handle + name + photo");
}

void test_avatar_photo_render() {
    imsg::Chat c;
    c.participants = {"+15551234567"};
    imsg::Message m;
    m.sender = "Jane Doe";
    m.avatar_uri = "data:image/jpeg;base64,QUJD";
    c.messages.push_back(m);
    check(contains(imsg::render_html(c),
                   "<span class=\"avatar\"><img loading=\"lazy\" alt=\"\" "
                   "src=\"data:image/jpeg;base64,QUJD\">"),
          "avatar: contact photo rendered as <img> when present");
}

void test_inline_media_fallback() {
    // The Messages DB often leaves attachment.mime_type empty; pictures/movies
    // must still inline (guessed from the file name), not degrade to bare links.
    imsg::Chat c;
    c.chat_identifier = "+15550001111";
    c.participants = {"+15550001111"};
    imsg::Message m;
    m.is_from_me = false;
    m.sender = "+15550001111";
    imsg::Attachment img;  // no mime_type, .png name, copied file
    img.transfer_name = "pic.png";
    img.copied_path = "Chat/pic.png";
    m.attachments.push_back(img);
    imsg::Attachment vid;  // no mime_type, .mov name
    vid.filename = "/tmp/movie.mov";
    vid.transfer_name = "movie.mov";
    vid.copied_path = "Chat/movie.mov";
    m.attachments.push_back(vid);
    c.messages.push_back(m);

    const std::string h = imsg::render_html(c);
    check(contains(h, "<img class=\"attachment\" loading=\"lazy\"") &&
              contains(h, "Chat/pic.png"),
          "media: image inlines (lazy) without a DB mime type");
    check(contains(h, "<video class=\"attachment\" controls preload=\"none\"") &&
              contains(h, "Chat/movie.mov"),
          "media: movie inlines as <video> without a DB mime type");

    const std::string md = imsg::render_markdown(c);
    check(contains(md, "![pic.png](Chat/pic.png)"), "media: markdown inline image");
    check(contains(md, "[movie.mov](Chat/movie.mov)"),
          "media: markdown links the movie to its copy");
}

void test_markdown_export() {
    imsg::Format f;
    check(imsg::parse_format("md", f) && f == imsg::Format::Markdown, "md: parse md");
    check(imsg::parse_format("markdown", f) && f == imsg::Format::Markdown,
          "md: parse markdown");
    check_eq(imsg::extension_for(imsg::Format::Markdown), "md", "md: extension");
    std::string md = imsg::render_markdown(make_chat());
    check(contains(md, "# "), "md: heading");
    check(contains(md, "Hello there"), "md: message text");
    check(contains(md, "**"), "md: bold sender");
}

void test_chat_title() {
    imsg::Chat c;
    c.rowid = 7;
    check_eq(c.title(), "chat-7", "title: fallback to rowid");
    c.chat_identifier = "+15550000000";
    check_eq(c.title(), "+15550000000", "title: identifier");
    c.participants = {"a@example.com", "b@example.com"};
    check_eq(c.title(), "a@example.com, b@example.com", "title: participants");
    c.display_name = "Weekend Plans";
    check_eq(c.title(), "Weekend Plans", "title: display name wins");
}

}  // namespace

int main() {
    test_attributed_body();
    test_sanitize_text();
    test_time_util();
    test_format_parsing();
    test_text_export();
    test_json_export();
    test_json_escaping();
    test_html_export();
    test_html_escaping();
    test_parse_date();
    test_log_level();
    test_contact_book();
    test_vcard();
    test_combined_export();
    test_attachment_copied_path();
    test_linkify_and_embeds();
    test_link_preview_resolver();
    test_url_only_message_card();
    test_avatar_in_recap();
    test_vcard_photo();
    test_avatar_photo_render();
    test_attachment_embed_html();
    test_inline_media_fallback();
    test_markdown_export();
    test_chat_title();

    if (g_failures == 0) {
        std::cout << "OK: all " << g_checks << " checks passed\n";
        return 0;
    }
    std::cerr << g_failures << " of " << g_checks << " checks FAILED\n";
    return 1;
}

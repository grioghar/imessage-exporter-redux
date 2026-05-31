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
#include "imsg/models.hpp"
#include "imsg/time_util.hpp"

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
                   "<img class=\"attachment\" src=\"attachments/x/photo.jpg\""),
          "attach: html embeds copied image");
    check(contains(imsg::render_text(c), "-> attachments/x/photo.jpg"),
          "attach: text shows copied path");
    check(contains(imsg::render_json(c), "\"copied_path\": \"attachments/x/photo.jpg\""),
          "attach: json includes copied path");
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
    test_time_util();
    test_format_parsing();
    test_text_export();
    test_json_export();
    test_json_escaping();
    test_html_export();
    test_html_escaping();
    test_parse_date();
    test_contact_book();
    test_combined_export();
    test_attachment_copied_path();
    test_chat_title();

    if (g_failures == 0) {
        std::cout << "OK: all " << g_checks << " checks passed\n";
        return 0;
    }
    std::cerr << g_failures << " of " << g_checks << " checks FAILED\n";
    return 1;
}

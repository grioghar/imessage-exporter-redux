#include "imsg/vcard.hpp"

#include <cctype>
#include <string>
#include <vector>

namespace imsg {
namespace {

std::string trim(const std::string& s) {
    std::size_t b = 0, e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    return s.substr(b, e - b);
}

std::string to_upper(std::string s) {
    for (char& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

// Splits text into logical vCard lines: drops CR, then unfolds continuation
// lines (those beginning with a space or tab) onto the preceding line.
std::vector<std::string> logical_lines(const std::string& text) {
    std::vector<std::string> raw;
    std::string line;
    for (char c : text) {
        if (c == '\n') {
            raw.push_back(line);
            line.clear();
        } else if (c != '\r') {
            line += c;
        }
    }
    if (!line.empty()) raw.push_back(line);

    std::vector<std::string> out;
    for (const std::string& r : raw) {
        if (!out.empty() && !r.empty() && (r[0] == ' ' || r[0] == '\t'))
            out.back() += r.substr(1);  // RFC 6350 unfolding: drop one leading WS
        else
            out.push_back(r);
    }
    return out;
}

// Splits "TEL;TYPE=CELL:+15551234567" into name="TEL" and value="+15551234567".
// The name is upper-cased and has any "group." prefix stripped.
void split_property(const std::string& line, std::string& name, std::string& value) {
    std::size_t colon = line.find(':');
    if (colon == std::string::npos) {
        name.clear();
        value.clear();
        return;
    }
    value = line.substr(colon + 1);

    std::string head = line.substr(0, colon);
    std::size_t semi = head.find(';');
    std::string prop = (semi == std::string::npos) ? head : head.substr(0, semi);
    std::size_t dot = prop.rfind('.');  // drop "item1." style group prefixes
    if (dot != std::string::npos) prop = prop.substr(dot + 1);
    name = to_upper(trim(prop));
}

// Turns a vCard PHOTO line into a "data:image/...;base64,..." URI, or "" when it
// isn't an embeddable inline image (e.g. a URI-only PHOTO that would need a
// network fetch). Handles vCard 3.0 (ENCODING=b/BASE64;TYPE=JPEG:<base64>) and
// 4.0 (PHOTO:data:image/...;base64,...). Long values were already unfolded.
std::string photo_data_uri(const std::string& line) {
    std::size_t colon = line.find(':');
    if (colon == std::string::npos) return "";
    const std::string head = to_upper(line.substr(0, colon));  // params
    std::string value = line.substr(colon + 1);

    if (value.compare(0, 5, "data:") == 0)  // 4.0 inline data URI
        return value.find("image") != std::string::npos ? value : "";
    if (value.compare(0, 4, "http") == 0) return "";  // URI: not embeddable offline
    if (head.find("BASE64") == std::string::npos &&
        head.find("ENCODING=B") == std::string::npos)
        return "";  // unknown / unencoded form

    std::string type = "jpeg";
    std::size_t tp = head.find("TYPE=");
    if (tp != std::string::npos) {
        std::string t;
        for (char c : head.substr(tp + 5)) {
            if (std::isalnum(static_cast<unsigned char>(c)))
                t += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            else
                break;
        }
        if (t == "jpg") t = "jpeg";
        if (!t.empty()) type = t;
    }
    std::string b64;
    for (char c : value)
        if (!std::isspace(static_cast<unsigned char>(c))) b64 += c;
    if (b64.empty()) return "";
    return "data:image/" + type + ";base64," + b64;
}

// "First Last" from an N value "Last;First;Middle;Prefix;Suffix".
std::string name_from_n(const std::string& value) {
    std::string last, first;
    std::size_t p = value.find(';');
    last = trim(value.substr(0, p));
    if (p != std::string::npos) {
        std::string rest = value.substr(p + 1);
        first = trim(rest.substr(0, rest.find(';')));
    }
    std::string name = trim(first + " " + last);
    return name;
}

}  // namespace

std::vector<VCardEntry> parse_vcard_entries(const std::string& text) {
    std::vector<VCardEntry> entries;
    std::string fn, n_name, org, photo;
    std::vector<std::string> handles;  // TEL + EMAIL values for the current card
    bool in_card = false;

    auto reset = [&] {
        fn.clear();
        n_name.clear();
        org.clear();
        photo.clear();
        handles.clear();
    };

    std::string name, value;
    for (const std::string& line : logical_lines(text)) {
        split_property(line, name, value);
        if (name.empty()) continue;

        if (name == "BEGIN" && to_upper(trim(value)) == "VCARD") {
            reset();
            in_card = true;
        } else if (name == "END") {
            if (in_card) {
                std::string display = !fn.empty() ? fn : (!n_name.empty() ? n_name : org);
                if (!display.empty())
                    for (const std::string& h : handles)
                        entries.push_back({h, display, photo});
            }
            in_card = false;
        } else if (!in_card) {
            continue;
        } else if (name == "FN") {
            fn = trim(value);
        } else if (name == "N") {
            n_name = name_from_n(value);
        } else if (name == "ORG") {
            org = trim(value.substr(0, value.find(';')));  // company, drop dept
        } else if (name == "TEL" || name == "EMAIL") {
            // ContactBook::key_for normalizes (digits-only phones, lowercased
            // emails), so a "tel:" URI scheme or formatting needs no cleanup.
            handles.push_back(trim(value));
        } else if (name == "PHOTO") {
            if (photo.empty()) photo = photo_data_uri(line);  // first usable photo
        }
    }
    return entries;
}

void parse_vcards(const std::string& text, ContactBook& book) {
    for (const VCardEntry& e : parse_vcard_entries(text)) {
        book.add(e.handle, e.name);
        if (!e.photo.empty()) book.add_photo(e.handle, e.photo);
    }
}

}  // namespace imsg

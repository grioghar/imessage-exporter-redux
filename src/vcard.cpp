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

void parse_vcards(const std::string& text, ContactBook& book) {
    std::string fn, n_name, org;
    std::vector<std::string> handles;  // TEL + EMAIL values for the current card
    bool in_card = false;

    auto reset = [&] {
        fn.clear();
        n_name.clear();
        org.clear();
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
                    for (const std::string& h : handles) book.add(h, display);
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
        }
    }
}

}  // namespace imsg

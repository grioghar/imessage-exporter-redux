#include "imsg/contact_book.hpp"

#include <cctype>

namespace imsg {

std::string ContactBook::key_for(const std::string& handle) {
    // Emails: match case-insensitively on the address as written.
    if (handle.find('@') != std::string::npos) {
        std::string key;
        key.reserve(handle.size());
        for (unsigned char c : handle) key += static_cast<char>(std::tolower(c));
        return key;
    }
    // Phones: keep digits only, then the last 10 (drops country code / leading
    // '+' / formatting). Emails never reach here, so a digit key can't collide.
    std::string digits;
    for (unsigned char c : handle)
        if (std::isdigit(c)) digits += static_cast<char>(c);
    if (digits.size() > 10) digits = digits.substr(digits.size() - 10);
    return digits;
}

void ContactBook::add(const std::string& handle, const std::string& name) {
    if (name.empty()) return;
    std::string key = key_for(handle);
    if (key.empty()) return;
    by_key_.emplace(key, name);  // emplace keeps the first name seen for a key
}

std::string ContactBook::name_for(const std::string& handle) const {
    auto it = by_key_.find(key_for(handle));
    return it != by_key_.end() ? it->second : std::string();
}

void ContactBook::add_photo(const std::string& handle, const std::string& data_uri) {
    if (data_uri.empty()) return;
    std::string key = key_for(handle);
    if (key.empty()) return;
    photo_by_key_.emplace(key, data_uri);  // first photo for a key wins
}

std::string ContactBook::photo_for(const std::string& handle) const {
    auto it = photo_by_key_.find(key_for(handle));
    return it != photo_by_key_.end() ? it->second : std::string();
}

}  // namespace imsg

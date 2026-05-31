// A lookup table from message handles (phone numbers / emails) to display
// names. SQLite-free so it can be unit-tested anywhere; the macOS AddressBook
// loader that populates one lives in the SQLite-backed `contacts.hpp`.
#pragma once

#include <string>
#include <unordered_map>

namespace imsg {

class ContactBook {
   public:
    // Normalizes a handle into a match key. Emails are lowercased; phone
    // numbers are reduced to digits and, when long enough, keyed on their last
    // 10 digits so "+1 (555) 123-4567" and "5551234567" collapse together
    // regardless of country code / formatting differences between the Messages
    // and Contacts databases.
    static std::string key_for(const std::string& handle);

    // Associates `name` with `handle`. The first non-empty name for a key wins,
    // so a later, blanker record can't clobber a good one.
    void add(const std::string& handle, const std::string& name);

    // Returns the display name for `handle`, or "" if none is known.
    std::string name_for(const std::string& handle) const;

    bool empty() const { return by_key_.empty(); }
    std::size_t size() const { return by_key_.size(); }

   private:
    std::unordered_map<std::string, std::string> by_key_;
};

}  // namespace imsg

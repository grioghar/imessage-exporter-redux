// Read-only access to the macOS Messages (`chat.db`) database.
#pragma once

#include <ctime>
#include <stdexcept>
#include <string>
#include <vector>

#include "imsg/contact_book.hpp"
#include "imsg/models.hpp"

namespace imsg {

// Thrown when the database cannot be opened or queried.
class DatabaseError : public std::runtime_error {
   public:
    explicit DatabaseError(const std::string& what) : std::runtime_error(what) {}
};

// Default location of the live Messages database for the current user.
std::string default_db_path();

// Per-handle summary used by GUIs to sort/filter the participant picker:
// the most recent message date for a handle and the service it came over.
struct HandleStat {
    std::string handle;
    std::time_t last_date = 0;
    bool has_last = false;
    std::string service;  // "iMessage"/"SMS"/"RCS" if present, else ""
};

class MessagesDatabase {
   public:
    // `me_label` is used as the sender name for messages you sent.
    explicit MessagesDatabase(std::string db_path, std::string me_label = "Me");
    ~MessagesDatabase();

    MessagesDatabase(const MessagesDatabase&) = delete;
    MessagesDatabase& operator=(const MessagesDatabase&) = delete;

    // Restricts loaded messages to those whose timestamp is within [since, until]
    // (epoch seconds; either bound may be disabled). Messages with no usable date
    // are excluded whenever a bound is active. Set before load_messages().
    void set_date_range(bool has_since, std::time_t since, bool has_until,
                        std::time_t until);

    // Supplies a contact book used to resolve handles (phone/email) to display
    // names for senders and participants. The pointer must outlive this object;
    // pass nullptr (the default) to leave handles unresolved.
    void set_contacts(const ContactBook* contacts) { contacts_ = contacts; }

    // Opens the database read-only/immutable. Throws DatabaseError on failure.
    void open();
    void close();

    // Loads a lightweight index of conversations: participants and a
    // message_count, but no message bodies. Cheap even on a huge database.
    std::vector<Chat> load_chat_index();

    // Populates chat.messages (with attachments) for a single conversation,
    // identified by chat.rowid. Lets callers export one chat at a time so peak
    // memory stays proportional to the largest single conversation, not the
    // whole database.
    void load_messages(Chat& chat);

    // Returns, for each handle that has any messages, the latest message date
    // and the service of that newest message. Cheap (one grouped query); used
    // by front-ends to sort/filter the participant picker.
    std::vector<HandleStat> handle_stats();

   private:
    std::string db_path_;
    std::string me_label_;
    void* db_ = nullptr;  // sqlite3* (opaque to avoid leaking the header here)

    // Schema flags detected once in load_chat_index() and reused per chat, so
    // the per-conversation queries don't re-run PRAGMA table_info each time.
    bool has_attributed_ = false;  // message.attributedBody present
    bool has_msg_service_ = false;  // message.service present

    bool has_since_ = false;
    std::time_t since_ = 0;
    bool has_until_ = false;
    std::time_t until_ = 0;
    const ContactBook* contacts_ = nullptr;  // not owned; may be null
};

}  // namespace imsg

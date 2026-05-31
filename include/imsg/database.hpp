// Read-only access to the macOS Messages (`chat.db`) database.
#pragma once

#include <stdexcept>
#include <string>
#include <vector>

#include "imsg/models.hpp"

namespace imsg {

// Thrown when the database cannot be opened or queried.
class DatabaseError : public std::runtime_error {
   public:
    explicit DatabaseError(const std::string& what) : std::runtime_error(what) {}
};

// Default location of the live Messages database for the current user.
std::string default_db_path();

class MessagesDatabase {
   public:
    // `me_label` is used as the sender name for messages you sent.
    explicit MessagesDatabase(std::string db_path, std::string me_label = "Me");
    ~MessagesDatabase();

    MessagesDatabase(const MessagesDatabase&) = delete;
    MessagesDatabase& operator=(const MessagesDatabase&) = delete;

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

   private:
    std::string db_path_;
    std::string me_label_;
    void* db_ = nullptr;  // sqlite3* (opaque to avoid leaking the header here)

    // Schema flags detected once in load_chat_index() and reused per chat, so
    // the per-conversation queries don't re-run PRAGMA table_info each time.
    bool has_attributed_ = false;  // message.attributedBody present
    bool has_msg_service_ = false;  // message.service present
};

}  // namespace imsg

// Core data models for exported iMessage structures.
#pragma once

#include <ctime>
#include <string>
#include <vector>

namespace imsg {

struct Attachment {
    std::string filename;
    std::string mime_type;
    std::string transfer_name;
    long long total_bytes = 0;

    // Path (relative to the output directory) of the copied file, set by the
    // export job when --copy-attachments is used; empty when only metadata is
    // exported. Always uses '/' separators so it works as an HTML/href path.
    std::string copied_path;

    // A "data:<mime>;base64,..." URI of the file's bytes, set by the export job
    // when --embed-attachments is used so HTML/JSON output is self-contained.
    std::string data_uri;

    // Human-friendly name, preferring the transfer name the sender saw.
    std::string display_name() const;
};

struct Message {
    long long rowid = 0;
    std::string guid;
    std::string text;

    bool has_date = false;
    std::time_t date = 0;        // epoch seconds (local rendering done at output)
    bool has_date_read = false;
    std::time_t date_read = 0;

    bool is_from_me = false;
    std::string sender;          // handle id, or the "me" label
    std::string service;         // "iMessage" / "SMS"

    // "data:image/...;base64,..." photo for the sender, resolved from contacts
    // when available; empty falls back to a monogram avatar in the HTML recap.
    std::string avatar_uri;

    bool has_chat = false;
    long long chat_id = -1;

    std::vector<Attachment> attachments;

    // Where this message was likely sent, correlated from an external location
    // source (see location.hpp). Empty/zero when no correlation was attempted or
    // none was confident. `location_label` is a place name or "lat,lon"; the
    // confidence is a 0..100 percent (higher = nearer in time to a known fix).
    std::string location_label;
    int location_confidence = 0;

    bool has_text() const;
};

// One conversation participant, resolved via contacts. Parallels `participants`
// (the display strings) but carries the raw handle + optional photo so the HTML
// header can render per-person avatars.
struct Participant {
    std::string handle;
    std::string name;
    std::string avatar_uri;
};

struct Chat {
    long long rowid = 0;
    std::string guid;
    std::string chat_identifier;
    std::string display_name;
    std::string service;
    std::vector<std::string> participants;
    std::vector<Participant> participant_details;
    long long message_count = 0;   // populated by the chat index; messages may be unloaded
    std::vector<Message> messages;

    // Optional background image for this conversation in the HTML export: any
    // URI usable in CSS background (an http(s) URL, a relative path, or a
    // "data:" URI). Empty leaves the theme's default background.
    std::string background_uri;

    // Best human-readable name for the conversation.
    std::string title() const;
};

}  // namespace imsg

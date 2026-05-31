#include "imsg/export_job.hpp"

#include <cctype>
#include <filesystem>
#include <fstream>
#include <string>
#include <unordered_set>
#include <vector>

#include "imsg/database.hpp"

namespace fs = std::filesystem;

namespace imsg {
namespace {

// Reduces a conversation title to a safe, lowercase filename stem. Keeping only
// alphanumerics and single dashes also prevents path traversal (e.g. "../").
std::string slugify(const std::string& value) {
    std::string out;
    bool last_dash = false;
    for (char ch : value) {
        unsigned char c = static_cast<unsigned char>(ch);
        if (std::isalnum(c)) {
            out += static_cast<char>(std::tolower(c));
            last_dash = false;
        } else if (!last_dash && !out.empty()) {
            out += '-';
            last_dash = true;
        }
        if (out.size() >= 80) break;
    }
    while (!out.empty() && out.back() == '-') out.pop_back();
    return out;
}

}  // namespace

ExportSummary export_database(const std::string& db_path,
                              const std::string& out_dir, Format fmt,
                              const std::string& me_label) {
    ExportSummary summary;
    try {
        MessagesDatabase db(db_path, me_label);
        db.open();
        std::vector<Chat> chats = db.load_chat_index();

        std::error_code ec;
        fs::create_directories(out_dir, ec);
        if (ec) {
            summary.error =
                "cannot create output directory '" + out_dir + "': " + ec.message();
            return summary;
        }

        const std::string ext = extension_for(fmt);
        std::unordered_set<std::string> used;
        int written = 0;

        for (Chat& chat : chats) {
            if (chat.message_count == 0) continue;

            db.load_messages(chat);  // bodies for just this conversation
            if (chat.messages.empty()) continue;

            std::string base = slugify(chat.title());
            if (base.empty()) base = "chat-" + std::to_string(chat.rowid);
            std::string name = base + "." + ext;
            for (int n = 2; used.count(name); ++n)
                name = base + "-" + std::to_string(n) + "." + ext;
            used.insert(name);

            fs::path path = fs::path(out_dir) / name;
            std::ofstream out(path, std::ios::binary);
            if (!out) {
                summary.error = "cannot write '" + path.string() + "'";
                return summary;
            }
            out << render(chat, fmt);

            // Release this conversation's bodies before moving to the next one.
            chat.messages.clear();
            chat.messages.shrink_to_fit();
            ++written;
        }

        summary.ok = true;
        summary.conversations = written;
        return summary;
    } catch (const DatabaseError& e) {
        summary.error = e.what();
        return summary;
    }
}

}  // namespace imsg

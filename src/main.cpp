// Command-line entry point for imessage-exporter.
#include <algorithm>
#include <iostream>
#include <string>
#include <vector>

#include "imsg/database.hpp"
#include "imsg/export_job.hpp"
#include "imsg/exporters.hpp"

namespace {

constexpr const char* kVersion = "0.1.0";

void print_usage(std::ostream& os) {
    os << "Usage: imessage-exporter [options]\n\n"
       << "Export macOS iMessage/SMS history to TXT, JSON, or HTML.\n\n"
       << "Options:\n"
       << "  --db PATH        Path to the Messages database\n"
       << "                   (default: $HOME/Library/Messages/chat.db)\n"
       << "  --format FMT     Output format: " << imsg::available_formats()
       << " (default: txt)\n"
       << "  --output DIR     Directory for exported files (default: ./imessage-export)\n"
       << "  --me NAME        Label for messages you sent (default: Me)\n"
       << "  --list-chats     List conversations and exit\n"
       << "  --version        Print version and exit\n"
       << "  --help           Show this help and exit\n";
}

// Reads the value following a flag, erroring if it's missing.
bool take_value(int argc, char** argv, int& i, const char* flag, std::string& out) {
    if (i + 1 >= argc) {
        std::cerr << "error: " << flag << " requires a value\n";
        return false;
    }
    out = argv[++i];
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    std::string db_path = imsg::default_db_path();
    std::string format_name = "txt";
    std::string output_dir = "imessage-export";
    std::string me_label = "Me";
    bool list_chats = false;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            print_usage(std::cout);
            return 0;
        } else if (arg == "--version") {
            std::cout << "imessage-exporter " << kVersion << "\n";
            return 0;
        } else if (arg == "--list-chats") {
            list_chats = true;
        } else if (arg == "--db") {
            if (!take_value(argc, argv, i, "--db", db_path)) return 2;
        } else if (arg == "--format") {
            if (!take_value(argc, argv, i, "--format", format_name)) return 2;
        } else if (arg == "--output") {
            if (!take_value(argc, argv, i, "--output", output_dir)) return 2;
        } else if (arg == "--me") {
            if (!take_value(argc, argv, i, "--me", me_label)) return 2;
        } else {
            std::cerr << "error: unknown argument '" << arg << "'\n\n";
            print_usage(std::cerr);
            return 2;
        }
    }

    imsg::Format format;
    if (!imsg::parse_format(format_name, format)) {
        std::cerr << "error: unknown format '" << format_name << "'; choose from "
                  << imsg::available_formats() << "\n";
        return 2;
    }

    if (list_chats) {
        std::vector<imsg::Chat> chats;
        try {
            imsg::MessagesDatabase db(db_path, me_label);
            db.open();
            chats = db.load_chat_index();
        } catch (const imsg::DatabaseError& e) {
            std::cerr << "error: " << e.what() << "\n";
            return 1;
        }
        chats.erase(std::remove_if(chats.begin(), chats.end(),
                                   [](const imsg::Chat& c) {
                                       return c.message_count == 0;
                                   }),
                    chats.end());
        if (chats.empty()) {
            std::cerr << "No conversations with messages were found.\n";
            return 1;
        }
        std::sort(chats.begin(), chats.end(),
                  [](const imsg::Chat& a, const imsg::Chat& b) {
                      return a.message_count > b.message_count;
                  });
        for (const auto& c : chats) {
            std::cout << c.message_count << "\t" << c.title() << "\n";
        }
        return 0;
    }

    imsg::ExportSummary summary =
        imsg::export_database(db_path, output_dir, format, me_label);
    if (!summary.ok) {
        std::cerr << "error: " << summary.error << "\n";
        return 1;
    }
    if (summary.conversations == 0) {
        std::cerr << "No conversations with messages were found.\n";
        return 1;
    }

    std::cout << "Exported " << summary.conversations << " conversation(s) to "
              << output_dir << " as " << format_name << ".\n";
    return 0;
}

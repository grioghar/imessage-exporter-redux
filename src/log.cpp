#include "imsg/log.hpp"

#include <cctype>
#include <iostream>

namespace imsg {
namespace {

LogLevel g_level = LogLevel::Warn;

const char* tag(LogLevel level) {
    switch (level) {
        case LogLevel::Error: return "ERROR";
        case LogLevel::Warn: return "WARN";
        case LogLevel::Info: return "INFO";
        case LogLevel::Debug: return "DEBUG";
    }
    return "?";
}

}  // namespace

void set_log_level(LogLevel level) { g_level = level; }
LogLevel log_level() { return g_level; }

const char* log_level_name(LogLevel level) {
    switch (level) {
        case LogLevel::Error: return "error";
        case LogLevel::Warn: return "warn";
        case LogLevel::Info: return "info";
        case LogLevel::Debug: return "debug";
    }
    return "warn";
}

bool parse_log_level(const std::string& name, LogLevel& out) {
    std::string n;
    for (unsigned char c : name) n += static_cast<char>(std::tolower(c));
    if (n == "error" || n == "0") { out = LogLevel::Error; return true; }
    if (n == "warn" || n == "warning" || n == "1") { out = LogLevel::Warn; return true; }
    if (n == "info" || n == "2") { out = LogLevel::Info; return true; }
    if (n == "debug" || n == "trace" || n == "3") { out = LogLevel::Debug; return true; }
    return false;
}

void log_message(LogLevel level, const std::string& msg) {
    if (static_cast<int>(level) > static_cast<int>(g_level)) return;
    std::cerr << "[" << tag(level) << "] " << msg << "\n";
}

}  // namespace imsg

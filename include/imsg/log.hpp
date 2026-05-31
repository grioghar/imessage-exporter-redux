// Tiny dependency-free logging facility shared by the core, CLI, and the C
// bridge (so every front-end — CLI, desktop GUI, iOS app — logs the same way).
// Messages go to stderr; the four levels are Error < Warn < Info < Debug.
#pragma once

#include <functional>
#include <string>

namespace imsg {

enum class LogLevel { Error = 0, Warn = 1, Info = 2, Debug = 3 };

// Sets / gets the active threshold. Messages at a level numerically greater than
// the threshold are dropped. Default threshold is Warn.
void set_log_level(LogLevel level);
LogLevel log_level();

// Parses "error"/"warn"/"info"/"debug" (case-insensitive) or "0".."3".
bool parse_log_level(const std::string& name, LogLevel& out);

// Routes emitted messages to `sink` instead of stderr (e.g. a GUI log pane).
// Pass nullptr to restore the default stderr writer. The threshold still
// applies before the sink is called.
using LogSink = std::function<void(LogLevel level, const std::string& msg)>;
void set_log_sink(LogSink sink);

// Lower-cased name of a level, for help text and echoing.
const char* log_level_name(LogLevel level);

// Emits `msg` to stderr when `level` is at or below the active threshold.
void log_message(LogLevel level, const std::string& msg);

inline void log_error(const std::string& m) { log_message(LogLevel::Error, m); }
inline void log_warn(const std::string& m) { log_message(LogLevel::Warn, m); }
inline void log_info(const std::string& m) { log_message(LogLevel::Info, m); }
inline void log_debug(const std::string& m) { log_message(LogLevel::Debug, m); }

// True when debug output is active — guard expensive debug string building with
// this to avoid the cost when debug logging is off.
inline bool log_debug_enabled() { return log_level() == LogLevel::Debug; }

}  // namespace imsg

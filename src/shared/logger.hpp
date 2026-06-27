#pragma once
#include <chrono>
#include <ctime>
#include <iostream>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>

namespace draind {

enum class LogLevel { Debug, Info, Warn, Error };
inline LogLevel g_log_level = LogLevel::Info;

inline const char* level_tag(LogLevel l) {
    switch (l) {
    case LogLevel::Debug: return "DEBUG";
    case LogLevel::Info:  return "INFO ";
    case LogLevel::Warn:  return "WARN ";
    case LogLevel::Error: return "ERROR";
    }
    return "?";
}

inline LogLevel parse_log_level(const std::string& s) {
    if (s == "debug") return LogLevel::Debug;
    if (s == "info")  return LogLevel::Info;
    if (s == "warn")  return LogLevel::Warn;
    if (s == "error") return LogLevel::Error;
    throw std::invalid_argument("unknown log level: " + s);
}

struct LogEntry {
    LogLevel level;
    std::ostringstream ss;

    explicit LogEntry(LogLevel l) : level(l) {}
    ~LogEntry() {
        if (level < g_log_level)
            return;
        auto now = std::chrono::system_clock::now();
        auto t   = std::chrono::system_clock::to_time_t(now);
        char buf[10];
        std::strftime(buf, sizeof(buf), "%H:%M:%S", std::localtime(&t));
        static std::mutex mx;
        std::lock_guard lock(mx);
        std::cerr << '[' << buf << "] " << level_tag(level) << "  " << ss.str() << '\n';
    }

    template <typename T>
    LogEntry& operator<<(const T& v) { ss << v; return *this; }
};

} // namespace draind

#define LOG_DEBUG draind::LogEntry{draind::LogLevel::Debug}
#define LOG_INFO  draind::LogEntry{draind::LogLevel::Info}
#define LOG_WARN  draind::LogEntry{draind::LogLevel::Warn}
#define LOG_ERROR draind::LogEntry{draind::LogLevel::Error}

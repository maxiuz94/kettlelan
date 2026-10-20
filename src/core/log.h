// KettleLAN – proste logowanie na stderr.
#pragma once

#include <chrono>
#include <ctime>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>

namespace kettle::log {

inline std::mutex& mutex() {
    static std::mutex m;
    return m;
}

inline void write(const char* level, const std::string& msg) {
    const auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &now);
#else
    localtime_r(&now, &tm);
#endif
    char ts[32];
    std::strftime(ts, sizeof(ts), "%H:%M:%S", &tm);
    std::lock_guard lk(mutex());
    std::clog << "[" << ts << "] " << level << " " << msg << std::endl;
}

} // namespace kettle::log

#define KLOG_IMPL(level, expr)                                                                     \
    do {                                                                                           \
        std::ostringstream kettle_log_ss_;                                                         \
        kettle_log_ss_ << expr; /* wyrażenia z ?: podawaj w nawiasach */                                                                    \
        ::kettle::log::write(level, kettle_log_ss_.str());                                         \
    } while (0)

#define KLOG_INFO(expr) KLOG_IMPL("INFO ", expr)
#define KLOG_WARN(expr) KLOG_IMPL("UWAGA", expr)
#define KLOG_ERROR(expr) KLOG_IMPL("BŁĄD ", expr)
#ifdef KETTLELAN_DEBUG
#define KLOG_DEBUG(expr) KLOG_IMPL("DEBUG", expr)
#else
#define KLOG_DEBUG(expr) do { } while (0)
#endif

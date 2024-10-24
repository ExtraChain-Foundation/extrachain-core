#ifndef EXC_LOGS_H
#define EXC_LOGS_H

#include <fmt/core.h>
#include <fmt/format.h>
#include <fmt/chrono.h>
#include <fmt/color.h>
#include <string_view>
#include <chrono>
#include <thread>
#include <source_location>
#include <fstream>
#include <filesystem>
#include <sstream>
#include <csignal>

#ifdef _WIN32
    #include <windows.h>
    #include <crtdbg.h>
#endif

enum class LogLevel {
    Debug,
    Info,
    Warning,
    Critical,
    Fatal
};

class Logger {
    std::ofstream     log_file;
    std::string       current_log_filename;
    bool              debug_enabled       = false;
    bool              file_output_enabled = true;
    const std::string logs_directory      = "logs";
    std::thread::id   main_thread_id;

    static std::string create_log_filename() {
        auto    now   = std::chrono::system_clock::now();
        auto    timer = std::chrono::system_clock::to_time_t(now);
        std::tm bt    = *std::localtime(&timer);
        auto    ms    = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;

        return fmt::format(
            "extrachain-{:02d}-{:02d}-{:02d}.{:02d}.{:02d}.{:03d}.log",
            bt.tm_hour,
            bt.tm_min,
            bt.tm_sec,
            bt.tm_mday,
            bt.tm_mon + 1,
            ms.count());
    }

    void ensure_logs_directory() {
        std::filesystem::path dir_path(logs_directory);
        if (!std::filesystem::exists(dir_path)) {
            std::filesystem::create_directory(dir_path);
        }
    }

    void open_log_file() {
        if (!file_output_enabled)
            return;
        ensure_logs_directory();
        current_log_filename = logs_directory + "/" + create_log_filename();
        log_file.open(current_log_filename, std::ios::out | std::ios::app);
    }

public:
    Logger() {
        main_thread_id = std::this_thread::get_id();
        open_log_file();
    }
    ~Logger() {
        if (log_file.is_open())
            log_file.close();
    }

    void set_debug(bool enabled) {
        debug_enabled = enabled;
    }
    bool is_debug() const {
        return debug_enabled;
    }

    void set_file_output(bool enabled) {
        if (file_output_enabled == enabled)
            return;
        file_output_enabled = enabled;
        if (enabled) {
            open_log_file();
        } else if (log_file.is_open()) {
            log_file.close();
        }
    }

    bool is_file_output() const {
        return file_output_enabled;
    }

    bool write_to_file(const std::string& message) {
        if (!file_output_enabled || !log_file.is_open())
            return false;
        log_file << message;
        log_file.flush();
        return true;
    }

    bool is_main_thread() const {
        return std::this_thread::get_id() == main_thread_id;
    }

    static Logger& instance() {
        static Logger logger;
        return logger;
    }
};

namespace detail {

static std::string g_abort_message;

#ifdef _WIN32
[[noreturn]] inline void terminate_application(const std::string& message) {
    _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_DEBUG | _CRTDBG_MODE_WNDW);
    _RPT1(_CRT_ERROR, "%s", message.c_str());
    std::abort();
}
#else
[[noreturn]] inline void terminate_application(const std::string& message) {
    fmt::println("{}", message);
    std::abort();
}
#endif

inline std::string get_current_time() {
    auto    now   = std::chrono::system_clock::now();
    auto    ms    = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
    auto    timer = std::chrono::system_clock::to_time_t(now);
    std::tm bt    = *std::localtime(&timer);

    return fmt::format("{:02d}:{:02d}:{:02d}.{:03d}", bt.tm_hour, bt.tm_min, bt.tm_sec, ms.count());
}

inline std::string get_full_time() {
    auto    now   = std::chrono::system_clock::now();
    auto    ms    = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
    auto    timer = std::chrono::system_clock::to_time_t(now);
    std::tm bt    = *std::localtime(&timer);

    return fmt::format(
        "{:04d}.{:02d}.{:02d} {:02d}:{:02d}:{:02d}.{:03d}",
        bt.tm_year + 1900,
        bt.tm_mon + 1,
        bt.tm_mday,
        bt.tm_hour,
        bt.tm_min,
        bt.tm_sec,
        ms.count());
}

inline std::string get_thread_id() {
    if (Logger::instance().is_main_thread()) {
        return "main";
    }
    std::stringstream ss;
    ss << std::this_thread::get_id();
    return ss.str();
}

inline std::string_view get_filename(std::string_view path) {
    size_t pos = path.find_last_of("/\\");
    return pos == std::string_view::npos ? path : path.substr(pos + 1);
}

inline bool should_log(LogLevel level) {
    switch (level) {
    case LogLevel::Info:
    case LogLevel::Fatal:
        return true;
    case LogLevel::Debug:
    case LogLevel::Warning:
    case LogLevel::Critical:
        return Logger::instance().is_debug();
    default:
        return false;
    }
}

inline fmt::text_style get_level_style(LogLevel level) {
    switch (level) {
    case LogLevel::Debug:
        return fmt::text_style();
    case LogLevel::Info:
        return fmt::text_style();
    case LogLevel::Warning:
        return fmt::fg(fmt::color::yellow);
    case LogLevel::Critical:
        return fmt::fg(fmt::color::orange_red);
    case LogLevel::Fatal:
        return fmt::emphasis::bold | fmt::fg(fmt::color::red);
    default:
        return fmt::text_style();
    }
}

inline std::string_view get_level_name(LogLevel level) {
    switch (level) {
    case LogLevel::Debug:
        return "";
    case LogLevel::Info:
        return "Info";
    case LogLevel::Warning:
        return "Warning";
    case LogLevel::Critical:
        return "Critical";
    case LogLevel::Fatal:
        return "Fatal";
    default:
        return "Unknown";
    }
}

template <typename... Args>
void println_impl(
    LogLevel                    level,
    const std::source_location& loc,
    fmt::format_string<Args...> format_str,
    Args&&... args) {
    if (!should_log(level))
        return;

    std::string message    = fmt::format(format_str, std::forward<Args>(args)...);
    auto        style      = get_level_style(level);
    auto        level_name = get_level_name(level);

    // Console output
    if (level_name.empty()) {
        fmt::print(
            stdout,
            "{} [file:/{}:{}] [{}] {}\n",
            get_current_time(),
            get_filename(loc.file_name()),
            loc.line(),
            get_thread_id(),
            message);
    } else {
        fmt::print(
            stdout,
            style,
            "{} [{}] [file:/{}:{}] [{}] {}\n",
            get_current_time(),
            level_name,
            get_filename(loc.file_name()),
            loc.line(),
            get_thread_id(),
            message);
    }

    // File output
    if (Logger::instance().is_file_output()) {
        std::string file_message;
        if (level_name.empty()) {
            file_message = fmt::format(
                "{} [file:/{}:{}] [{}] {}\n",
                get_full_time(),
                get_filename(loc.file_name()),
                loc.line(),
                get_thread_id(),
                message);
        } else {
            file_message = fmt::format(
                "{} [{}] [file:/{}:{}] [{}] {}\n",
                get_full_time(),
                level_name,
                get_filename(loc.file_name()),
                loc.line(),
                get_thread_id(),
                message);
        }
        Logger::instance().write_to_file(file_message);
    }

    fflush(stdout);
}

template <typename... Args>
[[noreturn]] void
fatal_impl(const std::source_location& loc, fmt::format_string<Args...> format_str, Args&&... args) {
    std::string message = fmt::format(format_str, std::forward<Args>(args)...);
    println_impl(LogLevel::Fatal, loc, format_str, std::forward<Args>(args)...);

    std::string abort_message = fmt::format(
        "{}, {}:{} - {}",
        loc.function_name(),
        get_filename(loc.file_name()),
        loc.line(),
        message);

    terminate_application(abort_message);
}

} // namespace detail

// Logging macros
#define eDebug(...)    detail::println_impl(LogLevel::Debug, std::source_location::current(), __VA_ARGS__)
#define eInfo(...)     detail::println_impl(LogLevel::Info, std::source_location::current(), __VA_ARGS__)
#define eWarning(...)  detail::println_impl(LogLevel::Warning, std::source_location::current(), __VA_ARGS__)
#define eCritical(...) detail::println_impl(LogLevel::Critical, std::source_location::current(), __VA_ARGS__)
#define eFatal(...)    detail::fatal_impl(std::source_location::current(), __VA_ARGS__)
#define eLog(...)      eDebug(__VA_ARGS__)

#endif // EXC_LOGS_H

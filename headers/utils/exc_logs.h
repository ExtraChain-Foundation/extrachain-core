/*
 * ExtraChain Core
 * Copyright (C) 2025 ExtraChain Foundation <official@extrachain.io>
 *
 * This library is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#pragma once

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

#ifdef _WIN32
    #include <windows.h>
    #include <crtdbg.h>
#endif

#ifdef __ANDROID__
    #include <android/log.h>
#endif

#include "utils/exc_logs_filter.h"
#include "utils/fs_path.h"

enum class LogLevel {
    Debug,
    Info,
    Warning,
    Critical,
    Fatal,
    Success
};

class Logger {
    std::ofstream     log_file;
    std::string       current_log_filename;
    bool              debug_enabled       = false;
    bool              file_output_enabled = true;
    const std::string logs_directory      = "logs";
    std::thread::id   main_thread_id;

    FileFilter file_filter;
    bool       filter_enabled = false;
    LogModule  active_modules = LogModule::All;

    static std::string create_log_filename() {
        auto    now   = std::chrono::system_clock::now();
        auto    timer = std::chrono::system_clock::to_time_t(now);
        std::tm bt    = *std::localtime(&timer);
        auto    ms    = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;

        return fmt::format("extrachain-{:02d}-{:02d}-{:02d}.{:02d}.{:02d}.{:03d}.log",
                           bt.tm_hour,
                           bt.tm_min,
                           bt.tm_sec,
                           bt.tm_mday,
                           bt.tm_mon + 1,
                           ms.count());
    }

    void ensure_logs_directory() {
        auto path = FsPath::create(logs_directory);
        if (!path) {
            fmt::println("Failed to create logs files: {}", std::to_underlying(path.error()));
            return;
        }

        auto exists = path->exists();
        if (!exists.has_value()) {
            fmt::println("Failed to check directory existence: {}", static_cast<int>(exists.error()));
            return;
        }

        if (exists.value()) {
            return;
        }

        std::error_code ec;
        std::filesystem::create_directory(path->native(), ec);
        if (ec) {
            fmt::println("Failed to create logs directory: {}", ec.message());
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

    void enable_filter(bool enable = true) {
        filter_enabled = enable;
    }

    void setInverseMode(bool inverse) {
        file_filter.setInverseMode(inverse);
    }

    void set_active_modules(LogModule modules) {
        active_modules = modules;
    }

    void add_active_module(LogModule module) {
        active_modules = active_modules | module;
    }

    void addExcludePattern(std::string_view pattern) {
        file_filter.addExcludePattern(pattern);
    }

    void clearExcludePatterns() {
        file_filter.clearExcludePatterns();
    }

    void addCustomPattern(std::string_view pattern) {
        file_filter.addCustomPattern(pattern);
    }

    void clearCustomPatterns() {
        file_filter.clearCustomPatterns();
    }

    bool should_log(std::string_view file) const {
        if (!filter_enabled)
            return true;

        size_t           pos      = file.find_last_of("/\\");
        std::string_view filename = (pos == std::string_view::npos) ? file : file.substr(pos + 1);

        // First check custom patterns if any exist
        if (!file_filter.getCustomPatterns().empty()) {
            return file_filter.matchesCustomPatterns(filename) ^ file_filter.is_inverse_mode();
        }

        // If no custom patterns, use module filtering
        LogModule file_module = file_filter.determineModule(filename);
        if (active_modules == LogModule::All) {
            return !file_filter.is_inverse_mode(); // true for normal mode, false for inverse
        }

        return (static_cast<int>(file_module & active_modules) != 0) ^ file_filter.is_inverse_mode();
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

    bool write_to_file(std::string_view message) {
        if (!file_output_enabled || !log_file.is_open())
            return false;
        log_file.write(message.data(), message.size());
        log_file.flush();
        return true;
    }

    bool write_to_file(const std::string& message) {
        return write_to_file(std::string_view(message));
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

        return fmt::format("{:04d}.{:02d}.{:02d} {:02d}:{:02d}:{:02d}.{:03d}",
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
        case LogLevel::Success:
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
        case LogLevel::Success:
            return fmt::fg(fmt::color::green);
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
        case LogLevel::Success:
            return "Success";
        default:
            return "Unknown";
        }
    }

    template <typename... Args>
    void println_impl(LogLevel                    level,
                      std::string_view            file,
                      uint32_t                    line,
                      fmt::format_string<Args...> format_str,
                      Args&&... args) {
        if (!should_log(level) || !Logger::instance().should_log(file))
            return;

        thread_local fmt::memory_buffer log_buffer;
        thread_local fmt::memory_buffer msg_buffer;

        log_buffer.clear();
        msg_buffer.clear();

        // Format message
        fmt::format_to(std::back_inserter(msg_buffer), format_str, std::forward<Args>(args)...);

        // Format base log
        const auto level_name = get_level_name(level);
        const bool has_level  = !level_name.empty();

        fmt::format_to(std::back_inserter(log_buffer),
                       "{}{}{}{} [file:/{}:{}] [{}] {}",
                       get_current_time(),
                       has_level ? " [" : "",
                       has_level ? level_name : "",
                       has_level ? "]" : "",
                       get_filename(file),
                       line,
                       get_thread_id(),
                       fmt::string_view(msg_buffer.data(), msg_buffer.size()));

        // Write to all outputs
        auto log_view = fmt::string_view(log_buffer.data(), log_buffer.size());

#ifdef __ANDROID__
        int android_priority = level == LogLevel::Debug      ? ANDROID_LOG_DEBUG
                               : level == LogLevel::Info     ? ANDROID_LOG_INFO
                               : level == LogLevel::Warning  ? ANDROID_LOG_WARN
                               : level == LogLevel::Critical ? ANDROID_LOG_ERROR
                               : level == LogLevel::Fatal    ? ANDROID_LOG_FATAL
                                                             : ANDROID_LOG_INFO;
        __android_log_print(android_priority,
                            "ExtraChain",
                            "%.*s",
                            static_cast<int>(log_buffer.size()),
                            log_buffer.data());
#else
    #ifdef _WIN32
        if (IsDebuggerPresent()) {
            log_buffer.push_back('\n');
            auto log_view = fmt::string_view(log_buffer.data(), log_buffer.size());
            OutputDebugStringA(log_view.data());
        }
    #endif

        // Console with color
        fmt::print(stdout, get_level_style(level), "{}\n", log_view);
        fflush(stdout);
#endif

        if (Logger::instance().is_file_output()) {
            msg_buffer.clear();
            fmt::format_to(std::back_inserter(msg_buffer), "{} {}\n", get_full_time(), log_view);
            Logger::instance().write_to_file(std::string_view(msg_buffer.data(), msg_buffer.size()));
        }
    }

    // source_location wrapper
    template <typename... Args>
    void println_impl(LogLevel                    level,
                      const std::source_location& loc,
                      fmt::format_string<Args...> format_str,
                      Args&&... args) {
        println_impl(level, loc.file_name(), loc.line(), format_str, std::forward<Args>(args)...);
    }

    template <typename... Args>
    [[noreturn]] void fatal_impl(const std::source_location& loc,
                                 fmt::format_string<Args...> format_str,
                                 Args&&... args) {
        std::string message = fmt::format(format_str, std::forward<Args>(args)...);
        println_impl(LogLevel::Fatal, loc, format_str, std::forward<Args>(args)...);

        std::string abort_message =
            fmt::format("{}, {}:{} - {}", loc.function_name(), get_filename(loc.file_name()), loc.line(), message);

        terminate_application(abort_message);
    }

} // namespace detail

// Logging macros
#define eDebug(...)    ::detail::println_impl(LogLevel::Debug, std::source_location::current(), __VA_ARGS__)
#define eInfo(...)     ::detail::println_impl(LogLevel::Info, std::source_location::current(), __VA_ARGS__)
#define eWarning(...)  ::detail::println_impl(LogLevel::Warning, std::source_location::current(), __VA_ARGS__)
#define eCritical(...) ::detail::println_impl(LogLevel::Critical, std::source_location::current(), __VA_ARGS__)
#define eFatal(...)    ::detail::fatal_impl(std::source_location::current(), __VA_ARGS__)
#define eSuccess(...)  ::detail::println_impl(LogLevel::Success, std::source_location::current(), __VA_ARGS__)
#define eLog(...)      eDebug(__VA_ARGS__)

#include "utils/exc_logs_extra.h"

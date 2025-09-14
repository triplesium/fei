#pragma once
#include <cstdint>
#include <exception>
#include <format>
#include <source_location>
#include <string>

namespace fei {

enum class LogLevel : std::uint32_t {
    Trace,
    Debug,
    Info,
    Warn,
    Error,
    Fatal,
    None
};
static constexpr const char* LOG_LEVEL_STR[] =
    {"trace", "debug", "info", "warn", "error", "fatal", nullptr};

struct FormatString {
    std::string_view str;
    std::source_location loc;
    FormatString(
        const char* str,
        const std::source_location& loc = std::source_location::current()
    ) : str(str), loc(loc) {}
};

namespace detail {

std::string make_log_prefix(LogLevel level, std::source_location loc);

void log(LogLevel level, const FormatString& format, std::format_args args);

} // namespace detail

template<typename... Args>
void trace(const FormatString& format, Args&&... args) {
    detail::log(LogLevel::Trace, format, std::make_format_args(args...));
}

template<typename... Args>
void debug(const FormatString& format, Args&&... args) {
    detail::log(LogLevel::Debug, format, std::make_format_args(args...));
}

template<typename... Args>
void info(const FormatString& format, Args&&... args) {
    detail::log(LogLevel::Info, format, std::make_format_args(args...));
}

template<typename... Args>
void warn(const FormatString& format, Args&&... args) {
    detail::log(LogLevel::Warn, format, std::make_format_args(args...));
}

template<typename... Args>
void error(const FormatString& format, Args&&... args) {
    detail::log(LogLevel::Error, format, std::make_format_args(args...));
}

template<typename... Args>
void fatal(const FormatString& format, Args&&... args) {
    detail::log(LogLevel::Fatal, format, std::make_format_args(args...));
    std::terminate();
}

} // namespace fei

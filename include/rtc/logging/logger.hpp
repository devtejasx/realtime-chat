#pragma once

#include <spdlog/spdlog.h>

#include <memory>
#include <string_view>

namespace rtc::logging {

// Initialises the global logging subsystem.
//
// Creates a colourised, thread-safe stdout logger with a structured pattern
// (timestamp, level, thread id, logger name, message) and applies the given
// level. Safe to call once at startup; subsequent calls reconfigure the level.
//
// `level` accepts: trace, debug, info, warn, warning, error, critical, off.
// Unrecognised values fall back to "info".
//
// `format` selects the output layout: "text" (human-readable, the default) or
// "json" (one JSON object per line, for log aggregation in production).
void init(std::string_view level, std::string_view format = "text");

// Builds the formatter init() installs, with the trace/span/request-id flags
// registered.
//
// Exposed so the correlation fields can be asserted against the *real* pattern
// rather than a copy of it in a test — a duplicated pattern would keep passing
// after the production one lost a field, which is precisely the regression worth
// catching.
[[nodiscard]] std::unique_ptr<spdlog::formatter> make_formatter(bool json);

// Returns the shared application logger. Never null after init(); before
// init() it lazily returns spdlog's default logger so early logging is safe.
[[nodiscard]] std::shared_ptr<spdlog::logger> get();

// Translates a textual level into an spdlog level, defaulting to info.
[[nodiscard]] spdlog::level::level_enum parse_level(std::string_view level) noexcept;

// Flushes any buffered log records; call during graceful shutdown.
void shutdown();

}  // namespace rtc::logging

// Convenience macros routing through the application logger. Using macros here
// (the one sanctioned exception to the "avoid macros" rule) preserves spdlog's
// compile-time format string checking and source-location capture.
#define RTC_LOG_TRACE(...) SPDLOG_LOGGER_TRACE(::rtc::logging::get(), __VA_ARGS__)
#define RTC_LOG_DEBUG(...) SPDLOG_LOGGER_DEBUG(::rtc::logging::get(), __VA_ARGS__)
#define RTC_LOG_INFO(...) SPDLOG_LOGGER_INFO(::rtc::logging::get(), __VA_ARGS__)
#define RTC_LOG_WARN(...) SPDLOG_LOGGER_WARN(::rtc::logging::get(), __VA_ARGS__)
#define RTC_LOG_ERROR(...) SPDLOG_LOGGER_ERROR(::rtc::logging::get(), __VA_ARGS__)
#define RTC_LOG_CRITICAL(...) SPDLOG_LOGGER_CRITICAL(::rtc::logging::get(), __VA_ARGS__)

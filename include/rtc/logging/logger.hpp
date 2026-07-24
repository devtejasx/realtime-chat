#pragma once

#include <memory>
#include <string_view>

#include <spdlog/spdlog.h>

namespace rtc::logging {

// Initialises the global logging subsystem.
//
// Creates a colourised, thread-safe stdout logger with a structured pattern
// (timestamp, level, thread id, logger name, message) and applies the given
// level. Safe to call once at startup; subsequent calls reconfigure the level.
//
// `level` accepts: trace, debug, info, warn, warning, error, critical, off.
// Unrecognised values fall back to "info".
void init(std::string_view level);

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

#include "rtc/logging/logger.hpp"

#include <algorithm>
#include <cctype>
#include <string>

#include <spdlog/sinks/stdout_color_sinks.h>

namespace rtc::logging {
namespace {

constexpr const char* kLoggerName = "realtime-chat";

// Human-readable: 2026-07-24 12:00:00.123 [info] [tid 12345] realtime-chat: msg
constexpr const char* kTextPattern = "%Y-%m-%d %H:%M:%S.%e [%^%l%$] [tid %t] %n: %v";

// One JSON object per line for structured log ingestion. (spdlog does not
// escape %v; log call sites avoid embedding raw quotes/newlines in messages.)
constexpr const char* kJsonPattern =
    R"({"time":"%Y-%m-%dT%H:%M:%S.%e%z","level":"%l","thread":%t,"logger":"%n","message":"%v"})";

[[nodiscard]] std::string to_lower(std::string_view value) {
    std::string out(value);
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

}  // namespace

spdlog::level::level_enum parse_level(std::string_view level) noexcept {
    const std::string normalized = to_lower(level);
    if (normalized == "trace") return spdlog::level::trace;
    if (normalized == "debug") return spdlog::level::debug;
    if (normalized == "info") return spdlog::level::info;
    if (normalized == "warn" || normalized == "warning") return spdlog::level::warn;
    if (normalized == "error" || normalized == "err") return spdlog::level::err;
    if (normalized == "critical" || normalized == "crit") return spdlog::level::critical;
    if (normalized == "off") return spdlog::level::off;
    return spdlog::level::info;
}

void init(std::string_view level, std::string_view format) {
    // Replace any previously registered logger so re-init is idempotent.
    spdlog::drop(kLoggerName);

    auto logger = spdlog::stdout_color_mt(kLoggerName);
    logger->set_pattern(to_lower(format) == "json" ? kJsonPattern : kTextPattern);

    const auto lvl = parse_level(level);
    logger->set_level(lvl);
    logger->flush_on(spdlog::level::warn);

    spdlog::set_default_logger(logger);
    spdlog::flush_every(std::chrono::seconds(3));
}

std::shared_ptr<spdlog::logger> get() {
    if (auto logger = spdlog::get(kLoggerName)) {
        return logger;
    }
    return spdlog::default_logger();
}

void shutdown() {
    if (auto logger = spdlog::get(kLoggerName)) {
        logger->flush();
    }
    spdlog::shutdown();
}

}  // namespace rtc::logging

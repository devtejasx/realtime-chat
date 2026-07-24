#pragma once

#include <chrono>
#include <optional>
#include <string>
#include <string_view>

namespace rtc::utils {

using Clock = std::chrono::system_clock;
using TimePoint = Clock::time_point;

// Current wall-clock time.
[[nodiscard]] inline TimePoint now() { return Clock::now(); }

// Formats a time point as an ISO-8601 UTC string, e.g. "2026-07-24T12:00:00Z".
// Used for API responses and log context.
[[nodiscard]] std::string to_iso8601(TimePoint tp);

// Parses a PostgreSQL timestamp (with or without timezone / fractional
// seconds) into a time point. Returns nullopt on malformed input.
[[nodiscard]] std::optional<TimePoint> parse_pg_timestamp(std::string_view value);

// Converts a time point to Unix epoch seconds (used for JWT iat/exp claims).
[[nodiscard]] inline std::int64_t to_unix_seconds(TimePoint tp) {
    return std::chrono::duration_cast<std::chrono::seconds>(tp.time_since_epoch()).count();
}

}  // namespace rtc::utils

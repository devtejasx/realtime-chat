#include "rtc/utils/time.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <string>
#include <tuple>

namespace rtc::utils {
namespace {

// Portable, thread-safe UTC calendar arithmetic based on Howard Hinnant's
// public-domain algorithms. We deliberately avoid gmtime_r/timegm (whose
// availability and thread-safety vary across libc/MSVC/mingw) so the same code
// behaves identically on every target.

// Days since 1970-01-01 for a proleptic-Gregorian y-m-d (m in [1,12]).
[[nodiscard]] std::int64_t days_from_civil(std::int64_t y, unsigned m, unsigned d) {
    y -= m <= 2;
    const std::int64_t era = (y >= 0 ? y : y - 399) / 400;
    const auto yoe = static_cast<unsigned>(y - era * 400);                 // [0, 399]
    const unsigned doy = (153 * (m > 2 ? m - 3 : m + 9) + 2) / 5 + d - 1;  // [0, 365]
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;            // [0, 146096]
    return era * 146097 + static_cast<std::int64_t>(doe) - 719468;
}

// Inverse of days_from_civil: days since epoch -> (year, month, day).
[[nodiscard]] std::tuple<std::int64_t, unsigned, unsigned> civil_from_days(std::int64_t z) {
    z += 719468;
    const std::int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    const auto doe = static_cast<unsigned>(z - era * 146097);                    // [0, 146096]
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;  // [0, 399]
    const std::int64_t y = static_cast<std::int64_t>(yoe) + era * 400;
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);  // [0, 365]
    const unsigned mp = (5 * doy + 2) / 153;                       // [0, 11]
    const unsigned d = doy - (153 * mp + 2) / 5 + 1;               // [1, 31]
    const unsigned m = mp < 10 ? mp + 3 : mp - 9;                  // [1, 12]
    return {y + (m <= 2), m, d};
}

// Floored division/modulo so pre-epoch instants split correctly.
[[nodiscard]] std::int64_t floor_div(std::int64_t a, std::int64_t b) {
    const std::int64_t q = a / b;
    return (a % b != 0 && ((a % b < 0) != (b < 0))) ? q - 1 : q;
}

}  // namespace

std::string to_iso8601(TimePoint tp) {
    const std::int64_t secs = to_unix_seconds(tp);
    const std::int64_t days = floor_div(secs, 86400);
    const auto sod = static_cast<std::int64_t>(secs - days * 86400);  // [0, 86399]

    const auto [year, month, day] = civil_from_days(days);
    const int hour = static_cast<int>(sod / 3600);
    const int minute = static_cast<int>((sod % 3600) / 60);
    const int second = static_cast<int>(sod % 60);

    std::array<char, 40> buffer{};
    std::snprintf(buffer.data(),
                  buffer.size(),
                  "%04lld-%02u-%02uT%02d:%02d:%02dZ",
                  static_cast<long long>(year),
                  month,
                  day,
                  hour,
                  minute,
                  second);
    return std::string(buffer.data());
}

std::optional<TimePoint> parse_pg_timestamp(std::string_view value) {
    if (value.empty()) {
        return std::nullopt;
    }

    // Accepts "YYYY-MM-DD HH:MM:SS" or the ISO "YYYY-MM-DDTHH:MM:SS" variant,
    // ignoring any trailing fractional seconds / timezone offset (our stored
    // timestamps are second-resolution UTC).
    int year = 0, month = 0, day = 0, hour = 0, minute = 0, second = 0;
    const std::string owned(value);
    const int matched = std::sscanf(
        owned.c_str(), "%d-%d-%d%*c%d:%d:%d", &year, &month, &day, &hour, &minute, &second);
    if (matched < 6 || month < 1 || month > 12 || day < 1 || day > 31) {
        return std::nullopt;
    }

    const std::int64_t days =
        days_from_civil(year, static_cast<unsigned>(month), static_cast<unsigned>(day));
    const std::int64_t secs =
        days * 86400 + static_cast<std::int64_t>(hour) * 3600 + minute * 60 + second;
    return Clock::from_time_t(static_cast<std::time_t>(secs));
}

}  // namespace rtc::utils

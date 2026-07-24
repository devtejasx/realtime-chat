#include "rtc/utils/time.hpp"

#include <array>
#include <cstdio>
#include <ctime>
#include <string>

namespace rtc::utils {
namespace {

// Thread-safe gmtime wrapper across platforms.
bool gmtime_utc(std::time_t t, std::tm& out) {
#if defined(_MSC_VER)
    return ::gmtime_s(&out, &t) == 0;
#else
    return ::gmtime_r(&t, &out) != nullptr;
#endif
}

// Thread-safe timegm-equivalent (converts a UTC std::tm to time_t).
std::time_t timegm_utc(std::tm& tm) {
#if defined(_MSC_VER)
    return ::_mkgmtime(&tm);
#else
    return ::timegm(&tm);
#endif
}

}  // namespace

std::string to_iso8601(TimePoint tp) {
    const std::time_t t = Clock::to_time_t(tp);
    std::tm tm{};
    if (!gmtime_utc(t, tm)) {
        return {};
    }
    std::array<char, 32> buffer{};
    const std::size_t written =
        std::strftime(buffer.data(), buffer.size(), "%Y-%m-%dT%H:%M:%SZ", &tm);
    return std::string(buffer.data(), written);
}

std::optional<TimePoint> parse_pg_timestamp(std::string_view value) {
    if (value.empty()) {
        return std::nullopt;
    }

    // Accepts "YYYY-MM-DD HH:MM:SS" or the ISO "YYYY-MM-DDTHH:MM:SS" variant,
    // ignoring any trailing fractional seconds / timezone offset for the
    // purpose of the coarse (second-resolution) timestamps we store.
    std::tm tm{};
    int year = 0, month = 0, day = 0, hour = 0, minute = 0, second = 0;
    const std::string owned(value);
    const int matched = std::sscanf(owned.c_str(), "%d-%d-%d%*c%d:%d:%d", &year, &month, &day,
                                    &hour, &minute, &second);
    if (matched < 6) {
        return std::nullopt;
    }

    tm.tm_year = year - 1900;
    tm.tm_mon = month - 1;
    tm.tm_mday = day;
    tm.tm_hour = hour;
    tm.tm_min = minute;
    tm.tm_sec = second;
    tm.tm_isdst = 0;

    const std::time_t t = timegm_utc(tm);
    if (t == static_cast<std::time_t>(-1)) {
        return std::nullopt;
    }
    return Clock::from_time_t(t);
}

}  // namespace rtc::utils

#include "rtc/ratelimit/rate_limiter.hpp"

#include <algorithm>
#include <string>

#include "rtc/errors/exceptions.hpp"

namespace rtc::ratelimit {

RateLimiter::Result RateLimiter::check(std::string_view bucket, std::string_view identity,
                                       std::int64_t max, std::chrono::seconds window) {
    Result result;
    result.limit = max;
    if (!enabled_ || max <= 0) {
        result.allowed = true;
        result.remaining = max;
        return result;
    }

    std::string key;
    key.reserve(10 + bucket.size() + identity.size());
    key.append("ratelimit:").append(bucket).append(":").append(identity);

    // Atomic increment; the window TTL is applied only when the key is created,
    // giving a fixed window that resets `window` seconds after the first hit.
    const std::int64_t count = store_.incr(key, window);
    result.allowed = count <= max;
    result.remaining = std::max<std::int64_t>(0, max - count);
    result.retry_after_seconds = result.allowed ? 0 : window.count();
    return result;
}

RateLimiter::Result RateLimiter::enforce(std::string_view bucket, std::string_view identity,
                                         std::int64_t max, std::chrono::seconds window) {
    const Result result = check(bucket, identity, max, window);
    if (!result.allowed) {
        throw rtc::errors::RateLimitException(
            "Rate limit exceeded, please retry later",
            "retry_after=" + std::to_string(result.retry_after_seconds));
    }
    return result;
}

}  // namespace rtc::ratelimit

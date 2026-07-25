#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <string_view>

#include "rtc/cache/cache_store.hpp"

namespace rtc::ratelimit {

// Fixed-window rate limiter backed by the shared cache. Because it uses the
// cache's atomic INCR + create-TTL primitive, the same limits apply across all
// application instances when Redis is the backend (and per-process with the
// in-memory store). Buckets are named per protected action (login, message,
// upload, ...) and scoped by an identity (user id or client IP).
class RateLimiter {
public:
    struct Result {
        bool allowed = true;
        std::int64_t limit = 0;
        std::int64_t remaining = 0;
        std::int64_t retry_after_seconds = 0;
    };

    RateLimiter(cache::ICacheStore& store, bool enabled) noexcept
        : store_(store), enabled_(enabled) {}

    // Records one hit and reports whether it is within `max` per `window`.
    [[nodiscard]] Result check(std::string_view bucket, std::string_view identity,
                               std::int64_t max, std::chrono::seconds window);

    // Same as check() but throws rtc::errors::RateLimitException (HTTP 429) when
    // the limit is exceeded. Returns the Result on success so callers can set
    // rate-limit headers.
    Result enforce(std::string_view bucket, std::string_view identity, std::int64_t max,
                   std::chrono::seconds window);

private:
    cache::ICacheStore& store_;
    bool enabled_;
};

}  // namespace rtc::ratelimit

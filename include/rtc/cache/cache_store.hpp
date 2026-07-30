#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace rtc::cache {

// Low-level key/value cache abstraction modelled on the subset of Redis
// operations this system needs: strings with TTL, atomic counters (rate
// limiting), and sets (online users, typing). Services never depend on a
// concrete backend — an in-memory store powers single-instance and test
// deployments, while a Redis-backed store enables horizontal scaling. Swapping
// implementations requires no change to business logic.
//
// All methods must be thread-safe. A TTL of zero means "no expiry".
class ICacheStore {
  public:
    using Seconds = std::chrono::seconds;

    virtual ~ICacheStore() = default;

    // --- strings ---
    virtual void set(std::string_view key, std::string_view value, Seconds ttl = Seconds{0}) = 0;
    [[nodiscard]] virtual std::optional<std::string> get(std::string_view key) = 0;
    virtual bool del(std::string_view key) = 0;
    [[nodiscard]] virtual bool exists(std::string_view key) = 0;
    virtual void expire(std::string_view key, Seconds ttl) = 0;

    // --- counters (atomic) ---
    // Increments the integer at `key` by one and returns the new value. If the
    // key is new, it is created at 1 with the given TTL (applied only on
    // creation), which is exactly the primitive a fixed-window rate limiter needs.
    [[nodiscard]] virtual std::int64_t incr(std::string_view key,
                                            Seconds ttl_on_create = Seconds{0}) = 0;

    // --- sets ---
    virtual void sadd(std::string_view key, std::string_view member) = 0;
    virtual void srem(std::string_view key, std::string_view member) = 0;
    [[nodiscard]] virtual bool sismember(std::string_view key, std::string_view member) = 0;
    [[nodiscard]] virtual std::vector<std::string> smembers(std::string_view key) = 0;
    [[nodiscard]] virtual std::size_t scard(std::string_view key) = 0;

    // Removes expired entries eagerly (called by the background cleanup job).
    // Returns the number of entries purged. Redis handles this itself, so its
    // implementation may be a no-op.
    virtual std::size_t purge_expired() = 0;

    // A short human-readable backend name for logging/metrics ("memory"/"redis").
    [[nodiscard]] virtual std::string_view backend_name() const = 0;
};

}  // namespace rtc::cache

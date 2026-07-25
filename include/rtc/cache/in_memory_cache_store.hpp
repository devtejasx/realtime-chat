#pragma once

#include <chrono>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "rtc/cache/cache_store.hpp"

namespace rtc::cache {

// Process-local, thread-safe ICacheStore. The default backend: it makes the
// whole caching/rate-limiting/session stack work with zero external
// dependencies (ideal for development, tests, and single-instance deploys).
// Expiry is lazy on access, with an explicit purge_expired() for the background
// cleanup job. For multi-instance deployments, swap in the Redis store.
class InMemoryCacheStore final : public ICacheStore {
public:
    void set(std::string_view key, std::string_view value, Seconds ttl) override;
    [[nodiscard]] std::optional<std::string> get(std::string_view key) override;
    bool del(std::string_view key) override;
    [[nodiscard]] bool exists(std::string_view key) override;
    void expire(std::string_view key, Seconds ttl) override;

    [[nodiscard]] std::int64_t incr(std::string_view key, Seconds ttl_on_create) override;

    void sadd(std::string_view key, std::string_view member) override;
    void srem(std::string_view key, std::string_view member) override;
    [[nodiscard]] bool sismember(std::string_view key, std::string_view member) override;
    [[nodiscard]] std::vector<std::string> smembers(std::string_view key) override;
    [[nodiscard]] std::size_t scard(std::string_view key) override;

    std::size_t purge_expired() override;
    [[nodiscard]] std::string_view backend_name() const override { return "memory"; }

private:
    using Clock = std::chrono::steady_clock;

    struct StringEntry {
        std::string value;
        std::optional<Clock::time_point> expires_at;
    };
    struct SetEntry {
        std::unordered_set<std::string> members;
        std::optional<Clock::time_point> expires_at;
    };

    // Returns true if an optional expiry has elapsed.
    static bool is_expired(const std::optional<Clock::time_point>& expiry);
    static std::optional<Clock::time_point> deadline(Seconds ttl);

    mutable std::mutex mutex_;
    std::unordered_map<std::string, StringEntry> strings_;
    std::unordered_map<std::string, SetEntry> sets_;
};

}  // namespace rtc::cache

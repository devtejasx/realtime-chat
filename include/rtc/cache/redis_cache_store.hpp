#pragma once

#include <memory>
#include <string>

#include "rtc/cache/cache_store.hpp"

namespace rtc::cache {

// Redis-backed ICacheStore for multi-instance / horizontally-scaled
// deployments. Uses the PIMPL idiom so this header stays free of the
// redis-plus-plus dependency — the concrete client lives entirely in the .cpp,
// compiled only when the build is configured with -DRTC_WITH_REDIS=ON. When the
// project is built without Redis support, constructing this type throws, and
// the composition root falls back to the in-memory store.
class RedisCacheStore final : public ICacheStore {
public:
    explicit RedisCacheStore(const std::string& uri);
    ~RedisCacheStore() override;

    RedisCacheStore(const RedisCacheStore&) = delete;
    RedisCacheStore& operator=(const RedisCacheStore&) = delete;

    // Defaults restated for the same reason as in InMemoryCacheStore: a default
    // argument on a virtual is bound at the static call type, not inherited.
    void set(std::string_view key, std::string_view value, Seconds ttl = Seconds{0}) override;
    [[nodiscard]] std::optional<std::string> get(std::string_view key) override;
    bool del(std::string_view key) override;
    [[nodiscard]] bool exists(std::string_view key) override;
    void expire(std::string_view key, Seconds ttl) override;

    [[nodiscard]] std::int64_t incr(std::string_view key,
                                    Seconds ttl_on_create = Seconds{0}) override;

    void sadd(std::string_view key, std::string_view member) override;
    void srem(std::string_view key, std::string_view member) override;
    [[nodiscard]] bool sismember(std::string_view key, std::string_view member) override;
    [[nodiscard]] std::vector<std::string> smembers(std::string_view key) override;
    [[nodiscard]] std::size_t scard(std::string_view key) override;

    std::size_t purge_expired() override { return 0; }  // Redis expires keys itself
    [[nodiscard]] std::string_view backend_name() const override { return "redis"; }

    // True when the binary was compiled with Redis support.
    [[nodiscard]] static bool available() noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace rtc::cache

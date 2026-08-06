#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <string_view>

#include "rtc/cache/cache_store.hpp"
#include "rtc/cache/invalidation.hpp"
#include "rtc/reliability/circuit_breaker.hpp"

namespace rtc::cache {

// High-level, typed caching facade over an ICacheStore.
//
// Services depend on this (not the raw store): it namespaces keys, serialises
// JSON, records hit/miss counters for the metrics endpoint, and offers a
// read-through `remember` helper. Invalidation is explicit and lives next to
// the writes that require it, keeping cache coherence a service-layer concern.
class CacheService {
  public:
    explicit CacheService(ICacheStore& store) noexcept : store_(store) {}

    // Reads a JSON value; records a hit or miss. Returns nullopt on miss or on
    // a corrupt cached payload (treated as a miss and evicted).
    [[nodiscard]] std::optional<nlohmann::json> get(std::string_view ns, std::string_view key);

    // Stores a JSON value with a TTL.
    void put(std::string_view ns,
             std::string_view key,
             const nlohmann::json& value,
             std::chrono::seconds ttl);

    // Evicts a single entry on every instance.
    //
    // Local eviction alone is correct only while there is one process holding a
    // cache. With replicas, whoever performed the write drops its copy and the
    // others keep serving the stale value until it expires.
    void invalidate(std::string_view ns, std::string_view key);

    // Evicts locally without announcing. What the cluster subscriber calls, so
    // that receiving a notice cannot re-broadcast it.
    void invalidate_local(std::string_view ns, std::string_view key);

    // Optional cross-instance publisher, injected by the composition root.
    void set_invalidation_publisher(IInvalidationPublisher& publisher) noexcept {
        invalidations_ = &publisher;
    }

    // Optional circuit breaker over the cache backend.
    //
    // A cache is an optimisation, so its failure must degrade to a miss rather
    // than to an error: every read falls back to the source of truth and every
    // write becomes a no-op. Without the breaker, a Redis outage does not slow
    // the service down — it turns cache lookups into exceptions and converts a
    // performance problem into a availability one.
    void set_circuit_breaker(reliability::CircuitBreaker& breaker) noexcept { breaker_ = &breaker; }

    // Read-through cache: returns the cached value, or computes it via `loader`,
    // stores it under `ttl`, and returns it. `loader` returns a JSON value.
    template <typename Loader>
    [[nodiscard]] nlohmann::json remember(std::string_view ns,
                                          std::string_view key,
                                          std::chrono::seconds ttl,
                                          Loader&& loader) {
        if (auto cached = get(ns, key)) {
            return *cached;
        }
        nlohmann::json value = std::forward<Loader>(loader)();
        put(ns, key, value, ttl);
        return value;
    }

    [[nodiscard]] std::uint64_t hits() const noexcept { return hits_.load(); }
    [[nodiscard]] std::uint64_t misses() const noexcept { return misses_.load(); }

    // hits / (hits + misses); 0 when there has been no traffic.
    [[nodiscard]] double hit_ratio() const noexcept;

    [[nodiscard]] ICacheStore& store() noexcept { return store_; }

    // Builds a fully-qualified key "rtc:<ns>:<key>".
    [[nodiscard]] static std::string make_key(std::string_view ns, std::string_view key);

  private:
    ICacheStore& store_;
    IInvalidationPublisher* invalidations_{&NullInvalidationPublisher::instance()};
    reliability::CircuitBreaker* breaker_ = nullptr;
    std::atomic<std::uint64_t> hits_{0};
    std::atomic<std::uint64_t> misses_{0};
};

}  // namespace rtc::cache

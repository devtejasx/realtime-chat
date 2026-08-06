#include "rtc/cache/cache_service.hpp"

#include <string>

#include "rtc/logging/logger.hpp"

namespace rtc::cache {

std::string CacheService::make_key(std::string_view ns, std::string_view key) {
    std::string full;
    full.reserve(4 + ns.size() + 1 + key.size());
    full.append("rtc:");
    full.append(ns);
    full.push_back(':');
    full.append(key);
    return full;
}

std::optional<nlohmann::json> CacheService::get(std::string_view ns, std::string_view key) {
    // A cache miss is always a legal answer: every caller either falls back to
    // the source of truth or recomputes through remember(). That is what makes
    // it safe to degrade a backend failure into a miss rather than propagate it
    // — and propagating it would turn a Redis outage into a wave of 500s from
    // endpoints that could have served the request perfectly well from
    // PostgreSQL.
    if (breaker_ != nullptr && !breaker_->allow()) {
        misses_.fetch_add(1, std::memory_order_relaxed);
        return std::nullopt;
    }

    const std::string full = make_key(ns, key);
    std::optional<std::string> raw;
    try {
        raw = store_.get(full);
        if (breaker_ != nullptr) {
            breaker_->on_success();
        }
    } catch (const std::exception& ex) {
        if (breaker_ != nullptr) {
            breaker_->on_failure();
        }
        RTC_LOG_WARN("Cache read failed for '{}': {}", full, ex.what());
        misses_.fetch_add(1, std::memory_order_relaxed);
        return std::nullopt;
    }

    if (!raw) {
        misses_.fetch_add(1, std::memory_order_relaxed);
        return std::nullopt;
    }
    auto parsed = nlohmann::json::parse(*raw, nullptr, /*allow_exceptions=*/false);
    if (parsed.is_discarded()) {
        // Corrupt payload: evict and treat as a miss. Eviction is best-effort —
        // failing to delete a bad entry must not fail the read that found it.
        try {
            store_.del(full);
        } catch (const std::exception&) {
        }
        misses_.fetch_add(1, std::memory_order_relaxed);
        return std::nullopt;
    }
    hits_.fetch_add(1, std::memory_order_relaxed);
    return parsed;
}

void CacheService::put(std::string_view ns,
                       std::string_view key,
                       const nlohmann::json& value,
                       std::chrono::seconds ttl) {
    if (breaker_ != nullptr && !breaker_->allow()) {
        return;  // not cached; the next read recomputes
    }
    try {
        store_.set(make_key(ns, key), value.dump(), ttl);
        if (breaker_ != nullptr) {
            breaker_->on_success();
        }
    } catch (const std::exception& ex) {
        if (breaker_ != nullptr) {
            breaker_->on_failure();
        }
        // Swallowed on purpose: the value simply is not cached. Failing the
        // write that produced it because the *cache* is unavailable would be
        // the tail wagging the dog.
        RTC_LOG_WARN("Cache write failed for '{}:{}': {}", ns, key, ex.what());
    }
}

void CacheService::invalidate_local(std::string_view ns, std::string_view key) {
    // Deliberately attempted even when the breaker is open. An eviction that
    // does not happen leaves stale data served until its TTL expires, which is
    // the one cache failure with a correctness cost rather than a performance
    // one — so it is worth the call even against a backend that is probably
    // down.
    try {
        store_.del(make_key(ns, key));
    } catch (const std::exception& ex) {
        // Warn rather than throw: the caller has already performed the mutation
        // this invalidation follows, and there is nothing useful it could do.
        // Staleness is bounded by the entry's TTL.
        RTC_LOG_WARN(
            "Cache invalidation failed for '{}:{}': {} (stale until TTL)", ns, key, ex.what());
    }
}

void CacheService::invalidate(std::string_view ns, std::string_view key) {
    invalidate_local(ns, key);
    invalidations_->publish_invalidation(
        InvalidationEvent{.scope = std::string(invalidation_scopes::kNamespacedKey),
                          .key = std::string(ns),
                          .sub_key = std::string(key)});
}

double CacheService::hit_ratio() const noexcept {
    const std::uint64_t h = hits_.load();
    const std::uint64_t m = misses_.load();
    const std::uint64_t total = h + m;
    return total == 0 ? 0.0 : static_cast<double>(h) / static_cast<double>(total);
}

}  // namespace rtc::cache

#include "rtc/cache/cache_service.hpp"

#include <string>

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
    const std::string full = make_key(ns, key);
    auto raw = store_.get(full);
    if (!raw) {
        misses_.fetch_add(1, std::memory_order_relaxed);
        return std::nullopt;
    }
    auto parsed = nlohmann::json::parse(*raw, nullptr, /*allow_exceptions=*/false);
    if (parsed.is_discarded()) {
        // Corrupt payload: evict and treat as a miss.
        store_.del(full);
        misses_.fetch_add(1, std::memory_order_relaxed);
        return std::nullopt;
    }
    hits_.fetch_add(1, std::memory_order_relaxed);
    return parsed;
}

void CacheService::put(std::string_view ns, std::string_view key, const nlohmann::json& value,
                       std::chrono::seconds ttl) {
    store_.set(make_key(ns, key), value.dump(), ttl);
}

void CacheService::invalidate(std::string_view ns, std::string_view key) {
    store_.del(make_key(ns, key));
}

double CacheService::hit_ratio() const noexcept {
    const std::uint64_t h = hits_.load();
    const std::uint64_t m = misses_.load();
    const std::uint64_t total = h + m;
    return total == 0 ? 0.0 : static_cast<double>(h) / static_cast<double>(total);
}

}  // namespace rtc::cache

#include "rtc/cache/in_memory_cache_store.hpp"

#include <charconv>
#include <utility>

namespace rtc::cache {

bool InMemoryCacheStore::is_expired(const std::optional<Clock::time_point>& expiry) {
    return expiry.has_value() && Clock::now() >= *expiry;
}

std::optional<InMemoryCacheStore::Clock::time_point> InMemoryCacheStore::deadline(Seconds ttl) {
    if (ttl.count() <= 0) {
        return std::nullopt;
    }
    return Clock::now() + ttl;
}

void InMemoryCacheStore::set(std::string_view key, std::string_view value, Seconds ttl) {
    std::lock_guard<std::mutex> lock(mutex_);
    strings_[std::string(key)] = StringEntry{std::string(value), deadline(ttl)};
}

std::optional<std::string> InMemoryCacheStore::get(std::string_view key) {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = strings_.find(std::string(key));
    if (it == strings_.end()) {
        return std::nullopt;
    }
    if (is_expired(it->second.expires_at)) {
        strings_.erase(it);
        return std::nullopt;
    }
    return it->second.value;
}

bool InMemoryCacheStore::del(std::string_view key) {
    std::lock_guard<std::mutex> lock(mutex_);
    const std::string k(key);
    return strings_.erase(k) + sets_.erase(k) > 0;
}

bool InMemoryCacheStore::exists(std::string_view key) {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = strings_.find(std::string(key));
    if (it == strings_.end()) {
        return false;
    }
    if (is_expired(it->second.expires_at)) {
        strings_.erase(it);
        return false;
    }
    return true;
}

void InMemoryCacheStore::expire(std::string_view key, Seconds ttl) {
    std::lock_guard<std::mutex> lock(mutex_);
    const std::string k(key);
    if (const auto it = strings_.find(k); it != strings_.end()) {
        it->second.expires_at = deadline(ttl);
    }
    if (const auto it = sets_.find(k); it != sets_.end()) {
        it->second.expires_at = deadline(ttl);
    }
}

std::int64_t InMemoryCacheStore::incr(std::string_view key, Seconds ttl_on_create) {
    std::lock_guard<std::mutex> lock(mutex_);
    const std::string k(key);
    auto it = strings_.find(k);
    if (it != strings_.end() && is_expired(it->second.expires_at)) {
        strings_.erase(it);
        it = strings_.end();
    }
    if (it == strings_.end()) {
        // New window: create at 1 with the caller's TTL.
        strings_[k] = StringEntry{"1", deadline(ttl_on_create)};
        return 1;
    }
    std::int64_t current = 0;
    std::from_chars(
        it->second.value.data(), it->second.value.data() + it->second.value.size(), current);
    current += 1;
    it->second.value = std::to_string(current);
    return current;
}

void InMemoryCacheStore::sadd(std::string_view key, std::string_view member) {
    std::lock_guard<std::mutex> lock(mutex_);
    sets_[std::string(key)].members.insert(std::string(member));
}

void InMemoryCacheStore::srem(std::string_view key, std::string_view member) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (const auto it = sets_.find(std::string(key)); it != sets_.end()) {
        it->second.members.erase(std::string(member));
    }
}

bool InMemoryCacheStore::sismember(std::string_view key, std::string_view member) {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = sets_.find(std::string(key));
    if (it == sets_.end() || is_expired(it->second.expires_at)) {
        return false;
    }
    return it->second.members.count(std::string(member)) > 0;
}

std::vector<std::string> InMemoryCacheStore::smembers(std::string_view key) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::string> out;
    const auto it = sets_.find(std::string(key));
    if (it != sets_.end() && !is_expired(it->second.expires_at)) {
        out.reserve(it->second.members.size());
        for (const auto& m : it->second.members) {
            out.push_back(m);
        }
    }
    return out;
}

std::size_t InMemoryCacheStore::scard(std::string_view key) {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = sets_.find(std::string(key));
    if (it == sets_.end() || is_expired(it->second.expires_at)) {
        return 0;
    }
    return it->second.members.size();
}

std::size_t InMemoryCacheStore::purge_expired() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::size_t purged = 0;
    for (auto it = strings_.begin(); it != strings_.end();) {
        if (is_expired(it->second.expires_at)) {
            it = strings_.erase(it);
            ++purged;
        } else {
            ++it;
        }
    }
    for (auto it = sets_.begin(); it != sets_.end();) {
        if (is_expired(it->second.expires_at)) {
            it = sets_.erase(it);
            ++purged;
        } else {
            ++it;
        }
    }
    return purged;
}

}  // namespace rtc::cache

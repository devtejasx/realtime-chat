#include "rtc/cache/presence_cache.hpp"

#include <charconv>
#include <string>

namespace rtc::cache {
namespace {

constexpr std::string_view kOnlineSet = "presence:online";

[[nodiscard]] std::string conn_key(std::int64_t user_id) {
    return "presence:conn:" + std::to_string(user_id);
}

[[nodiscard]] std::int64_t to_int(const std::string& s) {
    std::int64_t v = 0;
    std::from_chars(s.data(), s.data() + s.size(), v);
    return v;
}

}  // namespace

bool PresenceCache::add_connection(std::int64_t user_id) {
    // Atomic increment of the per-user connection counter; first connection
    // adds the user to the shared online set.
    const std::int64_t count = store_.incr(conn_key(user_id));
    const std::string id = std::to_string(user_id);
    store_.sadd(kOnlineSet, id);
    return count == 1;
}

bool PresenceCache::remove_connection(std::int64_t user_id) {
    const std::string key = conn_key(user_id);
    const auto current = store_.get(key);
    const std::int64_t remaining = current ? to_int(*current) - 1 : 0;
    const std::string id = std::to_string(user_id);
    if (remaining <= 0) {
        store_.del(key);
        store_.srem(kOnlineSet, id);
        return true;  // went offline
    }
    store_.set(key, std::to_string(remaining));
    return false;
}

bool PresenceCache::is_online(std::int64_t user_id) {
    return store_.sismember(kOnlineSet, std::to_string(user_id));
}

std::vector<std::int64_t> PresenceCache::online_user_ids() {
    std::vector<std::int64_t> ids;
    for (const auto& member : store_.smembers(kOnlineSet)) {
        ids.push_back(to_int(member));
    }
    return ids;
}

std::size_t PresenceCache::online_count() {
    return store_.scard(kOnlineSet);
}

}  // namespace rtc::cache

#include "rtc/cache/redis_cache_store.hpp"

#include "rtc/errors/exceptions.hpp"

#ifdef RTC_WITH_REDIS
#include <sw/redis++/redis++.h>
#endif

namespace rtc::cache {

#ifdef RTC_WITH_REDIS

// Real client — thin wrapper over redis-plus-plus. redis-plus-plus is itself
// thread-safe (it manages a connection pool), so no additional locking is
// needed here.
struct RedisCacheStore::Impl {
    explicit Impl(const std::string& uri) : redis(uri) {}
    sw::redis::Redis redis;
};

RedisCacheStore::RedisCacheStore(const std::string& uri) : impl_(std::make_unique<Impl>(uri)) {}

RedisCacheStore::~RedisCacheStore() = default;

bool RedisCacheStore::available() noexcept {
    return true;
}

void RedisCacheStore::set(std::string_view key, std::string_view value, Seconds ttl) {
    if (ttl.count() > 0) {
        impl_->redis.set(std::string(key), std::string(value), std::chrono::seconds(ttl));
    } else {
        impl_->redis.set(std::string(key), std::string(value));
    }
}

std::optional<std::string> RedisCacheStore::get(std::string_view key) {
    auto value = impl_->redis.get(std::string(key));
    if (value) {
        return *value;
    }
    return std::nullopt;
}

bool RedisCacheStore::del(std::string_view key) {
    return impl_->redis.del(std::string(key)) > 0;
}

bool RedisCacheStore::exists(std::string_view key) {
    return impl_->redis.exists(std::string(key)) > 0;
}

void RedisCacheStore::expire(std::string_view key, Seconds ttl) {
    impl_->redis.expire(std::string(key), std::chrono::seconds(ttl));
}

std::int64_t RedisCacheStore::incr(std::string_view key, Seconds ttl_on_create) {
    const auto value = impl_->redis.incr(std::string(key));
    if (value == 1 && ttl_on_create.count() > 0) {
        impl_->redis.expire(std::string(key), std::chrono::seconds(ttl_on_create));
    }
    return value;
}

void RedisCacheStore::sadd(std::string_view key, std::string_view member) {
    impl_->redis.sadd(std::string(key), std::string(member));
}

void RedisCacheStore::srem(std::string_view key, std::string_view member) {
    impl_->redis.srem(std::string(key), std::string(member));
}

bool RedisCacheStore::sismember(std::string_view key, std::string_view member) {
    return impl_->redis.sismember(std::string(key), std::string(member));
}

std::vector<std::string> RedisCacheStore::smembers(std::string_view key) {
    std::vector<std::string> out;
    impl_->redis.smembers(std::string(key), std::back_inserter(out));
    return out;
}

std::size_t RedisCacheStore::scard(std::string_view key) {
    return impl_->redis.scard(std::string(key));
}

#else  // RTC_WITH_REDIS not defined — compiled but non-functional.

struct RedisCacheStore::Impl {};

namespace {
[[noreturn]] void unavailable() {
    throw rtc::errors::ConfigException(
        "Redis support was not compiled in; rebuild with -DRTC_WITH_REDIS=ON");
}
}  // namespace

RedisCacheStore::RedisCacheStore(const std::string&) {
    unavailable();
}
RedisCacheStore::~RedisCacheStore() = default;
bool RedisCacheStore::available() noexcept {
    return false;
}

void RedisCacheStore::set(std::string_view, std::string_view, Seconds) {
    unavailable();
}
std::optional<std::string> RedisCacheStore::get(std::string_view) {
    unavailable();
}
bool RedisCacheStore::del(std::string_view) {
    unavailable();
}
bool RedisCacheStore::exists(std::string_view) {
    unavailable();
}
void RedisCacheStore::expire(std::string_view, Seconds) {
    unavailable();
}
std::int64_t RedisCacheStore::incr(std::string_view, Seconds) {
    unavailable();
}
void RedisCacheStore::sadd(std::string_view, std::string_view) {
    unavailable();
}
void RedisCacheStore::srem(std::string_view, std::string_view) {
    unavailable();
}
bool RedisCacheStore::sismember(std::string_view, std::string_view) {
    unavailable();
}
std::vector<std::string> RedisCacheStore::smembers(std::string_view) {
    unavailable();
}
std::size_t RedisCacheStore::scard(std::string_view) {
    unavailable();
}

#endif  // RTC_WITH_REDIS

}  // namespace rtc::cache

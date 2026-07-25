#include "rtc/cache/cache_service.hpp"

#include <chrono>

#include <gtest/gtest.h>

#include "rtc/cache/in_memory_cache_store.hpp"

namespace {

using rtc::cache::CacheService;
using rtc::cache::InMemoryCacheStore;
using namespace std::chrono_literals;

TEST(CacheServiceTest, PutAndGetRoundTrip) {
    InMemoryCacheStore store;
    CacheService cache(store);
    cache.put("user", "1", nlohmann::json{{"id", 1}, {"name", "alice"}}, 60s);

    const auto value = cache.get("user", "1");
    ASSERT_TRUE(value.has_value());
    EXPECT_EQ((*value)["name"], "alice");
}

TEST(CacheServiceTest, TracksHitsAndMisses) {
    InMemoryCacheStore store;
    CacheService cache(store);
    cache.put("ns", "k", nlohmann::json{{"x", 1}}, 60s);

    (void) cache.get("ns", "k");       // hit
    (void) cache.get("ns", "absent");  // miss
    EXPECT_EQ(cache.hits(), 1U);
    EXPECT_EQ(cache.misses(), 1U);
    EXPECT_DOUBLE_EQ(cache.hit_ratio(), 0.5);
}

TEST(CacheServiceTest, InvalidateEvicts) {
    InMemoryCacheStore store;
    CacheService cache(store);
    cache.put("ns", "k", nlohmann::json{{"x", 1}}, 60s);
    cache.invalidate("ns", "k");
    EXPECT_FALSE(cache.get("ns", "k").has_value());
}

TEST(CacheServiceTest, RememberComputesOnceThenCaches) {
    InMemoryCacheStore store;
    CacheService cache(store);
    int calls = 0;
    const auto loader = [&] {
        ++calls;
        return nlohmann::json{{"v", 42}};
    };
    const auto first = cache.remember("ns", "k", 60s, loader);
    const auto second = cache.remember("ns", "k", 60s, loader);
    EXPECT_EQ(first["v"], 42);
    EXPECT_EQ(second["v"], 42);
    EXPECT_EQ(calls, 1);  // second call served from cache
}

TEST(CacheServiceTest, KeysAreNamespaced) {
    EXPECT_EQ(CacheService::make_key("user", "1"), "rtc:user:1");
}

}  // namespace

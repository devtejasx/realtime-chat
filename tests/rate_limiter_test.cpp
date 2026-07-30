#include "rtc/ratelimit/rate_limiter.hpp"

#include <gtest/gtest.h>

#include <chrono>

#include "rtc/cache/in_memory_cache_store.hpp"
#include "rtc/errors/exceptions.hpp"

namespace {

using rtc::cache::InMemoryCacheStore;
using rtc::errors::RateLimitException;
using rtc::ratelimit::RateLimiter;
using namespace std::chrono_literals;

TEST(RateLimiterTest, AllowsUpToLimitThenBlocks) {
    InMemoryCacheStore store;
    RateLimiter limiter(store, /*enabled=*/true);

    for (int i = 0; i < 3; ++i) {
        EXPECT_TRUE(limiter.check("login", "user:1", 3, 60s).allowed) << "iteration " << i;
    }
    EXPECT_FALSE(limiter.check("login", "user:1", 3, 60s).allowed);
}

TEST(RateLimiterTest, SeparateIdentitiesAreIndependent) {
    InMemoryCacheStore store;
    RateLimiter limiter(store, true);
    EXPECT_TRUE(limiter.check("login", "user:1", 1, 60s).allowed);
    EXPECT_FALSE(limiter.check("login", "user:1", 1, 60s).allowed);
    EXPECT_TRUE(limiter.check("login", "user:2", 1, 60s).allowed);  // different identity
}

TEST(RateLimiterTest, DisabledAlwaysAllows) {
    InMemoryCacheStore store;
    RateLimiter limiter(store, /*enabled=*/false);
    for (int i = 0; i < 100; ++i) {
        EXPECT_TRUE(limiter.check("x", "id", 1, 60s).allowed);
    }
}

TEST(RateLimiterTest, EnforceThrowsWhenExceeded) {
    InMemoryCacheStore store;
    RateLimiter limiter(store, true);
    EXPECT_NO_THROW(limiter.enforce("upload", "user:1", 1, 60s));
    EXPECT_THROW(limiter.enforce("upload", "user:1", 1, 60s), RateLimitException);
}

TEST(RateLimiterTest, ReportsRemaining) {
    InMemoryCacheStore store;
    RateLimiter limiter(store, true);
    const auto first = limiter.check("b", "id", 5, 60s);
    EXPECT_EQ(first.remaining, 4);
    EXPECT_EQ(first.limit, 5);
}

}  // namespace

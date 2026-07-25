#include "rtc/cache/in_memory_cache_store.hpp"

#include <chrono>
#include <thread>

#include <gtest/gtest.h>

namespace {

using rtc::cache::InMemoryCacheStore;
using namespace std::chrono_literals;

TEST(InMemoryCacheStoreTest, SetAndGet) {
    InMemoryCacheStore store;
    store.set("k", "v", 0s);
    ASSERT_TRUE(store.get("k").has_value());
    EXPECT_EQ(*store.get("k"), "v");
    EXPECT_TRUE(store.exists("k"));
}

TEST(InMemoryCacheStoreTest, MissReturnsNullopt) {
    InMemoryCacheStore store;
    EXPECT_FALSE(store.get("absent").has_value());
    EXPECT_FALSE(store.exists("absent"));
}

TEST(InMemoryCacheStoreTest, TtlExpires) {
    InMemoryCacheStore store;
    store.set("k", "v", 1s);
    EXPECT_TRUE(store.get("k").has_value());
    std::this_thread::sleep_for(1100ms);
    EXPECT_FALSE(store.get("k").has_value());
}

TEST(InMemoryCacheStoreTest, IncrCreatesAndIncrements) {
    InMemoryCacheStore store;
    EXPECT_EQ(store.incr("c"), 1);
    EXPECT_EQ(store.incr("c"), 2);
    EXPECT_EQ(store.incr("c"), 3);
}

TEST(InMemoryCacheStoreTest, SetOperations) {
    InMemoryCacheStore store;
    store.sadd("s", "a");
    store.sadd("s", "b");
    store.sadd("s", "a");  // duplicate ignored
    EXPECT_EQ(store.scard("s"), 2U);
    EXPECT_TRUE(store.sismember("s", "a"));
    store.srem("s", "a");
    EXPECT_FALSE(store.sismember("s", "a"));
    EXPECT_EQ(store.scard("s"), 1U);
}

TEST(InMemoryCacheStoreTest, DeleteRemovesKey) {
    InMemoryCacheStore store;
    store.set("k", "v", 0s);
    EXPECT_TRUE(store.del("k"));
    EXPECT_FALSE(store.exists("k"));
    EXPECT_FALSE(store.del("k"));
}

TEST(InMemoryCacheStoreTest, PurgeExpiredRemovesStaleEntries) {
    InMemoryCacheStore store;
    store.set("a", "1", 1s);
    store.set("b", "2", 0s);  // no expiry
    std::this_thread::sleep_for(1100ms);
    EXPECT_GE(store.purge_expired(), 1U);
    EXPECT_TRUE(store.exists("b"));
}

}  // namespace

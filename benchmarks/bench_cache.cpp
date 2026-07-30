// Cache store and rate limiter.
//
// These measure the in-memory backend only. That is deliberate: the in-memory
// store is what a single-instance deployment actually runs, and its numbers are
// reproducible on any machine. Redis figures would be dominated by network round
// trips (typically 100-500 us against a local instance, versus the tens of
// nanoseconds below), so the useful comparison is not "which is faster" but "how
// many round trips does a request make" — which is why the Redis path is measured
// with the k6 suite against a real deployment instead.
#include <benchmark/benchmark.h>

#include <string>
#include <vector>

#include "rtc/cache/in_memory_cache_store.hpp"
#include "rtc/ratelimit/rate_limiter.hpp"

namespace {

void BM_CacheSet(benchmark::State& state) {
    rtc::cache::InMemoryCacheStore store;
    const std::string value(128, 'x');
    std::int64_t counter = 0;

    for (auto _ : state) {
        // A distinct key per iteration, so this measures insertion rather than
        // repeatedly overwriting one entry (which would be unrealistically cheap
        // and would not grow the map).
        store.set("bench:key:" + std::to_string(counter++), value, std::chrono::seconds(300));
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_CacheSet);

void BM_CacheGetHit(benchmark::State& state) {
    rtc::cache::InMemoryCacheStore store;
    store.set("bench:hit", std::string(128, 'x'), std::chrono::seconds(300));

    for (auto _ : state) {
        benchmark::DoNotOptimize(store.get("bench:hit"));
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_CacheGetHit);

// A miss should be cheaper than a hit (no string copy). Measured to confirm that,
// because the authorisation cache is checked on every request and a miss there
// falls through to a database query.
void BM_CacheGetMiss(benchmark::State& state) {
    rtc::cache::InMemoryCacheStore store;
    for (auto _ : state) {
        benchmark::DoNotOptimize(store.get("bench:absent"));
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_CacheGetMiss);

// Lookup cost as the store grows, to confirm the hash map really is O(1) here and
// that a large working set does not degrade the hot path.
void BM_CacheGetWithPopulatedStore(benchmark::State& state) {
    rtc::cache::InMemoryCacheStore store;
    const auto entries = static_cast<std::int64_t>(state.range(0));
    for (std::int64_t i = 0; i < entries; ++i) {
        store.set("bench:key:" + std::to_string(i), "value", std::chrono::seconds(300));
    }
    const std::string target = "bench:key:" + std::to_string(entries / 2);

    for (auto _ : state) {
        benchmark::DoNotOptimize(store.get(target));
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_CacheGetWithPopulatedStore)->Arg(100)->Arg(10'000)->Arg(100'000);

// The atomic counter primitive behind rate limiting.
void BM_CacheIncr(benchmark::State& state) {
    rtc::cache::InMemoryCacheStore store;
    for (auto _ : state) {
        benchmark::DoNotOptimize(store.incr("bench:counter", std::chrono::seconds(60)));
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_CacheIncr);

// Contended increments across threads — the realistic shape for a rate limiter
// under load, where every request thread hits the same window key.
void BM_CacheIncrContended(benchmark::State& state) {
    static rtc::cache::InMemoryCacheStore shared_store;
    for (auto _ : state) {
        benchmark::DoNotOptimize(shared_store.incr("bench:shared", std::chrono::seconds(60)));
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_CacheIncrContended)->Threads(1)->Threads(2)->Threads(4)->Threads(8);

void BM_CacheSetOperations(benchmark::State& state) {
    rtc::cache::InMemoryCacheStore store;
    for (std::int64_t i = 0; i < 1'000; ++i) {
        store.sadd("bench:online", std::to_string(i));
    }
    for (auto _ : state) {
        benchmark::DoNotOptimize(store.sismember("bench:online", "500"));
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_CacheSetOperations);

// The full rate-limiter check, as a request pays it. Limit set high so the
// allow path is measured rather than the throwing rejection path.
void BM_RateLimiterAllow(benchmark::State& state) {
    rtc::cache::InMemoryCacheStore store;
    rtc::ratelimit::RateLimiter limiter(store, /*enabled=*/true);

    for (auto _ : state) {
        try {
            limiter.enforce("bench", "user-1", /*max=*/1'000'000'000, std::chrono::seconds(60));
        } catch (const std::exception&) {
        }
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_RateLimiterAllow);

// Disabled limiter: should be near-free, confirming RATE_LIMIT_ENABLED=false costs
// nothing beyond a branch.
void BM_RateLimiterDisabled(benchmark::State& state) {
    rtc::cache::InMemoryCacheStore store;
    rtc::ratelimit::RateLimiter limiter(store, /*enabled=*/false);

    for (auto _ : state) {
        limiter.enforce("bench", "user-1", 10, std::chrono::seconds(60));
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_RateLimiterDisabled);

}  // namespace

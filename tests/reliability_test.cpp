#include <gtest/gtest.h>

#include <chrono>
#include <nlohmann/json.hpp>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "rtc/cache/cache_service.hpp"
#include "rtc/cache/in_memory_cache_store.hpp"
#include "rtc/reliability/circuit_breaker.hpp"
#include "rtc/reliability/retry_policy.hpp"

// Retry and circuit breaking.
//
// These two solve opposite halves of one problem and are easy to confuse. Retry
// absorbs a *blip* — a connection reset, a leader election — by trying again.
// A breaker absorbs an *outage* by refusing to try at all. Applying only retry
// to an outage makes it worse: every request holds a worker thread for the full
// backoff schedule, the pool fills with threads waiting on something that is not
// going to answer, and endpoints that never touch the failed dependency start
// timing out too.
//
// Both the sleeper and the clock are injected, so these tests assert the state
// machine rather than waiting for wall-clock time. A test that sleeps through a
// 30-second cooldown is a test nobody runs.

namespace {

using namespace std::chrono_literals;
using rtc::reliability::CircuitBreaker;
using rtc::reliability::CircuitOpenError;
using rtc::reliability::RetryPolicy;
using rtc::reliability::RetryStats;
using rtc::reliability::run_with_breaker;
using rtc::reliability::run_with_retry;

// Retries anything. Callers in production must be more selective; see
// NonRetryableFailuresAreNotRetried for why.
const rtc::reliability::RetryPredicate kAlways = [](const std::exception&) { return true; };
const rtc::reliability::RetryPredicate kNever = [](const std::exception&) { return false; };

// --- retry -----------------------------------------------------------------

TEST(RetryPolicyTest, SucceedsAfterTransientFailures) {
    int calls = 0;
    std::chrono::milliseconds slept{0};
    RetryStats stats;

    const int result = run_with_retry(
        RetryPolicy{.max_attempts = 3, .initial_delay = 50ms, .jitter_ratio = 0.0},
        "flaky",
        kAlways,
        [&]() -> int {
            if (++calls < 3) {
                throw std::runtime_error("transient");
            }
            return 42;
        },
        &stats,
        [&](std::chrono::milliseconds d) { slept += d; });

    EXPECT_EQ(result, 42);
    EXPECT_EQ(calls, 3);
    EXPECT_EQ(slept, 150ms) << "expected 50ms + 100ms of backoff";
    EXPECT_EQ(stats.attempts, 3);
}

TEST(RetryPolicyTest, ExhaustedRetriesRethrowRatherThanSwallow) {
    // The caller must not be able to mistake "gave up after 3 tries" for
    // success. Silently returning a default here is how a write disappears.
    int calls = 0;
    EXPECT_THROW(run_with_retry(
                     RetryPolicy{.max_attempts = 2, .jitter_ratio = 0.0},
                     "dead",
                     kAlways,
                     [&]() -> int {
                         ++calls;
                         throw std::runtime_error("down");
                     },
                     nullptr,
                     [](std::chrono::milliseconds) {}),
                 std::runtime_error);
    EXPECT_EQ(calls, 2) << "max_attempts counts the first call, not extra ones";
}

TEST(RetryPolicyTest, NonRetryableFailuresAreNotRetried) {
    // A validation error fails identically every time. Retrying it spends three
    // times the latency to return the same 400 — and for a non-idempotent write
    // that timed out after the server applied it, a retry applies it twice.
    int calls = 0;
    EXPECT_THROW(run_with_retry(
                     RetryPolicy{.max_attempts = 5},
                     "validation",
                     kNever,
                     [&]() -> int {
                         ++calls;
                         throw std::runtime_error("bad input");
                     },
                     nullptr,
                     [](std::chrono::milliseconds) {}),
                 std::runtime_error);
    EXPECT_EQ(calls, 1);
}

TEST(RetryPolicyTest, SucceedsWithoutSleepingWhenTheFirstAttemptWorks) {
    bool slept = false;
    const int result = run_with_retry(
        RetryPolicy{},
        "fine",
        kAlways,
        []() -> int { return 7; },
        nullptr,
        [&](std::chrono::milliseconds) { slept = true; });
    EXPECT_EQ(result, 7);
    EXPECT_FALSE(slept) << "the happy path must not pay for the retry machinery";
}

TEST(RetryPolicyTest, BackoffGrowsAndIsCapped) {
    const RetryPolicy policy{
        .initial_delay = 100ms, .multiplier = 10.0, .max_delay = 500ms, .jitter_ratio = 0.0};
    EXPECT_EQ(policy.delay_for(1), 0ms) << "the first attempt is immediate";
    EXPECT_EQ(policy.delay_for(2), 100ms);
    EXPECT_EQ(policy.delay_for(3), 500ms) << "1000ms clamped to max_delay";
    EXPECT_EQ(policy.delay_for(20), 500ms) << "must stay clamped, not overflow";
}

TEST(RetryPolicyTest, JitterStaysWithinItsBandAndIsNeverZero) {
    const RetryPolicy policy{.initial_delay = 100ms, .max_delay = 100ms, .jitter_ratio = 0.2};
    for (int i = 0; i < 200; ++i) {
        const auto delay = policy.delay_for(2);
        EXPECT_GE(delay, 80ms) << "below the jitter band";
        EXPECT_LE(delay, 120ms) << "above the jitter band";
        EXPECT_GT(delay.count(), 0) << "a zero wait makes the retry a tight loop";
    }
}

TEST(RetryPolicyTest, NoneMeansExactlyOneAttempt) {
    int calls = 0;
    EXPECT_THROW(run_with_retry(
                     RetryPolicy::none(),
                     "once",
                     kAlways,
                     [&]() -> int {
                         ++calls;
                         throw std::runtime_error("no");
                     },
                     nullptr,
                     [](std::chrono::milliseconds) {}),
                 std::runtime_error);
    EXPECT_EQ(calls, 1);
}

// --- circuit breaker -------------------------------------------------------

// Drives the breaker's clock by hand.
class BreakerTest : public ::testing::Test {
  protected:
    std::chrono::steady_clock::time_point now_ = std::chrono::steady_clock::now();
    CircuitBreaker::Clock clock() {
        return [this] { return now_; };
    }
    void advance(std::chrono::milliseconds by) { now_ += by; }
};

TEST_F(BreakerTest, OpensOnlyAfterConsecutiveFailuresReachTheThreshold) {
    CircuitBreaker breaker("db", {.failure_threshold = 3, .open_duration = 1000ms}, clock());
    EXPECT_EQ(breaker.state(), CircuitBreaker::State::kClosed);

    breaker.on_failure();
    breaker.on_failure();
    EXPECT_EQ(breaker.state(), CircuitBreaker::State::kClosed) << "two of three is not enough";

    breaker.on_failure();
    EXPECT_EQ(breaker.state(), CircuitBreaker::State::kOpen);
    EXPECT_EQ(breaker.opened_count(), 1U);
}

TEST_F(BreakerTest, SuccessBreaksAFailureStreak) {
    // "Consecutive" has to mean consecutive, or a dependency failing 40% of the
    // time trips the breaker eventually no matter how healthy the gaps are.
    CircuitBreaker breaker("db", {.failure_threshold = 3}, clock());
    breaker.on_failure();
    breaker.on_failure();
    breaker.on_success();
    breaker.on_failure();
    breaker.on_failure();
    EXPECT_EQ(breaker.state(), CircuitBreaker::State::kClosed);
}

TEST_F(BreakerTest, OpenRejectsWithoutTouchingTheDependency) {
    CircuitBreaker breaker("db", {.failure_threshold = 1, .open_duration = 1000ms}, clock());
    breaker.on_failure();
    ASSERT_EQ(breaker.state(), CircuitBreaker::State::kOpen);

    EXPECT_FALSE(breaker.allow());
    EXPECT_FALSE(breaker.allow());
    EXPECT_EQ(breaker.rejected_count(), 2U);
    // Still open: rejections must not extend or reset the cooldown.
    EXPECT_EQ(breaker.state(), CircuitBreaker::State::kOpen);
}

TEST_F(BreakerTest, HalfOpensOnDemandAfterTheCooldown) {
    CircuitBreaker breaker("db", {.failure_threshold = 1, .open_duration = 1000ms}, clock());
    breaker.on_failure();

    advance(999ms);
    EXPECT_FALSE(breaker.allow()) << "cooldown has not elapsed";

    advance(2ms);
    EXPECT_TRUE(breaker.allow()) << "a trial call should now be admitted";
    EXPECT_EQ(breaker.state(), CircuitBreaker::State::kHalfOpen);
}

TEST_F(BreakerTest, HalfOpenAdmitsOnlyTheConfiguredNumberOfTrials) {
    // Admitting full traffic to something that has not proven it recovered is
    // how a breaker turns a recovery into a second outage.
    CircuitBreaker breaker(
        "db", {.failure_threshold = 1, .open_duration = 10ms, .half_open_max_calls = 1}, clock());
    breaker.on_failure();
    advance(11ms);

    EXPECT_TRUE(breaker.allow());
    EXPECT_FALSE(breaker.allow()) << "second concurrent trial must be rejected";
    EXPECT_FALSE(breaker.allow());
}

TEST_F(BreakerTest, SuccessfulProbeClosesTheBreaker) {
    CircuitBreaker breaker("db", {.failure_threshold = 1, .open_duration = 10ms}, clock());
    breaker.on_failure();
    advance(11ms);
    ASSERT_TRUE(breaker.allow());

    breaker.on_success();
    EXPECT_EQ(breaker.state(), CircuitBreaker::State::kClosed);
    EXPECT_TRUE(breaker.allow()) << "closed again, traffic resumes";
}

TEST_F(BreakerTest, FailedProbeReopensImmediately) {
    CircuitBreaker breaker("db", {.failure_threshold = 1, .open_duration = 10ms}, clock());
    breaker.on_failure();
    advance(11ms);
    ASSERT_TRUE(breaker.allow());

    breaker.on_failure();
    EXPECT_EQ(breaker.state(), CircuitBreaker::State::kOpen);
    EXPECT_EQ(breaker.opened_count(), 2U);
    EXPECT_FALSE(breaker.allow()) << "the cooldown restarts from the failed probe";
}

TEST_F(BreakerTest, RequiresSeveralSuccessesWhenConfiguredTo) {
    CircuitBreaker breaker("db",
                           {.failure_threshold = 1,
                            .open_duration = 10ms,
                            .half_open_max_calls = 3,
                            .success_threshold = 2},
                           clock());
    breaker.on_failure();
    advance(11ms);

    ASSERT_TRUE(breaker.allow());
    breaker.on_success();
    EXPECT_EQ(breaker.state(), CircuitBreaker::State::kHalfOpen) << "one success is not enough";

    ASSERT_TRUE(breaker.allow());
    breaker.on_success();
    EXPECT_EQ(breaker.state(), CircuitBreaker::State::kClosed);
}

TEST_F(BreakerTest, ResetForcesClosed) {
    CircuitBreaker breaker("db", {.failure_threshold = 1, .open_duration = 100000ms}, clock());
    breaker.on_failure();
    ASSERT_EQ(breaker.state(), CircuitBreaker::State::kOpen);

    breaker.reset();
    EXPECT_EQ(breaker.state(), CircuitBreaker::State::kClosed);
    EXPECT_TRUE(breaker.allow());
}

// --- run_with_breaker ------------------------------------------------------

TEST_F(BreakerTest, WrapperReportsOutcomesAutomatically) {
    // A hand-written allow()/on_failure() pair is one early return away from
    // lying about the dependency's health.
    CircuitBreaker breaker("broker", {.failure_threshold = 1, .open_duration = 10000ms}, clock());

    EXPECT_EQ(run_with_breaker(breaker, []() -> int { return 5; }), 5);
    EXPECT_EQ(breaker.state(), CircuitBreaker::State::kClosed);

    EXPECT_THROW(run_with_breaker(breaker, []() -> int { throw std::runtime_error("boom"); }),
                 std::runtime_error);
    EXPECT_EQ(breaker.state(), CircuitBreaker::State::kOpen) << "failure reported without help";
}

TEST_F(BreakerTest, OpenBreakerThrowsWithoutRunningTheOperation) {
    CircuitBreaker breaker("broker", {.failure_threshold = 1, .open_duration = 10000ms}, clock());
    breaker.on_failure();

    bool called = false;
    EXPECT_THROW(run_with_breaker(breaker,
                                  [&]() -> int {
                                      called = true;
                                      return 1;
                                  }),
                 CircuitOpenError);
    EXPECT_FALSE(called) << "the whole point is not touching the dependency";
}

TEST_F(BreakerTest, VoidOperationsAreSupported) {
    CircuitBreaker breaker("void", {.failure_threshold = 1}, clock());
    bool ran = false;
    run_with_breaker(breaker, [&] { ran = true; });
    EXPECT_TRUE(ran);
    EXPECT_EQ(breaker.state(), CircuitBreaker::State::kClosed);
}

// --- the two together ------------------------------------------------------

TEST_F(BreakerTest, BreakerShortCircuitsRetryOnceTheDependencyIsDown) {
    // The composition that matters. Retry alone against an outage holds a worker
    // thread for the whole backoff schedule on every request; once the breaker
    // opens, those requests fail immediately instead.
    CircuitBreaker breaker("db", {.failure_threshold = 2, .open_duration = 10000ms}, clock());
    int dependency_calls = 0;

    auto call = [&] {
        return run_with_breaker(breaker, [&]() -> int {
            return run_with_retry(
                RetryPolicy{.max_attempts = 2, .jitter_ratio = 0.0},
                "db",
                kAlways,
                [&]() -> int {
                    ++dependency_calls;
                    throw std::runtime_error("connection refused");
                },
                nullptr,
                [](std::chrono::milliseconds) {});
        });
    };

    EXPECT_THROW(call(), std::runtime_error);
    EXPECT_THROW(call(), std::runtime_error);
    EXPECT_EQ(dependency_calls, 4) << "two calls, each retried once";
    ASSERT_EQ(breaker.state(), CircuitBreaker::State::kOpen);

    EXPECT_THROW(call(), CircuitOpenError);
    EXPECT_EQ(dependency_calls, 4) << "the breaker stopped the retries from running at all";
}

}  // namespace

// --- cache degradation -----------------------------------------------------
//
// A cache is an optimisation. Its failure must degrade to a miss, because every
// caller either falls back to the source of truth or recomputes through
// remember(). Before this, a backend exception propagated straight out of
// CacheService::get() into the caller — so a Redis outage did not make the
// service slower, it made it return 500s from endpoints that could have been
// served perfectly well from PostgreSQL.

namespace {

// Fails every operation, as an unreachable Redis does.
class BrokenCacheStore final : public rtc::cache::ICacheStore {
  public:
    void set(std::string_view, std::string_view, Seconds) override { fail(); }
    [[nodiscard]] std::optional<std::string> get(std::string_view) override { fail(); }
    bool del(std::string_view) override { fail(); }
    [[nodiscard]] bool exists(std::string_view) override { fail(); }
    void expire(std::string_view, Seconds) override { fail(); }
    [[nodiscard]] std::int64_t incr(std::string_view, Seconds) override { fail(); }
    void sadd(std::string_view, std::string_view) override { fail(); }
    void srem(std::string_view, std::string_view) override { fail(); }
    [[nodiscard]] bool sismember(std::string_view, std::string_view) override { fail(); }
    [[nodiscard]] std::vector<std::string> smembers(std::string_view) override { fail(); }
    [[nodiscard]] std::size_t scard(std::string_view) override { fail(); }
    std::size_t purge_expired() override { return 0; }
    [[nodiscard]] std::string_view backend_name() const override { return "broken"; }

    int calls = 0;

  private:
    [[noreturn]] void fail() {
        ++calls;
        throw std::runtime_error("connection refused");
    }
};

}  // namespace

TEST(CacheResilience, ReadFailureDegradesToAMissRatherThanThrowing) {
    BrokenCacheStore store;
    rtc::cache::CacheService cache(store);

    std::optional<nlohmann::json> value;
    EXPECT_NO_THROW(value = cache.get("users", "1"))
        << "a cache outage must not turn into a request failure";
    EXPECT_FALSE(value.has_value()) << "the correct answer is a miss";
    EXPECT_EQ(cache.misses(), 1U) << "and it should be counted as one";
}

TEST(CacheResilience, WriteFailureIsSwallowed) {
    // Failing the write that produced the value because the *cache* is down
    // would be the tail wagging the dog.
    BrokenCacheStore store;
    rtc::cache::CacheService cache(store);
    EXPECT_NO_THROW(cache.put("users", "1", nlohmann::json{{"id", 1}}, std::chrono::seconds(60)));
}

TEST(CacheResilience, InvalidationFailureIsSwallowed) {
    // The caller has already performed the mutation this follows; there is
    // nothing useful it could do with the error. Staleness is TTL-bounded.
    BrokenCacheStore store;
    rtc::cache::CacheService cache(store);
    EXPECT_NO_THROW(cache.invalidate_local("users", "1"));
}

TEST(CacheResilience, RememberFallsBackToTheLoaderWhenTheCacheIsDown) {
    // The property that makes degrading safe: the value is still produced.
    BrokenCacheStore store;
    rtc::cache::CacheService cache(store);

    int loader_calls = 0;
    const auto value = cache.remember("users", "1", std::chrono::seconds(60), [&] {
        ++loader_calls;
        return nlohmann::json{{"id", 1}, {"from", "database"}};
    });

    EXPECT_EQ(loader_calls, 1);
    EXPECT_EQ(value.at("from"), "database") << "the request still gets its answer";
}

TEST(CacheResilience, BreakerStopsCallingADeadCacheAltogether) {
    BrokenCacheStore store;
    rtc::cache::CacheService cache(store);
    CircuitBreaker breaker("cache", {.failure_threshold = 2, .open_duration = 10000ms});
    cache.set_circuit_breaker(breaker);

    EXPECT_FALSE(cache.get("users", "1").has_value());
    EXPECT_FALSE(cache.get("users", "2").has_value());
    ASSERT_EQ(breaker.state(), CircuitBreaker::State::kOpen);

    const int calls_before = store.calls;
    EXPECT_FALSE(cache.get("users", "3").has_value());
    cache.put("users", "3", nlohmann::json{{"id", 3}}, std::chrono::seconds(60));
    EXPECT_EQ(store.calls, calls_before)
        << "an open breaker must stop touching the backend, not just ignore its errors";
}

TEST(CacheResilience, AWorkingCacheIsUnaffectedByTheBreaker) {
    // The default path must not change: hits still hit, misses still miss.
    rtc::cache::InMemoryCacheStore store;
    rtc::cache::CacheService cache(store);
    CircuitBreaker breaker("cache", {.failure_threshold = 1});
    cache.set_circuit_breaker(breaker);

    EXPECT_FALSE(cache.get("users", "1").has_value());
    cache.put("users", "1", nlohmann::json{{"id", 1}}, std::chrono::seconds(60));
    const auto value = cache.get("users", "1");
    ASSERT_TRUE(value.has_value());
    EXPECT_EQ(value->at("id"), 1);
    EXPECT_EQ(breaker.state(), CircuitBreaker::State::kClosed);
}

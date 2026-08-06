#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <string_view>

namespace rtc::reliability {

// Stops a failing dependency from taking the service down with it.
//
// The failure mode this exists for
// --------------------------------
// When a dependency stops answering, every request that touches it blocks until
// its timeout. Those requests hold a worker thread the whole time. With enough
// traffic the pool fills with threads waiting on something that is not going to
// answer, and endpoints that never touch that dependency start timing out too.
// The database being slow becomes the whole service being down.
//
// Retries make this worse, not better: three attempts with backoff hold the
// thread three times as long. Retry and circuit breaking solve opposite halves
// of the same problem — retry absorbs a *blip*, the breaker absorbs an *outage*
// — which is why both exist and why the breaker wraps the retry rather than the
// other way round.
//
// States
// ------
//   Closed    calls pass through; consecutive failures are counted
//   Open      calls are rejected immediately, without touching the dependency
//   HalfOpen  a limited number of trial calls probe whether it has recovered
//
// Open -> HalfOpen happens on the first call after open_duration elapses, not on
// a timer: a breaker on an idle path should not spend its life probing something
// nobody is asking for.
//
// Thread-safe. Every public method may be called concurrently from request
// threads.
class CircuitBreaker {
  public:
    enum class State { kClosed, kOpen, kHalfOpen };

    struct Options {
        // Consecutive failures in Closed before opening. Consecutive, not a
        // rate: a rate needs a window and a minimum volume to avoid opening on
        // "1 of 1 failed", and this is the simpler contract to reason about
        // during an incident.
        int failure_threshold = 5;

        // How long Open lasts before a trial is allowed.
        std::chrono::milliseconds open_duration{30000};

        // Trial calls permitted concurrently in HalfOpen. One is usually right:
        // the point is to test the water, not to re-flood a recovering
        // dependency.
        int half_open_max_calls = 1;

        // Consecutive successes in HalfOpen required to close.
        int success_threshold = 1;
    };

    // Injectable so tests can drive state transitions without sleeping through
    // open_duration, and so a test asserts the transition rather than a timer.
    using Clock = std::function<std::chrono::steady_clock::time_point()>;

    CircuitBreaker(std::string name, Options options, Clock clock = default_clock);

    CircuitBreaker(const CircuitBreaker&) = delete;
    CircuitBreaker& operator=(const CircuitBreaker&) = delete;

    // Whether a call may proceed. Transitions Open -> HalfOpen when the cooldown
    // has elapsed. A caller that receives false must not touch the dependency.
    [[nodiscard]] bool allow();

    // Report the outcome. Exactly one of these must follow every allow() that
    // returned true, or the breaker's view of the world drifts from reality —
    // an unreported failure leaves it Closed over a dead dependency.
    void on_success();
    void on_failure();

    [[nodiscard]] State state() const;
    [[nodiscard]] std::string_view name() const noexcept { return name_; }

    // Counters for /metrics. A rejection count that climbs is the breaker doing
    // its job; one that never falls is a dependency that never recovered.
    [[nodiscard]] std::uint64_t rejected_count() const noexcept { return rejected_.load(); }
    [[nodiscard]] std::uint64_t opened_count() const noexcept { return opened_.load(); }

    // Forces Closed and clears counters. For administrative recovery, not for
    // normal operation.
    void reset();

    [[nodiscard]] static std::string_view to_string(State state) noexcept;

  private:
    static std::chrono::steady_clock::time_point default_clock();

    // Callers must hold mutex_.
    void open_locked();

    const std::string name_;
    const Options options_;
    const Clock clock_;

    mutable std::mutex mutex_;
    State state_ = State::kClosed;
    int consecutive_failures_ = 0;
    int half_open_successes_ = 0;
    int half_open_in_flight_ = 0;
    std::chrono::steady_clock::time_point opened_at_{};

    std::atomic<std::uint64_t> rejected_{0};
    std::atomic<std::uint64_t> opened_{0};
};

// Thrown when a call is rejected because the breaker is open.
//
// A distinct type so a caller can tell "the dependency refused" from "we did not
// ask". They warrant different responses: the first may be worth retrying, the
// second is guaranteed to fail again until the cooldown elapses.
class CircuitOpenError : public std::runtime_error {
  public:
    explicit CircuitOpenError(std::string_view name)
        : std::runtime_error("circuit breaker '" + std::string(name) + "' is open") {}
};

// Runs `operation` through `breaker`, or throws CircuitOpenError without calling
// it. Success and failure are reported automatically, which is what keeps the
// breaker's state honest — a hand-written allow()/on_failure() pair is one early
// return away from lying.
template <typename Operation>
auto run_with_breaker(CircuitBreaker& breaker, Operation&& operation) -> decltype(operation()) {
    if (!breaker.allow()) {
        throw CircuitOpenError(breaker.name());
    }
    try {
        if constexpr (std::is_void_v<decltype(operation())>) {
            operation();
            breaker.on_success();
            return;
        } else {
            auto result = operation();
            breaker.on_success();
            return result;
        }
    } catch (...) {
        breaker.on_failure();
        throw;
    }
}

}  // namespace rtc::reliability

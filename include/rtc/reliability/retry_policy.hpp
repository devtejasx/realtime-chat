#pragma once

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

namespace rtc::reliability {

// Bounded retry with exponential backoff and jitter.
//
// The repository already had backoff — hand-rolled inside the Redis cluster
// bus's reconnect loop, with its own doubling and its own ceiling. That is the
// shape this replaces: policy expressed as control flow, once per call site,
// impossible to configure and impossible to test without the subsystem it lives
// in.
//
// Two properties matter more than the arithmetic:
//
//   Retries are bounded. There is no "retry forever" mode. An unbounded retry
//   against a dependency that is down converts a fast failure into a hung
//   request, and hung requests exhaust the thread pool — which turns one broken
//   dependency into a broken service.
//
//   Jitter is on by default. Synchronised clients retrying on identical
//   schedules arrive as a thundering herd precisely when the dependency is
//   least able to absorb one, so a struggling database gets a coordinated
//   stampede at every backoff boundary.
struct RetryPolicy {
    // Total attempts *including* the first. 1 means "no retry", which is a
    // legitimate policy rather than a disabled one.
    int max_attempts = 3;

    // Delay before the second attempt; each subsequent delay multiplies by
    // `multiplier`, clamped to `max_delay`.
    std::chrono::milliseconds initial_delay{50};
    double multiplier = 2.0;
    std::chrono::milliseconds max_delay{2000};

    // Fraction of the computed delay to randomise by, in [0, 1]. 0.2 spreads a
    // 100ms delay across 80–120ms.
    double jitter_ratio = 0.2;

    // Delay before attempt number `attempt` (1-based; attempt 1 is immediate, so
    // delay_for(1) is zero). Jitter is applied unless jitter_ratio is 0.
    [[nodiscard]] std::chrono::milliseconds delay_for(int attempt) const;

    // Single attempt, no waiting. Named rather than written inline so a call
    // site that deliberately does not retry says so.
    [[nodiscard]] static RetryPolicy none() noexcept { return RetryPolicy{.max_attempts = 1}; }
};

// Outcome of a retried operation, for metrics and logging.
struct RetryStats {
    int attempts = 0;                     // how many were actually made
    std::chrono::milliseconds waited{0};  // total time spent sleeping
};

// Decides whether a failure is worth retrying.
//
// There is deliberately no default. Retrying blindly is the dangerous case: a
// validation error will fail identically every time (three times the latency for
// the same 400), and a non-idempotent write that timed out *after* the server
// applied it will be applied twice. The caller knows which of its failures are
// transient; this layer does not.
using RetryPredicate = std::function<bool(const std::exception&)>;

// Injectable sleep, so tests can exercise backoff without waiting for it.
using Sleeper = std::function<void(std::chrono::milliseconds)>;

// The real sleeper. Separate function so tests can assert they did not get it.
void sleep_for(std::chrono::milliseconds duration);

// Runs `operation` under `policy`, retrying while `should_retry` accepts the
// exception and attempts remain.
//
// Rethrows the final exception when attempts are exhausted — a caller must not
// be able to mistake "gave up after 3 tries" for success. `name` appears in the
// log line emitted on each retry, which is what makes a retry storm visible
// rather than merely slow.
//
// The operation must be safe to run more than once. That is the caller's
// contract to uphold and the reason `should_retry` is mandatory.
template <typename Operation>
auto run_with_retry(const RetryPolicy& policy,
                    std::string_view name,
                    const RetryPredicate& should_retry,
                    Operation&& operation,
                    RetryStats* stats = nullptr,
                    const Sleeper& sleeper = sleep_for) -> decltype(operation());

// Logs a retry. Declared out-of-line so this header does not pull in the logger.
void log_retry(std::string_view name,
               int attempt,
               int max_attempts,
               std::chrono::milliseconds delay,
               const char* reason);

template <typename Operation>
auto run_with_retry(const RetryPolicy& policy,
                    std::string_view name,
                    const RetryPredicate& should_retry,
                    Operation&& operation,
                    RetryStats* stats,
                    const Sleeper& sleeper) -> decltype(operation()) {
    const int attempts = std::max(1, policy.max_attempts);
    RetryStats local;

    for (int attempt = 1;; ++attempt) {
        try {
            local.attempts = attempt;
            if (stats != nullptr) {
                *stats = local;
            }
            return operation();
        } catch (const std::exception& ex) {
            const bool last = attempt >= attempts;
            // Order matters: check exhaustion first so the final failure is
            // rethrown unchanged rather than being asked about again.
            if (last || !should_retry(ex)) {
                if (stats != nullptr) {
                    local.attempts = attempt;
                    *stats = local;
                }
                throw;
            }
            const auto delay = policy.delay_for(attempt + 1);
            log_retry(name, attempt, attempts, delay, ex.what());
            if (delay.count() > 0) {
                sleeper(delay);
                local.waited += delay;
            }
        }
    }
}

}  // namespace rtc::reliability

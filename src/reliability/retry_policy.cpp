#include "rtc/reliability/retry_policy.hpp"

#include <cmath>
#include <random>
#include <thread>

#include "rtc/logging/logger.hpp"

namespace rtc::reliability {
namespace {

// Thread-local rather than a shared engine behind a mutex: jitter is computed on
// every retry across every worker, and contending on a global RNG to decide how
// long to wait would be its own small bottleneck. Each thread owning its engine
// is not shared mutable state.
[[nodiscard]] double unit_jitter() {
    static thread_local std::mt19937 engine{std::random_device{}()};
    std::uniform_real_distribution<double> distribution(-1.0, 1.0);
    return distribution(engine);
}

}  // namespace

void sleep_for(std::chrono::milliseconds duration) {
    std::this_thread::sleep_for(duration);
}

std::chrono::milliseconds RetryPolicy::delay_for(int attempt) const {
    // Attempt 1 is the initial call; nothing has failed yet, so there is nothing
    // to wait for.
    if (attempt <= 1) {
        return std::chrono::milliseconds{0};
    }

    // initial_delay * multiplier^(attempt-2): attempt 2 waits initial_delay.
    double delay = static_cast<double>(initial_delay.count());
    for (int i = 2; i < attempt; ++i) {
        delay *= multiplier;
        if (delay >= static_cast<double>(max_delay.count())) {
            // Clamped early so a large attempt number cannot overflow the double
            // into inf before the clamp below sees it.
            delay = static_cast<double>(max_delay.count());
            break;
        }
    }
    delay = std::min(delay, static_cast<double>(max_delay.count()));

    if (jitter_ratio > 0.0) {
        delay += delay * jitter_ratio * unit_jitter();
    }
    // Never negative, and never zero for a real retry — a zero delay would make
    // the "wait" a tight loop against a dependency that is already struggling.
    const auto milliseconds = static_cast<std::int64_t>(std::llround(delay));
    return std::chrono::milliseconds{std::max<std::int64_t>(1, milliseconds)};
}

void log_retry(std::string_view name,
               int attempt,
               int max_attempts,
               std::chrono::milliseconds delay,
               const char* reason) {
    // Warn, not debug. A retry means a dependency failed; a service quietly
    // retrying its way through a degraded database looks healthy right up to the
    // point it stops being able to.
    RTC_LOG_WARN("Retrying '{}' (attempt {}/{}) in {}ms after: {}",
                 name,
                 attempt,
                 max_attempts,
                 delay.count(),
                 reason != nullptr ? reason : "unknown error");
}

}  // namespace rtc::reliability

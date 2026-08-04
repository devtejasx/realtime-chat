#include "rtc/reliability/circuit_breaker.hpp"

#include <algorithm>
#include <utility>

#include "rtc/logging/logger.hpp"

namespace rtc::reliability {

std::chrono::steady_clock::time_point CircuitBreaker::default_clock() {
    return std::chrono::steady_clock::now();
}

CircuitBreaker::CircuitBreaker(std::string name, Options options, Clock clock)
    : name_(std::move(name)), options_(options), clock_(std::move(clock)) {}

std::string_view CircuitBreaker::to_string(State state) noexcept {
    switch (state) {
        case State::kClosed:
            return "closed";
        case State::kOpen:
            return "open";
        case State::kHalfOpen:
            return "half_open";
    }
    return "unknown";
}

void CircuitBreaker::open_locked() {
    state_ = State::kOpen;
    opened_at_ = clock_();
    consecutive_failures_ = 0;
    half_open_successes_ = 0;
    half_open_in_flight_ = 0;
    opened_.fetch_add(1, std::memory_order_relaxed);
    // Error, not warning: the service has stopped calling a dependency
    // altogether. Whatever that dependency serves is now failing fast, and an
    // operator needs to know which one without reading a dashboard.
    RTC_LOG_ERROR("Circuit breaker '{}' opened for {}ms after {} consecutive failures",
                  name_,
                  options_.open_duration.count(),
                  options_.failure_threshold);
}

bool CircuitBreaker::allow() {
    std::lock_guard<std::mutex> lock(mutex_);
    switch (state_) {
        case State::kClosed:
            return true;

        case State::kOpen: {
            if (clock_() - opened_at_ < options_.open_duration) {
                rejected_.fetch_add(1, std::memory_order_relaxed);
                return false;
            }
            // Cooldown elapsed. Transition on demand rather than on a timer, so
            // a breaker guarding an idle path does not probe a dependency
            // nobody is asking for.
            state_ = State::kHalfOpen;
            half_open_successes_ = 0;
            half_open_in_flight_ = 1;
            RTC_LOG_WARN("Circuit breaker '{}' half-open; probing recovery", name_);
            return true;
        }

        case State::kHalfOpen:
            if (half_open_in_flight_ >= options_.half_open_max_calls) {
                // Trials already in flight. Rejecting the rest is the point:
                // admitting full traffic to something that has not proven it
                // recovered is how a breaker turns a recovery into a second
                // outage.
                rejected_.fetch_add(1, std::memory_order_relaxed);
                return false;
            }
            ++half_open_in_flight_;
            return true;
    }
    return true;
}

void CircuitBreaker::on_success() {
    std::lock_guard<std::mutex> lock(mutex_);
    switch (state_) {
        case State::kClosed:
            // A success breaks a failure streak. Consecutive means consecutive.
            consecutive_failures_ = 0;
            return;

        case State::kHalfOpen:
            half_open_in_flight_ = std::max(0, half_open_in_flight_ - 1);
            if (++half_open_successes_ >= options_.success_threshold) {
                state_ = State::kClosed;
                consecutive_failures_ = 0;
                half_open_successes_ = 0;
                RTC_LOG_INFO("Circuit breaker '{}' closed; dependency recovered", name_);
            }
            return;

        case State::kOpen:
            // A result from a call that was admitted just before the breaker
            // opened. Harmless, and deliberately not treated as evidence of
            // recovery: it proves the dependency worked in the past, not now.
            return;
    }
}

void CircuitBreaker::on_failure() {
    std::lock_guard<std::mutex> lock(mutex_);
    switch (state_) {
        case State::kClosed:
            if (++consecutive_failures_ >= options_.failure_threshold) {
                open_locked();
            }
            return;

        case State::kHalfOpen:
            // One failed trial is enough. The dependency was given a chance and
            // did not take it; re-opening immediately is cheaper than letting
            // the remaining trials queue against it.
            RTC_LOG_WARN("Circuit breaker '{}' probe failed; reopening", name_);
            open_locked();
            return;

        case State::kOpen:
            return;
    }
}

CircuitBreaker::State CircuitBreaker::state() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return state_;
}

void CircuitBreaker::reset() {
    std::lock_guard<std::mutex> lock(mutex_);
    state_ = State::kClosed;
    consecutive_failures_ = 0;
    half_open_successes_ = 0;
    half_open_in_flight_ = 0;
    RTC_LOG_INFO("Circuit breaker '{}' reset to closed", name_);
}

}  // namespace rtc::reliability

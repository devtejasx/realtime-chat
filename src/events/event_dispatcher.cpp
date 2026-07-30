#include "rtc/events/event_dispatcher.hpp"

#include <algorithm>
#include <exception>

#include "rtc/logging/logger.hpp"

namespace rtc::events {

void EventDispatcher::subscribe(IEventSubscriber& subscriber) {
    std::unique_lock lock(mutex_);
    // Guard against double registration, which would double-apply side effects
    // (two audit rows for one event) — a subtle bug worth making impossible.
    const auto existing = std::find(subscribers_.begin(), subscribers_.end(), &subscriber);
    if (existing != subscribers_.end()) {
        RTC_LOG_WARN("Event subscriber '{}' is already registered; ignoring",
                     subscriber.subscriber_name());
        return;
    }
    subscribers_.push_back(&subscriber);
    RTC_LOG_DEBUG("Registered event subscriber '{}' ({} total)",
                  subscriber.subscriber_name(),
                  subscribers_.size());
}

void EventDispatcher::dispatch(const DomainEvent& event) noexcept {
    try {
        std::shared_lock lock(mutex_);
        dispatched_.fetch_add(1, std::memory_order_relaxed);
        for (IEventSubscriber* subscriber : subscribers_) {
            if (!subscriber->interested_in(event.type)) {
                continue;
            }
            // Per-subscriber isolation: subscribers are independent side effects,
            // so one failing must not prevent the rest from running.
            try {
                subscriber->handle(event);
            } catch (const std::exception& ex) {
                failures_.fetch_add(1, std::memory_order_relaxed);
                RTC_LOG_ERROR("Event subscriber '{}' failed handling '{}' (event_id={}): {}",
                              subscriber->subscriber_name(),
                              event.name(),
                              event.event_id,
                              ex.what());
            } catch (...) {
                failures_.fetch_add(1, std::memory_order_relaxed);
                RTC_LOG_ERROR(
                    "Event subscriber '{}' failed handling '{}' (event_id={}): "
                    "unknown exception",
                    subscriber->subscriber_name(),
                    event.name(),
                    event.event_id);
            }
        }
    } catch (...) {
        // Only reachable if locking itself fails. Nothing useful remains to do
        // except stay silent-safe; dispatch is declared noexcept.
    }
}

std::size_t EventDispatcher::subscriber_count() const noexcept {
    try {
        std::shared_lock lock(mutex_);
        return subscribers_.size();
    } catch (...) {
        return 0;
    }
}

std::uint64_t EventDispatcher::dispatched_count() const noexcept {
    return dispatched_.load(std::memory_order_relaxed);
}

std::uint64_t EventDispatcher::handler_failure_count() const noexcept {
    return failures_.load(std::memory_order_relaxed);
}

}  // namespace rtc::events

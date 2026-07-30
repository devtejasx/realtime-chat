#include "rtc/events/in_process_event_bus.hpp"

#include <utility>

#include "rtc/logging/logger.hpp"

namespace rtc::events {

NullEventPublisher& NullEventPublisher::instance() noexcept {
    static NullEventPublisher shared;
    return shared;
}

void InProcessEventBus::publish(DomainEvent event) noexcept {
    published_.fetch_add(1, std::memory_order_relaxed);
    try {
        if (executor_ == nullptr) {
            dispatcher_.dispatch(event);
            return;
        }
        // Move the envelope into the task: the producer's stack frame is gone by
        // the time a worker picks this up, so the task must own everything it
        // touches. dispatcher_ outlives the executor (destruction order in the
        // composition root), so capturing the reference is safe.
        executor_->submit([this, moved = std::move(event)]() mutable {
            dispatcher_.dispatch(moved);
        });
    } catch (const std::exception& ex) {
        dropped_.fetch_add(1, std::memory_order_relaxed);
        RTC_LOG_WARN("Dropped domain event '{}': {}", to_string(event.type), ex.what());
    } catch (...) {
        dropped_.fetch_add(1, std::memory_order_relaxed);
    }
}

}  // namespace rtc::events

#pragma once

#include <atomic>
#include <cstdint>

#include "rtc/events/event_bus.hpp"
#include "rtc/events/event_dispatcher.hpp"
#include "rtc/jobs/background_executor.hpp"

namespace rtc::events {

// In-process implementation of IEventBus.
//
// Delivery mode is chosen by whether an executor is supplied:
//
//   - **Asynchronous** (executor provided, the production wiring): publish()
//     hands the event to the background pool and returns immediately. This is
//     what keeps a slow subscriber — an audit INSERT, a broker round-trip — off
//     the request's critical path. Ordering across events is therefore not
//     guaranteed; every subscriber here is order-independent by design, and
//     anything needing strict ordering belongs on the message broker with a
//     partition key, not on an in-process bus.
//
//   - **Synchronous** (no executor, used by tests and by `--migrate` mode):
//     publish() dispatches inline. Tests get deterministic assertions with no
//     sleeping or polling.
//
// publish() is noexcept per IEventPublisher: a publication failure (a full queue
// during shutdown, say) is counted and dropped, never surfaced to the producer.
class InProcessEventBus final : public IEventBus {
public:
    // Synchronous bus.
    explicit InProcessEventBus(EventDispatcher& dispatcher) noexcept : dispatcher_(dispatcher) {}

    // Asynchronous bus, dispatching on `executor`.
    InProcessEventBus(EventDispatcher& dispatcher, jobs::BackgroundExecutor& executor) noexcept
        : dispatcher_(dispatcher), executor_(&executor) {}

    void publish(DomainEvent event) noexcept override;

    void subscribe(IEventSubscriber& subscriber) override { dispatcher_.subscribe(subscriber); }

    [[nodiscard]] std::size_t subscriber_count() const noexcept override {
        return dispatcher_.subscriber_count();
    }

    [[nodiscard]] bool is_asynchronous() const noexcept { return executor_ != nullptr; }

    [[nodiscard]] std::uint64_t published_count() const noexcept {
        return published_.load(std::memory_order_relaxed);
    }
    [[nodiscard]] std::uint64_t dropped_count() const noexcept {
        return dropped_.load(std::memory_order_relaxed);
    }

    [[nodiscard]] const EventDispatcher& dispatcher() const noexcept { return dispatcher_; }

private:
    EventDispatcher& dispatcher_;
    jobs::BackgroundExecutor* executor_ = nullptr;
    std::atomic<std::uint64_t> published_{0};
    std::atomic<std::uint64_t> dropped_{0};
};

}  // namespace rtc::events

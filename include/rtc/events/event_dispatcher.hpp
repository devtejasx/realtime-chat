#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <shared_mutex>
#include <vector>

#include "rtc/events/event_bus.hpp"

namespace rtc::events {

// Routes a domain event to the subscribers that asked for its type.
//
// Note the deliberate name overlap with rtc::realtime::EventDispatcher, which is
// a different thing: that one dispatches *inbound WebSocket frames* to handlers,
// this one dispatches *domain events* to subscribers. Both are "dispatchers" in
// their own layer; the namespaces keep them apart.
//
// Kept separate from InProcessEventBus so the routing policy — filtering, and
// crucially error isolation — is unit-testable without any threading involved.
//
// Error isolation is the whole point: subscribers are independent side effects
// (audit persistence, broker publication, metrics). If the audit database is
// down, notifications must still be delivered. Every subscriber therefore runs
// inside its own try/catch and a failure is counted and logged, never propagated.
//
// Thread-safe. Subscription uses a unique lock (rare, bootstrap only) and
// dispatch a shared lock, so concurrent publishes never serialise on each other.
class EventDispatcher {
public:
    EventDispatcher() = default;

    EventDispatcher(const EventDispatcher&) = delete;
    EventDispatcher& operator=(const EventDispatcher&) = delete;

    void subscribe(IEventSubscriber& subscriber);

    // Invokes every interested subscriber. Never throws.
    void dispatch(const DomainEvent& event) noexcept;

    [[nodiscard]] std::size_t subscriber_count() const noexcept;

    // Counters for /metrics and the admin diagnostics endpoint.
    [[nodiscard]] std::uint64_t dispatched_count() const noexcept;
    [[nodiscard]] std::uint64_t handler_failure_count() const noexcept;

private:
    mutable std::shared_mutex mutex_;
    std::vector<IEventSubscriber*> subscribers_;
    // Atomic, not mutex-guarded: dispatch() holds only a *shared* lock, so
    // concurrent publishes would race on plain counters. Relaxed ordering is
    // sufficient — these are monotonic statistics with no ordering relationship
    // to any other state.
    std::atomic<std::uint64_t> dispatched_{0};
    std::atomic<std::uint64_t> failures_{0};
};

}  // namespace rtc::events

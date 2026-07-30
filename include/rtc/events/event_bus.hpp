#pragma once

#include <string_view>

#include "rtc/events/domain_event.hpp"

namespace rtc::events {

// Publish side of the bus.
//
// Producers (services) depend on this narrow interface and nothing else, so a
// service can be unit-tested by injecting a recording publisher, and it cannot
// accidentally reach into the subscriber registry.
class IEventPublisher {
  public:
    virtual ~IEventPublisher() = default;

    // Publishes an event. Must never throw and must never fail the caller's
    // operation: an event has already happened by the time it is published, so
    // a delivery problem is the bus's concern, not the producer's.
    virtual void publish(DomainEvent event) noexcept = 0;
};

// Consume side of the bus.
//
// Implementations declare which event types they care about, so the dispatcher
// filters before invoking rather than every subscriber re-checking the tag.
class IEventSubscriber {
  public:
    virtual ~IEventSubscriber() = default;

    // True when this subscriber wants `type`. Called on every publish, so keep
    // it cheap and side-effect free.
    [[nodiscard]] virtual bool interested_in(EventType type) const noexcept = 0;

    // Handles an event. May throw: the dispatcher isolates and logs failures so
    // one broken subscriber cannot stop the others.
    virtual void handle(const DomainEvent& event) = 0;

    // Short name for logging and admin introspection.
    [[nodiscard]] virtual std::string_view subscriber_name() const noexcept = 0;
};

// The full bus: publishing plus subscription management.
class IEventBus : public IEventPublisher {
  public:
    // Registers a subscriber. The bus does not take ownership — the subscriber
    // must outlive the bus, which the composition root guarantees by destruction
    // order. Registration happens during bootstrap, before traffic is served.
    virtual void subscribe(IEventSubscriber& subscriber) = 0;

    [[nodiscard]] virtual std::size_t subscriber_count() const noexcept = 0;
};

// A publisher that drops everything.
//
// Used as the default for services constructed without a bus, which is what
// keeps the event bus a *non-breaking* addition: existing constructors and every
// existing unit test keep working, and no producer needs a null check.
class NullEventPublisher final : public IEventPublisher {
  public:
    void publish(DomainEvent /*event*/) noexcept override {}

    // Shared immutable instance. Safe as a global: stateless and const-behaving,
    // so this is not mutable global state.
    [[nodiscard]] static NullEventPublisher& instance() noexcept;
};

}  // namespace rtc::events

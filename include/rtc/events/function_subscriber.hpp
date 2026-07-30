#pragma once

#include <bitset>
#include <functional>
#include <initializer_list>
#include <string>
#include <string_view>
#include <utility>

#include "rtc/events/event_bus.hpp"

namespace rtc::events {

// Adapts a callable into an IEventSubscriber.
//
// Most subscribers deserve their own class (see AuditLogSubscriber): they hold
// dependencies and carry real logic. But a handful are genuinely one-liners —
// bumping a metric, forwarding to the broker — and forcing a class per callback
// would be ceremony without benefit. This is the escape hatch for those.
//
// Interest is stored as a bitset so `interested_in` is a single bit test, which
// matters because it runs for every subscriber on every published event.
class FunctionSubscriber final : public IEventSubscriber {
  public:
    using Handler = std::function<void(const DomainEvent&)>;

    FunctionSubscriber(std::string name, std::initializer_list<EventType> types, Handler handler)
        : name_(std::move(name)), handler_(std::move(handler)) {
        for (const EventType type : types) {
            interest_.set(static_cast<std::size_t>(type));
        }
    }

    // Subscribes to every event type. Useful for a broker forwarder or a debug tap.
    FunctionSubscriber(std::string name, Handler handler)
        : name_(std::move(name)), handler_(std::move(handler)) {
        interest_.set();
    }

    [[nodiscard]] bool interested_in(EventType type) const noexcept override {
        return interest_.test(static_cast<std::size_t>(type));
    }

    void handle(const DomainEvent& event) override {
        if (handler_) {
            handler_(event);
        }
    }

    [[nodiscard]] std::string_view subscriber_name() const noexcept override { return name_; }

  private:
    std::string name_;
    std::bitset<kEventTypeCount> interest_;
    Handler handler_;
};

}  // namespace rtc::events

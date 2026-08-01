#pragma once

#include <cstdint>
#include <nlohmann/json.hpp>
#include <string>

namespace rtc::services {

// Cross-instance presence propagation, expressed as an interface the service
// layer owns.
//
// Same shape and the same reasoning as rtc::cache::IInvalidationPublisher:
// presence semantics are a service concern, cluster transport is a realtime
// concern, and PresenceService must stay constructible in a test with neither
// Redis nor a WebSocket stack. The composition root injects an adapter backed by
// the cluster bus.

// One instance's view of one user changed. Deliberately reports the *local*
// fact ("this node now has / no longer has live sessions for the user") rather
// than a global verdict: only the receiver, which knows what every other node
// has reported, can decide whether the user is globally online.
struct PresenceDelta {
    std::int64_t user_id = 0;
    // True when the publishing node now holds at least one live session.
    bool online = false;
    // Unix milliseconds of the transition. Carried so a receiver can maintain a
    // last-seen value without a clock round trip; treated as advisory, since it
    // comes from another machine's clock.
    std::int64_t at_ms = 0;

    [[nodiscard]] nlohmann::json to_json() const {
        return nlohmann::json{{"user_id", user_id}, {"online", online}, {"at_ms", at_ms}};
    }

    [[nodiscard]] static PresenceDelta from_json(const nlohmann::json& body) {
        PresenceDelta delta;
        if (const auto it = body.find("user_id"); it != body.end() && it->is_number_integer()) {
            delta.user_id = it->get<std::int64_t>();
        }
        if (const auto it = body.find("online"); it != body.end() && it->is_boolean()) {
            delta.online = it->get<bool>();
        }
        if (const auto it = body.find("at_ms"); it != body.end() && it->is_number_integer()) {
            delta.at_ms = it->get<std::int64_t>();
        }
        return delta;
    }
};

class IPresencePublisher {
  public:
    virtual ~IPresencePublisher() = default;
    // Never throws: a presence hop that fails must not break the WebSocket
    // lifecycle that triggered it.
    virtual void publish_presence(const PresenceDelta& delta) noexcept = 0;
};

class NullPresencePublisher final : public IPresencePublisher {
  public:
    void publish_presence(const PresenceDelta& /*delta*/) noexcept override {}

    [[nodiscard]] static NullPresencePublisher& instance() noexcept {
        static NullPresencePublisher singleton;
        return singleton;
    }
};

}  // namespace rtc::services

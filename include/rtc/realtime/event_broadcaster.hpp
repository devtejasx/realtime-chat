#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

namespace rtc::realtime {

// Abstraction the service layer uses to push real-time events to clients,
// without depending on the WebSocket implementation. Services persist first,
// then call an IEventBroadcaster to fan out — so REST and WebSocket callers
// both trigger identical broadcasts through the same service methods.
//
// The concrete implementation (backed by the connection/session managers)
// serialises {type, data} into an envelope and delivers it to every live
// session of each target user. A no-op implementation is used when the realtime
// layer is disabled (e.g. in unit tests), so services never special-case it.
class IEventBroadcaster {
public:
    virtual ~IEventBroadcaster() = default;

    // Delivers an event to all live sessions of the given users. Implementations
    // must be thread-safe and non-blocking with respect to callers.
    virtual void publish(const std::vector<std::int64_t>& user_ids, std::string_view type,
                         const nlohmann::json& data) = 0;
};

// Null object: swallows all events. Lets services run identically with or
// without an active realtime layer.
class NullEventBroadcaster final : public IEventBroadcaster {
public:
    void publish(const std::vector<std::int64_t>&, std::string_view,
                 const nlohmann::json&) override {}
};

}  // namespace rtc::realtime

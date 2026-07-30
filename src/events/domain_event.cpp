#include "rtc/events/domain_event.hpp"

#include <utility>

#include "rtc/tracing/scoped_span.hpp"
#include "rtc/utils/random.hpp"

namespace rtc::events {

std::optional<EventType> parse_event_type(std::string_view name) noexcept {
    for (std::size_t i = 0; i < kEventTypeCount; ++i) {
        if (kEventTypeNames[i] == name) {
            return static_cast<EventType>(i);
        }
    }
    return std::nullopt;
}

DomainEvent DomainEvent::make(EventType type,
                              nlohmann::json payload,
                              std::optional<std::int64_t> actor_id) {
    DomainEvent event;
    event.type = type;
    event.event_id = utils::generate_hex_token(12);
    event.occurred_at = utils::now();
    event.actor_id = actor_id;
    event.payload = std::move(payload);
    // Adopt the ambient trace, if this thread is inside a traced scope. The
    // correlation id defaults to the trace id so an event is always joinable to
    // something, even when no request-scoped id was supplied.
    if (const auto* context = tracing::current_span_context(); context != nullptr) {
        event.trace_id = context->trace_id;
        event.correlation_id = context->trace_id;
    }
    return event;
}

nlohmann::json DomainEvent::to_json() const {
    nlohmann::json out{
        {"event_id", event_id},
        {"type", std::string(name())},
        {"occurred_at", utils::to_iso8601(occurred_at)},
        {"payload", payload},
    };
    if (actor_id) {
        out["actor_id"] = *actor_id;
    }
    if (!correlation_id.empty()) {
        out["correlation_id"] = correlation_id;
    }
    if (!trace_id.empty()) {
        out["trace_id"] = trace_id;
    }
    return out;
}

}  // namespace rtc::events

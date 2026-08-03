#pragma once

#include <nlohmann/json.hpp>
#include <optional>
#include <string>

#include "rtc/tracing/scoped_span.hpp"
#include "rtc/tracing/trace_context.hpp"

namespace rtc::realtime {

// Carries W3C trace context across the cluster bus.
//
// Why this is needed
// ------------------
// A trace follows a request through the layers of one process because
// tracing::current_span_context() is thread-local, and Crow handles a request on
// one thread. Redis Pub/Sub breaks both assumptions at once: the publish happens
// on a request thread in one process, and the delivery happens on a subscriber
// thread in a *different* process. The thread-local deliberately does not leak
// across that boundary, so without an explicit hop the trace simply ends at the
// publish and a fresh, unrelated one begins at the receiver.
//
// Operationally that is worse than having no trace. A message delivered across
// instances is exactly the path an operator wants to follow when a recipient
// reports not receiving something, and a trace that stops at the publish looks
// identical to a message that was never published.
//
// The key is namespaced with an underscore, matching the existing `_node` origin
// marker, so it cannot collide with a payload field. Receivers that predate this
// ignore unknown keys — every handler reads named fields rather than iterating —
// so a stamped message is safe to deliver to an older build during a rolling
// upgrade, and an unstamped one from an older publisher simply starts a new
// trace as before.
namespace cluster_trace {

// Field carrying the serialised traceparent. Underscore-prefixed like `_node`
// to keep envelope metadata visibly distinct from payload.
inline constexpr const char* kTraceParentKey = "_traceparent";

// Adds the caller's active trace context to an outbound message.
//
// A no-op when nothing is being traced (tracing disabled, or a sampling decision
// dropped this request), which keeps unsampled traffic free of a field that
// would only ever decode to an invalid context.
inline void stamp(nlohmann::json& envelope) {
    const auto* context = tracing::current_span_context();
    if (context == nullptr || !context->is_valid()) {
        return;
    }
    envelope[kTraceParentKey] = tracing::format_traceparent(*context);
}

// Recovers the publisher's context from an inbound message, if it carried one.
[[nodiscard]] inline std::optional<tracing::SpanContext> extract(const nlohmann::json& envelope) {
    const auto it = envelope.find(kTraceParentKey);
    if (it == envelope.end() || !it->is_string()) {
        return std::nullopt;
    }
    // parse_traceparent already rejects malformed headers by returning nullopt,
    // so a peer on a different build cannot make a receiver throw on the
    // subscriber thread — where an exception would take cross-instance delivery
    // down for every channel.
    return tracing::parse_traceparent(it->get<std::string>());
}

// RAII activation of a remote context for the duration of a handler call.
//
// Holds the extracted context by value: SpanScope stores a pointer to what it
// was given, and the envelope that produced it may not outlive the handler.
class RemoteScope {
  public:
    explicit RemoteScope(const nlohmann::json& envelope) {
        if (auto context = extract(envelope)) {
            context_ = std::move(*context);
            scope_.emplace(context_);
        }
    }

    RemoteScope(const RemoteScope&) = delete;
    RemoteScope& operator=(const RemoteScope&) = delete;
    RemoteScope(RemoteScope&&) = delete;
    RemoteScope& operator=(RemoteScope&&) = delete;

    // True when a context was found and activated.
    [[nodiscard]] bool active() const noexcept { return scope_.has_value(); }

  private:
    tracing::SpanContext context_;
    std::optional<tracing::SpanScope> scope_;
};

}  // namespace cluster_trace

}  // namespace rtc::realtime

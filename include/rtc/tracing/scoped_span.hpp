#pragma once

#include <string>
#include <utility>

#include "rtc/tracing/span.hpp"
#include "rtc/tracing/trace_context.hpp"

namespace rtc::tracing {

// The active span context for the calling thread.
//
// Crow handles a request (middlewares, router, handler) on a single thread, so a
// thread-local "current span" is exactly the right scope for implicit parenting:
// a repository or cache call deep inside a handler can attach itself to the
// server span without that span being passed down through every layer.
//
// This is thread-*local*, not global mutable state: each thread owns its stack,
// there is no sharing and no synchronisation. Work handed to the background
// executor or the broker starts a fresh context and must carry the parent
// SpanContext explicitly — the thread-local deliberately does not leak across
// that boundary.
[[nodiscard]] const SpanContext* current_span_context() noexcept;

// RAII activation of a span as the thread's current context.
//
// Prefer the free functions below (trace_scope / db_scope / cache_scope), which
// bundle span creation and activation. Use this type directly only when a span
// must be created and activated separately.
class SpanScope {
public:
    explicit SpanScope(const SpanContext& context) noexcept;
    ~SpanScope();

    SpanScope(const SpanScope&) = delete;
    SpanScope& operator=(const SpanScope&) = delete;
    SpanScope(SpanScope&&) = delete;
    SpanScope& operator=(SpanScope&&) = delete;

private:
    const SpanContext* previous_ = nullptr;
    bool active_ = false;
};

// A span plus its activation, as one movable object.
//
// Declare one at the top of any scope worth timing:
//
//     auto scope = tracing::trace_scope("MessageService.send");
//     scope.span().set_attribute("conversation.id", conversation_id);
//
// Destruction ends the span (recording it even if an exception is unwinding) and
// restores the previous active context. Zero cost when tracing is disabled: the
// span is inert and no context is pushed.
class ScopedSpan {
public:
    ScopedSpan() noexcept = default;
    explicit ScopedSpan(Span span);
    ~ScopedSpan();

    ScopedSpan(ScopedSpan&&) noexcept;
    ScopedSpan& operator=(ScopedSpan&&) noexcept;
    ScopedSpan(const ScopedSpan&) = delete;
    ScopedSpan& operator=(const ScopedSpan&) = delete;

    [[nodiscard]] Span& span() noexcept { return span_; }
    [[nodiscard]] const Span& span() const noexcept { return span_; }

    // Convenience passthroughs so the common cases read cleanly.
    ScopedSpan& set_attribute(std::string key, std::string value) {
        span_.set_attribute(std::move(key), std::move(value));
        return *this;
    }
    ScopedSpan& set_attribute(std::string key, std::int64_t value) {
        span_.set_attribute(std::move(key), value);
        return *this;
    }
    ScopedSpan& record_error(std::string_view message) {
        span_.record_error(message);
        return *this;
    }

private:
    void deactivate() noexcept;

    Span span_;
    const SpanContext* previous_ = nullptr;
    bool active_ = false;
};

// Starts and activates a child of the thread's current span (or a root span).
[[nodiscard]] ScopedSpan trace_scope(std::string name, SpanKind kind = SpanKind::kInternal);

// Instrumentation helpers applying OpenTelemetry semantic conventions, so spans
// render correctly in Jaeger/Zipkin/Tempo without per-call-site boilerplate.

// A PostgreSQL client span. `operation` is the logical statement name, not the
// SQL text — statements can contain user data and must never reach a trace.
[[nodiscard]] ScopedSpan db_scope(std::string operation);

// A Redis / cache client span.
[[nodiscard]] ScopedSpan cache_scope(std::string operation);

// A WebSocket frame-handling span. `event` is the protocol event name.
[[nodiscard]] ScopedSpan ws_scope(std::string event);

// A message-broker publish (kProducer) or delivery (kConsumer) span.
[[nodiscard]] ScopedSpan broker_publish_scope(std::string subject);
[[nodiscard]] ScopedSpan broker_consume_scope(std::string subject);

}  // namespace rtc::tracing

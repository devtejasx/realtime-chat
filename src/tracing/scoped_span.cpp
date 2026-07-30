#include "rtc/tracing/scoped_span.hpp"

#include <utility>

#include "rtc/tracing/tracer.hpp"

namespace rtc::tracing {
namespace {

// Per-thread active span. A raw pointer (never owning) into a SpanContext that
// lives inside a ScopedSpan further up the same thread's stack, so its lifetime
// strictly encloses every use.
thread_local const SpanContext* t_current = nullptr;

}  // namespace

const SpanContext* current_span_context() noexcept {
    return t_current;
}

SpanScope::SpanScope(const SpanContext& context) noexcept {
    if (context.is_valid()) {
        previous_ = t_current;
        t_current = &context;
        active_ = true;
    }
}

SpanScope::~SpanScope() {
    if (active_) {
        t_current = previous_;
    }
}

ScopedSpan::ScopedSpan(Span span) : span_(std::move(span)) {
    if (span_.is_recording()) {
        previous_ = t_current;
        t_current = &span_.context();
        active_ = true;
    }
}

void ScopedSpan::deactivate() noexcept {
    if (active_) {
        t_current = previous_;
        active_ = false;
    }
}

ScopedSpan::~ScopedSpan() {
    // Restore the thread context *before* ending the span, so nothing can
    // observe a dangling pointer while the span is being reported.
    deactivate();
    span_.end();
}

ScopedSpan::ScopedSpan(ScopedSpan&& other) noexcept
    : span_(std::move(other.span_)), previous_(other.previous_), active_(other.active_) {
    other.active_ = false;
    // The active context pointed into the moved-from object's span; repoint it
    // at ours, which now owns that span.
    if (active_) {
        t_current = &span_.context();
    }
}

ScopedSpan& ScopedSpan::operator=(ScopedSpan&& other) noexcept {
    if (this != &other) {
        deactivate();
        span_ = std::move(other.span_);
        previous_ = other.previous_;
        active_ = other.active_;
        other.active_ = false;
        if (active_) {
            t_current = &span_.context();
        }
    }
    return *this;
}

ScopedSpan trace_scope(std::string name, SpanKind kind) {
    return ScopedSpan(tracer().start_child_span(std::move(name), kind));
}

ScopedSpan db_scope(std::string operation) {
    // OpenTelemetry database semantic conventions. db.statement is deliberately
    // never set: SQL text can embed user content and must not leak into traces.
    auto scope = ScopedSpan(tracer().start_child_span("db " + operation, SpanKind::kClient));
    scope.span().set_attribute("db.system", std::string("postgresql"));
    scope.span().set_attribute("db.operation", std::move(operation));
    return scope;
}

ScopedSpan cache_scope(std::string operation) {
    auto scope = ScopedSpan(tracer().start_child_span("cache " + operation, SpanKind::kClient));
    scope.span().set_attribute("db.system", std::string("redis"));
    scope.span().set_attribute("db.operation", std::move(operation));
    return scope;
}

ScopedSpan ws_scope(std::string event) {
    auto scope = ScopedSpan(tracer().start_child_span("ws " + event, SpanKind::kServer));
    scope.span().set_attribute("network.protocol.name", std::string("websocket"));
    scope.span().set_attribute("rtc.ws.event", std::move(event));
    return scope;
}

ScopedSpan broker_publish_scope(std::string subject) {
    auto scope = ScopedSpan(tracer().start_child_span("publish " + subject, SpanKind::kProducer));
    scope.span().set_attribute("messaging.operation", std::string("publish"));
    scope.span().set_attribute("messaging.destination.name", std::move(subject));
    return scope;
}

ScopedSpan broker_consume_scope(std::string subject) {
    auto scope = ScopedSpan(tracer().start_child_span("process " + subject, SpanKind::kConsumer));
    scope.span().set_attribute("messaging.operation", std::string("process"));
    scope.span().set_attribute("messaging.destination.name", std::move(subject));
    return scope;
}

}  // namespace rtc::tracing

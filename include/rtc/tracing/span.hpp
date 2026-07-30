#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "rtc/tracing/trace_context.hpp"

namespace rtc::tracing {

class Tracer;

// OpenTelemetry span kinds. `kServer` for inbound HTTP/WebSocket handling,
// `kClient` for outbound calls we make (PostgreSQL, Redis), `kProducer` /
// `kConsumer` for message-broker publish and delivery, `kInternal` for
// in-process work worth timing.
enum class SpanKind { kInternal, kServer, kClient, kProducer, kConsumer };

[[nodiscard]] constexpr std::string_view to_string(SpanKind kind) noexcept {
    switch (kind) {
        case SpanKind::kInternal:
            return "SPAN_KIND_INTERNAL";
        case SpanKind::kServer:
            return "SPAN_KIND_SERVER";
        case SpanKind::kClient:
            return "SPAN_KIND_CLIENT";
        case SpanKind::kProducer:
            return "SPAN_KIND_PRODUCER";
        case SpanKind::kConsumer:
            return "SPAN_KIND_CONSUMER";
    }
    return "SPAN_KIND_INTERNAL";
}

enum class SpanStatus { kUnset, kOk, kError };

[[nodiscard]] constexpr std::string_view to_string(SpanStatus status) noexcept {
    switch (status) {
        case SpanStatus::kUnset:
            return "STATUS_CODE_UNSET";
        case SpanStatus::kOk:
            return "STATUS_CODE_OK";
        case SpanStatus::kError:
            return "STATUS_CODE_ERROR";
    }
    return "STATUS_CODE_UNSET";
}

// A completed span, ready for export. Deliberately a plain value type: the
// tracer buffers these and the exporter thread serialises them, so nothing here
// may reference per-request state that could outlive the request.
struct SpanData {
    std::string name;
    SpanKind kind = SpanKind::kInternal;
    std::string trace_id;
    std::string span_id;
    std::string parent_span_id;  // empty for a root span
    std::string trace_state;
    bool sampled = true;
    std::chrono::system_clock::time_point start_time{};
    std::chrono::nanoseconds duration{0};
    SpanStatus status = SpanStatus::kUnset;
    std::string status_message;
    // Attribute ordering is preserved for readable exports, and a vector beats a
    // map at these sizes (spans carry a handful of attributes at most).
    std::vector<std::pair<std::string, std::string>> attributes;
};

// An in-flight span.
//
// RAII: the destructor ends the span and hands it to the owning Tracer, so a
// span is recorded on every exit path including an exception unwinding through
// the traced scope. Move-only — a span has exactly one owner, and copying one
// would double-report it.
//
// A default-constructed / moved-from Span is *inert*: every method is a safe
// no-op. That is what lets call sites hold a Span unconditionally without
// branching on whether tracing is enabled.
class Span {
public:
    Span() noexcept = default;
    Span(Tracer& tracer, SpanData data) noexcept;

    ~Span();

    Span(Span&& other) noexcept;
    Span& operator=(Span&& other) noexcept;

    Span(const Span&) = delete;
    Span& operator=(const Span&) = delete;

    // Attribute setters. Following OTel semantic conventions at call sites
    // (http.request.method, db.system, server.address, ...) keeps the traces
    // readable in any compliant backend.
    Span& set_attribute(std::string key, std::string value);
    Span& set_attribute(std::string key, std::int64_t value);
    Span& set_attribute(std::string key, double value);
    Span& set_attribute(std::string key, bool value);

    Span& set_status(SpanStatus status, std::string message = {});

    // Marks the span failed and attaches the exception text. Use in a catch
    // block before rethrowing.
    Span& record_error(std::string_view message);

    // Ends the span early and reports it. Idempotent; the destructor calls this.
    void end();

    [[nodiscard]] const SpanContext& context() const noexcept { return context_; }
    [[nodiscard]] bool is_recording() const noexcept { return tracer_ != nullptr && !ended_; }

private:
    Tracer* tracer_ = nullptr;
    SpanData data_;
    SpanContext context_;
    std::chrono::steady_clock::time_point started_{};
    bool ended_ = true;  // an inert (default-constructed) span is already "ended"
};

}  // namespace rtc::tracing

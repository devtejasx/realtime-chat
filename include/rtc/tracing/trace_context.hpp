#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace rtc::tracing {

// Identifier widths mandated by the W3C Trace Context / OpenTelemetry specs:
// a 16-byte trace id and an 8-byte span id, carried on the wire as lowercase
// hex. Ids are held as strings because every consumer (headers, JSON exporters,
// log lines) wants the hex form; converting once at creation is cheaper than
// formatting at each use.
inline constexpr std::size_t kTraceIdHexLength = 32;
inline constexpr std::size_t kSpanIdHexLength = 16;

// The all-zero ids are explicitly invalid per spec and must never be exported.
inline constexpr std::string_view kInvalidTraceId = "00000000000000000000000000000000";
inline constexpr std::string_view kInvalidSpanId = "0000000000000000";

// Standard propagation headers.
inline constexpr std::string_view kTraceParentHeader = "traceparent";
inline constexpr std::string_view kTraceStateHeader = "tracestate";

// Identifies a span on the wire. Equivalent to OpenTelemetry's SpanContext:
// the (trace_id, span_id) pair plus the sampling decision, which downstream
// services must honour so a trace is sampled consistently end to end.
struct SpanContext {
    std::string trace_id;
    std::string span_id;
    bool sampled = false;
    std::string trace_state;  // opaque vendor state, propagated verbatim

    [[nodiscard]] bool is_valid() const noexcept {
        return trace_id.size() == kTraceIdHexLength && trace_id != kInvalidTraceId &&
               span_id.size() == kSpanIdHexLength && span_id != kInvalidSpanId;
    }
};

// Generates a fresh, spec-valid random trace id (32 hex chars).
[[nodiscard]] std::string new_trace_id();

// Generates a fresh, spec-valid random span id (16 hex chars).
[[nodiscard]] std::string new_span_id();

// Parses a W3C `traceparent` header:
//
//     00-0af7651916cd43dd8448eb211c80319c-b7ad6b7169203331-01
//     ^version ^trace-id                  ^parent-id       ^flags
//
// Returns nullopt when the header is absent, malformed, or carries invalid
// (all-zero) ids — in which case the caller starts a brand-new trace rather
// than trusting partially-parsed input. Future versions (>00) are accepted so
// long as the first four fields are well-formed, as the spec requires.
[[nodiscard]] std::optional<SpanContext> parse_traceparent(std::string_view header);

// Renders a SpanContext as a `traceparent` header value (version 00).
[[nodiscard]] std::string format_traceparent(const SpanContext& context);

}  // namespace rtc::tracing

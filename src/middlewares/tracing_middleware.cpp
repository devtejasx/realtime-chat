#include "rtc/middlewares/tracing_middleware.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <utility>

#include <crow/common.h>

#include "rtc/tracing/tracer.hpp"
#include "rtc/utils/random.hpp"

namespace rtc::middlewares {
namespace {

constexpr const char* kRequestIdHeader = "X-Request-Id";

}  // namespace

void TracingMiddleware::before_handle(crow::request& req, crow::response& /*res*/, context& ctx) {
    // --- request id (always, tracing or not) ---
    const std::string& inbound_id = req.get_header_value(kRequestIdHeader);
    if (inbound_id.empty()) {
        ctx.request_id = utils::generate_hex_token(8);
        // Publish it on the request so LoggingMiddleware — which runs after this
        // one and already honours an inbound value — reuses the same id.
        req.headers.emplace(kRequestIdHeader, ctx.request_id);
    } else {
        ctx.request_id = inbound_id;
    }

    // --- trace context ---
    auto& tracer = tracing::tracer();
    if (!tracer.enabled()) {
        return;
    }

    const std::string& traceparent =
        req.get_header_value(std::string(tracing::kTraceParentHeader));
    std::optional<tracing::SpanContext> parent = tracing::parse_traceparent(traceparent);
    if (parent) {
        parent->trace_state = req.get_header_value(std::string(tracing::kTraceStateHeader));
    }

    auto span = tracer.start_span(std::string(crow::method_name(req.method)) + " " + req.url,
                                 tracing::SpanKind::kServer, parent ? &*parent : nullptr);

    // OpenTelemetry HTTP semantic conventions. The query string is excluded from
    // url.path because it can carry user-supplied values.
    span.set_attribute("http.request.method", std::string(crow::method_name(req.method)));
    span.set_attribute("url.path", req.url);
    span.set_attribute("url.scheme", std::string("http"));
    span.set_attribute("network.protocol.name", std::string("http"));
    span.set_attribute("rtc.request_id", ctx.request_id);
    if (!req.remote_ip_address.empty()) {
        span.set_attribute("client.address", req.remote_ip_address);
    }
    if (const std::string& agent = req.get_header_value("User-Agent"); !agent.empty()) {
        span.set_attribute("user_agent.original", agent);
    }

    if (span.is_recording()) {
        ctx.traceparent = tracing::format_traceparent(span.context());
    }
    // Activating happens here: from this point until after_handle, any
    // db_scope()/cache_scope() on this thread parents itself to this span.
    ctx.span = tracing::ScopedSpan(std::move(span));
}

void TracingMiddleware::after_handle(crow::request& /*req*/, crow::response& res, context& ctx) {
    res.set_header(kRequestIdHeader, ctx.request_id);
    if (!ctx.traceparent.empty()) {
        res.set_header(std::string(tracing::kTraceParentHeader), ctx.traceparent);
    }

    if (ctx.span.span().is_recording()) {
        ctx.span.span().set_attribute("http.response.status_code",
                                      static_cast<std::int64_t>(res.code));
        // Per the HTTP conventions, only 5xx marks the *server* span as failed;
        // a 4xx is a correct server response to a bad request.
        if (res.code >= 500) {
            ctx.span.span().set_status(tracing::SpanStatus::kError,
                                      "HTTP " + std::to_string(res.code));
        } else {
            ctx.span.span().set_status(tracing::SpanStatus::kOk);
        }
    }
    // Ends the span and restores the previous thread context. Doing it here
    // rather than at context destruction keeps the recorded duration bounded to
    // handler work, excluding the outer middlewares' unwinding.
    ctx.span = tracing::ScopedSpan{};
}

}  // namespace rtc::middlewares

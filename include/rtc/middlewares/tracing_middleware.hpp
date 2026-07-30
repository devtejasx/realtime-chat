#pragma once

#include <string>

#include <crow/http_request.h>
#include <crow/http_response.h>

#include "rtc/tracing/scoped_span.hpp"

namespace rtc::middlewares {

// Global middleware opening one SERVER span per HTTP request.
//
// Responsibilities:
//   1. Extract W3C trace context from the inbound `traceparent`/`tracestate`
//      headers so a request arriving from an upstream service continues that
//      trace rather than starting an orphan.
//   2. Open a SERVER span, activate it as the thread's current context (which is
//      what lets repository and cache calls attach themselves without any
//      plumbing), and close it when the handler returns.
//   3. Unify log and trace correlation. The span records the request id, and
//      when the client did not supply `X-Request-Id` the generated one is
//      injected into the request headers — LoggingMiddleware already prefers an
//      inbound value, so both the access log and the trace end up carrying the
//      same id with no change to the logging middleware at all.
//   4. Echo `traceparent` on the response so a caller (or a browser devtools
//      panel) can follow the request into the backend trace.
//
// Entirely inert when tracing is disabled: the span is inert, no context is
// activated, and the only residual work is the request-id handling, which is
// wanted regardless.
//
// Threading: relies on before_handle and after_handle running on the same thread,
// which holds for Crow's synchronous handlers as used throughout this service.
struct TracingMiddleware {
    struct context {
        tracing::ScopedSpan span;
        std::string request_id;
        std::string traceparent;  // rendered response value; empty when not tracing
    };

    void before_handle(crow::request& req, crow::response& res, context& ctx);
    void after_handle(crow::request& req, crow::response& res, context& ctx);
};

}  // namespace rtc::middlewares

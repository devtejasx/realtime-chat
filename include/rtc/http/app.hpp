#pragma once

#include <crow/app.h>

#include "rtc/middlewares/logging_middleware.hpp"
#include "rtc/middlewares/metrics_middleware.hpp"
#include "rtc/middlewares/security_middleware.hpp"
#include "rtc/middlewares/tracing_middleware.hpp"
#include "rtc/middlewares/versioning_middleware.hpp"

namespace rtc::http {

// The concrete Crow application type for this service, with its global
// middleware stack baked in. Controllers accept `App&` and register routes on
// it. Centralising the alias means the middleware list is defined in exactly
// one place.
//
// Order (before_handle runs top-to-bottom, after_handle bottom-to-top):
//
//   1. SecurityMiddleware   — headers/CORS + preflight short-circuit. Outermost
//                             so *every* response, including ones short-circuited
//                             further in, carries the security headers.
//   2. MetricsMiddleware    — request timing and status-class counters.
//   3. TracingMiddleware    — W3C trace context extraction and the server span,
//                             opened after timing starts so the span duration
//                             brackets handler work only.
//   4. LoggingMiddleware    — access log with a request id, correlated with the
//                             trace id captured above.
//   5. ApiVersionMiddleware — innermost: rewrites "/api/v<n>/..." to "/api/..."
//                             immediately before the router matches, and is the
//                             first to unwind so the outer layers still observe
//                             the client-visible path and status.
//
// Adding a middleware here changes no controller: handlers are written against
// `App&` and are agnostic to the stack's contents.
using App = crow::App<middlewares::SecurityMiddleware, middlewares::MetricsMiddleware,
                      middlewares::TracingMiddleware, middlewares::LoggingMiddleware,
                      middlewares::ApiVersionMiddleware>;

}  // namespace rtc::http

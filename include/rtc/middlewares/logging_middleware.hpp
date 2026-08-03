#pragma once

#include <crow/common.h>  // crow::HTTPMethod, crow::method_name
#include <crow/http_request.h>
#include <crow/http_response.h>

#include <chrono>
#include <string>

#include "rtc/logging/log_context.hpp"
#include "rtc/logging/logger.hpp"
#include "rtc/utils/random.hpp"

namespace rtc::middlewares {

// Global Crow middleware that logs every request and its response with a
// per-request id and the wall-clock handling duration.
//
// The request id is taken from an inbound `X-Request-Id` header when present
// (so a correlation id set by an upstream proxy / gateway is preserved), or
// generated otherwise. It is echoed back on the response `X-Request-Id` header.
//
// It is also published to the thread's logging context for the life of the
// request, which is what makes *every* log line emitted while handling it —
// including ones written by services and repositories that know nothing about
// HTTP — carry the same id. Before that, only these two lines did, so an error
// logged three layers down could not be tied back to the request that caused it.
struct LoggingMiddleware {
    struct context {
        std::chrono::steady_clock::time_point start;
        std::string request_id;
    };

    void before_handle(crow::request& req, crow::response& /*res*/, context& ctx) {
        ctx.start = std::chrono::steady_clock::now();
        const std::string& inbound = req.get_header_value("X-Request-Id");
        ctx.request_id = inbound.empty() ? utils::generate_hex_token(8) : inbound;
        // Set rather than scope-guarded: Crow requires this context to be
        // move-assignable, which an RAII guard cannot be. after_handle always
        // runs (Crow unwinds it even when a handler throws), so the id cannot
        // outlive its request and mislabel the next one.
        logging::set_request_id(ctx.request_id);
        // No explicit id in the message: the formatter attaches it to every line
        // now, and repeating it here would print it twice.
        RTC_LOG_INFO("--> {} {}", crow::method_name(req.method), req.url);
    }

    void after_handle(crow::request& req, crow::response& res, context& ctx) {
        // Set here (not in before_handle): a handler returns a fresh response,
        // so headers must be applied after it runs.
        res.set_header("X-Request-Id", ctx.request_id);

        const auto elapsed = std::chrono::steady_clock::now() - ctx.start;
        const auto micros = std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count();
        // 5xx responses are logged at error level so they surface in alerting.
        if (res.code >= 500) {
            RTC_LOG_ERROR(
                "<-- {} {} {} ({} us)", crow::method_name(req.method), req.url, res.code, micros);
        } else {
            RTC_LOG_INFO(
                "<-- {} {} {} ({} us)", crow::method_name(req.method), req.url, res.code, micros);
        }
        logging::clear_request_id();
    }
};

}  // namespace rtc::middlewares

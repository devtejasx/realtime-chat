#pragma once

#include <chrono>

#include <crow/http_request.h>
#include <crow/http_response.h>
#include <crow/utility.h>

#include "rtc/logging/logger.hpp"

namespace rtc::middlewares {

// Global Crow middleware that logs every request and its response, including
// the wall-clock handling duration. Registered once on the application; runs
// before and after each handler.
struct LoggingMiddleware {
    struct context {
        std::chrono::steady_clock::time_point start;
    };

    void before_handle(crow::request& req, crow::response& /*res*/, context& ctx) {
        ctx.start = std::chrono::steady_clock::now();
        RTC_LOG_INFO("--> {} {}", crow::method_name(req.method), req.url);
    }

    void after_handle(crow::request& req, crow::response& res, context& ctx) {
        const auto elapsed = std::chrono::steady_clock::now() - ctx.start;
        const auto micros =
            std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count();
        // 5xx responses are logged at error level so they surface in alerting.
        if (res.code >= 500) {
            RTC_LOG_ERROR("<-- {} {} {} ({} us)", crow::method_name(req.method), req.url,
                          res.code, micros);
        } else {
            RTC_LOG_INFO("<-- {} {} {} ({} us)", crow::method_name(req.method), req.url,
                         res.code, micros);
        }
    }
};

}  // namespace rtc::middlewares

#pragma once

#include <chrono>

#include <crow/http_request.h>
#include <crow/http_response.h>

#include "rtc/metrics/metrics_registry.hpp"

namespace rtc::middlewares {

// Global middleware recording per-request metrics: total requests, request
// duration (feeding average latency), and a coarse status-class counter. The
// registry pointer is injected at startup via set_registry(); until then the
// middleware is inert, so the type is safe to place in the app's middleware
// tuple before wiring.
struct MetricsMiddleware {
    struct context {
        std::chrono::steady_clock::time_point start;
    };

    void set_registry(metrics::MetricsRegistry* registry) { registry_ = registry; }

    void before_handle(crow::request& /*req*/, crow::response& /*res*/, context& ctx) {
        ctx.start = std::chrono::steady_clock::now();
    }

    void after_handle(crow::request& /*req*/, crow::response& res, context& ctx) {
        if (registry_ == nullptr) {
            return;
        }
        const auto elapsed = std::chrono::steady_clock::now() - ctx.start;
        const double seconds = std::chrono::duration<double>(elapsed).count();
        registry_->increment("rtc_http_requests_total");
        registry_->observe("rtc_http_request_seconds", seconds);
        if (res.code >= 500) {
            registry_->increment("rtc_http_5xx_total");
        } else if (res.code >= 400) {
            registry_->increment("rtc_http_4xx_total");
        }
    }

private:
    metrics::MetricsRegistry* registry_ = nullptr;
};

}  // namespace rtc::middlewares

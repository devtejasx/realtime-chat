#pragma once

#include "rtc/http/app.hpp"
#include "rtc/metrics/metrics_registry.hpp"

namespace rtc::controllers {

// Exposes GET /metrics in Prometheus text exposition format. Unauthenticated so
// a Prometheus scraper can collect it; it emits only aggregate measurements,
// never user data. Live gauges (active users, websocket connections, cache hit
// ratio, uptime, memory) are registered as callbacks on the registry by the
// composition root, so this controller only renders.
class MetricsController {
public:
    explicit MetricsController(metrics::MetricsRegistry& registry) noexcept
        : registry_(registry) {}

    void register_routes(http::App& app);

private:
    metrics::MetricsRegistry& registry_;
};

}  // namespace rtc::controllers

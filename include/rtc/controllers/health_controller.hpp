#pragma once

#include "rtc/config/config.hpp"
#include "rtc/http/app.hpp"

namespace rtc::controllers {

// Exposes GET /health — a lightweight liveness endpoint used by load balancers,
// orchestrators and uptime checks. Reports the service name, version and
// deployment environment.
class HealthController {
public:
    explicit HealthController(const config::Config& config) noexcept : config_(config) {}

    void register_routes(http::App& app);

private:
    const config::Config& config_;
};

}  // namespace rtc::controllers

#pragma once

#include "rtc/cache/cache_store.hpp"
#include "rtc/config/config.hpp"
#include "rtc/database/connection_pool.hpp"
#include "rtc/http/app.hpp"

namespace rtc::controllers {

// Health and probe endpoints for load balancers and orchestrators (Kubernetes,
// ECS, systemd). Registers:
//
//   GET /health        — service/version/environment summary (always 200 if up)
//   GET /health/live   — liveness: the process is running (cheap, no deps)
//   GET /health/ready  — readiness: dependencies (DB, cache) are reachable;
//                        returns 503 when a dependency is unavailable, so the
//                        orchestrator stops routing traffic during an outage.
//
// Readiness dependencies are optional and injected after construction via
// set_readiness_dependencies(), keeping the original constructor (and callers)
// unchanged. Without them, /health/ready degrades to a liveness check.
class HealthController {
public:
    explicit HealthController(const config::Config& config) noexcept : config_(config) {}

    // Wires the probes used by /health/ready. Call once at startup.
    void set_readiness_dependencies(database::ConnectionPool& pool,
                                    cache::ICacheStore& cache) noexcept {
        pool_ = &pool;
        cache_ = &cache;
    }

    void register_routes(http::App& app);

private:
    [[nodiscard]] bool database_ready() const;
    [[nodiscard]] bool cache_ready() const;

    const config::Config& config_;
    database::ConnectionPool* pool_ = nullptr;
    cache::ICacheStore* cache_ = nullptr;
};

}  // namespace rtc::controllers

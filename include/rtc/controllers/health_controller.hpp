#pragma once

#include <atomic>

#include "rtc/cache/cache_store.hpp"
#include "rtc/config/config.hpp"
#include "rtc/database/connection_pool.hpp"
#include "rtc/http/app.hpp"
#include "rtc/jobs/background_executor.hpp"
#include "rtc/jobs/periodic_scheduler.hpp"
#include "rtc/realtime/cluster_bus.hpp"

namespace rtc::controllers {

// Health and probe endpoints for load balancers and orchestrators (Kubernetes,
// ECS, systemd). Registers:
//
//   GET /health          — service/version/environment summary (always 200 if up)
//   GET /health/live     — liveness: the process is running (cheap, no deps)
//   GET /health/ready    — readiness: dependencies reachable and workers running
//   GET /health/startup  — startup: bootstrap has completed
//
// The three probes answer genuinely different questions, and conflating them is
// a common source of production outages:
//
//   * **liveness** must never touch a dependency. If it did, a transient database
//     blip would make Kubernetes *restart* every replica — turning a recoverable
//     dependency problem into a full outage. It only reports that the process is
//     responsive.
//   * **readiness** is allowed to fail: it takes the pod out of the load-balancer
//     rotation without killing it, which is exactly right while a dependency is
//     unavailable. It checks PostgreSQL, the cache, the worker pool and the
//     maintenance scheduler.
//   * **startup** covers the slow initial phase (migrations can take a while on a
//     large database). A startupProbe suppresses liveness and readiness until
//     bootstrap finishes, so a slow start is not mistaken for a hung process.
//
// Every dependency is injected after construction, so the original constructor
// and all existing callers are unchanged, and any probe with nothing wired
// degrades gracefully rather than reporting a false failure.
class HealthController {
  public:
    explicit HealthController(const config::Config& config) noexcept : config_(config) {}

    // Wires the data-store probes used by /health/ready.
    void set_readiness_dependencies(database::ConnectionPool& pool,
                                    cache::ICacheStore& cache) noexcept {
        pool_ = &pool;
        cache_ = &cache;
    }

    // Wires the background-work probes: the worker pool must have live threads and
    // the periodic scheduler must be running, otherwise the instance accepts
    // traffic it cannot fully process (notifications and cleanup would stall).
    void set_worker_dependencies(jobs::BackgroundExecutor& executor,
                                 jobs::PeriodicScheduler& scheduler) noexcept {
        executor_ = &executor;
        scheduler_ = &scheduler;
    }

    // Wires the cluster bus so probes can report whether cross-instance fan-out
    // is active. Reported, not required: a single-replica deployment is healthy
    // without it.
    void set_cluster_bus(const realtime::IClusterBus& bus) noexcept { cluster_ = &bus; }

    // Marks bootstrap complete. Until this is called, /health/startup returns 503.
    void mark_started() noexcept { started_.store(true, std::memory_order_release); }

    void register_routes(http::App& app);

  private:
    struct CheckResult {
        bool healthy = true;
        const char* detail = "up";
    };

    [[nodiscard]] CheckResult database_check() const;
    [[nodiscard]] CheckResult cache_check() const;
    [[nodiscard]] CheckResult worker_check() const;
    [[nodiscard]] CheckResult scheduler_check() const;

    const config::Config& config_;
    database::ConnectionPool* pool_ = nullptr;
    cache::ICacheStore* cache_ = nullptr;
    jobs::BackgroundExecutor* executor_ = nullptr;
    jobs::PeriodicScheduler* scheduler_ = nullptr;
    const realtime::IClusterBus* cluster_ = nullptr;
    std::atomic<bool> started_{false};
};

}  // namespace rtc::controllers

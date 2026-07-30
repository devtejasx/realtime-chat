#include "rtc/controllers/health_controller.hpp"

#include <chrono>
#include <exception>

#include <nlohmann/json.hpp>
#include <pqxx/transaction>

#include "rtc/http/response.hpp"
#include "rtc/realtime/protocol.hpp"

namespace rtc::controllers {

HealthController::CheckResult HealthController::database_check() const {
    if (pool_ == nullptr) {
        return {true, "not_configured"};
    }
    try {
        auto lease = pool_->acquire();
        pqxx::work txn(lease.get());
        txn.exec("SELECT 1");
        txn.commit();
        return {true, "up"};
    } catch (const std::exception&) {
        return {false, "down"};
    }
}

HealthController::CheckResult HealthController::cache_check() const {
    if (cache_ == nullptr) {
        return {true, "not_configured"};
    }
    try {
        // A write-then-read round trip, not just a ping: a Redis in a read-only
        // failover state answers PING happily while every write fails, which is
        // exactly the condition this probe needs to catch.
        cache_->set("health:ready", "1", std::chrono::seconds(5));
        return cache_->get("health:ready").has_value() ? CheckResult{true, "up"}
                                                       : CheckResult{false, "read_failed"};
    } catch (const std::exception&) {
        return {false, "down"};
    }
}

HealthController::CheckResult HealthController::worker_check() const {
    if (executor_ == nullptr) {
        return {true, "not_configured"};
    }
    if (!executor_->is_running()) {
        return {false, "stopped"};
    }
    if (executor_->worker_count() == 0) {
        return {false, "no_workers"};
    }
    return {true, "up"};
}

HealthController::CheckResult HealthController::scheduler_check() const {
    if (scheduler_ == nullptr) {
        return {true, "not_configured"};
    }
    return scheduler_->is_running() ? CheckResult{true, "up"} : CheckResult{false, "stopped"};
}

void HealthController::register_routes(http::App& app) {
    CROW_ROUTE(app, "/health")
    ([this]() {
        return http::json_response(200, nlohmann::json{
                                            {"status", "ok"},
                                            {"service", "realtime-chat"},
                                            {"version", RTC_VERSION},
                                            {"environment", config_.app_env},
                                        });
    });

    // Liveness — the process is up. Deliberately dependency-free: a probe that
    // consulted the database would convert a database outage into a restart loop
    // across every replica.
    CROW_ROUTE(app, "/health/live")
    ([]() {
        return http::json_response(200, nlohmann::json{{"status", "alive"}});
    });

    // Startup — has bootstrap finished? Migrations dominate the startup time on a
    // large database, and a startupProbe pointed here keeps liveness/readiness
    // from firing during that window.
    CROW_ROUTE(app, "/health/startup")
    ([this]() {
        const bool started = started_.load(std::memory_order_acquire);
        return http::json_response(started ? 200 : 503,
                                   nlohmann::json{{"status", started ? "started" : "starting"}});
    });

    // Readiness — safe to receive traffic. Fails (503) so the orchestrator stops
    // routing to this instance without restarting it.
    CROW_ROUTE(app, "/health/ready")
    ([this]() {
        const CheckResult database = database_check();
        const CheckResult cache = cache_check();
        const CheckResult workers = worker_check();
        const CheckResult scheduler = scheduler_check();
        const bool started = started_.load(std::memory_order_acquire);

        const bool ready =
            started && database.healthy && cache.healthy && workers.healthy && scheduler.healthy;

        nlohmann::json checks{
            {"database", database.detail},
            {"cache", cache.detail},
            {"workers", workers.detail},
            {"scheduler", scheduler.detail},
            {"bootstrap", started ? "complete" : "pending"},
        };

        nlohmann::json body{
            {"status", ready ? "ready" : "not_ready"},
            {"checks", std::move(checks)},
        };

        // Cluster state is reported but never gates readiness: a single-replica
        // deployment is perfectly healthy without a cluster bus.
        if (cluster_ != nullptr) {
            body["cluster"] = {{"node_id", std::string(cluster_->node_id())},
                               {"distributed", cluster_->is_distributed()}};
        }
        body["websocket_protocol"] = {
            {"default", realtime::protocol::to_number(realtime::protocol::kDefaultVersion)},
            {"current", realtime::protocol::to_number(realtime::protocol::kCurrentVersion)}};

        return http::json_response(ready ? 200 : 503, body);
    });
}

}  // namespace rtc::controllers

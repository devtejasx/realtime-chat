#include "rtc/controllers/health_controller.hpp"

#include <chrono>
#include <exception>

#include <nlohmann/json.hpp>
#include <pqxx/transaction>

#include "rtc/http/response.hpp"

namespace rtc::controllers {

bool HealthController::database_ready() const {
    if (pool_ == nullptr) {
        return true;  // no probe configured; treat as ready
    }
    try {
        auto lease = pool_->acquire();
        pqxx::work txn(lease.get());
        txn.exec("SELECT 1");
        txn.commit();
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

bool HealthController::cache_ready() const {
    if (cache_ == nullptr) {
        return true;
    }
    try {
        cache_->set("health:ready", "1", std::chrono::seconds(5));
        return cache_->get("health:ready").has_value();
    } catch (const std::exception&) {
        return false;
    }
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

    // Liveness — the process is up; deliberately dependency-free and fast.
    CROW_ROUTE(app, "/health/live")
    ([]() { return http::json_response(200, nlohmann::json{{"status", "alive"}}); });

    // Readiness — safe to receive traffic only when dependencies respond.
    CROW_ROUTE(app, "/health/ready")
    ([this]() {
        const bool db = database_ready();
        const bool cache = cache_ready();
        const bool ready = db && cache;
        nlohmann::json body{
            {"status", ready ? "ready" : "not_ready"},
            {"checks", {{"database", db ? "up" : "down"}, {"cache", cache ? "up" : "down"}}},
        };
        return http::json_response(ready ? 200 : 503, body);
    });
}

}  // namespace rtc::controllers

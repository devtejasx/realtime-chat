#include "rtc/controllers/health_controller.hpp"

#include <nlohmann/json.hpp>

#include "rtc/http/response.hpp"

namespace rtc::controllers {

void HealthController::register_routes(http::App& app) {
    CROW_ROUTE(app, "/health")
    ([this]() {
        const nlohmann::json body{
            {"status", "ok"},
            {"service", "realtime-chat"},
            {"version", RTC_VERSION},
            {"environment", config_.app_env},
        };
        return http::json_response(200, body);
    });
}

}  // namespace rtc::controllers

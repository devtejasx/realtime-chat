#include "rtc/controllers/metrics_controller.hpp"

#include "rtc/http/guard.hpp"

namespace rtc::controllers {

void MetricsController::register_routes(http::App& app) {
    CROW_ROUTE(app, "/metrics")
    ([this]() {
        return http::run_guarded([&] {
            crow::response response(200, registry_.render_prometheus());
            response.set_header("Content-Type", "text/plain; version=0.0.4");
            return response;
        });
    });
}

}  // namespace rtc::controllers

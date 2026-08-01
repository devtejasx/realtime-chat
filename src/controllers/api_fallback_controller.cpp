#include "rtc/controllers/api_fallback_controller.hpp"

#include <string>

#include "rtc/errors/error_response.hpp"
#include "rtc/errors/error_type.hpp"
#include "rtc/http/api_version.hpp"

namespace rtc::controllers {
namespace {

[[nodiscard]] crow::response not_found_for(const std::string& path) {
    crow::response response(errors::http_status_for(errors::ErrorType::kNotFound));
    response.set_header("Content-Type", "application/json");
    response.body = errors::make_error_body("not_found",
                                            "No such endpoint: " + path,
                                            "supported=" + http::supported_api_versions_label())
                        .dump();
    return response;
}

}  // namespace

void ApiFallbackController::register_routes(http::App& app) {
    // Matches any path under an explicitly versioned prefix. Reached only when
    // no concrete route claimed the request first — Crow keeps the lowest
    // matching rule index, and the composition root registers this last.
    //
    // An unsupported version never arrives here: ApiVersionMiddleware ends the
    // request during before_handle. So by construction this responds only for a
    // *supported* version pointed at a path that does not exist, which is an
    // ordinary 404 — just one that carries the project's error envelope and the
    // usual security/CORS headers rather than Crow's bare default.
    CROW_ROUTE(app, "/api/v<int>/<path>")
        .methods(crow::HTTPMethod::Get,
                 crow::HTTPMethod::Post,
                 crow::HTTPMethod::Put,
                 crow::HTTPMethod::Patch,
                 crow::HTTPMethod::Delete,
                 crow::HTTPMethod::Head,
                 crow::HTTPMethod::Options)([](const crow::request& req, int, const std::string&) {
            return not_found_for(req.url);
        });

    // "/api/v<n>" with no trailing segment: same treatment, since the pattern
    // above requires a non-empty tail.
    CROW_ROUTE(app, "/api/v<int>")
        .methods(crow::HTTPMethod::Get,
                 crow::HTTPMethod::Post,
                 crow::HTTPMethod::Put,
                 crow::HTTPMethod::Patch,
                 crow::HTTPMethod::Delete,
                 crow::HTTPMethod::Head,
                 crow::HTTPMethod::Options)(
            [](const crow::request& req, int) { return not_found_for(req.url); });
}

}  // namespace rtc::controllers

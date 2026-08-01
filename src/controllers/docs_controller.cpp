#include "rtc/controllers/docs_controller.hpp"

#include <nlohmann/json.hpp>
#include <string>

#include "rtc/docs/openapi.hpp"
#include "rtc/errors/error_response.hpp"
#include "rtc/http/api_version.hpp"
#include "rtc/http/guard.hpp"
#include "rtc/http/response.hpp"
#include "rtc/http/route_registrar.hpp"
#include "rtc/utils/env.hpp"

namespace rtc::controllers {
namespace {

// Documentation endpoints are on unless explicitly switched off. Read once at
// route-registration time: this is a deployment decision, not something to
// re-evaluate per request.
[[nodiscard]] bool docs_enabled() {
    const std::string raw = utils::get_env_or("RTC_DOCS_ENABLED", "true");
    return !(raw == "0" || raw == "false" || raw == "no" || raw == "off");
}

[[nodiscard]] crow::response not_found() {
    crow::response response(
        404, errors::make_error_body("not_found", "Documentation endpoints are disabled").dump());
    response.set_header("Content-Type", "application/json");
    return response;
}

}  // namespace

std::string DocsController::origin_of(const crow::request& req) {
    // Behind nginx the request arrives over plain HTTP on an internal port, so the
    // scheme and host the *client* used are only knowable from the forwarded
    // headers. Falling back to the Host header keeps direct access working.
    std::string scheme = req.get_header_value("X-Forwarded-Proto");
    if (scheme.empty()) {
        scheme = "http";
    }
    std::string host = req.get_header_value("X-Forwarded-Host");
    if (host.empty()) {
        host = req.get_header_value("Host");
    }
    if (host.empty()) {
        return {};  // no usable origin; the spec keeps its default server entry
    }
    return scheme + "://" + host;
}

void DocsController::register_routes(http::App& app) {
    const bool enabled = docs_enabled();

    const auto serve_spec = [enabled](const crow::request& req) {
        if (!enabled) {
            return not_found();
        }
        return http::run_guarded([&]() -> crow::response {
            crow::response response(200, docs::openapi_json_for(origin_of(req)));
            response.set_header("Content-Type", "application/json");
            // The spec changes only with the binary, so it is safely cacheable —
            // and Swagger UI fetches it on every page load.
            response.set_header("Cache-Control", "public, max-age=300");
            response.set_header(std::string(http::kApiVersionHeader),
                                http::api_version_label(http::kCurrentApiVersion));
            return response;
        });
    };

    // The canonical, unversioned location: the document describes every version
    // this build serves, so it is not itself version-scoped.
    CROW_ROUTE(app, "/openapi.json").methods(crow::HTTPMethod::Get)(serve_spec);

    // Also served under the API prefixes, so a client that discovers the service
    // through /api/v1 finds the spec where it expects it rather than having to
    // know that /openapi.json sits outside the namespace.
    RTC_API_ROUTE(app, "/openapi.json").methods(crow::HTTPMethod::Get)(serve_spec);

    CROW_ROUTE(app, "/docs").methods(crow::HTTPMethod::Get)([enabled]() {
        if (!enabled) {
            return not_found();
        }
        crow::response response(200, docs::swagger_ui_html("/openapi.json"));
        response.set_header("Content-Type", "text/html; charset=utf-8");
        // The global security middleware sets `default-src 'none'`, which would
        // block the viewer's own assets. after_handle would overwrite whatever
        // is set here, so SecurityMiddleware is told to leave this response
        // alone via the opt-out header below, which it strips before sending.
        response.set_header("X-RTC-CSP-Override", std::string(docs::swagger_ui_csp()));
        return response;
    });
}

}  // namespace rtc::controllers

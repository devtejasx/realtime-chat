#include "rtc/controllers/auth_controller.hpp"

#include <nlohmann/json.hpp>

#include "rtc/dto/auth_dto.hpp"
#include "rtc/dto/user_dto.hpp"
#include "rtc/errors/exceptions.hpp"
#include "rtc/http/guard.hpp"
#include "rtc/http/response.hpp"

namespace rtc::controllers {
namespace {

// Parses a request body as JSON, translating malformed input into a 400
// ValidationException rather than letting nlohmann throw an opaque error.
[[nodiscard]] nlohmann::json parse_json_body(const crow::request& req) {
    nlohmann::json parsed = nlohmann::json::parse(req.body, /*cb=*/nullptr,
                                                  /*allow_exceptions=*/false);
    if (parsed.is_discarded()) {
        throw rtc::errors::ValidationException("Request body is not valid JSON");
    }
    return parsed;
}

}  // namespace

void AuthController::register_routes(http::App& app) {
    CROW_ROUTE(app, "/api/auth/register")
        .methods(crow::HTTPMethod::Post)([this](const crow::request& req) {
            return http::run_guarded([&] {
                const auto body = parse_json_body(req);
                auto request = dto::RegisterRequest::from_json(body);
                const auto response = auth_service_.register_user(std::move(request));
                return http::json_response(201, response.to_json());
            });
        });

    CROW_ROUTE(app, "/api/auth/login")
        .methods(crow::HTTPMethod::Post)([this](const crow::request& req) {
            return http::run_guarded([&] {
                const auto body = parse_json_body(req);
                auto request = dto::LoginRequest::from_json(body);
                const auto response = auth_service_.login(std::move(request));
                return http::json_response(200, response.to_json());
            });
        });

    // Protected route: requires a valid access token. Demonstrates the JWT
    // guard returning 401 for missing/invalid credentials.
    CROW_ROUTE(app, "/api/auth/me")
        .methods(crow::HTTPMethod::Get)([this](const crow::request& req) {
            return http::run_guarded([&] {
                const auto claims = auth_guard_.authenticate(req);
                const auto user = user_service_.get_by_id(claims.user_id);
                return http::json_response(200, dto::UserResponse::from(user).to_json());
            });
        });
}

}  // namespace rtc::controllers

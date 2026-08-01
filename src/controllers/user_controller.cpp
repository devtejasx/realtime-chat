#include "rtc/controllers/user_controller.hpp"

#include <cstdint>

#include "rtc/dto/profile_dto.hpp"
#include "rtc/dto/user_dto.hpp"
#include "rtc/http/guard.hpp"
#include "rtc/http/json_body.hpp"
#include "rtc/http/response.hpp"
#include "rtc/http/route_registrar.hpp"

namespace rtc::controllers {

void UserController::register_routes(http::App& app) {
    // GET /api/users/me — the authenticated user's own profile.
    RTC_API_ROUTE(app, "/users/me")
        .methods(crow::HTTPMethod::Get)([this](const crow::request& req) {
            return http::run_guarded([&] {
                const auto claims = auth_guard_.authenticate(req);
                const auto user = user_service_.get_by_id(claims.user_id);
                return http::json_response(200, dto::UserResponse::from(user).to_json());
            });
        });

    // PUT /api/users/me — partial profile update.
    RTC_API_ROUTE(app, "/users/me")
        .methods(crow::HTTPMethod::Put)([this](const crow::request& req) {
            return http::run_guarded([&] {
                const auto claims = auth_guard_.authenticate(req);
                auto request = dto::UpdateProfileRequest::from_json(http::parse_json_body(req));
                const auto user = user_service_.update_profile(claims.user_id, std::move(request));
                return http::json_response(200, dto::UserResponse::from(user).to_json());
            });
        });

    // GET /api/users/<id> — another user's public profile (email withheld).
    RTC_API_ROUTE(app, "/users/<int>")
        .methods(crow::HTTPMethod::Get)([this](const crow::request& req, std::int64_t id) {
            return http::run_guarded([&] {
                (void) auth_guard_.authenticate(req);  // any authenticated user may view
                const auto user = user_service_.get_by_id(id);
                return http::json_response(200, dto::UserResponse::from(user).to_public_json());
            });
        });
}

}  // namespace rtc::controllers

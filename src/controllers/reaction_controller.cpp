#include "rtc/controllers/reaction_controller.hpp"

#include <cstdint>
#include <nlohmann/json.hpp>

#include "rtc/dto/reaction_dto.hpp"
#include "rtc/http/guard.hpp"
#include "rtc/http/json_body.hpp"
#include "rtc/http/response.hpp"
#include "rtc/http/route_registrar.hpp"

namespace rtc::controllers {

void ReactionController::register_routes(http::App& app) {
    RTC_API_ROUTE(app, "/messages/<int>/reactions")
        .methods(crow::HTTPMethod::Post)([this](const crow::request& req, std::int64_t id) {
            return http::run_guarded([&] {
                const auto claims = auth_guard_.authenticate(req);
                const auto request = dto::ReactionRequest::from_json(http::parse_json_body(req));
                const auto reaction = reactions_.react(claims.user_id, id, request.emoji);
                return http::json_response(201, dto::ReactionResponse::from(reaction).to_json());
            });
        });

    RTC_API_ROUTE(app, "/messages/<int>/reactions")
        .methods(crow::HTTPMethod::Delete)([this](const crow::request& req, std::int64_t id) {
            return http::run_guarded([&] {
                const auto claims = auth_guard_.authenticate(req);
                reactions_.unreact(claims.user_id, id);
                return http::json_response(200, nlohmann::json{{"removed", true}});
            });
        });

    RTC_API_ROUTE(app, "/messages/<int>/reactions")
        .methods(crow::HTTPMethod::Get)([this](const crow::request& req, std::int64_t id) {
            return http::run_guarded([&] {
                const auto claims = auth_guard_.authenticate(req);
                const auto reactions = reactions_.list(claims.user_id, id);
                nlohmann::json items = nlohmann::json::array();
                for (const auto& reaction : reactions) {
                    items.push_back(dto::ReactionResponse::from(reaction).to_json());
                }
                return http::json_response(200, nlohmann::json{{"reactions", items}});
            });
        });
}

}  // namespace rtc::controllers

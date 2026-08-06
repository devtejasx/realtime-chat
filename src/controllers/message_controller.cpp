#include "rtc/controllers/message_controller.hpp"

#include <chrono>
#include <cstdint>
#include <nlohmann/json.hpp>
#include <string>

#include "rtc/dto/message_dto.hpp"
#include "rtc/dto/pagination.hpp"
#include "rtc/http/guard.hpp"
#include "rtc/http/json_body.hpp"
#include "rtc/http/response.hpp"
#include "rtc/http/route_registrar.hpp"

namespace rtc::controllers {

void MessageController::register_routes(http::App& app) {
    RTC_API_ROUTE(app, "/messages")
        .methods(crow::HTTPMethod::Post)([this](const crow::request& req) {
            return http::run_guarded([&] {
                const auto claims = auth_guard_.authenticate(req);
                // Scoped by user id: send is authenticated, and the budget belongs
                // to the account rather than to whatever address it dialled from.
                rate_limiter_.enforce("message",
                                      std::to_string(claims.user_id),
                                      config_.rate_limit_message_max,
                                      std::chrono::seconds(config_.rate_limit_window_seconds));

                const auto request = dto::SendMessageRequest::from_json(http::parse_json_body(req));
                const auto message = messages_.send(claims.user_id, request);
                return http::json_response(201, messages_.to_response(message).to_json());
            });
        });

    RTC_API_ROUTE(app, "/messages")
        .methods(crow::HTTPMethod::Get)([this](const crow::request& req) {
            return http::run_guarded([&] {
                const auto claims = auth_guard_.authenticate(req);
                const auto query = dto::MessageQuery::from_request(req);
                const auto page = dto::Pagination::from_request(req);
                const auto messages = messages_.list(claims.user_id, query, page);
                nlohmann::json items = nlohmann::json::array();
                for (const auto& message : messages) {
                    items.push_back(messages_.to_response(message).to_json());
                }
                return http::json_response(200, nlohmann::json{{"messages", items}});
            });
        });

    RTC_API_ROUTE(app, "/messages/<int>")
        .methods(crow::HTTPMethod::Patch)([this](const crow::request& req, std::int64_t id) {
            return http::run_guarded([&] {
                const auto claims = auth_guard_.authenticate(req);
                const auto request =
                    dto::UpdateMessageRequest::from_json(http::parse_json_body(req));
                const auto message = messages_.edit(claims.user_id, id, request);
                return http::json_response(200, messages_.to_response(message).to_json());
            });
        });

    RTC_API_ROUTE(app, "/messages/<int>")
        .methods(crow::HTTPMethod::Delete)([this](const crow::request& req, std::int64_t id) {
            return http::run_guarded([&] {
                const auto claims = auth_guard_.authenticate(req);
                const auto message = messages_.remove(claims.user_id, id);
                return http::json_response(200, messages_.to_response(message).to_json());
            });
        });
}

}  // namespace rtc::controllers

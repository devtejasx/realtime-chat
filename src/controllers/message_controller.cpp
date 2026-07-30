#include "rtc/controllers/message_controller.hpp"

#include <cstdint>
#include <nlohmann/json.hpp>

#include "rtc/dto/message_dto.hpp"
#include "rtc/dto/pagination.hpp"
#include "rtc/http/guard.hpp"
#include "rtc/http/json_body.hpp"
#include "rtc/http/response.hpp"

namespace rtc::controllers {

void MessageController::register_routes(http::App& app) {
    CROW_ROUTE(app, "/api/messages")
        .methods(crow::HTTPMethod::Post)([this](const crow::request& req) {
            return http::run_guarded([&] {
                const auto claims = auth_guard_.authenticate(req);
                const auto request = dto::SendMessageRequest::from_json(http::parse_json_body(req));
                const auto message = messages_.send(claims.user_id, request);
                return http::json_response(201, messages_.to_response(message).to_json());
            });
        });

    CROW_ROUTE(app, "/api/messages")
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

    CROW_ROUTE(app, "/api/messages/<int>")
        .methods(crow::HTTPMethod::Patch)([this](const crow::request& req, std::int64_t id) {
            return http::run_guarded([&] {
                const auto claims = auth_guard_.authenticate(req);
                const auto request =
                    dto::UpdateMessageRequest::from_json(http::parse_json_body(req));
                const auto message = messages_.edit(claims.user_id, id, request);
                return http::json_response(200, messages_.to_response(message).to_json());
            });
        });

    CROW_ROUTE(app, "/api/messages/<int>")
        .methods(crow::HTTPMethod::Delete)([this](const crow::request& req, std::int64_t id) {
            return http::run_guarded([&] {
                const auto claims = auth_guard_.authenticate(req);
                const auto message = messages_.remove(claims.user_id, id);
                return http::json_response(200, messages_.to_response(message).to_json());
            });
        });
}

}  // namespace rtc::controllers

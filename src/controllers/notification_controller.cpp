#include "rtc/controllers/notification_controller.hpp"

#include <cstdint>
#include <nlohmann/json.hpp>
#include <string>

#include "rtc/dto/notification_dto.hpp"
#include "rtc/dto/pagination.hpp"
#include "rtc/http/guard.hpp"
#include "rtc/http/response.hpp"

namespace rtc::controllers {

void NotificationController::register_routes(http::App& app) {
    CROW_ROUTE(app, "/api/notifications")
        .methods(crow::HTTPMethod::Get)([this](const crow::request& req) {
            return http::run_guarded([&] {
                const auto claims = auth_guard_.authenticate(req);
                const auto page = dto::Pagination::from_request(req);
                const char* unread = req.url_params.get("unread");
                const bool unread_only = unread != nullptr && std::string(unread) == "true";

                const auto notifications = notifications_.list(claims.user_id, page, unread_only);
                nlohmann::json items = nlohmann::json::array();
                for (const auto& notification : notifications) {
                    items.push_back(dto::NotificationResponse::from(notification).to_json());
                }
                return http::json_response(
                    200,
                    nlohmann::json{{"notifications", items},
                                   {"unread_count", notifications_.unread_count(claims.user_id)}});
            });
        });

    CROW_ROUTE(app, "/api/notifications/<int>/read")
        .methods(crow::HTTPMethod::Post)([this](const crow::request& req, std::int64_t id) {
            return http::run_guarded([&] {
                const auto claims = auth_guard_.authenticate(req);
                const bool marked = notifications_.mark_read(id, claims.user_id);
                return http::json_response(200, nlohmann::json{{"read", marked}});
            });
        });

    CROW_ROUTE(app, "/api/notifications/read-all")
        .methods(crow::HTTPMethod::Post)([this](const crow::request& req) {
            return http::run_guarded([&] {
                const auto claims = auth_guard_.authenticate(req);
                const auto count = notifications_.mark_all_read(claims.user_id);
                return http::json_response(200, nlohmann::json{{"marked", count}});
            });
        });

    CROW_ROUTE(app, "/api/notifications/<int>")
        .methods(crow::HTTPMethod::Delete)([this](const crow::request& req, std::int64_t id) {
            return http::run_guarded([&] {
                const auto claims = auth_guard_.authenticate(req);
                const bool deleted = notifications_.remove(id, claims.user_id);
                return http::json_response(200, nlohmann::json{{"deleted", deleted}});
            });
        });
}

}  // namespace rtc::controllers

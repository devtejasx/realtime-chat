#include "rtc/controllers/session_controller.hpp"

#include <nlohmann/json.hpp>
#include <string>

#include "rtc/dto/session_dto.hpp"
#include "rtc/http/guard.hpp"
#include "rtc/http/response.hpp"

namespace rtc::controllers {

void SessionController::register_routes(http::App& app) {
    CROW_ROUTE(app, "/api/sessions")
        .methods(crow::HTTPMethod::Get)([this](const crow::request& req) {
            return http::run_guarded([&] {
                const auto claims = auth_guard_.authenticate(req);
                // Optionally mark which session is the caller's current one.
                const char* current = req.url_params.get("current");
                const std::string current_id = current != nullptr ? current : std::string{};

                nlohmann::json items = nlohmann::json::array();
                for (const auto& session : sessions_.list(claims.user_id)) {
                    items.push_back(dto::SessionResponse::from(session, current_id).to_json());
                }
                return http::json_response(200, nlohmann::json{{"sessions", items}});
            });
        });

    CROW_ROUTE(app, "/api/sessions/<string>")
        .methods(crow::HTTPMethod::Delete)([this](const crow::request& req, const std::string& id) {
            return http::run_guarded([&] {
                const auto claims = auth_guard_.authenticate(req);
                const bool revoked = sessions_.revoke(claims.user_id, id);
                return http::json_response(200, nlohmann::json{{"revoked", revoked}});
            });
        });
}

}  // namespace rtc::controllers

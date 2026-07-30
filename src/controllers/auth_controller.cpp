#include "rtc/controllers/auth_controller.hpp"

#include <optional>
#include <string>
#include <utility>

#include <nlohmann/json.hpp>

#include "rtc/dto/auth_dto.hpp"
#include "rtc/dto/user_dto.hpp"
#include "rtc/errors/exceptions.hpp"
#include "rtc/events/event_types.hpp"
#include "rtc/http/guard.hpp"
#include "rtc/http/json_body.hpp"
#include "rtc/http/response.hpp"

namespace rtc::controllers {
namespace {

[[nodiscard]] std::optional<std::string> non_empty(std::string value) {
    if (value.empty()) return std::nullopt;
    return value;
}

// Extracts a required string field from a JSON body.
[[nodiscard]] std::string require_string(const nlohmann::json& body, const char* field) {
    const auto it = body.find(field);
    if (it == body.end() || !it->is_string()) {
        throw errors::ValidationException(std::string("Missing string field: ") + field,
                                          std::string("field=") + field);
    }
    return it->get<std::string>();
}

}  // namespace

events::IEventPublisher& AuthController::publisher() const noexcept {
    return publisher_ != nullptr ? *publisher_ : events::NullEventPublisher::instance();
}

void AuthController::register_routes(http::App& app) {
    // Records a session for a freshly authenticated user and returns the augmented
    // response JSON (adds "session_id").
    const auto with_session = [this](const crow::request& req, const dto::AuthResponse& response) {
        const std::string session_id = session_service_.record(
            response.user.id, response.tokens.refresh_token,
            non_empty(req.get_header_value("User-Agent")), non_empty(req.remote_ip_address));
        auto json = response.to_json();
        json["session_id"] = session_id;
        return json;
    };

    CROW_ROUTE(app, "/api/auth/register")
        .methods(crow::HTTPMethod::Post)([this, with_session](const crow::request& req) {
            return http::run_guarded([&] {
                auto request = dto::RegisterRequest::from_json(http::parse_json_body(req));
                const auto response = auth_service_.register_user(std::move(request));
                auto body = with_session(req, response);
                publisher().publish(events::UserRegistered{
                    .user_id = response.user.id,
                    .username = response.user.username,
                    .email = response.user.email,
                }.to_event());
                return http::json_response(201, std::move(body));
            });
        });

    CROW_ROUTE(app, "/api/auth/login")
        .methods(crow::HTTPMethod::Post)([this, with_session](const crow::request& req) {
            return http::run_guarded([&] {
                auto request = dto::LoginRequest::from_json(http::parse_json_body(req));
                const auto response = auth_service_.login(std::move(request));
                auto body = with_session(req, response);
                // ip / user_agent are the fields a security review actually reads
                // on a sign-in record, and they only exist at this boundary.
                publisher().publish(events::UserLoggedIn{
                    .user_id = response.user.id,
                    .username = response.user.username,
                    .ip = req.remote_ip_address,
                    .user_agent = req.get_header_value("User-Agent"),
                }.to_event());
                return http::json_response(200, std::move(body));
            });
        });

    // Rotate tokens using a refresh token + session id. Public (the refresh
    // token is the credential).
    CROW_ROUTE(app, "/api/auth/refresh")
        .methods(crow::HTTPMethod::Post)([this](const crow::request& req) {
            return http::run_guarded([&] {
                const auto body = http::parse_json_body(req);
                const std::string refresh_token = require_string(body, "refresh_token");
                const std::string session_id = require_string(body, "session_id");
                const auto pair = session_service_.rotate(refresh_token, session_id);
                return http::json_response(
                    200, nlohmann::json{{"access_token", pair.access_token},
                                        {"refresh_token", pair.refresh_token},
                                        {"token_type", "Bearer"},
                                        {"access_expires_in", pair.access_expires_in_seconds},
                                        {"refresh_expires_in", pair.refresh_expires_in_seconds}});
            });
        });

    // Revoke the current session (client passes its session_id).
    CROW_ROUTE(app, "/api/auth/logout")
        .methods(crow::HTTPMethod::Post)([this](const crow::request& req) {
            return http::run_guarded([&] {
                // authenticate_token_only: a suspended account must still be able
                // to sign out cleanly. Blocking logout for a banned user would
                // leave their refresh token alive and give them no way to
                // invalidate it themselves.
                const auto claims = auth_guard_.authenticate_token_only(req);
                const auto body = http::parse_json_body(req);
                const std::string session_id = require_string(body, "session_id");
                const bool revoked = session_service_.revoke(claims.user_id, session_id);
                if (revoked) {
                    publisher().publish(events::UserLoggedOut{
                        .user_id = claims.user_id,
                        .session_id = session_id,
                        .all_sessions = false,
                    }.to_event());
                }
                return http::json_response(200, nlohmann::json{{"revoked", revoked}});
            });
        });

    // Revoke every session for the user (logout from all devices).
    CROW_ROUTE(app, "/api/auth/logout-all")
        .methods(crow::HTTPMethod::Post)([this](const crow::request& req) {
            return http::run_guarded([&] {
                const auto claims = auth_guard_.authenticate_token_only(req);
                const auto count = session_service_.revoke_all(claims.user_id);
                publisher().publish(events::UserLoggedOut{
                    .user_id = claims.user_id,
                    .all_sessions = true,
                }.to_event());
                return http::json_response(200, nlohmann::json{{"revoked", count}});
            });
        });

    CROW_ROUTE(app, "/api/auth/me")
        .methods(crow::HTTPMethod::Get)([this](const crow::request& req) {
            return http::run_guarded([&] {
                // Readable while suspended, so a client can explain *why* the
                // rest of the API is refusing it.
                const auto claims = auth_guard_.authenticate_token_only(req);
                const auto user = user_service_.get_by_id(claims.user_id);
                return http::json_response(200, dto::UserResponse::from(user).to_json());
            });
        });
}

}  // namespace rtc::controllers

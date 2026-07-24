#include "rtc/controllers/websocket_controller.hpp"

#include <memory>
#include <optional>
#include <string>

#include "rtc/logging/logger.hpp"
#include "rtc/middlewares/auth_middleware.hpp"
#include "rtc/realtime/session.hpp"
#include "rtc/security/token.hpp"

namespace rtc::controllers {

bool WebSocketController::authenticate(const crow::request& req, void** userdata) const {
    // Prefer the query parameter (browsers can't set headers on a WS upgrade),
    // falling back to a standard Authorization: Bearer header.
    std::optional<std::string> token;
    if (const char* q = req.url_params.get("token"); q != nullptr) {
        token = std::string(q);
    } else {
        token = middlewares::AuthMiddleware::extract_bearer_token(req);
    }
    if (!token) {
        return false;
    }
    try {
        const auto claims = token_service_.verify(*token, security::TokenType::kAccess);
        *userdata = new realtime::AuthContext{claims.user_id, claims.username};
        return true;
    } catch (const std::exception&) {
        return false;  // reject the upgrade on any verification failure
    }
}

void WebSocketController::register_routes(http::App& app) {
    CROW_WEBSOCKET_ROUTE(app, "/ws")
        .onaccept([this](const crow::request& req, void** userdata) {
            return authenticate(req, userdata);
        })
        .onopen([this](crow::websocket::connection& conn) {
            auto* ctx = static_cast<realtime::AuthContext*>(conn.userdata());
            if (ctx == nullptr) {
                conn.close("unauthorized");
                return;
            }
            dispatcher_.on_open(conn, ctx->user_id, ctx->username);
        })
        .onmessage([this](crow::websocket::connection& conn, const std::string& data,
                          bool is_binary) {
            if (is_binary) {
                return;  // this protocol is text/JSON only
            }
            dispatcher_.on_message(conn, data);
        })
        .onclose([this](crow::websocket::connection& conn, const std::string& reason) {
            dispatcher_.on_close(conn);
            auto* ctx = static_cast<realtime::AuthContext*>(conn.userdata());
            delete ctx;  // release the identity allocated in onaccept
            conn.userdata(nullptr);
            RTC_LOG_DEBUG("ws closed: {}", reason);
        })
        .onerror([](crow::websocket::connection&, const std::string& reason) {
            RTC_LOG_WARN("ws error: {}", reason);
        });
}

}  // namespace rtc::controllers

#include "rtc/controllers/websocket_controller.hpp"

#include <charconv>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "rtc/logging/logger.hpp"
#include "rtc/middlewares/auth_middleware.hpp"
#include "rtc/realtime/protocol.hpp"
#include "rtc/realtime/session.hpp"
#include "rtc/security/token.hpp"

namespace rtc::controllers {
namespace {

// Reads the requested wire protocol from `?protocol=<n>` on the upgrade request.
// The handshake is the only point at which the query string is available, so the
// decision has to be made here and carried on the connection's userdata.
//
// Anything absent or unparseable yields nullopt, which protocol::negotiate maps
// to the default (v1) — a client must never be denied a connection over this.
[[nodiscard]] std::optional<int> requested_protocol(const crow::request& req) {
    const char* raw = req.url_params.get(std::string(realtime::protocol::kVersionQueryParam));
    if (raw == nullptr) {
        return std::nullopt;
    }
    const std::string_view text(raw);
    int value = 0;
    const auto [ptr, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (ec != std::errc{} || ptr != text.data() + text.size()) {
        return std::nullopt;
    }
    return value;
}

}  // namespace

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
        *userdata = new realtime::AuthContext{
            claims.user_id, claims.username,
            realtime::protocol::negotiate(requested_protocol(req))};
        return true;
    } catch (const std::exception&) {
        return false;  // reject the upgrade on any verification failure
    }
}

void WebSocketController::register_routes(http::App& app) {
    // Registered at both "/ws" and "/api/v1/ws".
    //
    // WebSocket upgrades bypass the global middleware chain in Crow, so
    // ApiVersionMiddleware's path rewrite never runs for them — the versioned
    // path has to be a real route. Both share one handler set via
    // configure_route, so there is a single implementation and no drift.
    configure_route(CROW_WEBSOCKET_ROUTE(app, "/ws"));
    configure_route(CROW_WEBSOCKET_ROUTE(app, "/api/v1/ws"));
}

template <typename Rule>
void WebSocketController::configure_route(Rule& rule) {
    rule.onaccept([this](const crow::request& req, void** userdata) {
            return authenticate(req, userdata);
        })
        .onopen([this](crow::websocket::connection& conn) {
            auto* ctx = static_cast<realtime::AuthContext*>(conn.userdata());
            if (ctx == nullptr) {
                conn.close("unauthorized");
                return;
            }
            dispatcher_.on_open(conn, ctx->user_id, ctx->username, ctx->protocol_version);
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

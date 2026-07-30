#pragma once

#include "rtc/http/app.hpp"
#include "rtc/realtime/event_dispatcher.hpp"
#include "rtc/security/token_service.hpp"

namespace rtc::controllers {

// Registers the WebSocket endpoints (`/ws` and `/api/v1/ws`) on the Crow
// application and bridges Crow's callbacks to the EventDispatcher.
//
// Authentication happens during the handshake (onaccept): the JWT access token
// is taken from the `token` query parameter or the Authorization header and
// verified before the socket is accepted, so unauthenticated peers are rejected
// with a failed upgrade. The negotiated wire protocol version (see
// rtc/realtime/protocol.hpp) is decided at the same moment, because the upgrade
// request is the only place the query string is visible. Both are stored as the
// connection's userdata for the lifetime of the session.
class WebSocketController {
public:
    WebSocketController(const security::ITokenService& token_service,
                        realtime::EventDispatcher& dispatcher) noexcept
        : token_service_(token_service), dispatcher_(dispatcher) {}

    void register_routes(http::App& app);

private:
    // Verifies the handshake and, on success, allocates an AuthContext into
    // *userdata. Returns false to reject the upgrade.
    bool authenticate(const crow::request& req, void** userdata) const;

    // Installs the handler set on a websocket rule. Templated because
    // CROW_WEBSOCKET_ROUTE's return type depends on the compile-time route
    // string; this keeps one implementation shared by every registered path.
    template <typename Rule>
    void configure_route(Rule& rule);

    const security::ITokenService& token_service_;
    realtime::EventDispatcher& dispatcher_;
};

}  // namespace rtc::controllers

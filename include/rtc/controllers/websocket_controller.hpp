#pragma once

#include "rtc/http/app.hpp"
#include "rtc/realtime/event_dispatcher.hpp"
#include "rtc/security/token_service.hpp"

namespace rtc::controllers {

// Registers the WebSocket endpoint (`/ws`) on the Crow application and bridges
// Crow's callbacks to the EventDispatcher.
//
// Authentication happens during the handshake (onaccept): the JWT access token
// is taken from the `token` query parameter or the Authorization header and
// verified before the socket is accepted, so unauthenticated peers are rejected
// with a failed upgrade. The authenticated identity is stored as the
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

    const security::ITokenService& token_service_;
    realtime::EventDispatcher& dispatcher_;
};

}  // namespace rtc::controllers

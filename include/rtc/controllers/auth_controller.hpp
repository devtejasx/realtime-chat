#pragma once

#include "rtc/http/app.hpp"
#include "rtc/middlewares/auth_middleware.hpp"
#include "rtc/services/auth_service.hpp"
#include "rtc/services/session_service.hpp"
#include "rtc/services/user_service.hpp"

namespace rtc::controllers {

// HTTP layer for authentication. Handles only transport concerns — JSON
// parsing, status codes, header extraction — and delegates all logic to the
// injected services. Registers:
//
//   POST /api/auth/register     (public)
//   POST /api/auth/login        (public)
//   POST /api/auth/refresh      (public — rotate tokens)
//   POST /api/auth/logout       (protected — revoke current session)
//   POST /api/auth/logout-all   (protected — revoke all sessions)
//   GET  /api/auth/me           (protected)
//
// On register/login a session is recorded (via SessionService) and its id is
// returned so clients can manage devices and rotate refresh tokens.
class AuthController {
public:
    AuthController(services::AuthService& auth_service, services::UserService& user_service,
                   services::SessionService& session_service,
                   middlewares::AuthMiddleware& auth_guard) noexcept
        : auth_service_(auth_service),
          user_service_(user_service),
          session_service_(session_service),
          auth_guard_(auth_guard) {}

    void register_routes(http::App& app);

private:
    services::AuthService& auth_service_;
    services::UserService& user_service_;
    services::SessionService& session_service_;
    middlewares::AuthMiddleware& auth_guard_;
};

}  // namespace rtc::controllers

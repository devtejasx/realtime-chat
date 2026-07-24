#pragma once

#include "rtc/http/app.hpp"
#include "rtc/middlewares/auth_middleware.hpp"
#include "rtc/services/auth_service.hpp"
#include "rtc/services/user_service.hpp"

namespace rtc::controllers {

// HTTP layer for authentication. Handles only transport concerns — JSON
// parsing, status codes, header extraction — and delegates all logic to the
// injected services. Registers:
//
//   POST /api/auth/register  (public)
//   POST /api/auth/login     (public)
//   GET  /api/auth/me        (protected — demonstrates the JWT guard)
class AuthController {
public:
    AuthController(services::AuthService& auth_service, services::UserService& user_service,
                   middlewares::AuthMiddleware& auth_guard) noexcept
        : auth_service_(auth_service), user_service_(user_service), auth_guard_(auth_guard) {}

    void register_routes(http::App& app);

private:
    services::AuthService& auth_service_;
    services::UserService& user_service_;
    middlewares::AuthMiddleware& auth_guard_;
};

}  // namespace rtc::controllers

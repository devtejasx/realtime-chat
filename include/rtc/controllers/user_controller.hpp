#pragma once

#include "rtc/http/app.hpp"
#include "rtc/middlewares/auth_middleware.hpp"
#include "rtc/services/user_service.hpp"

namespace rtc::controllers {

// HTTP layer for user profiles (Phase 2). All routes require a valid JWT.
// Registers:
//
//   GET  /api/users/me       — the caller's own profile (includes email)
//   PUT  /api/users/me        — partial update of the caller's profile
//   GET  /api/users/<int:id>  — another user's public profile (no email)
//
// Only transport concerns live here; profile rules stay in UserService.
class UserController {
  public:
    UserController(services::UserService& user_service,
                   middlewares::AuthMiddleware& auth_guard) noexcept
        : user_service_(user_service), auth_guard_(auth_guard) {}

    void register_routes(http::App& app);

  private:
    services::UserService& user_service_;
    middlewares::AuthMiddleware& auth_guard_;
};

}  // namespace rtc::controllers

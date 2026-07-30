#pragma once

#include "rtc/http/app.hpp"
#include "rtc/middlewares/auth_middleware.hpp"
#include "rtc/services/session_service.hpp"

namespace rtc::controllers {

// REST layer for session management. All routes require JWT. Registers:
//
//   GET    /api/sessions           — list the caller's active sessions
//   DELETE /api/sessions/<id>      — revoke a specific session
//
// (Logout of the current session and all sessions live on the auth controller.)
class SessionController {
  public:
    SessionController(services::SessionService& sessions,
                      middlewares::AuthMiddleware& auth_guard) noexcept
        : sessions_(sessions), auth_guard_(auth_guard) {}

    void register_routes(http::App& app);

  private:
    services::SessionService& sessions_;
    middlewares::AuthMiddleware& auth_guard_;
};

}  // namespace rtc::controllers

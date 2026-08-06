#pragma once

#include "rtc/config/config.hpp"
#include "rtc/events/event_bus.hpp"
#include "rtc/http/app.hpp"
#include "rtc/middlewares/auth_middleware.hpp"
#include "rtc/ratelimit/rate_limiter.hpp"
#include "rtc/services/auth_service.hpp"
#include "rtc/services/session_service.hpp"
#include "rtc/services/user_service.hpp"

namespace rtc::controllers {

// HTTP layer for authentication. Handles only transport concerns — JSON
// parsing, status codes, header extraction — and delegates all logic to the
// injected services. Registers:
//
//   POST /api/auth/register     (public, rate-limited by client IP)
//   POST /api/auth/login        (public, rate-limited by client IP)
//   POST /api/auth/refresh      (public — rotate tokens)
//   POST /api/auth/logout       (protected — revoke current session)
//   POST /api/auth/logout-all   (protected — revoke all sessions)
//   GET  /api/auth/me           (protected)
//
// On register/login a session is recorded (via SessionService) and its id is
// returned so clients can manage devices and rotate refresh tokens.
class AuthController {
  public:
    // The limiter is a constructor dependency rather than an optional setter on
    // purpose. Brute-force protection that a caller can forget to attach is
    // protection that will eventually be missing in exactly the deployment that
    // needed it, and the compiler is the only reviewer that never forgets.
    AuthController(services::AuthService& auth_service,
                   services::UserService& user_service,
                   services::SessionService& session_service,
                   middlewares::AuthMiddleware& auth_guard,
                   ratelimit::RateLimiter& rate_limiter,
                   const config::Config& config) noexcept
        : auth_service_(auth_service),
          user_service_(user_service),
          session_service_(session_service),
          auth_guard_(auth_guard),
          rate_limiter_(rate_limiter),
          config_(config) {}

    // Attaches the domain event bus so authentication events reach the audit log.
    //
    // Published here rather than inside AuthService deliberately: the client IP
    // and User-Agent are the most valuable fields on a sign-in audit record, and
    // they exist only at the transport boundary. Pushing them down into the
    // service would mean giving the service layer a dependency on crow::request,
    // which is exactly the coupling the layering exists to prevent.
    void set_event_publisher(events::IEventPublisher& publisher) noexcept {
        publisher_ = &publisher;
    }

    void register_routes(http::App& app);

  private:
    [[nodiscard]] events::IEventPublisher& publisher() const noexcept;

    services::AuthService& auth_service_;
    services::UserService& user_service_;
    services::SessionService& session_service_;
    middlewares::AuthMiddleware& auth_guard_;
    ratelimit::RateLimiter& rate_limiter_;
    const config::Config& config_;
    events::IEventPublisher* publisher_ = nullptr;
};

}  // namespace rtc::controllers

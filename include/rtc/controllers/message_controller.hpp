#pragma once

#include "rtc/config/config.hpp"
#include "rtc/http/app.hpp"
#include "rtc/middlewares/auth_middleware.hpp"
#include "rtc/ratelimit/rate_limiter.hpp"
#include "rtc/services/message_service.hpp"

namespace rtc::controllers {

// REST layer for messages. Every route requires a valid JWT. Registers:
//
//   POST   /api/messages        — send a message (persists + broadcasts, rate-limited)
//   GET    /api/messages        — list/search (?conversation_id=&q=&sender_id=&limit=&...)
//   PATCH  /api/messages/<id>   — edit (author only)
//   DELETE /api/messages/<id>   — soft-delete (author or group owner)
class MessageController {
  public:
    MessageController(services::MessageService& messages,
                      middlewares::AuthMiddleware& auth_guard,
                      ratelimit::RateLimiter& rate_limiter,
                      const config::Config& config) noexcept
        : messages_(messages),
          auth_guard_(auth_guard),
          rate_limiter_(rate_limiter),
          config_(config) {}

    void register_routes(http::App& app);

  private:
    services::MessageService& messages_;
    middlewares::AuthMiddleware& auth_guard_;
    ratelimit::RateLimiter& rate_limiter_;
    const config::Config& config_;
};

}  // namespace rtc::controllers

#pragma once

#include "rtc/config/config.hpp"
#include "rtc/http/app.hpp"
#include "rtc/middlewares/auth_middleware.hpp"
#include "rtc/ratelimit/rate_limiter.hpp"
#include "rtc/services/attachment_service.hpp"

namespace rtc::controllers {

// REST layer for attachments. All routes require JWT. Registers:
//
//   POST   /api/attachments               — multipart upload (rate-limited)
//   GET    /api/attachments/<id>          — download bytes
//   GET    /api/attachments/<id>/thumbnail— download thumbnail (images)
//   DELETE /api/attachments/<id>          — delete (owner)
class AttachmentController {
public:
    AttachmentController(services::AttachmentService& attachments,
                         middlewares::AuthMiddleware& auth_guard,
                         ratelimit::RateLimiter& rate_limiter, const config::Config& config) noexcept
        : attachments_(attachments),
          auth_guard_(auth_guard),
          rate_limiter_(rate_limiter),
          config_(config) {}

    void register_routes(http::App& app);

private:
    services::AttachmentService& attachments_;
    middlewares::AuthMiddleware& auth_guard_;
    ratelimit::RateLimiter& rate_limiter_;
    const config::Config& config_;
};

}  // namespace rtc::controllers
